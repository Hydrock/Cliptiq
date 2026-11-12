"""Measure the REAL Windows Base application, with only its own windows hidden.

Capture listener stays active; fixture policy disables payload retention. No
clipboard contents are read by this harness. Separate LOCALAPPDATA isolates IPC.
ShowWindow is restricted to HWNDs enumerated for the spawned test PID.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import platform
import runpy
import sqlite3
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
shared = runpy.run_path(str(ROOT / "experiments/raven-settings/measure.py"))
snapshot, kernel, user = (shared[x] for x in ("snapshot", "kernel", "user"))
enum_callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [enum_callback, W.LPARAM]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.IsWindowVisible.argtypes = [W.HWND]
user.ShowWindow.argtypes = [W.HWND, C.c_int]
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
kernel.GetProcessHandleCount.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]


def windows(pid):
    result = []
    @enum_callback
    def callback(hwnd, _):
        owner = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(owner))
        if owner.value == pid:
            name = C.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, len(name))
            result.append(dict(hwnd=int(hwnd), visible=bool(user.IsWindowVisible(hwnd)), window_class=name.value))
        return True
    if not user.EnumWindows(callback, 0):
        raise C.WinError()
    return result


def measure(process, seconds, interval):
    handle = kernel.OpenProcess(0x410, False, process.pid)
    if not handle:
        raise C.WinError()
    try:
        before = snapshot(handle)
        start = previous = time.perf_counter()
        last = before
        samples = []
        while time.perf_counter() - start < seconds:
            time.sleep(min(interval, max(0, seconds - (time.perf_counter() - start))))
            now = time.perf_counter()
            if process.poll() is not None:
                raise RuntimeError(f"Application exited early: {process.returncode}")
            current = snapshot(handle)
            handles = W.DWORD()
            kernel.GetProcessHandleCount(handle, C.byref(handles))
            samples.append(dict(elapsed_seconds=now-start,
                                cpu_percent_one_core=100*(current['cpu_seconds']-last['cpu_seconds'])/(now-previous),
                                handle_count=handles.value,
                                **{k:v for k,v in current.items() if k!='cpu_seconds'}))
            previous, last = now, current
        return dict(duration_seconds=previous-start,
                    cpu_seconds=last['cpu_seconds']-before['cpu_seconds'],
                    cpu_percent_one_core=100*(last['cpu_seconds']-before['cpu_seconds'])/(previous-start),
                    samples=samples)
    finally:
        kernel.CloseHandle(handle)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--entries',type=int,default=1000)
    p.add_argument('--seconds',type=float,default=30)
    p.add_argument('--settle',type=float,default=5)
    p.add_argument('--open-settings',action='store_true')
    p.add_argument('--expect-background',action='store_true')
    p.add_argument('--expect-settings-destroyed',action='store_true')
    args=p.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    root=args.output.parent.resolve()/f'profile-{time.time_ns()}'
    root.mkdir()
    db=root/'history.sqlite3'
    build=args.build_dir.resolve()
    subprocess.run([str(build/'idle_profile.exe'), str(db), str(args.entries)],check=True)
    env=os.environ.copy(); env['LOCALAPPDATA']=str(root)
    exe=build/'Cliptiq.exe'
    log=open(root/'app.log','w')
    process=subprocess.Popen([str(exe),'--database',str(db),'--background','--no-auto-paste'],env=env,stdout=log,stderr=log)
    own=[]
    try:
        deadline=time.perf_counter()+30
        while time.perf_counter()<deadline:
            if process.poll() is not None:raise RuntimeError(f'App exited: {process.returncode}')
            own=windows(process.pid)
            launcher=[w for w in own if w['window_class']=='Cliptiq.NativeMenu']
            if launcher:break
            time.sleep(.05)
        else:raise TimeoutError('Launcher HWND not created')
        time.sleep(1)
        initial=windows(process.pid)
        initial_visible=[w for w in initial if w['visible']]
        if args.expect_background and initial_visible:
            raise AssertionError(f'--background left visible windows: {initial_visible}')
        # Baseline ignores --background. Hide its launcher without destroying
        # it, preserving the same native object set and live capture listener.
        for w in initial_visible:user.ShowWindow(w['hwnd'],0)
        time.sleep(args.settle)
        assert not any(w['visible'] for w in windows(process.pid))
        rows={'startup_hidden':measure(process,args.seconds,5)}
        if args.open_settings:
            forwarded=subprocess.run([str(exe),'--database',str(db),'--show-settings'],env=env,timeout=15)
            assert forwarded.returncode==0
            deadline=time.perf_counter()+10
            settings=[]
            while time.perf_counter()<deadline:
                settings=[w for w in windows(process.pid) if w['window_class']=='Cliptiq.Settings.v1' and w['visible']]
                if settings:break
                time.sleep(.05)
            assert len(settings)==1,'Settings did not open through IPC'
            user.PostMessageW(settings[0]['hwnd'],0x10,0,0) # same path as user close
            time.sleep(args.settle)
            assert not any(w['visible'] for w in windows(process.pid))
            if args.expect_settings_destroyed:
                assert not any(w['window_class']=='Cliptiq.Settings.v1' for w in windows(process.pid)), 'Closed settings retained its HWND and child controls'
            rows['after_settings_close']=measure(process,args.seconds,5)
            if args.expect_settings_destroyed:
                subprocess.run([str(exe),'--database',str(db),'--show-settings'],env=env,check=True,timeout=15)
                deadline=time.perf_counter()+10
                reopened=[]
                while time.perf_counter()<deadline:
                    reopened=[w for w in windows(process.pid) if w['window_class']=='Cliptiq.Settings.v1' and w['visible']]
                    if reopened:break
                    time.sleep(.05)
                assert len(reopened)==1,'Settings failed to reopen after destruction'
                user.PostMessageW(reopened[0]['hwnd'],0x10,0,0)
                deadline=time.perf_counter()+5
                while time.perf_counter()<deadline and any(w['window_class']=='Cliptiq.Settings.v1' for w in windows(process.pid)):
                    time.sleep(.05)
                assert not any(w['window_class']=='Cliptiq.Settings.v1' for w in windows(process.pid))
        content=dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                     exe_bytes=exe.stat().st_size,environment=dict(os=platform.platform(),logical_cpus=os.cpu_count()),
                     fixture=dict(synthetic_entries=args.entries,clipboard_payload_retention=False,
                                  capture_listener=True,ocr=False,raven=False,updates=False),
                     initial_visible_windows=initial_visible,windows_after=windows(process.pid),
                     settings_destroyed_and_reopened=bool(args.open_settings and args.expect_settings_destroyed),scenarios=rows)
        args.output.write_text(json.dumps(content,indent=2)+'\n')
        print(json.dumps({key:{'cpu_percent_one_core':v['cpu_percent_one_core'],
                              'working_set_mib':v['samples'][-1]['working_set_bytes']/1048576,
                              'private_mib':v['samples'][-1]['private_bytes']/1048576}
                          for key,v in rows.items()},indent=2),flush=True)
    finally:
        for w in own:
            if w['window_class']=='Cliptiq.NativeMenu':user.PostMessageW(w['hwnd'],0x10,0,0)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        log.close()
    if process.returncode:raise RuntimeError(f'Nonzero shutdown: {process.returncode}')


if __name__=='__main__':main()


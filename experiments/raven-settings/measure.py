"""Windows-only A/B harness. Run on an unlocked, interactive desktop.

Opening is measured to the first UI dispatch/submission, NOT first displayed
pixel. Each launch gets an in-memory profile; no real Cliptiq profile is opened.
CPU % is one logical core (100% = one core fully busy), not Task Manager's
whole-machine normalization. GPU/service memory is outside these counters.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time
import zipfile

kernel = C.WinDLL("kernel32", use_last_error=True)
psapi = C.WinDLL("psapi", use_last_error=True)
user = C.WinDLL("user32", use_last_error=True)
kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
kernel.OpenProcess.restype = W.HANDLE
kernel.CloseHandle.argtypes = [W.HANDLE]
kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(C.c_ulonglong)] * 4
kernel.GetProcessTimes.restype = W.BOOL
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.PostMessageW.restype = W.BOOL


class Memory(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(Memory), W.DWORD]
psapi.GetProcessMemoryInfo.restype = W.BOOL


def snapshot(handle):
    ticks = [C.c_ulonglong() for _ in range(4)]
    if not kernel.GetProcessTimes(handle, *(C.byref(t) for t in ticks)):
        raise C.WinError(C.get_last_error())
    mem = Memory()
    mem.cb = C.sizeof(mem)
    if not psapi.GetProcessMemoryInfo(handle, C.byref(mem), mem.cb):
        raise C.WinError(C.get_last_error())
    return dict(cpu_seconds=(ticks[2].value + ticks[3].value) / 1e7,
                working_set_bytes=mem.WorkingSetSize, private_bytes=mem.PrivateUsage,
                peak_working_set_bytes=mem.PeakWorkingSetSize)


def ready(path, process, count=1):
    if count > 1:
        path = Path(str(path) + f".{count}")
    deadline = time.perf_counter() + 45
    while time.perf_counter() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Process exited early: {process.returncode}; see ready-*.error.txt")
        try:
            data = json.loads(path.read_text())
            if len(data["open_dispatch_ms"]) >= count:
                return data
        except (FileNotFoundError, json.JSONDecodeError, PermissionError):
            pass
        time.sleep(.005)
    raise TimeoutError(f"UI readiness timed out: {path}")


def post(hwnd, msg):
    if not user.PostMessageW(hwnd, msg, 0, 0):
        raise C.WinError(C.get_last_error())


def trial(exe, report, settle, idle, reopens):
    start = time.perf_counter()
    process = subprocess.Popen([str(exe), "--memory-profile", "--report", str(report)])
    handle = None
    info = None
    try:
        info = ready(report, process)
        launch = (time.perf_counter() - start) * 1000
        handle = kernel.OpenProcess(0x410, False, process.pid)
        if not handle:
            raise C.WinError(C.get_last_error())
        time.sleep(settle)
        before = snapshot(handle)
        idle_start = time.perf_counter()
        time.sleep(idle)
        duration = time.perf_counter() - idle_start
        after = snapshot(handle)
        # Reopening is measured after idle so it cannot contaminate that sample.
        for i in range(reopens):
            post(info["control_hwnd"], 0x8000 + 41)
            info = ready(report, process, i + 2)
            time.sleep(.15)
        if process.poll() is not None:
            raise RuntimeError("UI exited during the measurement")
        return dict(variant=info["variant"], field_count=info["field_count"],
                    launch_to_ready_ms=launch, entry_to_ready_ms=info["entry_to_ready_ms"],
                    first_open_dispatch_ms=info["open_dispatch_ms"][0],
                    reopen_dispatch_ms=info["open_dispatch_ms"][1:],
                    idle_seconds=duration,
                    idle_cpu_percent_one_core=100 * (after["cpu_seconds"] - before["cpu_seconds"]) / duration,
                    **{k: v for k, v in after.items() if k != "cpu_seconds"})
    finally:
        if info and process.poll() is None:
            post(info["control_hwnd"], 0x8000 + 42)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        elif process.poll() is None:
            process.kill()
            process.wait()
        if handle:
            kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=Path("out/raven-settings/settings-bin"))
    parser.add_argument("--output", type=Path, default=Path("out/raven-settings/measurements.json"))
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--settle", type=float, default=3)
    parser.add_argument("--idle", type=float, default=10)
    parser.add_argument("--reopens", type=int, default=3)
    args = parser.parse_args()
    if args.trials < 1 or args.idle <= 0 or args.settle < 0 or args.reopens < 0:
        parser.error("Invalid sample counts/durations")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    sizes = {}
    for variant in ("native", "raven"):
        exe = args.bin_dir.resolve() / f"settings_{variant}.exe"
        compressed = args.output.parent / f"settings_{variant}-exe-only.zip"
        with zipfile.ZipFile(compressed, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            z.write(exe, exe.name)
        sizes[variant] = dict(exe_bytes=exe.stat().st_size, exe_zip_bytes=compressed.stat().st_size,
                              sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    for n in range(args.trials):
        # Alternate order to reduce systematic filesystem-cache/temperature bias.
        for variant in (("native", "raven") if n % 2 == 0 else ("raven", "native")):
            report = args.output.parent.resolve() / f"ready-{variant}-{time.time_ns()}.json"
            row = trial(args.bin_dir.resolve() / f"settings_{variant}.exe", report,
                        args.settle, args.idle, args.reopens)
            rows.append(row)
            print(json.dumps(row), flush=True)
    summary = {}
    for variant in ("native", "raven"):
        group = [r for r in rows if r["variant"] == variant]
        summary[variant] = {key: statistics.median(r[key] for r in group) for key in (
            "launch_to_ready_ms", "entry_to_ready_ms", "first_open_dispatch_ms",
            "idle_cpu_percent_one_core", "working_set_bytes", "private_bytes")}
        warm = [v for r in group for v in r["reopen_dispatch_ms"]]
        summary[variant]["reopen_dispatch_ms"] = statistics.median(warm) if warm else None
    output = dict(method="first UI dispatch/submission; not first displayed pixel; process-cold, OS cache uncontrolled",
                  cpu_definition="100% = one logical core", memory_scope="process working set/private commit; excludes GPU and external services",
                  environment=dict(os=platform.platform(), logical_cpus=os.cpu_count(),
                                   python=platform.python_version(),
                                   raven_commit="9f0cfa6fe3da33022a577e06a6ea843a5ef3f76e"),
                  parameters=vars(args) | {"bin_dir": str(args.bin_dir), "output": str(args.output)},
                  sizes=sizes, summary_medians=summary, trials=rows)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()

"""Run only on a newly owned test display; never use ambient desktop clipboard."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

owned_bus = sys.argv[1] == '--owned-bus'
mode, executable, *args = sys.argv[2:] if owned_bus else sys.argv[1:]
env = os.environ.copy()
for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'SWAYSOCK', 'XAUTHORITY'):
    env.pop(key, None)
if not owned_bus:
    env.pop('DBUS_SESSION_BUS_ADDRESS', None)
    raise SystemExit(subprocess.run(['dbus-run-session', '--', sys.executable, __file__, '--owned-bus', mode, executable, *args], env=env).returncode)
env['CLIPTIQ_TEST_ISOLATED'] = mode
if mode in ('x11', 'wayland'):
    env['GDK_BACKEND'] = mode
    env['XDG_SESSION_TYPE'] = mode
if mode == 'x11':
    raise SystemExit(subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 800x600x24 -nolisten tcp', executable, *args], env=env).returncode)
with tempfile.TemporaryDirectory(prefix='cliptiq-wayland-') as name:
    root = Path(name)
    runtime = root / 'runtime'
    runtime.mkdir(mode=0o700)
    env['XDG_RUNTIME_DIR'] = str(runtime)
    if mode == 'fixture':
        raise SystemExit(subprocess.run([executable, *args], env=env).returncode)
    if mode != 'wayland':
        raise ValueError('unknown isolated display mode')
    env.update(WLR_BACKENDS='headless', WLR_LIBINPUT_NO_DEVICES='1', WLR_RENDERER='pixman', XDG_CURRENT_DESKTOP='sway')
    config = root / 'sway.conf'
    config.write_text('output * resolution 800x600\nseat seat0 fallback true\nxwayland disable\n')
    with (root / 'sway.log').open('w+') as log:
        process = subprocess.Popen(['sway', '-c', str(config), '-d'], env=env, stdout=log, stderr=log, start_new_session=True)
        try:
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                sockets = [p for p in runtime.glob('wayland-*') if p.is_socket()]
                if sockets:
                    break
                if process.poll() is not None:
                    raise RuntimeError('test compositor exited')
                time.sleep(.03)
            else:
                raise RuntimeError('test compositor startup timeout')
            env['WAYLAND_DISPLAY'] = sockets[0].name
            result = subprocess.run([executable, *args], env=env)
            if result.returncode:
                log.flush()
                print('\n'.join((root / 'sway.log').read_text().splitlines()[-20:]), file=sys.stderr)
            raise SystemExit(result.returncode)
        finally:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

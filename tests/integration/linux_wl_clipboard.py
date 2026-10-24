"""Independent wl-clipboard clients on a launcher-owned Sway display."""
import os, subprocess, sys, time
assert os.environ.get('CLIPTIQ_TEST_ISOLATED') == 'wayland'
sensitive_option = '--sensitive' in subprocess.check_output(['wl-copy', '--help'], text=True)
print('wl-copy sensitive_option_available:', sensitive_option)
for sensitive in (False, True):
    source = subprocess.Popen(['wl-copy', '--foreground'] + ((['--sensitive'] if sensitive_option else ['--type', 'x-kde-passwordManagerHint']) if sensitive else []), stdin=subprocess.PIPE)
    source.stdin.write(b'secret' if sensitive else b'ext\xc3\xa9'); source.stdin.close()
    try:
        time.sleep(.05)
        reader = subprocess.Popen([sys.argv[1], 'wayland', '--external-sensitive' if sensitive else '--external-text'], stdout=subprocess.PIPE, text=True)
        try:
            if sensitive:
                stdout, _ = reader.communicate(timeout=8)
                assert reader.returncode == 0, stdout
                print(stdout, end='')
            else:
                assert reader.stdout.readline().strip() == 'READY'
                pasted = subprocess.check_output(['wl-paste', '--no-newline'], timeout=5)
                assert pasted == b'reply', pasted
                reader.wait(timeout=6); assert reader.returncode == 0
                print('PASS independent_wl_copy_and_wl_paste_roundtrip')
        finally:
            if reader.poll() is None: reader.kill(); reader.wait()
    finally:
        if source.poll() is None: source.terminate()
        source.wait(timeout=5)

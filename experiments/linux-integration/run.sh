#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="$(realpath "${1:-../../docs/support/linux-integration}")"
mkdir -p "$out"
tmp=$(mktemp -d)
cleanup() { if [[ -n ${sway_pid:-} ]]; then kill "$sway_pid" 2>/dev/null || true; wait "$sway_pid" 2>/dev/null || true; fi; rm -rf "$tmp"; }
trap cleanup EXIT
{
 date -u --iso-8601=seconds
 uname -a
 cat /etc/os-release
 gcc --version | head -1
 dpkg-query -W sway libwlroots12t64 wl-clipboard wayland-utils libx11-6 libxtst6 dbus python3-dbus python3-gi xvfb
 dpkg-query -W -f='${Package}\t${Version}\t${Installed-Size}\n' sway libwlroots12t64 wl-clipboard wayland-utils python3-dbus python3-gi
 dpkg-query -W 'xdg-desktop-portal*' 2>/dev/null || true
} > "$out/environment.txt"
gcc -Os -s -Wall -Wextra -o "$tmp/x11-probe" x11.c $(pkg-config --cflags --libs x11 xtst)
xvfb-run -a -s '-screen 0 800x600x24 -nolisten tcp' timeout 10 "$tmp/x11-probe" > "$out/x11.json" || { status=$?; echo "x11 probe failed: $status" >&2; exit "$status"; }
export XDG_RUNTIME_DIR="$tmp/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
unset DISPLAY WAYLAND_DISPLAY SWAYSOCK
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman
printf 'output * resolution 800x600\nseat seat0 fallback true\n' > "$tmp/sway.conf"
sway -c "$tmp/sway.conf" -d > "$out/sway.log" 2>&1 &
sway_pid=$!
for n in {1..100}; do
 socket=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' -print -quit)
 [[ -n "$socket" ]] && break
 kill -0 "$sway_pid" || { echo 'Sway failed' >&2; exit 1; }
 sleep .05
done
export WAYLAND_DISPLAY="$(basename "$socket")"
export XDG_CURRENT_DESKTOP=sway
CLIPTIQ_RESULT_PATH="$out/dbus.json" dbus-run-session -- timeout 15 /usr/bin/python3 dbus_probe.py > "$out/dbus-stdout.log" 2> "$out/dbus.log"
timeout 10 wayland-info > "$out/wayland-info.txt"
grep -q zwlr_data_control_manager_v1 "$out/wayland-info.txt"
printf 'Cliptiq isolated Wayland: Привет' > "$tmp/expected"
timeout 10 wl-copy --type 'text/plain;charset=utf-8' < "$tmp/expected"
timeout 10 wl-paste --no-newline > "$tmp/actual"
cmp "$tmp/expected" "$tmp/actual"
timeout 10 wl-copy --clear
printf '{"compositor":"Sway headless pixman","data_control_advertised":true,"utf8_clipboard_roundtrip":true,"physical_keyboard":false}\n' > "$out/wayland.json"
# ELF attribution only, not a full toolkit or package installation closure.
python3 - "$tmp/x11-probe" "$out" <<'PY'
import hashlib,json,pathlib,subprocess,sys
exe=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2])
paths=set()
for line in subprocess.check_output(['ldd',str(exe)],text=True).splitlines():
 for token in line.split():
  if token.startswith('/') and pathlib.Path(token).is_file(): paths.add(pathlib.Path(token).resolve())
deps=[{'path':str(p),'bytes':p.stat().st_size} for p in sorted(paths)]
(out/'x11-size.json').write_text(json.dumps({'probe_bytes':exe.stat().st_size,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'elf_runtime_dependencies':deps,'elf_runtime_bytes':sum(p['bytes'] for p in deps),'app_owned_production_bytes':None,'offline_install_total_bytes':None},indent=2)+'\n')
PY
sha256sum x11.c dbus_probe.py run.sh > "$out/source-sha256.txt"
echo 'PASS: isolated X11 selection/hotkey, mock SNI/menu, headless Wayland data-control clipboard'

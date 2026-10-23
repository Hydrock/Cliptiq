#!/usr/bin/env bash
set -euo pipefail
sources="${RUNNER_TEMP:-/tmp}/cliptiq-backend-ubuntu.sources"
cat > "$sources" <<'SOURCES'
Types: deb
URIs: https://archive.ubuntu.com/ubuntu
Suites: noble noble-updates
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: https://security.ubuntu.com/ubuntu
Suites: noble-security
Components: main universe
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
SOURCES
options=(-o "Dir::Etc::sourcelist=$sources" -o Dir::Etc::sourceparts=- -o Acquire::Retries=1 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 -o DPkg::Lock::Timeout=30)
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 180 apt-get "${options[@]}" update
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 240 apt-get "${options[@]}" --no-install-recommends install -y libxcb1-dev libxcb-xfixes0-dev libwayland-dev libgtk-3-dev pkg-config xvfb xauth sway dbus-daemon wl-clipboard

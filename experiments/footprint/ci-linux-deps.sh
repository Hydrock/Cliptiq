#!/usr/bin/env bash
set -euo pipefail
# Hosted runners can contain unrelated vendor repositories and interactive
# package hooks. Keep this measurement bootstrap bounded and Ubuntu-only.
source_file="${RUNNER_TEMP:-/tmp}/cliptiq-ubuntu.sources"
cat > "$source_file" <<'SOURCES'
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
apt_sources=(-o "Dir::Etc::sourcelist=$source_file" -o Dir::Etc::sourceparts=-)
common=(-o Acquire::Retries=1 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 -o DPkg::Lock::Timeout=30)
packages=(libgtk-3-dev)
if [[ " $* " == *' --gtk4 '* ]]; then packages=(libgtk-4-dev); fi
if [[ " $* " == *' --ocr '* ]]; then packages+=(tesseract-ocr tesseract-ocr-eng tesseract-ocr-rus); fi
command -v cmake >/dev/null || packages+=(cmake)
command -v ninja >/dev/null || packages+=(ninja-build)
command -v pkg-config >/dev/null || packages+=(pkg-config)
command -v xvfb-run >/dev/null || packages+=(xvfb xauth)
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 180 \
  apt-get "${apt_sources[@]}" "${common[@]}" update
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 240 \
  apt-get "${apt_sources[@]}" "${common[@]}" --no-install-recommends install -y "${packages[@]}"

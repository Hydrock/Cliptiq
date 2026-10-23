#!/usr/bin/env bash
set -euo pipefail
# Hosted runners can contain unrelated vendor repositories and interactive
# package hooks. Keep this measurement bootstrap bounded and Ubuntu-only.
apt_sources=()
if [[ -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
  apt_sources=(-o Dir::Etc::sourcelist=/etc/apt/sources.list.d/ubuntu.sources -o Dir::Etc::sourceparts=-)
fi
common=(-o Acquire::Retries=1 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20 -o DPkg::Lock::Timeout=30)
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 180 \
  apt-get "${apt_sources[@]}" "${common[@]}" update
sudo env DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a timeout 240 \
  apt-get "${apt_sources[@]}" "${common[@]}" --no-install-recommends install -y \
  cmake ninja-build pkg-config libgtk-3-dev libgtk-4-dev xvfb tesseract-ocr-eng tesseract-ocr-rus

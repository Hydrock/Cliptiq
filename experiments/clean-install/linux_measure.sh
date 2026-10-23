#!/bin/sh
set -eu
phase=$1
lane=$2
case "$lane" in
  gtk3) packages='libgtk-3-0t64 libstdc++6 fonts-dejavu-core' ;;
  gtk4) packages='libgtk-4-1 libstdc++6 fonts-dejavu-core' ;;
  ocr) packages='libgtk-3-0t64 libstdc++6 fonts-dejavu-core tesseract-ocr tesseract-ocr-eng tesseract-ocr-rus' ;;
  *) echo 'unknown lane' >&2; exit 2 ;;
esac
root_bytes() {
  du -sbx --exclude=/proc --exclude=/sys --exclude=/dev --exclude=/work --exclude=/app --exclude=/out \
     --exclude=/var/lib/apt/lists --exclude=/var/cache/apt --exclude=/tmp / 2>/dev/null | awk '{print $1}'
}
metadata_bytes() { dpkg-query -W -f='${Installed-Size}\n' | awk '{n+=$1} END {printf "%.0f",n*1024}'; }
if [ "$phase" = download ]; then
  cat /etc/os-release > /out/os-release.txt
  dpkg-query -W -f='${Package} ${Version}\n' > /out/packages-before.txt
  root_bytes > /out/root-before.txt
  metadata_bytes > /out/package-metadata-before.txt
  # A clean image must actually fail without its runtime, not just pass ldd.
  set +e
  /app/probe_cpp --self-test /tmp/pre.xml > /out/before-runtime.txt 2>&1
  status=$?
  set -e
  printf '%s\n' "$status" > /out/before-runtime-exit.txt
  if [ "$status" -ne 127 ] || ! grep -q 'error while loading shared libraries' /out/before-runtime.txt; then
    echo 'clean-image negative control did not fail for a missing shared library' >&2
    exit 3
  fi
  apt-get -o Acquire::Retries=2 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 update > /out/apt-update.log 2>&1
  mkdir -p /out/runtime-debs/partial /out/harness-debs/partial
  # Runtime and GUI test harness archives are kept distinct and counted apart.
  apt-get -y --download-only --no-install-recommends -o Dir::Cache::archives=/out/runtime-debs \
      -o Acquire::Retries=2 -o Acquire::http::Timeout=30 install $packages > /out/runtime-download.log 2>&1
  apt-get -y --download-only --no-install-recommends -o Dir::Cache::archives=/out/harness-debs \
      -o Acquire::Retries=2 -o Acquire::http::Timeout=30 install xvfb xauth > /out/harness-download.log 2>&1
elif [ "$phase" = install ]; then
  # Driver disconnects the container network before entering this phase.
  apt-get -y --no-download --no-install-recommends -o Dir::Cache::archives=/out/runtime-debs \
      install $packages > /out/runtime-install.log 2>&1
  root_bytes > /out/root-after-runtime.txt
  metadata_bytes > /out/package-metadata-after.txt
  dpkg-query -W -f='${Package} ${Version}\n' > /out/packages-after-runtime.txt
  find /out/runtime-debs -name '*.deb' -printf '%f %s\n' | sort > /out/runtime-deb-inventory.txt
  sha256sum /out/runtime-debs/*.deb | sed 's#/out/runtime-debs/##' > /out/runtime-deb-sha256.txt
  download_bytes=$(awk '{n+=$NF} END {printf "%.0f",n}' /out/runtime-deb-inventory.txt)
  before=$(cat /out/root-before.txt)
  after=$(cat /out/root-after-runtime.txt)
  delta=$((after-before))
  /app/probe_cpp --self-test /out/cpp-snippets.xml > /out/cpp-self-test.json
  /app/probe_rust --self-test /out/rust-snippets.xml > /out/rust-self-test.json
  cmp /out/cpp-snippets.xml /out/rust-snippets.xml
  ldd /app/probe_cpp > /out/cpp-ldd.txt
  ldd /app/probe_rust > /out/rust-ldd.txt
  apt-get -y --no-download --no-install-recommends -o Dir::Cache::archives=/out/harness-debs \
      install xvfb xauth > /out/harness-install.log 2>&1
  xvfb-run -a timeout 10 /app/probe_cpp --lifecycle-smoke > /out/cpp-lifecycle.json
  xvfb-run -a timeout 10 /app/probe_rust --lifecycle-smoke > /out/rust-lifecycle.json
  grep -q '"native_window_lifecycle":true' /out/cpp-lifecycle.json
  grep -q '"native_window_lifecycle":true' /out/rust-lifecycle.json
  xvfb-run -a /app/probe_cpp --benchmark /out/cpp-runtime.json
  xvfb-run -a /app/probe_rust --benchmark /out/rust-runtime.json
  printf '{"schema_version":1,"lane":"%s","environment":"clean ubuntu:24.04 container, not a desktop VM","network_disabled_during_install":true,"runtime_download_bytes":%s,"runtime_apparent_install_delta_bytes":%s,"root_before_bytes":%s,"root_after_runtime_bytes":%s,"core_and_xml_roundtrip":true,"native_window_lifecycle":true,"gui_harness_excluded_from_runtime_delta":true}\n' \
      "$lane" "$download_bytes" "$delta" "$before" "$after" > /out/report.json
else
  exit 2
fi

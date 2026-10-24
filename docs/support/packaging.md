# CI candidates (T17)

Build Base with `cmake --preset release-test`, `cmake --build --preset release-test`, and `ctest --preset release-test --output-on-failure`; then run `python packaging/package.py` from the repository root, in the configured native compiler environment. Linux packaging requires the dependencies installed by `tools/install-linux-backend-deps.sh`, including dpkg-dev/rpm/cpio. Windows requires the Windows SDK MakeAppx. macOS candidates use ad-hoc codesign only.

For the optional offline OCR package, first run `python tools/test-ocr.py --engine native` on macOS or `--engine tesseract` on Windows/Linux; then `python packaging/package.py --variant ocr`. Native Windows OCR remains a separately tested identity-negative lane, not a claimed positive packaged OCR product.

Outputs go only to owned `out/t17-{target}-{arch}-{variant}` directories. The packager refuses to replace foreign output. `payload` contains the staged application; `size.json` carries exact files/imports/architecture/model sizes and budgets; `package-report.json` carries container sizes and SHA256. CI artifacts preserve each package and both reports. Mac ZIPs are per architecture; Windows includes ZIP/MSIX; Linux includes ZIP/tar.xz/DEB/RPM. DEB Depends and RPM Requires are generated from native binaries. Base excludes external OCR models and optional Realm/Sparkle SDKs.

ZIP timestamps are fixed for comparable measurements; this alone does not establish reproducible binaries. Actions are pinned in `actions-lock.json`; existing dependency locks enforce downloaded content hashes. Hosted runner SDKs/compilers remain mutable and their configuration is recorded in each manifest.

These are unsigned/ad-hoc test candidates. Do not interpret CI success or `--version` as clean-machine installation, desktop acceptance or publisher trust. [Release gates](release-checklist.md) remain OPEN, including T04–T16. `release.yml` validates candidates and never publishes a release; requesting production readiness fails closed while gates are open.

Linux dependency sizes are OS-specific and unknown without clean consumer machines. Windows/MSIX signing, macOS Developer ID/notarization and Linux authenticated repository delivery require a later production configuration. No project-wide license has been assigned by packaging; dependency notices are bundled.

# T16 manual acceptance — OPEN

T04–T15 manual acceptance remains OPEN. Do not infer completion from CI.
T17 is not started; these fixtures are not signed releases or clean-machine proof.

- macOS Intel/ARM: enable optional native OCR build, copy EN/RU images, verify
  OS-reported availability and retained OCR status. Delete/clear during work,
  revoke capture permissions, quit during recognition; no result resurrection.
- Windows portable: native WinRT engine must report package identity unavailable;
  Tesseract pack must recognize EN/RU fully offline. MSIX positive native OCR
  acceptance requires an actually installed signed Cliptiq MSIX and stays OPEN.
- Linux X11/Wayland: native accepted clipboard images and configured screenshot
  files enter OCR only after existing privacy gate. Confirm disabled Image,
  transient/concealed, exclusions/unknown source and capture revocation.
- Install Linux OCR build through CMake install: binary and pinned models must
  remain adjacent (`bin/cliptiq`, `bin/tessdata`). Test installed tree offline.
- Change privacy policy during a running job; no rejected output is published.
  Clear/delete while indexing or recognizing; no companion retains deleted data
  after a successful mutation. A short publication conflict returns Busy;
  repeat the interactive action after it ends.
- Switch indexed → Base → indexed in the same profile. Main history/snippets and
  imported OCR remain; Base never opens FTS and clears owned index files. Check
  corrupted/removed companion recovery after restart.
- Repair missing model pack: Unavailable entries retry after restart, explicit
  retry hook or60s epoch. Failed engine outputs remain distinct from unavailable.
- Check shared single-instance lock across build variants and OS session restart.
- Measure real workload RSS/latency, cold start/quit, offline clean-system
  installation/uninstallation; CI only establishes synthetic integration.

Known deferred UI detail: capability label is declared Available before the
actual native/model probe; processing returns Unsupported when unavailable.
No search field or OCR-copy action is part of T16.

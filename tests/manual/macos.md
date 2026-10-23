# T04 macOS manual acceptance

Status: **NOT EXECUTED**. No interactive Mac was available to this Windows
executor. Native CI named-pasteboard API tests and injected permission states
do not replace TextEdit/Safari/Finder/Preview or actual General-pasteboard consent
testing. Keep this gate open; do not mark it PASS based on a green CI run.

## Build and opt-in helper

On Mac: fetch pinned SQLite, `cmake --preset release`,
`cmake --build --preset release`, then `ctest --preset release -R macos_clipboard`.
`out/core-Darwin-release/macos_clipboard_manual` is a developer helper built only
with BUILD_TESTING. It is not a product UI, and is not part of size probes or
shipping payload. Invoking it with no arguments prints usage and does not read
General. CI compiles it but never invokes General modes.

- `read`: explicit programmatic General read, permitted to request the native
  consent prompt. Reports format names/item indices/byte lengths and gate reason,
  not clipboard contents or source IDs. Its inspection policy enables HTML too.
- `echo`: same read/privacy gate, then replays the accepted native snapshot to
  General. This replaces clipboard contents and tests native roundtrip.
- `write text|rich|image|files`: replaces General with generated fixtures. The
  files mode creates two owned temporary text files in a printed 0700 fixture
  directory; remove that exact directory after pasting/testing is finished.
- `watch 20 [excluded-bundle-id]`: monitors General for 20 sec with RAM-only
  history; prints decisions/retained boolean, not contents. Move focus to the
  source app and copy a generated test value. Source remains BestEffort.

Use synthetic nonsensitive content. Record the helper identity actually shown
by macOS; CLI/Terminal permission identity does not prove signed Cliptiq bundle
behavior. Permission persistence and production app identity need signed-app
acceptance later. Never edit TCC databases or bypass consent for this test.

## Checklist (all pending)

Record date, OS product/build, CPU, Xcode/SDK, commit, app versions, helper identity,
commands, observed native formats, result and limitation. Screenshots must not
include personal clipboard content.

- [ ] TextEdit plain/rich text including Unicode/emoji: copy → read/echo → paste
  back; verify text and formatting, created/used behavior via API tests separately.
- [ ] Safari: copy formatted text/HTML and URL; echo then paste into appropriate
  editor/URL field. Repeat with HTML disabled in the integration host; no disabled
  representation may reach history or return on replay.
- [ ] Finder: copy two actual generated temporary files/folders; echo and paste
  into a separate owned test folder. Both paths remain usable; test legacy
  root-array compatibility and modern per-item URLs. This is not a sandbox
  extension/bookmark guarantee for remote Apple-managed files.
- [ ] Preview: image-only PNG/TIFF with alpha, PDF and paste/new-from-clipboard;
  echo and verify no unwanted text fallback. RTFD attachment roundtrip through
  TextEdit. Malformed content/decoder safety is T12, not a native decode claim.
- [ ] General permission granted/denied/Ask/default/revoked during monitoring on
  an OS enforcing pasteboard privacy: background pause/reason, explicit read
  prompt, Deny refusal, recovery. No repeated automatic prompt.
- [ ] Focus switches/copy timing and excluded app markers: no claim of guaranteed
  clipboard authorship from frontmost app. Unknown source with exclusions must
  visibly reject rather than advertise source protection.
- [ ] Stop/start, sleep/resume, clipboard owner termination and sustained copying
  during blocked SQLite writes: visible Busy/overflow, bounded pending captures,
  no stale callbacks or own-write loops. Actual server failure after replay clear
  is not an atomic clipboard transaction or safe-erase guarantee.

The automatic counterpart runs named-pasteboard cases on macOS arm64/x86_64,
including source-token consistency, retries, bounds, worker dispatch, injected
privacy states, restart/destruction, and queue pressure. It does not establish
all supported OS versions, actual permission UI or end-to-end app compatibility.

Official API references: [NSPasteboard](https://developer.apple.com/documentation/appkit/nspasteboard),
[changeCount](https://developer.apple.com/documentation/appkit/nspasteboard/changecount),
[access behavior](https://developer.apple.com/documentation/appkit/nspasteboard/accessbehavior-swift.enum),
[AppKit privacy updates](https://developer.apple.com/documentation/updates/appkit).

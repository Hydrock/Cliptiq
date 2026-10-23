# macOS clipboard backend (T04)

Objective-C++17/ARC, system AppKit/Foundation/libdispatch, no third-party GUI or
Swift runtime. Root CMake enables this target only on Apple platforms. macOS
deployment target is 13.0; actual runtime evidence currently covers macOS 15 CI,
not every proposed supported OS version.

`MacClipboardBackend` implements the common ClipboardBackend contract. Creation,
capability probing, reads, writes, policy configuration and start run on the main
thread. stop can be requested off-main; generation-scoped cleanup is delivered
on main so it cannot invalidate a restarted monitor. Owners normally stop and
destroy on main. The timer ticks every 500 ms in common run-loop modes. Callbacks
are main-thread callbacks and may stop/destroy their owner; no callback should
throw. Read/write tokens are pasteboard-name + stable changeCount, independent
of foreground app changes. Successful own replay updates the observation token
and does not loop back into history. Initial cached count zero matches Clipy:
the first tick also examines existing content, through the same privacy gate.
Monitoring samples changes; it cannot
capture every intermediate clipboard version between ticks.

Reads retry at most three times when changeCount changes during metadata/data
acquisition. A declared readable native representation that cannot be fetched
aborts the snapshot rather than silently persisting a partial entry. Supported
modern/legacy forms cover Text/RTF/RTFD/HTML/PDF/Files/URL/PNG/TIFF. Modern types
replace their legacy alias within an item; PNG takes priority over TIFF **within
the same item**, preserving a separate TIFF-only item. Native bytes and original
item boundaries remain intact. Root fallback handles formats absent from items;
RootPasteboardItem (UINT32_MAX) explicitly identifies those representations.

Legacy NSFilenamesPboardType is retained on the root, including its complete path
array even when AppKit synthesizes a root public.file-url for the first path.
It cannot be put on NSPasteboardItem; replay writes its raw data on the root
before modern items. The other six legacy aliases replay unchanged bytes under
their modern UTI because NSPasteboardItem rejects legacy names. Alias/modern
collisions are rejected before clearing the clipboard. Other root fallback forms are attached to the first replay
item so root readers can retrieve them. Sparse normal indices replay as ordered
native items, preserving grouping, not numeric gaps. Clipboard replay is not a
transaction: validation/preparation occurs before clearContents, but a native
server/write failure afterward can leave the clipboard replaced or incomplete.
No auto-paste or OS focus/permission automation is implemented.

## Privacy and permissions

Read metadata enumerates complete root/item offers and cooperative markers first.
T03 is evaluated with empty payload placeholders before dataForType. Rejected
reads return a metadata-only snapshot that the shared gate rejects again; allowed
reads fetch only registered, enabled, non-suppressed formats. Unknown types,
disabled HTML and privacy markers' payloads are not fetched by this adapter.
The capture policy used by the reader must match the dispatch policy; configure
via MacCaptureSession.start or stop/restart for settings changes.

The native type registry is static trusted backend configuration, independent of
the enabled switches. Frontmost bundle ID from NSWorkspace is only BestEffort,
not clipboard authorship. Source-app capability remains Limited; unknown sources
remain Unknown and default reject with configured exclusions. Marker-based
exclusion is checked even for unknown native formats. Concealed/remote metadata
survive when allowed, but arbitrary marker payloads and source IDs are not
replayed as clipboard content. This does not detect every password manager.

On macOS 15.4+, accessBehavior is checked before background reads. General
Ask/default is Limited and paused, AlwaysDeny is Unavailable, and explicit
read_snapshot(true) may trigger the system permission prompt for Ask/default.
It never bypasses Deny. Named pasteboards use their own system access behavior.
Pre-15.4 builds use the older native API and explain cooperative-marker limits.
Dynamic status reasons are exposed to the caller; there is no product settings
UI yet. Actual General-pasteboard consent dialogs are a separate manual gate.

Bounds apply to advertised IDs, candidate count and NSData length before C++ byte
copy (default 32 MiB representation/64 MiB entry, configurable downwards). AppKit
dataForType is an eager native API: provider/server allocations and native call
latency are not bounded/preempted by these checks. No image decoding occurs here.

## Application/worker ownership

MacCaptureSession owns the backend and a serial libdispatch queue. Only that
queue opens/writes/closes Store. Accepted capture goes through dispatch_capture
and its sink opens SQLite lazily: rejected captures do not even create a DB.
UI reports contain disposition/reason/retained ID, not a retained clipboard blob.
Thumbnails/OCR are not implemented; OCR capability remains unavailable.

Pending captures are capped at two and a conservative capacity estimate of
128 MiB by default (queue limits configurable within hard ceilings). This is a
bound on retained queue input, not total process RSS/AppKit memory. Overflow
returns Busy / "Capture queue capacity exceeded; snapshot skipped"; it does not
silently grow the queue or block capture behind disk I/O. stop cancels queued
generations and drains in-flight work before closing Store on its worker. It may
wait for a current SQLite/native operation; callbacks never synchronously wait
on main from the worker. Policy/backend callbacks and ownership remain main-only.

The application host still owns private database directory permissions and
single-instance locking. No standalone production app/menu, launch-at-login,
hotkeys, paste injection, thumbnailer, OCR engine or updater is added in T04.

## Verification

See [manual checklist](../../tests/manual/macos.md), [fixture attribution](../../tests/fixtures/macos/README.md)
and [T04 results](../../docs/support/t04-results.md). Automated tests use only
unique named pasteboards and generated fixtures; they never read/replace General.
Provider wrappers are explicit fault injection, not proof of actual OS consent UI.

# T12 implementation ledger

Base: `89bbc9ebb0fa9d53e67b9636eb952c06002db4eb`.

Scope: H12 previews and S07 screenshot file observation. T13 is not started.
Manual T04–T11 acceptance remains OPEN. No clean Windows/macOS VM is available;
CI is not substituted for interactive desktop acceptance.

Baseline: Linux CTest 38/38 passed (out/t12-baseline.log).

Ruling: observe only an explicitly configured directory, default disabled —
this avoids importing arbitrary personal images; cost: users must select a folder.

Ruling: screenshot source identity is Unknown, including macOS — file events do
not prove an application identity; cost: default app exclusions block screenshots.

Screeen 3.0.0 at f8f4bfa6cb38cb69ec1ee104fb5ad949a92b2ae3 was examined:
NSMetadataQuery with kMDItemIsScreenCapture=1, configured screenshot directory
and Desktop scopes, update notifications. Clipy handles added files with NSImage.
Explicit folder selection is a deliberate scope difference.

T11 deferred minor remains: native XML picker may replace pending transfer status
with Saved; transfer remains locked. This is not part of T12.

Ruling: expose a typed PreviewTicket containing digest, size and generation rather
than an untyped void request — completion can verify all three atomically;
cost: internal API differs from the plan sketch, no external compatibility exists.

Ruling: a single worker performs file stability reads and codec work, while the
GUI thread commits only completed bounded results — each Store remains serialized;
cost: a growing file can delay other queued media work for up to ten seconds.

Ruling: screenshots import complete PNG files only; clipboard image previews use
the platform codecs for supported native image types — PNG is the portable format
and requires no new codec dependency; cost: non-PNG screenshot configurations need
conversion or a later extension.

Verification so far: Linux 40/40 groups; Windows 13/13 selected safe groups.
Windows stop regression was reproduced by native_media, then fixed: normal watcher
shutdown no longer reports loss of notification events. Native tests cover system
PNG decoding, malformed/40 MP input, partial writes, dedup in both arrival orders
using distinct PNG encodings, privacy exclusions/types, stop/restart and late deletion.
Native directory create/rename notifications are exercised on Windows/Linux;
macOS Spotlight event delivery remains manual (query start/stop is automated).

TDD note: the first core contract test failed to link before media.cpp existed,
then passed. Native integration tests were added after the initial native helpers;
they caught the Windows shutdown regression. This is weaker than full test-first
coverage for every native helper and is recorded without claiming otherwise.

Tool compatibility: task-start expects headings `Task 12`; the approved plan uses
`T12`. Its exact T12 text was read directly; task-done does not depend on heading
syntax. The task base remains the SHA above.

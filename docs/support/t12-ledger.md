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

Final review: one fresh read-only gpt-6-astra review of 89bbc9e..ade6bb6.
No Critical; four Important accepted, one Minor deferred. No re-review.

Final: fixed pending screenshot resurrection after clear/deletion — media_review_clear
RED→GREEN; a persistent `t12.erase_generation` internal settings key is advanced
transactionally by erase_history and periodic cleanup, including an empty clear.
Screenshot ingest checks the expected generation inside its own immediate transaction.

Final: fixed directory replacement escape — media_review_root RED→GREEN; pinned
directory handle/openat on POSIX, pinned handle plus final parent identity on Windows.

Final: fixed watcher failure race — observe is atomic, monitor failure is injected
from another thread in media_review_error; the former status test was RED, now GREEN.
This verifies observable failure behavior, not a TSan proof (TSan was not run).

Final: fixed invisible observation failure — media_review_error RED→GREEN; persistent
capability status, disabled menu notice, Settings status on reopen and runtime failure;
Linux pre-unmount/unmount/root-deletion events report monitor loss. Normal stop resets
the status. Three regressions GREEN, full Linux43/43, local Windows16/16 safe groups.

Final: minor (deferred): disabling image_previews hides menu images but does not
avoid worker decoding/cache generation; resource-saving behavior needs a follow-up.

Final: Ruling: fresh T04–T06 clipboard owner/INCR audit stays outside this review —
their unchanged regression suites run; cost: native interactive defects may remain.
Final: Ruling: fresh T08 lost-target/permission audit stays outside this review —
unchanged paste suites run; cost: real desktop paste restrictions remain manual.
Final: Ruling: full T11 XML re-audit stays outside this review — unchanged XML suites
run; cost: manual editor/transfer behavior is still unaccepted.
Final: Ruling: T16 OCR races are not implemented here — only preview completion is
guarded; cost: future OCR must adopt an equivalent completion contract.
Final: Ruling: interactive Spotlight/desktop permissions remain manual — query
startup is not proof of indexing/delivery; cost: real Mac screenshots may need fixes.
Final: Ruling: final size/CI acceptance waits for exact fixed runtime measurements —
initial green CI is insufficient; cost: publication waits for a new seven-job run.
Final: Ruling: T13 stays untouched as requested — cost: lifecycle/localization is pending.
Final: Ruling: no non-PNG import or automatic folder discovery — follows the scope
rulings above; cost: users explicitly choose a PNG screenshot directory.
Final: Ruling: existing T04–T11 manual gates stay OPEN — cost: no general release claim.

Ruling: internal erasure metadata uses its own t12 settings namespace, with no schema
version increase — existing T09 readers select only t09 keys; cost: older binaries do
not advance this new generation, so simultaneous older/newer writers are unsupported.

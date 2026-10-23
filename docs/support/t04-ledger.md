# T04 execution — 2026-10-23

Plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md. BASE a303dc9c427b1e8a8705c7043a7be2f3595c0442.

User authorizes macOS clipboard backend, privacy integration, size checks and upload. T05+ excluded. Reuse clean dedicated codex/portability-plan checkout/draft PR.

Preflight: backend callbacks run on main dispatcher; application-owned serial queue alone touches Store. All captured content must pass T03 before persistence/derivatives. Source identity is NSWorkspace foreground best effort, never clipboard authorship. Native type registry includes all supported kinds independent of toggles. Original offers/markers survive until privacy evaluation.

Ruling: use Objective-C++/ARC + system AppKit, with no new third-party runtime. macOS named pasteboards isolate automated tests from general clipboard; real NSPasteboard operations run only in macOS CI. Provider subclasses are limited to deterministic fault/root-only conditions and are labelled as such.

Ruling: accessBehavior (macOS 15.4+) is checked before background reads. Ask/default General access pauses capture with a reason instead of silently triggering a repeated prompt; explicit user-initiated reads can request the system prompt. Deny remains unavailable. Real consent UI/app-to-app manual verification cannot be substituted with provider injection or hosted CI.

Ruling: bound NSData length before C++ copy and enforce representation/entry limits, but NSPasteboard dataForType is an eager OS API. No claim of bounding provider/server allocations or preempting a stuck native provider. Root-only representations use reserved item_index UINT32_MAX; normal item indices are preserved.

Status: in progress. Native RED/green evidence and fresh final review pending. Manual app/permission scenarios remain explicit acceptance items until actually exercised.

Native RED: run39949102889 (0b0f83a), all 15 native scenarios failed on not-implemented backend; core suites stayed green. First implementation b9e7a5c reproduced root fallback/replay failures: legacy filenames is not a valid NSPasteboardItem UTI. Fixed actual root setData path and retained complete legacy array when modern root URL is synthesized. e364191 CI39950784833: 6/6 jobs PASS.

Fresh gpt-6-astra review: two P2 reliability issues, no critical privacy leak. Regression run39951378417 (9bdad50) reproduced stale off-main stop cleanup, absent count backpressure and absent byte backpressure. Generation-scoped cleanup and bounded pending inputs fixed; run39951888081 (4755ee2) 6/6 PASS. Minor coverage requests addressed: Ask/default/deny/recovery injection, item payload read counting, and session destruction inside completion. No deferred review minors.

Ruling: default pending input is capped at two snapshots/128 MiB conservative retained-capacity estimate. Overflow drops the new capture with an explicit Busy event; it does not silently grow memory or stall the UI behind SQLite. Cost: a copy can be skipped under persistent I/O pressure; UI must surface that condition.

Run39952471330 (3cc2ff2): 6/6 PASS including full root read/replay, compiled opt-in manual helper, 25 native cases, macOS and Linux ASan/UBSan. No General clipboard mode runs in CI. Final upstream parity check found Clipy's cached count starts at zero; extra native RED run39953094514 (002ed34) reproduced missing startup capture. Start now preserves cached/own-write counters and captures existing data on first tick like upstream. Final GREEN pending.

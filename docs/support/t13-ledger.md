# T13 execution ledger

Base1b3357a95193315d69f33ebeddcec5cfab7a2c96 → initiale017a5f → fixed runtime45d2349441ae51a36d78c68d94e7d789db687c0a. Existing approved T13 plan executed inline; no separate design approval needed. Existing isolated branch reused. One fresh gpt-6-astra read-only reviewer; no recursive review/subagents.

Review: With fixes;0Critical,2Important fixed in one pass,1Minor deferred. Complete findings/rulings/costs are in t13-results. All declined-to-judge items received an explicit decision; manual T04–T12 unchanged OPEN, T13 manual OPEN, T14 excluded.

RED evidence: initial route/API/registration/foreign DB/empty restart/fallback/GUI target; review initialization gate, symlink ancestor, stale preference rollback. GREEN: local full suites and exact runtime CI7/7. Initial Mac CI40082421053 failed lifecycle open via /var symlink; subsequent CI40083243041 supersedes that result. No clean-VM, logout/login, signing, screen-reader or DPI acceptance claimed. Partial translations retained honestly; no new heavy runtime.

Ruling: OS locks + existing SQLite mailbox — reuses dependency and keeps IPC bounded — cost:100ms polling, bounded busy waits, no exactly-once crash delivery.
Ruling: macOS login uses proper own bundle — SMAppService requirement — cost: unbundled probes cannot prove login or exact prior approval restoration.
Final: Ruling: owner-change capture — unchanged transport accepted with retained automated suite — cost: real owner-change/manual T04–T06 remain OPEN.
Final: Ruling: delete/OCR/thumbnail — retained cancellation/generation tests, no T16 work — cost: manual media acceptance OPEN, OCR absent.
Final: Ruling: denied/lost-target paste — retained boundary tests/manual fallback — cost: real app-to-app T08 remains OPEN.
Final: Ruling: INCR/image/XML bounds — retained bounded suites plus new IPC checks — cost: hostile live producer manual acceptance remains OPEN.
Final: Ruling: real desktop/login/signing/GUI/DPI/readers — explicit manual gates, CI not substituted — cost: no desktop/assistive-tech/clean-install acceptance claim.
Final: Ruling: final size/exact-code CI — four hashed reports and exact runtime7/7 CI verified — cost: review host measured, not installer/update/full distribution.
Final: Ruling: T14 migration — excluded by user — cost: migration absent, T14 not started.
Final: minor (deferred): T13 --background still opens launcher.
Final: minor (deferred, carried): T11 generic Saved during locked XML worker.
Final: minor (deferred, carried): T12 preview-off still decodes/caches.
Final: fixed restart request loss — lifecycle initialization-gate regression RED→GREEN, full suite48/48.
Final: fixed stale-preference rollback — isolated autostart regression RED→GREEN, full suite48/48.
Final: fixed macOS symlink ancestor open — Linux symlink-parent regression RED→GREEN, full suite48/48 and hosted macOS35/35.

Implementation and proof commits published on codex/portability-plan, existing draft PR1 updated; no merge/release. Owned SDD test log copied into evidence before cleanup. Initial native wiring was not uniformly test-first. Runtime remains review-stage software.

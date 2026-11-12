# macOS startup permissions implementation plan

> Use superpowers:executing-plans to implement inline, with test-first validation.

Goal: block normal macOS startup until clipboard and automatic-paste access pass.
Spec: ../specs/2026-11-13-macos-startup-permissions-design.md

- [x] Add native startup window and injectable permission calls. Test ready fast
  path, denied startup, disabled Continue, independent requests, final recheck,
  cancellation, foreground/fullscreen-capable presentation and timer cleanup in
  English/Russian. Observe initial failing test before implementation.
- [x] Integrate after locale activation and before runtime initialization; bypass
  smoke/demo only. Preserve native permission ownership and existing user data.
- [x] Build Debug/release with updated embedded Network settings, run complete
  suites, review changes, record actual evidence and remaining manual gates.


Validation: Debug 61/61 (56.66 s), release 61/61 (64.92 s). Test-first failures
covered missing gate behavior, modal timer dispatch and native UI cleanup.
Review found a LAN duplicate-replacement loss; deterministic reconnect/ACK/expiry
test failed before peer retention and passed after it, plus 20 Service repeats.
No worktree was created; the shared branch sync and local changes were preserved.
Live OS grants and visual desktop acceptance remain manual, not fixture claims.

# Windows/macOS CI recovery — 2026-11-16

Branch: feature/windows-workspace. Starting commit: 5eb2e63.

## Safety constraints
No tests disabled, no successful status substituted for a failed test, no personal clipboard fallback, no OS security setting or signature check weakened. Application changes are limited to the demonstrated LAN publication race described below. CI/test preparation changes preserve existing checks.

## Local Windows isolation diagnosis
All eleven local native failures originate in the same IsolatedClipboard fixture before clipboard access. The runner token is not elevated. A named CreateWindowStationW call fails with ERROR_ACCESS_DENIED (5). Microsoft documents that specifying a name requires Administrators membership. A system-named CWF_CREATE_ONLY call instead fails with ERROR_ALREADY_EXISTS (183). Opening that existing station would violate fresh isolation; it is not used.
Source: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowstationw
The suite must be run on an appropriately provisioned test runner. This session does not elevate credentials or request UAC interaction.

## CI investigation
Full Actions log download requires repository-admin/auth access not available through the current session. Public job/check annotations are available. Added bounded CTest failure annotations and command-failure tails, retaining original failing exit codes and escaping workflow command data.

### Commit 8c8d719
- Missing pinned TLS preparation fixed in Linux/macOS sanitizers, Windows ASAN and standalone OCR/Sparkle drivers. Fetcher and hashes unchanged; configure remains offline and verifies dependencies.
- Tool regression tests demonstrated five missing entrypoints before correction.
- First recovery CI run: 43784387765. Sparkle macOS and Windows native OCR now passed; sanitizer configures/builds reached actual tests.

### Commit b8055e2
- Windows/Linux native settings fixtures expected eight tabs despite the retained Sounds ninth tab. Updated strict count and verified the Sounds label rather than removing a tab or weakening the check.
- macOS workspace fixture expected Finder reopen to reveal start_hidden UI. Updated to assert hidden/released workspace on reopen and explicit show restores the actual list.
- macOS snippets shortcut probe's menu validation depended on the real system pasteboard. Its fixture now validates its own private copy/cut/paste operations while retaining content and routing assertions.
- TLS transport test hard-coded ports49881/49882. Occupying49881 reproduced the CI a-start failure. OS-selected distinct loopback ports avoid that collision; wrong-key, identity, framing, ACK, replay and bounds tests remain intact. Updated test passed with49881 occupied.
- Mac mesh E2E failure not yet explained; added precise stage/predicate/state diagnostics without changing semantics or increasing deadlines.

## Local verification
- CI-equivalent Debug preset configured and all328 build steps completed.
- Debug CTest:64/75 passed,11 known window-station isolation failures,134.21s. Compared to the probe-enabled76-test build, the socket idle probe is not enabled in the normal CI preset.
- Python tool suite:32 tests, success,3 existing Windows symlink privilege skips.
- Updated native_settings_test compiled; runtime remains behind the isolation precondition.
- Updated TLS transport security suite and LAN E2E passed; final diagnostic-only edits compile.
- Remote validation for b8055e2 is being monitored; no success is assumed from a submitted workflow.

## Follow-up evidence
- b8055e2 Windows AddressSanitizer job completed successfully (authoritative workflow https://github.com/Hydrock/Cliptiq/actions/runs/43785258993). Native isolated tests run successfully on its correctly provisioned runner. Local rights were not changed.
- Linux sanitizer job also passed in subsequent run. macOS workspace and private-paste assertions advanced successfully; snippets close/reopen exposed a separate asynchronous unmount race in the test. Commit3d2f93c waits on actual unmount and retains pending-save close veto/undo checks.
- Windows package failure reproduced locally: validator reported missing dependency DNSAPI.dll. This is the documented Windows DNS-SD system DLL, not an application redistributable. Added its exact basename (case-insensitive); paths and unknown/vendor names still fail. Regression test observed red then green. Source: https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsservicebrowse
- Package now passes closure validation and hits the unchanged size gate: app2388194 bytes >1867254; ZIP1178977 >957588. This package includes LAN/TLS, compared with the older baseline. No size threshold, growth allowance or feature default has been changed to conceal that result.
- macOS OCR packaging likewise reaches the size gate; the native OCR build/test stage itself succeeds. Packaging errors now annotate their command failure tail.
- GitHub public API core quota reached zero (reset01:48:31UTC); further API polling stopped. Normal public workflow UI remains available for read-only status/annotations. No authentication/rate-limit bypass attempted.

### Final test-race corrections (184ad63)
- Transport1MiB stage failed because callbacks incremented arrivals before persistence ACK released the sender budget. A temporary300ms callback delay reproduced the exact failure. Replaced two50ms sleeps with ready_to_publish predicates; the same perturbation and final clean test passed. Six-second delivery deadline and protocol/security checks unchanged.
- macOS close veto intentionally clears the text first responder; the test now explicitly resumes editing before Undo. It also waits for its owned key window before testing global nil-target main-menu actions. Actual shortcut/content/undo checks remain.
- Size-report stdout now includes budget_violations, while oversized packages retain exit1. Verified locally with exact values2388194 >1867254 and1178977 >957588.
- Tool suite now33 tests, successful with3 pre-existing symlink-privilege skips.
- Follow-up independent review reported no actionable correctness/security findings.
- Final source validation workflow: https://github.com/Hydrock/Cliptiq/actions/runs/43786824294 (commit184ad63).

## Remote evidence before the final transport correction
Workflow43786824294 / commit184ad63:
- windows_backend_asan: success,4m46s. The native isolation-dependent tests ran on the provisioned Windows runner; no local privilege change or isolation fallback was needed.
- macos_backend_sanitizers: success,3m24s.
- Linux sanitizers: success,3m24s.
- macOS ARM history reached packaging, so both required Debug and Release CTest steps succeeded. Packaging then failed the unchanged size policy.
- macOS native OCR build/tests reached packaging on ARM and Intel. Intel OCR size gate reports app3125127 >2340296 bytes, archive1302311 >1026273 bytes. ARM OCR reports app3070679 >2350777, archive1222241 >987169. ARM base archive1115422 >866157.
- Separate Linux packaging blocker surfaced: existing GTK/Pango import libpango-1.0.so.0 is not recognized in its platform dependency list. No Linux clipboard isolation workaround was introduced. The suspected Xvfb-reset race was not changed without runtime proof.
- All size policy files, limits and growth allowances remain unchanged. A green release/package pipeline is NOT claimed.

### Main matrix infrastructure limit
- Final-source Windows history job hit the15-minute job execution cap, despite Windows ASAN successfully completing its suite. The history job includes Debug build/tests, a separate IPO Release build/tests, then packaging. Increased only this history-matrix job budget to30 minutes; individual CTest timeouts, all required steps and package/security/size gates stay unchanged. This is runner provisioning, not a test bypass.

## Demonstrated product defect: LAN publication admission
A Service worker callback could queue a publication while an authenticated peer was live, but recipients were not selected until the next I/O pass. If that socket disappeared first, no outstanding recipient record was created and the event was lost. The same issue explains intermittent first-copy and mesh delivery failures despite eventual Connected statuses.

Two permanent synchronized transport regressions:
1. The receive callback admits a reply, then the current raw TLS peer disconnects before callback return/next-pass fanout. The already-admitted reply must survive an authenticated reconnect. This failed on the old transport and passed with the correction.
2. PeerA is retired by invalid framing before peerB's receive callback admits a reply in the same I/O pass. RetiredA must NOT be enrolled or replay the reply after reconnect; liveB still receives it. This exposed a stale-cache edge in review and failed before immediate liveness refresh was added.

Final design:
- Publication admission snapshots only authenticated, send-enabled recipient indices under the existing publication mutex.
- HELLO authentication and every retirement update eligibility in the same critical section as the liveness transition. PSK, remote identity pinning and canonical duplicate selection remain enforced.
- Retained payload and original10-second deadline are enrolled for those recipients at dispatch, even if their socket was replaced. Peers offline at admission are excluded.
- Existing one-publication/shared-payload budget, ACK release, size caps, expiry and stop/reconfigure cleanup remain in force. No callbacks execute while the publication mutex is held; outside threads do not read connection objects.
- Fresh independent security/correctness review: no remaining actionable findings after the retired-peer regression correction.
- Owner verification: full TLS/security suite and E2E passed in both Debug and MinSizeRel; app linked successfully. Parent independently reruns the Debug transport/E2E tests before the production commit.
- Installed application remains the earlier stable build; this task does not replace the running user instance before validation.

## Main Windows lane provisioning result
With the30-minute history-job budget, Windows completed its IPO release build (10m36s) and Release CTest (1m34s), then failed the unchanged size gate at16m16s total. This confirms the earlier15-minute cancellation was insufficient job provisioning, not a passing-test substitution.
CI Windows Base metrics: app2388623 >1867254 bytes; archive1182794 >957588 bytes. Thresholds remain unchanged.

## Production correction verification (e32338d)
- Parent CTest rerun, Debug: lan_end_to_end23.90s and lan_tls_transport44.18s,2/2 pass,68.08s total. MinSizeRel TLS/E2E passed in owner verification; full native app linked.
- Final-source workflow https://github.com/Hydrock/Cliptiq/actions/runs/43793512727: Windows ASAN **success5m13s**, macOS sanitizer **success4m27s**, Linux sanitizer **success3m50s**.
- Main macOS ARM lane passed Debug/Release CTest and failed only at unchanged package size gate. Native OCR build/tests passed on ARM/Intel before the size gate.
- Candidate app built, existing installed user process not restarted/replaced in this task.

## Idle cost comparison
Two sequential30-second measurements per binary, three real connected TLS Services in one synthetic test process. Fake clipboard hooks and test credentials; not main-application memory. Same E2E fixture, before/after executable hashes and all samples preserved alongside this report. No compile/test workloads ran concurrently on this host during measurement; no working-set trimming.

| Binary | Run | CPU % of one core | Private MiB | Working set MiB |
|---|---:|---:|---:|---:|
| before | 1 | 0.000 | 4.406 | 13.988 |
| before | 2 | 0.052 | 4.348 | 13.387 |
| after | 1 | 0.052 | 4.645 | 13.625 |
| after | 2 | 0.000 | 4.863 | 13.828 |

These short observations are not a statistical speedup claim. Idle CPU is at timer-resolution/noise scale (mean about0.026% of one core both before and after). Private commit was higher after by0.238MiB and0.516MiB in the paired runs; physical working-set ranges overlap. These two runs do not establish how much is causal code cost versus TLS/allocator-state variation. This is a three-Service synthetic process, not main-app RAM. No RAM reduction is claimed. The correctness fix preserves bounded one-publication retention and does not add an idle polling timer.

## Final outcome
Final production source: e32338d. Workflow43793512727 completed its Windows/macOS functional test stages, including both Debug and Release on Windows, macOS ARM and macOS Intel. Windows/macOS sanitizer lanes succeeded. No CTest failure annotations remained in this final run.

The overall workflow is still **failure**, intentionally not reported as green:
- Windows Base size gate: app2391695 >1867254 bytes, archive1184576 >957588.
- macOS Intel Base size gate: app2906585 >2082590 bytes, archive1188815 >900205.
- macOS ARM Base archive:1116369 >866157. macOS native OCR packages also exceed existing relative thresholds.
- Separate Linux Base/OCR packaging validation reports libpango-1.0.so.0 as missing from the platform dependency classification; Linux sanitizer/tests passed. No speculative Xvfb fixture change was made.

All budget values and growth allowances are unchanged. The TLS configuration remains the existing restricted TLS1.3 external-PSK/P-256 ECDHE/AES128GCM-SHA256 setup, with no certificate/TLS1.2/ticket/resumption/early-data expansion or reduction of pairing/security requirements. No LAN feature was disabled to make a smaller artifact. Package publication, installer delivery and consumer-VM manual acceptance remain blocked/open.

The eleven original local window-station failures were diagnosed, not hidden or converted to skipped/pass statuses. Their correctly provisioned Windows CI counterparts passed. The local host's OS permissions and personal clipboard/history were untouched.

All source changes are committed/pushed on feature/windows-workspace. The running installed user application was not replaced; the newly linked candidate remains in out/first-run-autostart/Cliptiq.exe.

# Overnight Cliptiq work — 2026-11-16

Checkpoint: `8594afe`, annotated tag `checkpoint/pre-night-2026-11-16`, both pushed before night changes.
Branch: `feature/windows-workspace`.

User authorized unattended execution. Local Windows host; macOS runtime unavailable.

## Decisions and evidence
- Existing first-run and quiet-start changes are preserved in the checkpoint.
- Plan budgets cover up to six hours; work ends when deliverables are complete, without artificial waiting.
- GUI isolation must remain fail closed; no fallback to the personal clipboard.

### Test diagnosis
- Reproduced editor_native failure before clipboard access: CreateWindowStation error 5. Isolation remains fail closed; OS security settings unchanged. The same shared fixture explains the other ten isolation failures.
- Authenticode verifier returned trust_status=0 but publisher_pin_match=0. PowerShell selected a Catalog signature while the verifier checks the embedded signature; fixture SignatureType=Catalog confirmed.
- Changed test-only pin extraction to X509Certificate.CreateFromSignedFile. Existing negative tests (wrong pin, malformed pin, unsigned and tampered file) and positive test pass. Production trust/revocation/pinning code unchanged.
- Source: https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.security/get-authenticodesignature?view=powershell-7.5

### Scope ruling
- Automatic approval review rejected removal of unreachable welcome-dialog code and its test/translations as a possible loss of previously requested functionality. Skipped the cleanup entirely; no approval is being awaited because the user asked to skip unavailable operations.
- Extended the existing isolated measurement harness to verify ordinary launch and three repeat launches without --background. This avoids relying only on background-mode tests for the new default behavior.

### Startup audit and review
- Lifecycle/settings tests already cover persisted off and on through unrelated saves; legacy off rows count as configured. First-run registration uses actual OS state for rollback.
- Ordinary launch with explicit synthetic database (isolated LOCALAPPDATA, no payload retention) remained hidden through three repeat launches. Explicit workspace/settings open routes remain exercised by the harness.
- Read-only independent reviewer reported no actionable correctness findings in script changes or checkpoint startup/autostart paths.
- macOS runtime unavailable locally. GitHub workflow 43754292212 ran after checkpoint push; several jobs failed. Log download returned HTTP 403. Do not infer a passing macOS suite from local Windows tests.
- Existing linked worktree verified via distinct git-dir/common-dir; no new branch/worktree created.

### Final Windows verification
- Build command: powershell -ExecutionPolicy Bypass -File experiments/windows-idle/build.ps1 -BuildDir out/first-run-autostart -Tests -Targets all. Exit 0; no rebuild needed after test/harness-only changes.
- Full CTest with isolated test TEMP: **65/76 pass**, 11 isolation failures, 121.64 seconds. Previously 64/76 passed; Authenticode now passes.
- Failures: editor_native, hotkeys_native_boundary, settings_native, lifecycle_ui_ru, hotkeys_native, paste_native, menu_regression_keyboard, menu_regression_dpi, menu_regression_context, menu_native, windows_clipboard. All report CreateWindowStation error 5; no tests silently skipped or marked passed.
- Python compile check and git diff --check pass.
- Installed application remains running hidden from the stable installation path. No reinstall needed: night changes touch tests, measurement and documentation only; installed/program/checkpoint binary hashes match.

### Performance interpretation
- Before and after executable SHA-256 are identical (5708834d84bc9245194685f02d2fdf2652df36af36a584e378a690c34dc1e7a5). No product optimization was made, so timing differences are measurement variation, not a speedup.
- Sequential 30-second runs, 5-second stabilization, synthetic text entries, capture listener active but clipboard payload retention disabled; OCR, updates and LAN disabled in these main-app fixtures.
- No cache flushing, working-set trimming or changes to process priority. Measurements are local observations, not hard performance guarantees.

| Scenario | CPU one core %, before / after | Working set MiB, before / after | Private MiB, before / after |
|---|---:|---:|---:|
| Hidden, empty, ordinary launch | 0.156 / 0.104 | 14.82 / 14.84 | 2.71 / 2.71 |
| Hidden, 1000 entries | 0.260 / 0.312 | 19.73 / 18.88 | 6.75 / 6.74 |
| Workspace, 1000 entries | 0.104 / 0.312 | 26.33 / 25.46 | 10.57 / 10.69 |
| After settings close, 1000 entries | 0.365 / 0.000 | 24.50 / 23.61 | 8.27 / 8.35 |

Synthetic 1000-row workspace benchmark, median across five runs:

| Metric | Before ms | After ms |
|---|---:|---:|
| open_dispatch_paint_ms | 57.809 | 56.220 |
| filter_median_ms | 3.813 | 3.313 |
| selection_median_ms | 7.311 | 7.411 |
| scroll_median_ms | 1.884 | 1.564 |

Raw metrics are preserved in the adjacent overnight-2026-11-16 directory. Both ordinary-launch fixtures passed three repeat launches; explicit workspace open, settings destruction and reopening also passed.

### Delivery
- Night correction commit: 780df18 (test fixture, quiet-launch measurement, plan).
- Product functionality and installed binary remain unchanged relative to checkpoint 8594afe.
- No reboot/logoff, security changes, dependency installation, release publication or merge performed.
- Deferred: actual logout/login and macOS interactive checks, isolated GUI tests requiring unavailable permissions, unreachable-dialog cleanup rejected by automatic approval review.
- The available validation package completed early. Six-hour plan budgets are not a claim of six hours of elapsed work; no idle waiting or speculative feature work was added to fill the night.

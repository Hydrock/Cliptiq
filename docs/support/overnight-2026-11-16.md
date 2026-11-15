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

# Overnight reliability and performance implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. User authorized unattended execution and skipping operations that need their participation.

**Goal:** Preserve Cliptiq functionality while improving startup reliability, validation coverage and measured performance.
**Architecture:** Keep native Win32/AppKit and current SQLite storage. Change only demonstrated defects or measured bottlenecks; validate each change independently.
**Tech Stack:** C++17, Win32, AppKit, SQLite, CMake/CTest, Python performance harnesses.
**Spec:** User requests in this chat: work only in feature/windows-workspace; quiet tray startup; default first-run autostart persisted across updates; preserve functionality.

## Global constraints
- Only feature/windows-workspace in the existing lan-clipboard worktree.
- Preserve clipboard history, preferences, LAN behavior and native UI.
- No reboot, signing identity changes, OS security changes or edits to personal clipboard data.
- Use isolated synthetic test profiles. No new dependencies or major feature expansion.
- Never disguise a failed or blocked test as a pass. macOS runtime checks require macOS and remain explicitly unverified here.
- Before night changes: commit existing state, annotated checkpoint tag, push both atomically.
- Times below are work budgets, not promises to wait or pad work; finish early if verified work is complete.

## Review focus
- Missing Explorer at login: remain hidden and restore tray after TaskbarCreated.
- Persisted off / OS disabled login entry: never silently re-enable on update.
- Registration failure: preserve user intent and expose actionable status.
- Clipboard test isolation failure: fail closed; never use the personal clipboard as fallback.
- Empty and large histories: bounded CPU, memory, rendering and IPC costs.

### Task 1: Checkpoint and plan (0:00–0:20)
**Files:** existing uncommitted first-run/quiet startup work; this plan.
**Interfaces:** produces immutable Git checkpoint for rollback and comparison.
- [x] Commit current state: 8594afe.
- [x] Annotated tag checkpoint/pre-night-2026-11-16.
- [x] Atomic push branch and tag to origin.
- [x] Save plan and maintain progress/results in docs/support/overnight-2026-11-16.md.

### Task 2: Reproduce and classify test failures (0:20–1:30)
**Files:** tests/fixtures/windows/IsolatedClipboard.hpp; tests/integration/update_authenticode.py; native update trust implementation discovered by rg.
**Interfaces:** existing CTest names and verifier CLI remain unchanged.
- [x] Reproduce CreateWindowStation access failure and Authenticode positive fixture failure, retaining diagnostics.
- [x] Identify whether failure is harness, environment or product. Do not weaken trust validation or clipboard isolation.
- [x] For a demonstrated fix, establish red reproduction, implement the smallest correction and rerun affected tests.
- [x] If environment privileges or signing infrastructure are required, record blocker and continue.
- [ ] Commit independently validated corrections.

### Task 3: Startup/autostart audit (1:30–3:00)
**Files:** platform/MenuHost.cpp; platform/Lifecycle.cpp; platform/windows/Lifecycle.cpp; platform/macos/Lifecycle.mm; tests/unit/lifecycle_test.cpp; tests/unit/settings_test.cpp.
**Interfaces:** Autostart::apply_first_run / apply / inspect; existing Settings login checkbox/status.
- [x] Trace normal, background, repeated and explicit-show startup, save failures and OS-disabled registration.
- [x] SKIPPED: welcome-dialog cleanup rejected by automatic approval review; functionality preserved.
- [ ] Add behavioral regression coverage for any demonstrated defect before changing production code.
- [ ] Build all Windows targets and run lifecycle/settings/quiet-start tests.
- [x] Statically review macOS changes and record runtime limitation. Commit validated changes.

### Task 4: Performance baseline and improvements (3:00–4:30)
**Files:** experiments/windows-idle/measure.py; workspace-benchmark.cpp; relevant source only if evidence identifies a bottleneck.
**Interfaces:** existing JSON counters, isolated synthetic profiles and workspace benchmark CLI.
- [x] Preserve checkpoint binary/hash before rebuilding.
- [ ] Sequential measurements: hidden empty history; hidden 1000 entries; workspace 1000 entries; settings open/close. Same fixtures, durations and warmup.
- [x] Inspect measured hot paths; optimize only with a reproducible performance case and behavior tests.
- [ ] Repeat identical measurements after changes; report CPU, private bytes, working set and workspace latency without overstating noise.
- [x] If no reliable gain is available, preserve implementation and publish baseline rather than speculative changes.

### Task 5: Final verification and handoff (4:30–6:00)
**Files:** docs/support/overnight-2026-11-16.md; final changes only.
**Interfaces:** build.ps1, full CTest, Git history and installed binary.
- [ ] Build all targets; run full suite with clean test TEMP; compare failures to checkpoint.
- [ ] Fresh whole-branch code review, fix important findings and rerun relevant suite.
- [ ] Record exact commands, results, benchmarks, blockers and unverified platforms.
- [ ] Commit results and push feature branch normally, no force push or merge.
- [ ] Update installed Windows build only after validation; leave it quiet in tray and preserve settings/history.

## Progress
Checkpoint pushed successfully before product changes. Subsequent decisions and evidence go in the support report.

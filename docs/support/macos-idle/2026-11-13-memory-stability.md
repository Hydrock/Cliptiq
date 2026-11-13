# macOS memory stability audit — 13 November 2026

Branch: feature/windows-workspace. The installed baseline was 6371925.
The audit uses physical footprint; RSS is recorded separately and is not added
or compared directly with footprint. These are workload observations, not limits.

## Confirmed growth and fix

An isolated native fixture opens and closes the workspace, visits all ten Settings
pages and expands Advanced LAN controls, then opens/closes the snippet editor.
It alternates a synthetic 512×512 RGBA preview and text, uses a temporary database,
and never writes the system clipboard or accesses user secrets.

Before the fix, the 1,200-cycle run retained 1,200 NSOutlineButtonCell objects.
`leaks --trace` found a system notification-center block retaining each old
NSButton/disclosure cell. This reachable accumulation is not necessarily counted
by the leaks detector. Own Cliptiq view roots stayed bounded (one workspace root,
zero closed Settings roots), so that check alone would have missed the problem.
The final heap was 73.8 MiB footprint; at cycle 100 it was 62.3 MiB. A control
without the snippet editor stayed around 54 MiB through 300 cycles. Disabling
implicit animation did not prevent accumulation. The long baseline leak scan
also reported a separate five-node/432-byte dictionary allocation; it must not
be described as a zero-leak result.

The editor previously recreated its window and outline on every opening. It now
reuses them for the same session and locale and rebuilds the outline only if
folder/snippet structure changes. Selection and detail are refreshed on reopen;
a locale change still rebuilds translated controls. The regression checks window,
outline and node identity across a close/reopen before exercising inline rename
in both locales. It failed on the original implementation.

## Final matched workload

The final 600-cycle run retained one NSOutlineButtonCell instead of the original
600 at the same checkpoint (1,200 in the longer baseline). Its footprint at cycle
100 was 57.5 MiB and at cycle 600 was 55.2 MiB. The original matched checkpoints
were 62.3 and 65.3 MiB. Final footprint is 15.4% lower at 600 cycles and the observed
per-opening growth has stopped. The final heap/leaks files document the remaining
fixed framework startup allocation separately. This is not a guarantee of zero
leaks under every workload.

Release build passed. Full suite: 74/74, 65.43 seconds. Additional EN/RU native
regressions then passed after extending coverage to same-session structural
changes and locale changes while hidden. Independent code review found no
important correctness issues. Persistent signature validation passed against
the installed requirement; installation completed after the user's codesign confirmation. The previous app
was moved to Trash before replacement; installed signature verification passed.
No TCC reset or identity migration was requested.

## Clipboard, codec and LAN workloads

The real installed app captured 62 alternating copy operations from a dedicated
TextEdit document and a synthetic 1024×1024 Preview image. User approved these
fixtures and their transmission to PC. A protected local SQLite backup was made
before testing, outside tracked files. Repeated identical fixtures limit history
churn. Network settings showed PC Connected (TLS 1.3). The user then copied a
new unique synthetic marker on PC; its presence in the Mac database confirmed
receipt. No private clipboard payloads are included in the recorded artifacts.

Installed baseline footprint was 53.25 MiB before copies, 58.60 MiB after, and
58.86 MiB after cooldown. The installed app leak scan after copies found zero
leaks. This small warm-image/cache increase is separate from the editor growth.

An isolated three-device TLS integration workload passed 300 alternating sender
transfers with text and PNG, bounded history (≤30 entries/device), provenance,
no echo, history-only receive and stop checks. Its synthetic clipboard sink is
cleared between iterations and uses a separate monotonic token counter, avoiding
test-induced retention. Fixture ingest timestamps use real milliseconds so
capacity pruning does not immediately discard new local test items. The network
PNG fixture is 1×1; the real app workload uses the larger 1024×1024 image.

The native media integration test passed 300 decode/publish/cancel cycles with a
2×4 PNG, in addition to its malformed image, cancellation, privacy and race checks.
This checks release/cancellation semantics, not large-image throughput. No memory
profile was taken on Windows or on the PC process.

A separate stack-logging run localized the tiny five-node dictionary allocation
(432–480 bytes depending on allocator instrumentation) to HIServices
_RegisterApplication during NSApplication initialization. It is a one-time macOS
startup allocation, not a per-cycle Cliptiq allocation; its stack is archived.
This framework allocation is reported explicitly rather than hidden behind a
blanket zero-leak claim.

## CPU measurement correction

The old sampler interpreted proc_pid_rusage user/system counters as nanoseconds.
They are Mach absolute-time ticks here. mach_timebase_info returns 125/3; a busy
loop measured with CLOCK_PROCESS_CPUTIME_ID and Python process_time independently
confirmed the factor. Prior CPU figures were understated by 41.67×. Raw saved
measurements and the two earlier reports are corrected, retaining the original
aggregate CPU value in legacy_cpu_percent_unconverted. Memory values are unchanged.
The sampler now converts ticks and a CTest regression compares its clock against
process_time. Fresh installed background intervals measured about 2.94–3.25% of
one core, not the previously reported ~0.07%. CPU optimization is outside this
memory-growth fix.

## Reproduction

```
cmake --preset release
cmake --build --preset release --target macos_memory_stability
out/core-Darwin-release/macos_memory_stability 600 300
CLIPTIQ_LAN_STABILITY_CYCLES=300 out/core-Darwin-release/lan_end_to_end_test
CLIPTIQ_MEDIA_STABILITY_CYCLES=300 out/core-Darwin-release/native_media_test
ctest --preset release --output-on-failure
```

Results are under experiments/macos-idle/results/2026-11-13-stability.
Native GUI profiles require a logged-in macOS GUI session. The fixture accepts
CLIPTIQ_STRESS_SKIP_EDITOR and CLIPTIQ_STRESS_NO_ANIMATION for diagnostic controls.

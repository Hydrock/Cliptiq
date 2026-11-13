# macOS memory profile and optimization — 13 November 2026

Baseline: `8f45652`; final optimization code: `6371925`, installed and verified
in `/Applications/Cliptiq.app`. Executable hashes and final-package checks are
recorded in comparison.json.
macOS 27.2 arm64, 18 logical CPUs, the same real user profile (30 displayed
history records). Previous app packages were moved to Trash before replacement.
Both executables use the existing persistent local signing certificate. No TCC
reset, key migration, history deletion or LAN configuration change was needed.

## Measured comparison

The table uses the second controlled A/B series, not the historical figures
from the earlier task. Values are mean physical footprint in MiB. CPU is process
user+system time as a percentage of one core (100% = one fully occupied core).

| Scenario | Before, MiB | After, MiB | Reduction | CPU before → after |
|---|---:|---:|---:|---:|
| Фон, свежий запуск | 30.0 | 27.7 | 7.8% | 0.066% → 0.067% |
| Общие настройки | 55.4 | 43.6 | 21.3% | 0.076% → 0.077% |
| Сетевые настройки | 59.1 | 44.1 | 25.3% | 0.070% → 0.067% |

For the A/B series, each archived, signed app was installed in turn. Each process
started with `--background`; a stack sample confirmed NativeMenu's running main
loop before sampling. Both then followed General → Network in the same order.
Each recorded interval is 20 seconds, sampled each second, after 10 seconds of
background settling or 5 seconds of UI settling. Discovery and sharing stayed
enabled; both Network pages showed PC Connected (TLS 1.3). This checks live LAN
operation but does not benchmark sustained transfer throughput or watts.

The earlier exploratory 30-second series is retained as supporting data. During
it the peer briefly showed a TLS failure, so that mixed-network series was not
used as the final comparison. A premature 7.1 MiB after-startup result was rejected
because the process had not finished starting. A main-window interval rose from
68.6 to 94.5 MiB while sampling; it is retained as unstable and is not used to
claim a main-window reduction. Absolute figures vary with AppKit caches, UI
state and clipboard workload; these are observed results, not guaranteed limits.

## Profile and confirmed causes

Xcode/Instruments `xctrace` is unavailable in the installed command-line toolchain.
Native `heap -s --noContent` and `vmmap -summary` were used instead. No object
contents, clipboard strings or credentials are included in the saved profile.
These are aggregate live-allocation/VM profiles, not a full malloc-stack/leak study.

The baseline created the entire ten-tab settings UI eagerly, plus the hidden
manual network form. It also rendered discovery controls before any settings
window existed and retained settings controls after a close. Modern AppKit's
control backing graphs account for substantial additional SwiftUI/AttributeGraph
allocations even though the application itself uses AppKit.

Representative Network heap snapshots (the first baseline run and final optimized
run, both with the Network page visible and the same profile):

| Live allocation/object | Before | After |
|---|---:|---:|
| Total heap allocations | 296,936 | 169,853 |
| Allocated heap bytes, MiB | 33.74 | 23.89 |
| NSTextField | 177 | 33 |
| NSStackView | 89 | 25 |
| NSButton | 67 | 15 |
| NSLayoutConstraint | 1,598 | 696 |
| SwiftUI ViewGraph | 116 | 37 |

In cold background snapshots, detached discovery controls disappeared: NSTextField
8 → 1 and NSButton 6 → 0; heap allocations fell 59,447 → 49,733. The remaining
AppKit/Foundation, allocator and networking overhead is not all application-owned
cache. Shared-library mappings and RSS must not be mistaken for private footprint.
No large decoded-image allocation dominated this selected-text workload; image-
heavy history would require its own additional profiling scenario.

## Changes

- Build each settings tab's controls only on first selection. Preserve drafts
  across tab switches and apply edits over the existing settings snapshot.
- Build manual LAN fields and interface buttons only when Advanced settings is
  first expanded. Keep entered drafts when it is collapsed/reopened.
- Release the settings view tree and field editor after closing, and release the
  embedded LAN view references on the next owner-thread event pass after an
  NSWindowWillClose notification. Hiding a window or switching tabs preserves
  controls and unsaved fields. An orderOut/reopen regression test verifies this.
- Keep background discovery state without rendering detached controls. Incoming
  pairing still opens its approval panel; protocol and transport code is unchanged.

## Verification

Release build succeeded. New regression checks first failed on eager tab creation
and retaining a closed view tree, then passed with the fix. Native settings tests
also cover hotkey recording, Apply, EN/RU, lazy advanced fields, draft retention,
reopen, compact top alignment and a background incoming-pairing panel.

The first full run passed 72/73, with a timeout in the existing three-device
`lan_end_to_end` test. Its three isolated repeats passed. The final complete
Release run passed **73/73 in 67.91 seconds**. Both the initial failure and retries
are retained, rather than hiding the intermittent transport test failure.

## Reproduction and artifacts

Raw time samples, CPU deltas, RSS, footprint, aggregate heap summaries, VM summaries,
executable hashes and test logs are under
`experiments/macos-idle/results/2026-11-13-memory/`. The original heap outputs stay
in `/private/tmp/cliptiq-*-heap.txt`; committed JSON summaries contain only counts,
byte sizes and class names.

```
python3 experiments/macos-idle/measure-running.py --pid <PID> --scenario <name> \
  --seconds 20 --settle 5 --output <file.json>
heap -s --noContent <PID> > /private/tmp/profile-heap.txt
python3 experiments/macos-idle/summarize-heap.py /private/tmp/profile-heap.txt <summary.json>
vmmap -summary <PID>
```

Regular future installations must use `python3 tools/install-macos-local.py`,
without `--migrate-identity`, to keep the persistent signing identity.

## Final installed package acceptance

The final signed `6371925` package was installed after authorization. It keeps
controls when a window is hidden and releases them only after actual close.
The 73/73 Release run includes the regression that first failed when hiding a
window discarded its manual fields. General → Network → General → Network was
also checked in the installed app; PC remained Connected (TLS 1.3).

A separate 20-second sanity measurement on the final package gave:

| Final package page | Footprint, MiB | CPU, one core |
|---|---:|---:|
| General | 44.8 | 0.075% |
| Network, PC connected | 45.6 | 0.065% |

Compared with the controlled baseline pages (55.4 and 59.1 MiB), these final
observations are approximately 19% and 23% lower. The earlier A/B table retains
its original data (43.6 and 44.1 MiB) from before the final close-notification
refinement; the modest run-to-run variation should not be treated as a precise
regression or guarantee. Both build hashes and all samples are retained.

An additional final direct-background restart waited inside SecItemCopyMatching.
The readiness guard rejected that interval, so it is not used as a low-memory
result. A subsequent normal Launch Services restart reached NativeMenu's event
loop, opened settings and connected to PC without another permission grant.
The background row above remains the earlier ready A/B observation. The startup
Keychain delay is an observed limitation outside this UI-memory optimization.

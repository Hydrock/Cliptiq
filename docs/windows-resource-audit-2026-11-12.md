# Native Windows resource audit — 2026-11-12

Baseline: commit 3459949, MSVC MinSizeRel, static CRT, Windows 11, 32 logical CPUs.
Full build succeeded. Full CTest: 39 passed / 51; twelve existing environment failures:
eleven CreateWindowStation denied (error 5), one update_authenticode positive fixture.
After optimizations, full rebuild succeeded and all 39 runnable tests passed.
Tests were not disabled or weakened. Logs: out/windows-audit/ctest-before.log and
ctest-after.log. Other platforms were not built.

## Changes

- Product Windows compact menus allocate HMENU/bitmap resources only on invocation
  and release them after selection or cancellation. The bounded metadata model
  remains available. Theme/DPI changes also preserve this invariant. Existing
  eager mode remains the default for library clients and regression fixtures.
- A selected large preview no longer computes a full pixel fingerprint: transient
  display is separate from persisted thumbnail deduplication.
- Preview publication checks current history ID metadata instead of reading and
  copying the original clipboard payload again on the UI thread.

Regressions observed RED then GREEN: closed-menu resource retention, theme-change
retention and duplicate payload read (SQLite trace changed from 2 reads to 1).
Tests also check reopening after a model change and preservation of paste target.
Independent source review caught the theme/DPI allocation path; fixed and covered.

## Real background application

Separate synthetic profiles, 1000 text entries, no visible windows, capture listener
active but payload retention disabled; no OCR/Raven/updates. 45-second baseline and
optimized startup samples after 5-second settling. CPU is percent of one logical
core, not total CPU. This is a short local comparison, not a statistical benchmark.

| Metric | Before | After |
|---|---:|---:|
| Working set | 18.18 MiB | 16.52 MiB |
| Private memory | 7.16 MiB | 6.34 MiB |
| CPU, one core | 0.208% | 0.174% |

Working set decreased about 9%; private memory about 12%. The CPU delta is one
Windows accounting tick across the sample and is not evidence of a reliable CPU
improvement. An earlier baseline overlapped the full test suite and had higher
startup CPU; it is retained as before-idle-1000.json but not used for this comparison.
Targeted test activity briefly overlapped startup measurements; no system-wide
background activity was controlled. After opening and closing settings, optimized
memory was 19.96 MiB working set / 6.56 MiB private, CPU 0.208%. The earlier 30-second
baseline after settings was 21.34 / 7.40 MiB and 0.208%; durations differed, so this
is supplementary evidence only. Settings HWND destruction/reopening was asserted.

## Native menu allocation benchmark

Optional menu_benchmark target, enabled by BUILD_TESTING + CLIPTIQ_BUILD_IDLE_PROBE.
Run separately with eager and transient arguments. Same current code and model:
1000 rows, 100 synthetic RGBA thumbnails. Real Win32 menu/bitmap allocation; popup
tracking is stubbed, no selection or clipboard write. 25 update+show cycles; first
cycle reported separately, median/p95 over remaining 24. This measures preparation,
not visible end-to-end menu opening or input latency.

| Metric | Eager | Transient |
|---|---:|---:|
| Private memory while menu closed | 5.74 MiB | 2.17 MiB |
| GDI objects while closed | 110 | 10 |
| Median update+show preparation | 29.98 ms | 32.47 ms |
| p95 preparation | 37.84 ms | 37.96 ms |

The new mode releases 100 thumbnail bitmaps in this scenario. Median preparation
was about 2.5 ms higher; no claim of faster opening is made. Core projection work
and the 100ms controller/IPC polling deadline remain optimization opportunities.
Decoder scratch memory and audio-driver residency after use were not benchmarked.

Raw JSON: experiments/windows-idle/results/2026-11-12-audit/.

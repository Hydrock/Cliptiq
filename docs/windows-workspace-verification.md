# Windows workspace and sound verification — 2026-11-12

Native Win32 hybrid workspace: history, snippets, metadata filter, bounded preview,
copy-only action, history deletion/clear, settings and snippet editor entry points.
Closing hides to tray and destroys workspace controls; left tray click or --show-main
reopens the main workspace. Right tray click retains the compact popup.

Sounds page: copy and paste toggles, volume 0–100, two preview buttons.
Defaults: both enabled, volume 25%. Short generated PCM cues; winmm loads lazily.
Cues accompany accepted retained captures (startup snapshot suppressed) and successful
Cliptiq copy / paste dispatch. No global keyboard hook or claim of target-app receipt.
macOS/Linux retain their shells and disable unsupported sound controls.

## Verification

MSVC MinSizeRel/static CRT full build succeeded. Final available CTest: 37/37 passed
with a clean TEMP directory and ordinary desktop permissions. Twelve pre-existing
checks excluded: eleven require unavailable isolated WindowStation access, one
Authenticode fixture fails in this environment. No claim that the full suite passes.
Independent review found and corrected legacy Tab focus interception and sound
suppression after settings Apply.

Interactive desktop checks: readable Cyrillic tabs, history and snippet navigation,
multiline previews, Tab traversal, Ctrl+F and Cyrillic empty-result filter, Sounds
preview buttons, settings Apply success, close-to-tray and --show-main reopening.
Synthetic profile only; capture formats disabled in the preview fixture.
Audio playback buttons were invoked; audibility was not recorded or measured.

## Idle measurements

30-second samples, separate synthetic profiles, capture listener running, no windows,
no OCR/Raven/updates. CPU percentages refer to one logical core (32 available).

| Scenario | Working set MiB | Private MiB | CPU % of one core |
|---|---:|---:|---:|
| Empty history | 12.68 | 2.38 | 0.26 |
| 1000 text entries | 17.44 | 7.11 | 0.26 |
| 1000 entries, settings opened then closed | 23.62 | 7.89 | 0.21 |

Settings destruction and reopening asserted by the harness. Working set includes
resident/shared DLL pages; closing settings does not immediately evict them.
The after-settings sample did not play sounds, so it does not measure audio-driver
residency after first playback. Remaining optimization opportunity: that residency,
post-workspace working set and the existing 100ms controller wake-up.

Executable: 1,706,496 bytes (1.63 MiB). Raw measurements are under
out/windows-workspace/idle-empty.json and idle-1000.json (generated artifacts).
No macOS/Linux build or DPI/high-contrast sweep performed.


## Larger workspace image preview

The workspace previously displayed the 100x32 menu thumbnail at native size.
It now requests only the selected image from the existing decoder worker, capped
at 512x512, independently of menu thumbnail settings. Win32 HALFTONE drawing fits
it to the preview panel with preserved aspect ratio and no cropping, including
window resizing. One transient selected-image buffer is retained; closing the
workspace cancels pending selected-image jobs and releases the cache. The original
clipboard payload and the small menu cache are unchanged.

Verification: MSVC build passed. Native media regression exercises an independent
workspace decode with menu thumbnails set to 1x1, cancellation/release and rejection
of stale completions, plus deletion of an unrelated entry during decode. The latter
failed before the fix and passed afterwards. Targeted CTest: 5/5 (native_media,
media, workspace and two idle services checks). Interactive Windows verification
confirmed a large preview from the existing history and after maximizing the window.
No fresh total idle-memory benchmark was taken for this patch.

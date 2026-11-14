# Win32 history implementation plan

Goal: apply the user's selected history improvements without adding a GUI runtime,
and remove the unused Raven experiment.

Design: keep native keyboard navigation, selection and accessibility text in
ListView. Draw two lines per row (content, then type/source), hide column headers,
use 56 DIP rows and a 64 DIP header. Keep one prominent Copy button and smaller
secondary deletion controls. Reuse fonts and precomputed row text; no animation
timer, per-row HWNDs, payload decoding or database queries during paint.

Alternative: retain the three-column table. Rejected because it leaves too little
room for content at the existing minimum window size. A new GUI framework is
outside the selected approach. Existing snippet editing stays native and mounted.

- [x] Preserve a release baseline from updated commit 839a915.
- [x] Add a native benchmark for open/filter/selection/scroll and a native layout
  regression test covering keyboard commands, empty state, resizing and DPI.
- [x] Implement compact header, cached two-line rows and button hierarchy in
  platform/windows/Workspace.cpp/.hpp.
- [x] Move generic Windows process counters out of Raven; remove experiment,
  build option, packages and obsolete comparison documentation.
- [x] Build, run regression tests, inspect synthetic-data UI, alternate matched
  benchmark trials and report CPU/memory/latency and limitations.

Measurements use synthetic data and isolated profiles, no personal clipboard
payload. Both full-app builds use MinSizeRel/static CRT/LTO with LAN disabled
because pinned Mbed TLS is unavailable locally. This does not measure LAN/OCR.
No installation or replacement of the user's running Cliptiq is requested.

Implementation notes: short display text is cached; full tooltip search text is
transient to avoid retaining a second payload-sized UTF-16 copy. Virtual-list
type-to-select and deselection were covered after independent review. Initial
search-cache measurements are discarded; final counters must match final EXE hash.

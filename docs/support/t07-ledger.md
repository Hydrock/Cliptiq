# T07 execution

BASE953de7e7762af85764b494995fa3a39f33de2669. User authorizes model/native popup/tray implementation, history integration, footprint checks and upload to existing draft PR. T04-T06 manual gates remain OPEN; T08 excluded. Reuse clean dedicated codex/portability-plan branch and existing approved design/plan; no fresh architecture approval needed.

Pre-flight T02->T07: Store/list_history/load_snapshot exist; avoid loading every full64MiB payload on UI. Need bounded read-only projection in common menu builder, one read transaction, stable IDs and data_revision. T10 snippets editor/enabled flags do not exist yet, but T02 snippet tables do; T07 renders existing persisted ordered folders/snippets, without adding editor/import/T10 CRUD.

Ruling: native adapters emit immutable commands carrying ID/revision; no auto-paste, modifier action or clipboard write in T07 (T08 owner). A native review host displays history/snippets and clear/status, preserving visible launcher when tray is hidden/missing. Host is a review harness, not finished shipping app. Icons/previews reference persisted assets; decode/image production belongs to T12. Native selection must never execute destructive or selection callbacks on stale models.

Ruling: small pinned Unicode17 libgrapheme3 character segmentation only, not full ICU; linked size measured. Existing defaults/history grouping follow upstream audit. No platform repeats SQL/grouping.

Model RED observed all initial feature cases fail on explicit stub, then GREEN. Native GTK X11/Wayland RED then GREEN (4 contract scenarios per transport). Preview RED swatch/aspect-fit then GREEN. Clear revision RED then implementation under BEGIN IMMEDIATE; no stale command may erase a newly captured row.

Ruling: MSVC resolves unused word symbols before /OPT:REF; retain upstream libgrapheme util.c for hashes but compile an attributed character-only copy omitting the unused word helper. No fake word API or full word tables. Linux GTK LSan initially reports external fontconfig caches/AT-SPI init; narrow library/symbol suppressions apply only to menu GUI cases, not core/clipboard tests. ASan/UBSan remain enabled.

Common model + native GTK contract now14/14 CTest groups GREEN, including Unicode17 official conformance vectors, independent watcher/DBusMenu wire client and restart. Context is carried to MenuCommand without any T08 paste. Menu metadata/depth and invalid UTF8/NUL tests RED->GREEN. Quit ignores irrelevant data revision while Clear is atomic. Preview DPI96/144/192 RED observed then bounded scaling; Win32 rebuilds rasters/layout on DPI/theme changes.

Ruling: review host composes existing capture backends only with explicit --capture/database. Smoke/default do not access clipboard. Settings/editor remain disabled, selection emits ID command; no T08 or T10 implementation hidden in T07. Manual keyboard/screen-reader/desktop/theme gates remain OPEN even when native object tests are green.

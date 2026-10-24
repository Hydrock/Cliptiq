# Optional offline Clipy migration

T14 automated validation and full size evidence: [results](../../docs/support/t14-results.md). Manual historical-backup/clean-system acceptance remains OPEN. This tool is not linked into Cliptiq.app/Cliptiq.exe/cliptiq.

Quit Clipy and Cliptiq before export/import. Keep your original backup. The converter never opens the original with SQLite/Realm: it reads private copies and rejects changing DB/WAL/rollback-journal bytes. Never delete Realm lock/management files from the source. Only the private Realm copy may be upgraded by the separate helper.

Build normal CMake targets cliptiq_import and, on macOS, cliptiq_import_macos. Export current Clipy SQLite v1–v4:

```
cliptiq-import export SOURCE.sqlite3 OUTPUT.neutral
cliptiq-import-macos SOURCE.sqlite3 PREFERENCES.plist OUTPUT.neutral
cliptiq-import import OUTPUT.neutral DESTINATION.sqlite3
cliptiq-import import OUTPUT.neutral DESTINATION.sqlite3 --apply-settings
```

Outputs must not exist. The explicit export is the portable backup; original sources are not updated/deleted. Import defaults to retaining raw settings without activating them. --apply-settings validates compatible settings/shortcuts and commits everything atomically; it never enables OS login, screenshots, telemetry or updates. Exact preferences plist is retained for manual reconciliation of unsupported keys/chords. Existing destination ID conflicts abort the whole import. Repeated identical imports are no-ops; changed sources with conflicting IDs require a fresh destination/manual reconciliation. All duplicates from the source survive; no normal history pruning is invoked by migration. A later ordinary capture can apply configured history limits.

Legacy schema7: separate macOS-only SwiftPM helper, pinned RealmSwift10.54.6. Its dependency/build-tree size is measured separately, not hidden in the app budget.

```
swift build --package-path tools/import-clipy --scratch-path out/legacy-build -c release --jobs 3
out/legacy-build/release/cliptiq-legacy export SOURCE.realm ASSET_DIRECTORY PREFERENCES.plist OUTPUT.neutral PATH_TO_CLIPTIQ_IMPORT_MACOS
```

Asset files are resolved by basename inside the explicit asset directory; symlinks/nonregular/missing assets reject the export. Duplicate basenames must be reconciled by the user before exporting. Unknown archived clipboard types reject export instead of silently dropping them. Exact legacy archives are retained as inert settings provenance. NSKeyed decoding is restricted to a small explicit allowed-class set in the optional helper; the main app never unarchives these objects.

Neutral SQLite format_version1 preserves original IDs, order, enabled flags, times, native representations, title/device/type/OCR/thumbnail provenance and original settings. macOS RTFD/file-list bytes remain native payloads, not cross-platform conversion claims. XML snippets-only fallback is the T11 editor import/export: XML loses IDs, enabled flags and shortcuts. T15 is excluded. Manual T04–T13 remain OPEN.

The CI size helper stages the complete optional migration tools, required dylib and notices, preserves release loader metadata, excludes dSYM, then verifies relocation with the build-tree dylib unavailable. This is not a notarized user release. The separate Realm helper may exceed the main app size target; both sizes and their sum are reported explicitly.

Exact bytes/ownership/limits and native-format portability are documented in [core/MIGRATION.md](../../core/MIGRATION.md). Preference plist limit16MiB is separate from legacy clipboard archive64MiB; image metadata is checked before pixel expansion.

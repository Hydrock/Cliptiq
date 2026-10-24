# Persistent core (T02)

C++17 static library, pinned SQLite 3.53.4; no UI framework, FTS, crypto DLL,
network client or background capture. SHA256 is internal and tested against
standard vectors. Only digest candidates whose ordered representations are
byte-equal are duplicates; UUID identity never substitutes for content digest.

`Store` is movable and owns one SQLite connection. Serialize all calls per store
and run I/O/compaction on the application worker thread. `open_store` accepts a
UTF-8 path (or `:memory:` for tests). Native shells will own user-only directory
permissions, single-instance locking and scheduling. [T03 privacy gate](POLICY.md)
filters captures before invoking ingest; the repository itself remains low level.

`ingest` atomically writes representations and metadata, applies count/quota
pruning in current sort order, and increments `data_revision`. Empty primary
text is rejected using Unicode White_Space, as is RTF-primary with a corresponding
plain-text representation that is blank. TextEncoding explicitly identifies UTF-8,
UTF-16LE or UTF-16BE while retaining original bytes; Windows adapters must set it.
A terminal UTF-16 NUL is accepted; embedded NUL is content. Opaque RTF is not decoded by storage.
`allow_duplicates=false` rejects an equal copy, regardless of previous UUID mode.
Otherwise `replace_duplicate=true` updates the matching ID/last-used/source
metadata without changing creation time; false allocates a fresh UUID. If pruning removes the accepted entry (for example
created-order replacement of an old item), ingest returns an empty optional.

Sort uses descending timestamp then descending insertion rowid for ties.
Limits are payload limits, not exact database file size: page/index overhead,
metadata and free pages are separate. Hard ceilings: 32 MiB per representation,
64 MiB per entry, 256 MiB history and 4096 representations per snapshot. Native
adapters must check these before allocating external clipboard bytes; core
validates the provided snapshot before any write. Persisted blob lengths, counts
and metadata lengths are checked before C++ materialization, including digest
candidates loaded for duplicate equality. No partial oversized capture.

Cleanup uses explicit `now_ms` input and persisted last-run/interval state.
First enable, interval change or backward clock reanchors the schedule. On due
tick (including after sleep), **all history** is cleared and the schedule anchors
at this tick; entries are not TTL-filtered. Disable clears the schedule. Quit
handling will invoke `erase_history` only if the later settings policy asks.

History representations/assets cascade on deletion; snippets/settings survive.
Schema v1/application ID identify our database. Newer/foreign/corrupt databases
return errors and are never automatically replaced. Errors include busy,
read-only, full, corrupt and I/O; rollback preserves prior history. No secure
erase claim: deleted bytes can remain in SQLite free pages, journals, backups
or storage. `compact_store` checkpoints/VACUUMs only when the worker requests it.

The main database has no FTS objects. Revision is the future companion index's
invalidation contract; no index or OCR work is started in T02. Ordinary nullable
OCR fields are reserved. Minimal snippet tables are storage boundaries, not the
T09 snippet repository/editor.

Build: `python tools/fetch-footprint-deps.py`, `cmake --preset test`,
`cmake --build --preset test`, `ctest --preset test`. Windows requires an MSVC
Developer shell and Ninja. Presets use host-specific build directories so WSL
and Windows can share a checkout. `sanitize` enables ASan/UBSan on GCC/Clang.
`release -DCLIPTIQ_BUILD_SIZE_PROBE=ON` builds the linked API fixture;
`python tests/size/report.py` reports its actual executable/ZIP and static archive
sizes. Archives contain unlinked code and are not installed-product sizes.

T09: [persistent settings, native panels and global shortcut contract](SETTINGS.md).

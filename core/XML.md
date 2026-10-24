# Clipy XML interchange (T11)

API: `include/cliptiq/xml.hpp`; UI: AppKit/Win32/GTK editor Import XML / Export XML. Default export name `snippets.xml`. Files are UTF-8/XML1.0 plain text. T11 does not implement SQLite/Realm migration (T14) or previews/screenshots (T12).

## Wire format and compatibility

```xml
<?xml version="1.0" encoding="UTF-8"?>
<folders><folder><title>Folder</title><snippets>
  <snippet><title>Title</title><content>  text
second line  </content></snippet>
</snippets></folder></folders>
```

Matches pinned Clipy develop71b0e82623d5c73b4b717d81233eb78db8bbe258, CPYSnippetsEditorWindowController.swift:21–27,158–229: append import, whitespace trimming disabled, missing/empty titles fallback to `untitled folder`/`untitled snippet`, missing content empty. Whitespace-only titles remain whitespace. Empty folders/order/Unicode/standard entities/numeric references/CDATA/comment-separated text are supported. XML1.0 literal CRLF and CR normalize to LF; character reference `&#13;` retains CR. Export encodes CR as reference. No rich text, nesting, tags or templates.

Export includes all folders/snippets regardless of enabled state. UUID/enabled/revision/hotkeys are not represented by Clipy XML. Import generates new IDs, defaults items enabled, leaves existing data/bindings in place, and may intentionally create duplicate titles/content on repeated import. This is append, not merge/dedup/replace. Live Clipy import/export remains manual OPEN; hand-authored source-derived fixture and independent ElementTree comparison do not substitute for the running application.

Strict schema: one `folders` root, `folder` children with optional single `title` and `snippets`, `snippet` children with optional single `title` and `content`. Unknown/duplicate fields, attributes/namespaces, nested text elements, processing instructions other than an initial valid XML declaration, alternate encodings/XML versions are rejected with a readable error. DTD/entity declarations are rejected before DOM parsing. Named entities are limited to amp/lt/gt/quot/apos; numeric references must be XML1.0 scalar characters. Invalid UTF-8, NUL and forbidden controls are rejected. The parser never fetches external files/network entities. A literal declaration string inside a comment/CDATA stays inert text.

## Limits and parser

`ImportLimits` can lower, never exceed hard ceilings:64MiB wire;1000 folders;10000 snippets;1024 UTF-8 bytes/title;1MiB/body;16MiB total body. Store representation quota may lower append/body export allowance. Preflight caps75000 markup tokens and element depth5 before DOM allocation and validates entities/characters. Plain-text terminator search is bounded to each text segment (regression37000 segments+63MiB comment); it does not rescan the entire suffix per segment. Parsing still uses bounded whole-file/DOM memory, not streaming constant memory.64MiB is deliberately above the early proposed8MiB to accommodate accepted T10 decoded16MiB collections plus escaping. Extreme escaping can exceed the wire ceiling; export then errors without touching the destination. Huge collections are not silently truncated.

Pinned static TinyXML-2 11.0.0 replaces the plan's proposed pugixml for this fixed schema. No new shared parser runtime. Original and compiled SHA256 in `third_party/tinyxml2/pins.json`, verified by CMake without network; Zlib license preserved. One marked local Identify alteration extends PEDANTIC whitespace preservation before comments/CDATA/elements; upstream unaltered regression fails. Header CRLF→LF is normalized for Git/CMake reproducibility. `PATCHES.md` records both changes. A pre-parse EOL pass corrects upstream LF-CR normalization to XML1.0 semantics without a second vendor code alteration.

Primary parser sources: [upstream](https://github.com/leethomason/tinyxml2), [release11.0.0](https://github.com/leethomason/tinyxml2/releases/tag/11.0.0). Build contribution is measured by paired probes; no claim that this is the smallest possible XML library.

## Database atomicity and export snapshot

`decode_clipy_xml(Bytes,ImportLimits)` never changes a Store. `append_import(Store&,vector<ImportedFolder>)` validates UTF-8/title/body and destination+incoming count/text budgets before inserting in one BEGIN IMMEDIATE transaction. All new folders/snippets are appended in wire order; folder positions are normalized atomically. Failure rolls back all inserted data/position/revision changes. Success advances global revision once; empty import is a no-op. Existing IDs, content, enabled flags and shortcut preferences remain. No schema change.

`encode_clipy_xml(Store&)` uses a consistent read transaction for folder order, snippet metadata and bodies, includes disabled items, validates XML1.0 representability and bounds before returning bytes. It does not mutate storage. Read transaction lifetime can delay concurrent SQLite writer commit; no slow-disk/capture throughput guarantee is claimed. A failed nested BEGIN does not roll back a caller's existing transaction. Store callers serialize each connection. Native transfers use their own worker connection.

Unfiled legacy snippets cannot be represented without a folder: export errors and asks the user to move them into a real folder first; it never silently omits them. T10 strings containing XML1.0-forbidden controls also error on export rather than altering content. Stored data is preserved.

## Files, worker, and recovery

`read_snippet_xml` reads a regular file in bounded chunks, enforcing the cap even if it grows; Linux nonblocking open rejects FIFO/device input via fstat. File byte/parse budgets are not a filesystem wall-clock timeout. `write_snippet_xml_atomic` expects prepared bytes: create an exclusive random temporary file in the destination directory, write fully, flush, then replace. POSIX uses0600/fsync/rename/directory fsync; Windows uses CreateFile/FlushFileBuffers/MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH). Temporary handles/files are cleaned on failure. Encoding/validation and pre-replacement failures leave the old destination intact. If a post-rename directory fsync fails, the error explicitly says replacement occurred but directory durability is unconfirmed; replacement is not falsely described as rolled back. Output is plain XML without encryption, selected explicitly by the user.

Native pickers use NSOpenPanel/NSSavePanel, OPENFILENAME and GtkFileChooser; save confirms overwrite, cancel starts no task. The shared owned async transfer copies only path/limits, opens a separate SQLite connection, and invokes no native controls from the worker. Only one transfer is allowed. Drafts must be saved or explicitly Reloaded first. Editor input, custom keyboard undo/redo, action/edit/selection/detail/drag/recorder entry points, reopen and close remain gated until UI completion polling. Path-based APIs exercise the same pipeline for CI. File-backed store required; memory-only size smoke never edits/imports/exports.

Successful import reloads the editor/catalog and invalidates host menu through the existing callback; T09 folder routes reconcile in the host. Successful export leaves storage/selection alone. Errors display readable reasons and restore editor controls. If append committed but UI refresh fails, the message explicitly says import committed and not to repeat import; Reload is the recovery. External processes may still change storage; this is no global database edit lock.

Known deferred Minor: picker wraps a successful transfer start in generic status(), which can overwrite Importing/Exporting with Saved locally while work is still pending. Completion/error remains authoritative and input/close is still gated. Path API progress does not have that extra picker wrapper. No silent success/rollback claim is made in the results.

[Results](../docs/support/t11-results.md), [manual acceptance](../tests/manual/xml.md), [raw evidence](../docs/support/t11-measurements/README.md).

# Snippet repository and native editor (T10)

C++17, SQLite owned by Cliptiq. API: `include/cliptiq/snippets.hpp`, `include/cliptiq/editor.hpp`. T11 XML is excluded. No tags, nesting, templates, rich-text formats, sync or network service.

## Storage and mutations

`open_store` atomically upgrades owned v1 to v2, preserving original `snippet_folders(id,title,position)` and `snippets(id,folder_id,title,content,position)` tables. Two FK extension tables hold enabled/revision state; missing metadata defaults to enabled/revision0. Earlier Cliptiq binaries reject v2. Foreign/unsupported stores retain earlier refusal rules. No Clipy database is opened or migrated.

`list_folders`, `list_snippet_headers` load bounded metadata; `get_snippet`/`list_snippets` load plain UTF-8 bodies. `create_folder`, `save_folder`, `save_snippet`, `move_snippet`, `reorder_folders`, `set_enabled`, `erase_snippet_item` mutate atomically. IDs are stable generated UUIDv4, never titles. `snippet_append` means append. Equal titles are allowed. Disabled items stay in editor/storage, filtered from menu and paste execution.

Updates carry per-item revision CAS. `save_folder_committed`/`save_snippet_committed` return the exact entity captured inside their write transaction; a post-commit peer write is not silently adopted. Normalized text/title edits skip per-sibling reindex updates; gaps/duplicates are repaired transactionally while retaining row order. Moves validate indices and normalize both folders. Folder reorder supports an optional whole-data revision snapshot to protect unrelated concurrent reorders. Each successful mutation invalidates menu data revision; failed/cancelled operations do not partially mutate.

Deleting a folder manually cascades its snippets (legacy tables have no cascade), extension states, and `t09.hotkey:folder:<id>` preference; the T09 revision is advanced so an old Settings snapshot cannot restore that binding. There is one folder level. Legacy unfiled items are displayed under a synthetic root and can be moved into a real folder; no new unfiled snippets can be created.

Limits: 1000 folders, 10000 snippets, titles<=1024 UTF-8 bytes, each body<=min(1MiB, Store representation limit), total body<=16MiB. NUL/invalid UTF-8 is rejected. Oversized legacy data is preserved, returns TooLarge, and is not silently truncated. Native input ceilings exceed repository ceilings, so oversize input reaches explicit validation. Folder deletion remains available as an explicit destructive operation. SQL fixture access is internal, not an import API.

## Editor session and persistence

`SnippetEditorSession` owns selection, cached bounded catalog, per-snippet plain-text undo/redo, and private drag tokens. The caller serializes its UI-side Store access and keeps Store alive. Selection failure preserves session selection; every native tree restores that same selection, including empty selection. Delete confirmation precedes mutation. Explicit Reload discards a failed draft; navigation/add/toggle/undo/delete cannot silently discard it. Reopening a dirty editor returns Busy. Closing while a save is in flight returns Busy; after failure the native dialog defaults to keeping the draft. Host Quit observes the editor close result.

Native controls call `background_saves` then `queue_title`/`queue_content`; a single owned async task uses a separate SQLite connection, retaining only the active save and the latest title/body draft. Completion is polled without waiting from the native event loop. Saves are ordered: the next coalesced draft uses the previous committed revision, not a reread. Failures retain the newest visible text and stop automatic retries; correction retries or Reload discards. No callbacks or native controls are invoked by the worker. The worker connection is never shared with the GUI connection. File-backed stores are required for background editing; empty `:memory:` smoke does not edit. Shutdown joins the one bounded pending worker; no detached work outlives Store/session.

Undo stacks contain at most64 entries and4MiB each, reset on selection. Coalesced successful changes define undo steps; native rich-text/undo is disabled. Discrete commands (add/delete/move/enable/undo) are synchronous and use a no-wait SQLite busy scope in native sessions; they return Busy on lock contention. They are not claimed free of filesystem I/O. UI reads/catalog reloads are bounded. General asynchronous document loading/CRUD is not implemented. Host completion invalidates the menu; folder hotkeys/settings reconcile in the existing periodic host pass rather than per keystroke. No typing-throughput or slow-disk guarantee is claimed.

`begin_drag` creates no database mutation; local native DnD transports an opaque token only. `finish_drag(...,committed=false)` cancels without mutation; committed drops validate item revision inside the transaction. Folder reorder additionally compares the catalog snapshot. A peer change returns Busy/reload rather than overwriting. Cancelled/stale/consumed tokens do not reorder the database. Native transport is same-editor only; cross-app rich payloads are not accepted.

## Platform controls and shortcuts

- macOS: AppKit NSOutlineView, plain NSTextView, NSTextField, enabled checkbox, NSAlert confirmation; local NSPasteboard DnD, Command-Z/Shift-Command-Z/Y routes to common undo. No WebKit.
- Windows: Win32 TreeView/checkboxes and system RichEdit in plain-text mode; `Msftedit.dll` loaded only from System32, not bundled. Local OLE IDataObject/IDropSource/IDropTarget, balanced OleInitialize/RevokeDragDrop/lifetime. Tree Space persists enabled state, Delete confirms, F2 focuses title, Ctrl-N/Ctrl-Shift-N create. Common Ctrl-Z/Y handles text.
- Linux: GTK3 TreeView/TreeStore + TextView, native confirmation and same-widget DnD. Tree Delete/F2; Ctrl-Z/Y/Shift-Ctrl-Z common undo. Existing GTK is reused, no GtkSourceView dependency. Same editor under X11/Wayland.

Open via menu `OpenEditor`, global `editor` action (unset until configured in T09), or `--show-editor`. Folder recorder composes T09 `apply_settings`/Registry; duplicate/reserved/OS-conflicting chords preserve previous preference and registration. The recorder receives a global chord only while visible, active/key and the chord field has focus. `available_hotkeys` filters disabled/deleted folder routes while disabled preferences remain configured. Re-enable restores native routing on reconciliation; deletion removes preference. Wayland GlobalShortcuts cannot remove one granted shortcut: disabling/deleting it closes the whole portal session, invalidates status for all actions, and requires explicit Settings authorization again. Pending portal requests are cancelled; no automatic consent prompt or input bypass.

Menu and `PastePipeline` both recheck enabled folder/snippet state, deletion and menu revision. Forging a current-revision disabled command still cannot write clipboard or inject input. User-created snippets are deliberate local data; clipboard history continues through the T03 privacy gate unchanged.

## Evidence and remaining acceptance

[Results](../docs/support/t10-results.md), [manual checklist](../tests/manual/snippets.md). Native tests use actual controls, SQLite, key-event paths, conflict rollback and private X11/Wayland/Windows fixtures. Common drag commit/cancel is tested; physical native pointer drag gestures still need manual acceptance. Real desktop activation, OS grants, portal consent UI, accessibility/DPI and clean Windows/macOS installations remain OPEN. This review host is not a released app/installer.

Primary API references: [Apple NSOutlineView pasteboard](https://developer.apple.com/documentation/appkit/nsoutlineviewdatasource/outlineview%28_%3Apasteboardwriterforitem%3A%29), [GTK TreeView reorderable](https://docs.gtk.org/gtk3/method.TreeView.set_reorderable.html), [Microsoft OLE drag and drop](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/shell/dragdrop.md). Manual DnD owns database ordering instead of permitting default model-only reordering.

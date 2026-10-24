# T10 execution

BASE3ed1619ab505b0bc7d2b21fc4ea78fd227654670; existing dedicated codex/portability-plan checkout reused. User authorized T10 snippet CRUD/native editor/menu/hotkeys/footprint/upload. T04-T09 manual stays OPEN; T11 explicitly excluded. Existing approved design/audit/plan used; no new product scope.

Preflight: T02 owns three-column snippet_folders/five-column snippets, no enabled state or editor. T07/T08 use raw SQL; T09 folder bindings and unavailable editor hooks are ready. Add an atomic Cliptiq-owned v1→v2 migration with extension state tables, preserving legacy table shapes and data; menu/paste must filter disabled state and global revision invalidate old commands. No Clipy database touched. Single-item text undo history resets at selection boundary; no nesting/tags/templates/XML.

Ruling: preserve legacy table shapes using small enabled/revision extension tables rather than adding columns. This keeps earlier fixture/consumer SQL compatible, while v2 rejects older binaries explicitly. Cost: two tiny metadata tables/joins and older Cliptiq builds cannot open v2 stores.
Ruling: editor limits: 1000 folders/10000 snippets (existing menu bounds), UTF-8 titles<=1024B, snippet text<=1MiB and total snippet text<=16MiB. Oversized legacy data is preserved but cannot be silently truncated/edited; readable error. Cost: very large text collections need a later explicit limit decision.

Execution checkpoints: baseline tests; repository RED→GREEN; editor controller/native UI RED→GREEN; menu/hotkey integration; one final fresh review and one tested fix pass; exact-source CI/footprint/docs; update existing draft PR1, no merge/release.

Repository checkpoint: initial snippets test group five scenarios RED on typed Unsupported stubs, then full Linux29/29 GREEN. CRUD uses per-item optimistic revisions, strict bounded UTF-8, stable UUIDs, transactional duplicate-position normalization/moves/reorder/cascade, enabled metadata and persisted folder-binding cleanup. Menu, command validation and paste query now filter disabled flags; settings_folders reports actual folder availability. Legacy v1→v2 restart/data preservation tested. Next: shared editor session and native adapters; no T11.

Editor session checkpoint: four initial scenarios RED on typed stubs; full Linux30/30 GREEN after autosave/entity-CAS, bounded undo/redo, selection reset, confirmation/cancel, drag token/version guard and folder-binding callback. Native UI gate RED on Unsupported show in private X11/Wayland; GTK native controls now pass2/2, editing actual SQLite and composing folder recorder with T09 registry/settings conflict rollback. AppKit implementation ready for cross-platform CI compile; Win32 still stub at this checkpoint.

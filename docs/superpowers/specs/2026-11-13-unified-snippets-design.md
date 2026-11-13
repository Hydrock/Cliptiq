# Unified snippets workspace

Approved direction: folder/snippet tree on the left and directly editable text on the right inside the main Snippets page. Copy remains available there. The user selected this layout on 2026-11-13.

Reuse the existing native editor and shared SnippetEditorSession, including inline rename, enable-in-menu, folder shortcuts, drag/drop, autosave, undo/redo and draft error protection. History remains read-only. Remove the sidebar editor entry; editor commands open the Snippets page. Windows and macOS use the same layout and actions with native controls. Linux retains its existing fallback.

Approaches considered: embed the existing editor (selected; preserves behavior with one screen); retain the flat list with an Edit mode (rejected; requires extra navigation). Builder choice: mount a native editor panel lazily and reuse it when switching pages. Do not create a second visible editor window. Copy must wait for pending saves and report errors without copying stale persisted text. Closing or switching must retain drafts and asynchronous completion. No new GUI library or network changes.

# T10 native snippet editor acceptance — OPEN

Automated CI is recorded in docs/support/t10-results.md. These boxes intentionally remain unchecked. T04–T09 manual acceptance remains OPEN. T11 XML is excluded. User has no separate clean Windows/macOS VM; hosted CI is not substituted for one.

Run on macOS arm64/Intel, Windows x64, Linux X11 and supported Wayland compositor/portal. Record OS/build, desktop/compositor, keyboard/layout, display scaling, source commit and exact result. Use a disposable Cliptiq store (`--database PATH`) and harmless owned target application.

- [ ] Open Snippets editor from tray/menu, configured global editor chord and CLI `--show-editor`; check correct focus/activation. Editor chord starts unset.
- [ ] Create empty folder and snippets; rename with emoji/Cyrillic/combining marks; type/paste multiline text with leading/trailing whitespace. Restart host and confirm exact persistence. Verify plain text rather than rich formatting.
- [ ] Rapid title/body typing while capture/menu is active; confirm responsive controls, Saving→Saved state, ordered final text and menu revision refresh. Hold another writer transaction; confirm controls remain responsive, pending navigation/close is blocked, failure retains newest draft, correction/Reload recovery is clear. Test slow/full/read-only disk separately; no latency guarantee claimed.
- [ ] Undo/redo keyboard and buttons, selection resets history, focus traversal/full keyboard access; Windows Space toggles enabled persistently, Delete confirmation, F2, Ctrl-N/Shift-Ctrl-N; Linux Delete/F2. Test IME and non-US layouts. Coalescing may group typing differently from native text systems.
- [ ] Disable snippet and folder, verify menu hides them and clipboard/paste cannot execute them; editor keeps them visible. Re-enable restores menu. Empty folder is handled without broken selection.
- [ ] Drag snippet within a folder and to another folder; reorder root folders before/after; test first/last/empty folder targets, cancel with Escape/drop outside. Check visual order, DB/restart order and selection agree. Folder nesting/cross-app payloads must not occur. Test stale drag after a second process edits/reorders.
- [ ] Select oversized or concurrently deleted legacy target after a valid item: error keeps previous highlight and edit/delete target consistent. Repeat from no selection. Never truncate original oversized text.
- [ ] Failed autosave: visible draft retained, toggles roll back, navigation/reopen/undo/add/delete blocked; Reload discards only explicitly. Close defaults to keep editing; pending save must finish first. Host Quit respects refusal. Test OS window close separately.
- [ ] Delete snippet after native confirmation/cancel. Delete folder cascades only its children, removes folder hotkey preference, invalidates old Settings snapshot. Restart and verify no orphan route.
- [ ] Folder shortcut recorder: focused active window only; logical/physical semantics follow T09. Internal duplicate, reserved chord and external OS conflict preserve old chord; disabled folder releases route, re-enable restores, deletion removes. Check menu opens correct folder, selection invokes T08 harmless paste target. Disabled/deleted stale menu commands cannot paste.
- [ ] Wayland: grant/deny GlobalShortcuts through real portal; confirm disabling/deleting a granted folder closes entire session, remaining routes report unregistered and require explicit reauthorization. Preferred chord is only a hint. No consent/input bypass.
- [ ] Native drag cursor/drop indicators, resizing, multiple monitors, theme/dark/high contrast, 100/150/200% DPI, screen reader labels/roles, keyboard-only operation. Record failures before claiming accessibility acceptance.
- [ ] Clean systems without development tools: launch/dependencies/resources, system Msftedit.dll, GTK themes/pixbuf/AT-SPI; measure installed/update packages only when later packaging exists. This gate is not covered by CI build images.

No release acceptance or T11 start is implied by the automated checks.

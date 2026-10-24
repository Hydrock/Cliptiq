# T11 XML acceptance — OPEN

T04–T10 manual remains OPEN. This checklist is deliberately unchecked. Automated source/fixture comparisons and hosted CI do not establish live Clipy compatibility or clean-system usability. No separate clean Windows/macOS VM is available per user. T12 is excluded.

Use disposable stores/files and pinned Clipy develop71b0e82623d5c73b4b717d81233eb78db8bbe258 on macOS. Record OS/build/architecture, desktop/portal, scaling, keyboard layout and runtime commit.

- [ ] In real Clipy, create ordered folders/empty folder/snippets with leading/trailing spaces, whitespace-only title, Unicode/emoji, multiline text and XML special characters. Export through Clipy, import in Cliptiq, compare exact values/order and fallback titles. Repeat inverse Cliptiq→Clipy. Record any newline/empty-field behavior explicitly.
- [ ] Disabled folders/snippets export too; XML contains no UUID/enabled/hotkey fields. Import defaults enabled/new IDs and appends to existing data. Repeated import appends duplicates intentionally; existing shortcuts/data stay intact. Restart and verify persistence/menu.
- [ ] Exercise native open/save/cancel/overwrite dialogs on macOS/Windows/X11/Wayland. Default snippets.xml, Unicode/long paths, current directory preserved on Windows, read-only/missing folder, existing target and permission denial. Cancellation must leave data/destination unchanged.
- [ ] While importing/exporting, controls remain disabled and window close/reopen refuses pending transfer. Focused Mac body Cmd-Z/Y, Win32 and GTK keyboard actions must not mutate or re-enable fields. Confirm recovery after success/error. Dirty/pending autosave blocks transfer rather than losing draft.
- [ ] Observe deferred Minor: picker can show Saved locally before transfer finishes. Verify completion/error and pending lock, not that premature label; record usability impact before release.
- [ ] Malformed XML, unknown/duplicate schema fields, attributes, alternate encoding, DTD/external entities, invalid UTF-8/control characters, excessive count/title/body/depth/node/wire limits fail without partial import or pre-replacement destination modification. No external entity content/file/network read occurs.
- [ ] Export unfiled legacy snippets or XML-forbidden stored controls: readable error preserves original file/data; moving/correcting those items permits retry. Oversized escaped output should fail explicitly, never truncate.
- [ ] Read/import near-limit files while capture/menu is active; UI stays responsive, bounded worker completion is visible. Test lock contention and disk-full/write failure. A post-rename durability error reports that replacement already occurred. If import committed but UI refresh failed, Reload rather than repeating import.
- [ ] Native picker accessibility/keyboard traversal/screen reader, high contrast/dark theme, 100/150/200% DPI, monitor/focus/activation. Check XML error messages and pending recovery are discoverable.
- [ ] Real clean systems without toolchains: system common dialogs/AppKit/GTK resources available; no parser shared library required. Installed/update/offline footprint remains for packaging stages.

No XML migration of Clipy SQLite/Realm, previews/screenshots or release acceptance is implied.

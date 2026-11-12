# Shared quick menu verification — 2026-11-12

Implemented the common Main model with two localized branches: History and Snippets.
The existing history projection, grouping, snippet folder order, command IDs,
source data and paste-target validation remain unchanged. Empty sections are
visible but disabled. Existing snippets_above preference orders the branches.
Global service actions remain after the sections.

Standard main/history/snippets hotkey actions and history/snippet IPC requests
all select the shared model. Saved bindings remain aliases, shown as Quick menu
and alternate bindings in settings. The active Windows profile retains Ctrl+M
and Ctrl+B. Folder-specific shortcuts remain one-shot folder menus; the tray
returns to Main afterwards, fixing a source-review finding about stale folder models.

The same core projection feeds AppKit and Win32. Explicit AppKit popup source now
uses NSEvent.mouseLocation with inView:nil and no makeKeyAndOrderFront, matching
the mouse-anchor/no-workspace-raise interaction. Full macOS workspace parity is
not implemented here. Its intended layout/interaction contract is recorded in
superpowers/specs/2026-11-12-shared-quick-menu.md. A Mac build and manual verification
remain necessary; no macOS runtime or performance claims are made.

Windows MSVC full build passed. Root structure/localization regressions failed
before implementation and passed after. The lifecycle localization test was
updated to inspect the history leaf under the new History section, preserving
its assertion that clipboard content is not translated. Final available CTest:
39/39 passed; twelve previously documented WindowStation/Authenticode checks were
excluded, not weakened. Log: out/windows-unified-menu/ctest-final.log.

Started Windows app --background with the existing history/settings profile.
No full new resource benchmark was performed for the extra submenu level.

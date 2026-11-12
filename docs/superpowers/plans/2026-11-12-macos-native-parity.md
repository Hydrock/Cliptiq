# macOS native workspace parity

Goal: Match the current Windows workflow and visual structure with native AppKit, preserving a lightweight background process.

Scope: main workspace, two-root quick menu, service status-item menu, settings help and typed choices, inline snippet names, optional sounds. Keep existing storage/capture/paste logic and permissions.

1. Windows inline rename: native TreeView label editor, F2/double-click, immediate rename after creation, Enter commits, Escape cancels; remove title edit control. Verify real queued keys, persistence and rejection/cancellation on isolated test data.
2. macOS main workspace: lazy native view/controller, sidebar, filter, stable-ID list, bounded selectable text / fitted image preview, copy/delete/clear/settings/editor/quit; release heavy UI when hidden. Preserve original paste targets and mouse popup positioning. Add AppKit workspace integration tests for CI.
3. macOS editor/settings: editable outline names with cancellation, remove title field, translated buttons that fit, corresponding choices and small wrapped shared descriptions, sound preview. Preserve stable-ID selection, background saves and validation.
4. Shared integration: enable workspace entry points on Apple, add sources to CMake, enable native sound hooks with lazy NSSound and existing PCM; keep disabled defaults and startup suppression.
5. Verification: Windows build and targeted regressions, source and lifecycle review for AppKit. macOS Debug/release tests run in existing macos-15 / macos-15-intel CI after changes are pushed; local Windows work cannot establish native Mac success. Do not publish or push without an explicit request.

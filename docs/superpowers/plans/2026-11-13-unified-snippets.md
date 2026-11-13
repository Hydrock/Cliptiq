# Unified Snippets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Edit and copy snippets directly in the main window on Windows and macOS.
**Architecture:** Reuse NativeSnippetEditor as a lazily mounted native panel, backed by the existing session. Workspace owns its container; MenuHost supplies session, close guards and copy dispatch.
**Tech Stack:** C++17, Win32/RichEdit, AppKit, existing SQLite session.
**Spec:** docs/superpowers/specs/2026-11-13-unified-snippets-design.md

## Global Constraints
- Preserve native UI, shared branch, profile and LAN settings.
- Retain inline rename, autosave, undo, shortcuts, drag/drop; history read-only.
- No second visible editor window. Copy must not use stale saved text.

## Review Focus
- Pending saves at Copy or page switch: wait or retain draft, never silently discard.
- Main close/DPI rebuild: mounted editor cannot keep dangling parent handles.
- Keyboard rename and Tab: nested editor must receive pretranslation.
- Repeated page switches: reuse AppKit outline to avoid observer growth.
- Hidden/disabled snippets: still editable and copyable in workspace.

### Task 1: Mount editor and safe copy
- [x] Add failing Windows integration test: same top-level window, editable body, no sidebar editor button, rename/copy/page switch retain data.
- [x] Add NativeSnippetEditor::show_embedded(session,parent,copy), hide/close ownership, pending-save copy action and copy button. Preserve standalone test entry points.
- [x] Run native regression, editor session and idle pump tests.

### Task 2: Workspace routing on both platforms
- [x] Add lazy editor container and mount callback to NativeMenuOptions, close guard and page selection to NativeMenu.
- [x] Route OpenEditor to Snippets page; retain Linux fallback. Remove sidebar editor button and hide history controls on Snippets.
- [x] Add macOS native test for mounting, page switching and copy; retain outline reuse.

### Task 3: Verification and delivery
- [x] Build Windows and run available CTest suite, localization and diff checks.
- [x] Review changed lifecycle and platform guards; document macOS build limitation and handoff.
- [x] Install Windows build preserving profile, inspect main Snippets UI, commit results.

Review rulings: retain the existing session on remount to preserve undo; refresh fonts in-place on DPI change; outline Cmd+C requires actual first-responder status so body selection copying stays native. Windows 62/62 available tests passed, then 6 affected tests passed after review fixes. macOS integration tests added, execution pending a Mac.

# Windows Workspace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Заменить технический launcher полноценным лёгким Windows-приложением.
**Architecture:** Общая bounded workspace projection/preview поверх SQLite.
Win32 workspace создаётся лениво внутри существующего NativeMenu owner; controller
сохраняет capture/paste/settings. Никакой новой GUI runtime зависимости.
**Tech Stack:** C++17, Win32 common controls/GDI, SQLite, CMake/MSVC.
**Spec:** ../specs/2026-11-12-windows-workspace-design.md

## Global Constraints

- Копирование из окна — без injection/auto-paste.
- --background без видимых workspace; закрытие — в трей с recovery fallback.
- Текст preview ≤64KiB SQL prefix, stale revision и disabled snippets отвергаются.
- Windows первым; macOS/Linux не получают ложного заявления о новом UI.

## Review Focus

- Удалённые/изменённые записи не исполняют старые команды.
- Ctrl/Cyrillic/multiline и пустые списки не ломают preview/filter.
- Закрытие/повторное открытие не оставляет HWND/GDI resources.
- Отсутствие трея не делает приложение недоступным.
- Фоновая нагрузка не растёт от закрытого UI.

## Tasks

- [x] 1. Core workspace rows/preview: tests RED→GREEN, bounded SQL and stable-ID validation.
- [x] 2. platform/windows/Workspace.hpp/.cpp: native navigation, list/filter/preview/actions; controller callbacks.
- [x] 3. NativeMenu/MenuHost integration: close-to-tray, reopen main, copy-only/delete intents; old popup preserved.
- [x] 4. NativeSettings: friendly labels/combos, typography and layout; retain validators/recorders.
- [x] 5. Build, available tests, interactive UI verification, idle measurements, ZIP and evidence.

## Ledger

Approach approved by explicit user selection of the recommended hybrid format.
Implementation details (layout, bounded local filter, Windows-first scope) are
engineering calls stated in commentary; existing platform services remain in use.

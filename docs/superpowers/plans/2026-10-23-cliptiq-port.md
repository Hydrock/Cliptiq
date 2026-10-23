# Cliptiq: Implementation Plan переноса Clipy

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Этот документ — план, не разрешение начать разработку или публикацию.

**Goal:** перенести все обнаруженные функции Clipy в один репозиторий с компактными отдельными сборками macOS, Windows, Linux и честными платформенными ограничениями.

**Architecture:** предлагаемое общее C++17 ядро и нативные оболочки AppKit/Objective-C++, Win32, GTK3; два Linux backend X11/Wayland. Решение подтверждается измерениями T01. Base, OCR и migration поставляются отдельно с суммарным отчётом размеров.

**Tech Stack:** CMake/CTest, C++17, SQLite amalgamation, pugixml, Unicode segmentation, native system APIs. Минимизируем зависимости, не считаем системные runtime бесплатными.

**Spec:** [дизайн](../specs/2026-10-23-cliptiq-design.md). **Coverage:** [аудит с ID функций](../../research/2026-10-23-clipy-audit.md). **Naming:** [проверка имён](../../research/2026-10-23-naming.md).

Статус: полный план охвата исследуемой ревизии; пользователь разрешил T01, [прототипы и первые измерения выполнены](../../support/architecture-decision.md). T01 закрыт в согласованных границах ([итог](../../support/t01-results.md)); T02–T17 не начаты. Product code и release binaries не созданы.

## Global Constraints

- Один `Hydrock/Cliptiq`, upstream baseline `71b0e82623d5c73b4b717d81233eb78db8bbe258`; ветки `codex/...`.
- Предложены macOS 13+ arm64/x86_64; Windows 10 22H2+/11 x64, arm64 после native smoke; Linux glibc 2.35+ x86_64 GTK3 3.24+, aarch64 после native smoke. Windows API floor не означает поддержку самой ОС поставщиком.
- C++17 + native UI — исходная гипотеза T01, не одобренный окончательный выбор пользователя. Репозиторий создан, публичное имя окончательно не проверено.
- Base download ≤5 MiB / installed app-owned ≤10 MiB на architecture — предварительные цели, уточнить после T01. Отдельно dependency delta, OCR/models, migration, installer и total offline closure; CI growth gate +10% к принятому baseline.
- Base не содержит bundled browser, Electron, Flutter runtime, .NET self-contained, Realm/Firebase; исключение Firebase и OCR lane — явные scope предложения, не молчаливое удаление функций.
- Defaults/диапазоны Clipy из аудита сохраняются кроме явно описанных надёжностных исправлений и подтверждённых адаптаций. HTML off; history 30 / 1–1000; auto-paste on с manual fallback; OCR max 10 000 chars.
- Потоковые limits: representation 32 MiB, entry 64 MiB, history payload 256 MiB, image 40 MP, XML 8 MiB и 10 000 snippets/import. Это предлагаемые новые значения, проверяемые в T01/T02.
- Payload не выходит в сеть. OS clipboard data — недоверенный вход. Privacy checks перед записью/OCR/preview; WAL/temporary file handling учитывает чувствительность данных.
- Wayland capability detection обязательно. Нет гарантии фонового чтения/вставки/source-app при отсутствии desktop API. RTFD и Universal Clipboard остаются macOS-specific.
- Только свои release/update endpoints и project identifiers после выбора имени. Сохранить применимые MIT notices Clipy/ClipMenu; не копировать иконки без проверки.

## Review Focus

1. Clipboard owner сменился посреди чтения: не смешать форматы разных записей (T04–06).
2. Удаление/clear во время OCR/thumbnail: фоновые результаты не должны воскресить запись (T12,16).
3. Auto-paste не разрешена/target потерян: выбранные данные доступны вручную, запись не теряется (T08).
4. Limits/INCR/повреждённые картинки/XML: bounded memory и атомарный отказ (T02,06,11,12).
5. Linux нет tray/portal/data-control/source identity: app остаётся доступной и показывает точные capabilities (T06,07,09).

## Порядок и контрольные точки

1. **M0 / T01:** сравнительный прототип и frozen architecture/support/size решения. Прототип не production; запускать после ревью дизайна. При сомнениях не писать все три оболочки до измерений.
2. **M1 / T02–04,07–09:** общий core и первая рабочая macOS text/image history вертикаль, затем Windows T05; M1 не весь parity.
3. **M2 / T06–13:** Linux, snippets/editor/XML, все settings/preview/screenshot/autostart/locales.
4. **M3 / T14–16:** migration, update/diagnostics решение, OCR/index lane.
5. **M4 / T17:** release verification, distribution и feature closure.

T04/T05/T06 можно разрабатывать независимо только после утверждения общей модели T02/T03; T07/T09 общие модели и оболочки должны проходить единый contract suite. T14 не блокирует новую чистую установку, но блокирует обещание миграции пользователей Clipy. Оценка сроков даётся после T01 по выбранным support/packaging/OCR решениям, не выдумывается до проверки платформ.

## Структура будущего репозитория

```text
CMakeLists.txt, CMakePresets.json
cmake/{Dependencies,Warnings,SizeOptimizations}.cmake
core/include/cliptiq/{types,capabilities,storage,policy,history,menu,paste,hotkeys,snippets,xml,settings,media,ocr,update}.hpp
core/src/{storage,policy,history,menu,paste,hotkeys,snippets,xml,settings,media,ocr,update}.cpp
platform/macos/{Clipboard,Application,Menu,Settings,SnippetEditor,Integration,Ocr}.mm
platform/windows/{Clipboard,Application,Menu,Settings,SnippetEditor,Integration,Ocr}.cpp
platform/linux/{Application,ClipboardX11,ClipboardWayland,Menu,Settings,SnippetEditor,Integration,Ocr}.cpp
platform/linux/protocols/{ext-data-control-v1,wlr-data-control-unstable-v1}.xml
resources/{icons,locales}/
tools/import-clipy/{CMakeLists.txt,main.mm,legacy.swift,README.md}
tools/{size-report.py,verify-coverage.py}
tests/{unit,integration,fixtures,manual}/
packaging/{macos,windows,linux}/
.github/workflows/{ci,release}.yml
docs/{research,superpowers,support}/
THIRD_PARTY_NOTICES.md
```

Production headers — boundaries между общими правилами и ОС. Внутренние private helpers не задаём заранее. Test runner минимальный CTest без большого production framework; Apple/Windows GUI harness отдельный. Developer SDK/CMake/Python не поставляются пользователю.

## Контракты, фиксируемые до платформенных работ

`types.hpp`: `EntryId`, `FolderId`, `SnippetId` — UUID strings; `Bytes=vector<uint8_t>`; `CanonicalKind` = Text/Rtf/Rtfd/Html/Pdf/Files/Url/Image/Opaque; `Representation { uint32_t item_index; string platform; string native_type; CanonicalKind kind; Bytes data; }`; `ClipboardSnapshot { vector<Representation> representations; string sequence_token; SourceInfo source; ClipboardFlags flags; }`.

`SourceInfo { optional<string> app_id; SourceConfidence confidence; }`, confidence = Known/BestEffort/Unknown. `ClipboardFlags` содержит transient/concealed/remote как Unknown/No/Yes, а не произвольную бинарную догадку. `HistoryEntry { EntryId id; string digest; int64_t created_ms; int64_t used_ms; }`. Persistent schema дополняет entry raw representations, preview и OCR nullable/state.

Capture contract: сохранять все разрешённые representations поддерживаемых классов; disabled kind удаляется из snapshot, unknown native types отбрасываются. Opaque bytes для известного RTFD/PDF допустимы. При непустом excluded-app list Unknown source по предлагаемому default отклоняется, в том числе у screenshot watcher; AllowUnknown требует явной настройки. Эти defaults — осознанное улучшение, а не утверждение о поведении исходного Clipy.

`Result<T>` — единый project result с error code/message, для `void` отдельная специализация. `Capabilities { Capability capture, write, hotkeys, auto_paste, source_app, tray, pointer_placement, ocr; }`, каждая Capability имеет status Available/Limited/Unavailable и user-readable reason. `Clock` внедряется для scheduler tests. Нативные callbacks доставляют immutable snapshots/core commands, не framework objects.

### T01. Измерительный прототип и решение по стеку

**Files:** создать `experiments/footprint/{native-cpp,native-rust}/`, `tools/size-report.py`, `docs/support/architecture-decision.md`, `docs/support/size-baseline.json`. Экспериментальные файлы не входят в release app.

**Interfaces:** результат — JSON report `{target,arch,toolchain,flags,app_bytes,archive_bytes,dependency_delta_bytes,ocr_bytes,total_offline_bytes,idle_memory_bytes,startup_ms}` и ADR. Schema report фиксируется в этом task; T17 использует её без переименования полей.

- [x] Сравнимый SQLite fixture C++/Rust с общими native UI hosts, XML parity, FTS on/off, GTK3/GTK4, static/dynamic CRT, OCR generated fixture: выполнено. Linux clipboard/hotkey/SNI проверены отдельными protocol probes, не сравнительным product UI.
- [x] Измерены archive/app-owned, Linux clean-userland offline closure, Mac Sparkle обоих CPU, Windows native update helper и Linux full update payload. Windows/macOS clean VM недоступны; пользователь явно согласовал фиксацию ограничения CI. Fresh profile не заменяет чистую ОС.
- [x] Exact flags/toolchains/hashes, raw evidence и ADR опубликованы. Фактические команды воспроизведения находятся в experiments/*/README.md; исходные примерные presets/cargo команды заменены экспериментальным tools/build-footprint.py с одинаковым UI host.
- [x] T01 закрыт в согласованных границах: [итог](../../support/t01-results.md). Native C++ рекомендован, окончательный выбор стека и budgets до T02 остаётся открытым решением пользователя.

Не заявлены выполненными: полноценный tray/settings product UI на всех ОС, Windows packaged-vs-portable OCR support matrix, OCR quality/redistributable pack, clean stock OS VM, production signed update end-to-end и GNOME/KDE desktop compatibility. Эти product/release проверки сохраняются для соответствующих T04–T17; нулевой размер или PASS для них не подставляется. T02 не начат.

### T02. Модель данных, SQLite, история и очистка

**Files:** создать `core/include/cliptiq/{types,storage,history}.hpp`, `core/src/{storage,history}.cpp`, `tests/unit/history_test.cpp`, `tests/fixtures/history/`, root CMake/presets/dependency pins. **Covers:** C01–03 persistence, C12, H01–05, S15.

**Interfaces:** `open_store(path, Limits)->Result<Store>`; `ingest(Store&, ClipboardSnapshot, HistoryPolicy, int64_t now_ms)->Result<optional<EntryId>>`; `list_history(Store&, SortOrder, size_t limit)->Result<vector<HistoryEntry>>`; `load_snapshot(Store&, EntryId)->Result<ClipboardSnapshot>`; `erase_history(Store&, optional<EntryId>)->Result<void>`; `tick_cleanup(Store&, CleanupPolicy, int64_t now_ms)->Result<void>`; `HistoryPolicy` содержит max_count, allow_duplicates, replace_duplicate; `Limits` — spec values.

- [ ] Failing tests `preserves_all_representations`, `duplicate_mode_switch_keeps_digest`, `blank_text_ignored`, `last_used_preserves_created`, `limit_three`, `periodic_clear_is_not_ttl`, `clear_preserves_snippets`, `oversize_is_atomic`. Assertions: 4 copies/limit3 → 3 visible; allow=false после UUID mode не создаёт дубликат; whole-history clear каждые 300 sec удаляет и свежие записи.
- [ ] `cmake --preset test && cmake --build --preset test && ctest --preset test -R history --output-on-failure` — сначала failing assertion/not-implemented, затем PASS после реализации. В PowerShell выполнять команды последовательно, без зависимости от `&&` старых версий shell.
- [ ] Реализовать schema/transactions/FK, canonical digest bytes, counters, quotas и injected clock; native assets не преобразовывать в один текст. SQL user data только bindings. Cleanup VACUUM/checkpoint по необходимости вне UI; не обещать secure erase.
- [ ] Проверить reopen, interrupted transaction, full disk/read-only/corrupt DB: понятная ошибка, исходная БД не стирается автоматически. ASan/UBSan на supported Clang targets.
- [ ] Commit `feat: add bounded persistent clipboard history`.

### T03. Privacy policy и capability contract

**Files:** `core/include/cliptiq/{capabilities,policy}.hpp`, `core/src/policy.cpp`, `tests/unit/policy_test.cpp`. **Covers:** C13–16.

**Interfaces:** `evaluate_capture(const ClipboardSnapshot&, const CapturePolicy&, const Capabilities&)->CaptureDecision`; decision = Accept/Reject + reason + filtered snapshot. Policy содержит enabled kinds, concealed/remote flags, excluded apps и unknown_source_mode. После policy только принятый snapshot может попасть в T02/T12/T16.

- [ ] Failing tests: `transient_always_rejected`, `concealed_option`, `remote_primary_file_rejected`, `remote_secondary_file_removed`, `marker_prefix_exclusion`, `unknown_source_is_not_known_safe`, `screenshot_uses_same_policy`, `html_off_keeps_text_rtf_only`, `unknown_type_is_not_persisted`, `unknown_source_with_exclusions_is_rejected`.
- [ ] Запустить `ctest --preset test -R policy --output-on-failure`, подтвердить meaningful failure.
- [ ] Реализовать правила из C11–16 с платформенными отличиями; source confidence сохранять честно. No hidden telemetry/default network events.
- [ ] Повторить tests до PASS, проверить что reject вызывает ноль storage/thumbnail/OCR операций fake spies.
- [ ] Commit `feat: gate capture through explicit privacy policy`.

### T04. macOS clipboard backend

**Files:** `platform/macos/{Clipboard,Application}.mm`, `tests/integration/macos_clipboard_test.mm`, `tests/manual/macos.md`. **Covers:** C01–16 macOS.

**Interfaces:** `ClipboardBackend::capabilities()->Capabilities`, `read_snapshot()->Result<ClipboardSnapshot>`, `write_snapshot(const ClipboardSnapshot&)->Result<string>` (own-write token), `start(CaptureCallback)->Result<void>`, `stop()`. Backend lifetime принадлежит application; callback вызывается на UI dispatcher, storage work ставится в worker queue.

- [ ] Failing integration cases: root filenames/TIFF fallback, PNG preference, multiple items + metadata, stable source change token. Fixture bytes из upstream tests воссоздать/атрибутировать.
- [ ] Настроить native test target; `ctest --preset test -R macos_clipboard --output-on-failure` на Mac, не эмулировать успешный OS test на Windows.
- [ ] NSPasteboard.changeCount polling 500 ms; snapshot consistency check до/после чтения с bounded retry; современный и legacy format mapping. При отказе pasteboard access показать capability reason.
- [ ] Проверить TextEdit/Safari/Finder/Preview, permission granted/denied, image-only/RTF/RTFD/PDF/multiple files; результаты записать с OS build.
- [ ] Commit `feat(macos): add native clipboard capture and replay`.

### T05. Windows clipboard backend

**Files:** `platform/windows/{Clipboard,Application}.cpp`, `tests/integration/windows_clipboard_test.cpp`, `tests/manual/windows.md`. **Covers:** C01–16 Windows.

**Interfaces:** тот же ClipboardBackend T04, SourceInfo с foreground/owner confidence.

- [ ] Failing cases: UTF-16/emoji/CRLF, CF_HTML UTF-8 offsets, Rich Text Format, CF_HDROP multiple paths, DIBV5 alpha, custom PDF bytes, clipboard busy/owner exits/sequence changes.
- [ ] `ctest --preset test -R windows_clipboard --output-on-failure` на Windows, failing before implementation.
- [ ] Message window + AddClipboardFormatListener, bounded retry с sequence guard, deep-copy transferable bytes, release handles по API ownership. Не сохранять дескрипторы объектов как байты для будущего процесса. WIC decode на worker с limits.
- [ ] Notepad/browser/Office или LibreOffice/Explorer/image editor smoke; virtual files/OLE-only objects явно классифицировать unsupported, не объявлять поддержкой всех Windows clipboard formats.
- [ ] Commit `feat(windows): add native multi-format clipboard backend`.

### T06. Linux X11 и Wayland backend

**Files:** `platform/linux/{Application,ClipboardX11,ClipboardWayland}.cpp`, protocol XML pinned copies, `tests/integration/linux_clipboard_test.cpp`, `tests/manual/linux-desktops.md`. **Covers:** C01–16 Linux.

**Interfaces:** ClipboardBackend T04; runtime `Capabilities` T03. X11-only process не объявляет native-Wayland support.

- [ ] Failing protocol/fixture tests: X11 TARGETS/INCR/owner death, Wayland offer fd cancel/timeout/change, MIME streams >32 MiB, no-data-control, source unknown. Expected: partial entry не сохраняется; capability reason точный.
- [ ] `ctest --preset test -R linux_clipboard --output-on-failure`; protocol state machine tests без desktop и отдельные native tests в Xvfb/на реальном compositor.
- [ ] XFixes events, selection ownership, bounded INCR; Wayland native ext-data-control с wlr fallback через libwayland. GTK clipboard focus не выдавать за background history. PRIMARY не добавлять в parity scope: Clipy имеет один general clipboard.
- [ ] Прогнать concrete GNOME/KDE/Sway/Hyprland versions и X11; записать capture/write/source/portal/tray отдельно. Clipboard portal не использовать как обход разрешений. Если нужен compositor plugin, реализовать и проверить version-pinned integration pack; без него limited environment, не supported history desktop. Support floor: capture+write+persistence+snippets+launcher+working popup route; auto-paste/tray могут иметь объявленный fallback.
- [ ] Commit `feat(linux): add capability-aware X11 and Wayland backends`.

### T07. Menu model и три нативных popup/tray UI

**Files:** `core/include/cliptiq/menu.hpp`, `core/src/menu.cpp`, `platform/{macos/Menu.mm,windows/Menu.cpp,linux/Menu.cpp}`, `tests/unit/menu_test.cpp`, native smoke docs. **Covers:** H06–12.

**Interfaces:** `build_menu(Store&, MenuKind, const MenuSettings&)->Result<MenuModel>`; `MenuItem` = label/action/entry-or-snippet ID/children/number/tooltip/preview reference/enabled; `render_menu(MenuModel, InvocationContext)->Result<void>`. InvocationContext содержит target и activation token, если предоставлен.

- [ ] Failing tests: inline0/1/100, folder1/10/100, zero/one numbering, first-ten shortcuts, title Unicode truncation, tooltip limits, snippets above/below, clear disabled empty.
- [ ] `ctest --preset test -R menu --output-on-failure`, затем реализовать общую model и adapters. Платформы не повторяют SQL/menu grouping алгоритм.
- [ ] AppKit NSMenu/NSStatusItem; Win32 native/owner draw; GTK popup + SNI/DBusMenu без GtkStatusIcon. Проверить image/tooltips в tray host; если host не рисует, полный popup даёт доступ к preview. Wayland не обещать произвольную абсолютную позицию.
- [ ] Keyboard-only navigation, screen reader labels, DPI100/150/200%, dark/high-contrast, hidden/no tray, Explorer restart, compositor restart. Видимый fallback launcher сохраняет доступ.
- [ ] Commit `feat: add native history and snippets menus`.

### T08. Вставка и modifier actions

**Files:** `core/include/cliptiq/paste.hpp`, `core/src/paste.cpp`, native Integration files, `tests/unit/paste_test.cpp`, native focus tests. **Covers:** H13–16.

**Interfaces:** `execute_selection(Store&, SelectionIntent, ClipboardBackend&, PasteBackend&)->Result<SelectionOutcome>`; intent включает entry/snippet, Plain/Original/DeleteOnly/PasteDelete и captured target; outcome = Copied/Dispatched/NeedsManualPaste/Cancelled. `PasteBackend::dispatch(TargetToken, Shortcut)->Result<void>` не подтверждает приём текста чужим приложением.

- [ ] Failing tests: auto off just copies; delete only no write; image-only plain no empty overwrite; self-write не recapture; denied permission/UIPI/focus changed сохраняет запись; accepted paste-delete удаляет entry.
- [ ] `ctest --preset test -R paste --output-on-failure`, затем core intent pipeline и native implementations.
- [ ] Mac Accessibility + CGEvent/keymap; Windows SendInput/held modifier checks/foreground rules; X11 XTest/XKB; Wayland только authorised integration/portal session, иначе NeedsManualPaste. Никаких root/uinput обходов по умолчанию.
- [ ] Browser/editor/terminal, RU/EN layouts, held modifiers, elevated target, blocked portal, target closes during popup. Возврат focus не обходить агрессивными hacks.
- [ ] Commit `feat: add safe native paste actions and manual fallback`.

### T09. Настройки и глобальные shortcuts

**Files:** `core/include/cliptiq/{settings,hotkeys}.hpp`, `core/src/{settings,hotkeys}.cpp`, native Settings/Integration files, `tests/unit/settings_test.cpp`, `tests/unit/hotkeys_test.cpp`. **Covers:** C11,14–16, H17–18, S09.

**Interfaces:** `load_settings(Store&)->Result<Settings>`; `save_settings(Store&, Settings)->Result<void>` with validation; `bind_hotkey(ActionId, optional<KeyChord>)->Result<BindingStatus>`. Action IDs main/history/snippets/settings/editor/clear/folder:<uuid>. KeyChord хранит platform-specific logical/physical meaning явно.

- [ ] Failing tests: defaults/ranges из аудита, settings cannot unbind, conflict rollback, disabled/deleted folder, unknown backend capability hides misleading claim.
- [ ] `ctest --preset test -R 'settings|hotkeys' --output-on-failure`; native adapters проверяются отдельно.
- [ ] Mac defaults сохранить. Windows/Linux proposed defaults: main Ctrl+Alt+V, history Ctrl+Alt+H, snippets Ctrl+Alt+B, settings Ctrl+Alt+Shift+V; editor/clear unset. Не занимать Win+V. При конфликте предлагаемое сочетание не считается зарегистрированным. X11 lock masks; Wayland portal user-controlled binding, `.desktop`/`cliptiq --show-*` fallback.
- [ ] Все восемь settings groups представлены; unavailable controls дают reason/recovery. Не хранить настройки в неподконтрольном чужом namespace. Запись atomic, live reload там где Clipy, restart indication только где нужно.
- [ ] Commit `feat: add persistent settings and platform shortcut bindings`.

### T10. Сниппеты и редактор на трёх ОС

**Files:** `core/include/cliptiq/snippets.hpp`, `core/src/snippets.cpp`, native SnippetEditor files, `tests/unit/snippets_test.cpp`. **Covers:** S01–04, H18.

**Interfaces:** `list_folders(Store&)->Result<vector<Folder>>`; `create_folder(Store&, string)->Result<FolderId>`; `save_snippet(Store&, Snippet)->Result<SnippetId>`; `move_snippet(Store&, SnippetId, FolderId, size_t index)->Result<void>`; `reorder_folders(Store&, vector<FolderId>)->Result<void>`; `set_enabled(Store&, ItemId,bool)->Result<void>`; `erase_snippet_item(Store&, ItemId)->Result<void>`. Folder/Snippet contain id/title/index/enabled; Snippet folder_id/content.

- [ ] Failing tests: CRUD, Unicode/multiline, disabled flags, inter-folder move, duplicate index repair transaction, folder delete cascades snippets/hotkey cleanup.
- [ ] `ctest --preset test -R snippets --output-on-failure`, затем repository/native editor. Не добавлять nesting/tag/templating, которых нет у Clipy.
- [ ] Implement native DnD, focus/undo-per-snippet, delete confirm, folder shortcut recorder. Содержимое plain text, не HTML editor.
- [ ] Restart persistence, cancelled drag, empty folder, keyboard editing, enable toggles; same tests на каждой оболочке.
- [ ] Commit `feat: add folders and native snippet editors`.

### T11. XML совместимость Clipy

**Files:** `core/include/cliptiq/xml.hpp`, `core/src/xml.cpp`, `tests/unit/xml_test.cpp`, `tests/fixtures/xml/`. **Covers:** S05–06.

**Interfaces:** `decode_clipy_xml(Bytes, ImportLimits)->Result<vector<ImportedFolder>>`; `append_import(Store&, vector<ImportedFolder>)->Result<ImportSummary>`; `encode_clipy_xml(Store&)->Result<Bytes>`. ImportedFolder title + ordered pairs title/content; XML не переносит flags/IDs/hotkeys, отдельный format migration T14.

- [ ] Failing tests: `roundtrip_whitespace_unicode`, `append_not_replace`, `fallback_titles`, `all_disabled_are_exported`, `entity_escape`, `malformed_is_atomic`, `external_entity_rejected`, size/count ceilings.
- [ ] `ctest --preset test -R xml --output-on-failure` meaningful fail; затем pinned parser, DTD/external entities запрещены, UTF-8, atomic destination write.
- [ ] Export default `snippets.xml`, native file pickers, readable error вместо молчаливого beep. XML layout `folders/folder/title/snippets/snippet/{title,content}` согласно audit.
- [ ] На Mac import/export в настоящий Clipy соответствующей baseline; fixtures не заменяют совместимость с живым приложением.
- [ ] Commit `feat: support Clipy-compatible snippet XML`.

### T12. Preview и screenshot file observation

**Files:** `core/include/cliptiq/media.hpp`, `core/src/media.cpp`, native Integration/image helpers, `tests/unit/media_test.cpp`, screenshot fixtures. **Covers:** H12, S07.

**Interfaces:** `request_preview(EntryId, PreviewSize, generation)->Result<void>`; `on_screenshot_file(path)->Result<void>` отправляет snapshot через T03. Background completion `store_preview_if_present(EntryId,generation,Bytes)` атомарно проверяет наличие/generation.

- [ ] Failing tests: aspect fit 100×32, 1–512 boundaries, hex swatch format fixtures, changed dimensions regenerate, delete race, malformed image/40MP, growing file waits, own clipboard screenshot dedup.
- [ ] `ctest --preset test -R media --output-on-failure`, затем OS codecs и bounded worker queue; preview cache ограничен, main menu не декодирует full image.
- [ ] Исследовать Screeen detector package перед заявлением точного Mac parity; native file notification + configured screenshot locations, Windows ReadDirectoryChangesW, Linux GFileMonitor. Не сохранять любой появившийся PNG как скриншот без выбранной директории.
- [ ] Проверить off по умолчанию, permission denied, rename/partial write, exclude policy, watcher stop/restart. Документировать различие от «захватить экран».
- [ ] Commit `feat: add bounded previews and screenshot folder observation`.

### T13. Автозапуск, локализация и доступность приложения

**Files:** native Integration/Application/Settings files, `resources/locales/`, packaging desktop/manifests, `tests/manual/lifecycle.md`, `THIRD_PARTY_NOTICES.md`. **Covers:** S08–09,12–13.

**Interfaces:** `set_autostart(bool)->Result<AutostartStatus>`; `show_main/history/snippets/settings` CLI routes к единственному user-session instance; schema locale keys генерируется одинаковая для всех оболочек.

- [ ] Failing contract tests: second-instance forwards action, cannot lose settings access, autostart failed registration remains visible as failure; missing locale → EN. Создать source locale inventory из xcstrings/lproj.
- [ ] Нативный test и `ctest --preset test -R lifecycle --output-on-failure`; затем SMAppService/HKCU Run или packaged StartupTask/XDG autostart. IPC per-user, не принимать произвольный executable/path command.
- [ ] Перенести доступные переводы с attribution, RU/EN обязательный quality smoke; missing translations явно fallback. About/version/support links только своего проекта. Donation URL не выдумывать до предоставления.
- [ ] Logout/login, hidden icon/no tray/no shortcut, uninstall autostart cleanup, DPI/accessibility focus, one app per session. Приложение работает без сети.
- [ ] Commit `feat: add desktop lifecycle integration and localized settings`.

### T14. Миграция данных Clipy без тяжёлого runtime

**Files:** `tools/import-clipy/{CMakeLists.txt,main.mm,legacy.swift,README.md}`, `core/src/storage.cpp` neutral import routines, `tests/fixtures/migration/`, `tests/integration/import_test.cpp`. **Covers:** S14–15.

**Interfaces:** utility создаёт versioned neutral SQLite interchange (`format_version=1`, original ids/order/flags/times/native representations/settings); `import_clipy(Store&, path, ImportOptions)->Result<ImportSummary>` — read-only source, transactional destination. Native format labels не объявляют переносимость RTFD на Windows.

- [ ] Failing tests: current SQLite v1–v4, legacy Realm/schema7 + archives, settings archived shortcuts/types/exclusions, disabled nil shortcuts, duplicate content, missing asset, repeated import, invalid DB/schema.
- [ ] `ctest --preset test -R import --output-on-failure`; utility native test на Mac с Realm отдельно. Не открывать чужой NSKeyedArchive общей небезопасной десериализацией внутри основного runtime.
- [ ] Сделать backup/export source после согласованного user action в будущей утилите; source никогда не обновлять и не удалять. Прямой SQLite importer использует consistent snapshot, legacy stage read-only насколько допускает Realm; если Realm требует sidecars — работать с копией источника.
- [ ] Перенести XML fallback для пользователей, которым нужна только коллекция snippets; объяснить loss enable/UUID/shortcuts в XML. Проверить neutral import на Windows/Linux.
- [ ] Commit `feat: add optional read-only Clipy migration tool`.

### T15. Обновления и решение по диагностике

**Files:** `core/include/cliptiq/update.hpp`, `core/src/update.cpp`, native Integration/update helpers, `packaging/`, `tests/unit/update_test.cpp`, `docs/support/privacy.md`. **Covers:** S10–11.

**Interfaces:** `check_update(UpdateChannel, Version)->Result<UpdateInfo>`; `UpdateInfo` содержит version/platform/arch/package_url/signing metadata; `apply_update(UpdateInfo)->Result<UpdateOutcome>` делегирует platform route. User schedule 86400/604800/2592000 sec + last-check хранится settings; actual manual/auto preferences поставщика учитываются.

- [ ] Failing tests: offline, timeout, invalid/missing signature, wrong target, downgrade, size ceiling, malformed version, install cancelled, package-managed binary never overwritten.
- [ ] `ctest --preset test -R update --output-on-failure`; реализовать native secure route. Mac измеренный Sparkle lane со своим feed/keys; Windows signed installer/MSIX manager, portable helper только с подписью/rollback plan; Linux package manager для deb/rpm, tar check + явная download/install route. HTTPS alone не выдавать за проверку подлинности release.
- [ ] Сохранить check now/auto schedule/last-check/version. Отдельно решить, требует ли full parity автоустановку в portable lane: если да — signed helper является обязательной частью размера, не скрытым скачиванием после установки.
- [ ] По предлагаемому решению убрать Firebase и предложить локальный export диагностики с redaction. Если нужна исходная аналитика/crash reporting, сохранить эту строку как pending, выделить opt-in telemetry компонент, новый endpoint/privacy и measured dependency closure. Clipboard/snippets ни в одной lane не отправляются.
- [ ] Commit `feat: add signed platform update routes and diagnostics policy`.

### T16. OCR и FTS backend, отдельно от нового UI

**Files:** `core/include/cliptiq/ocr.hpp`, `core/src/ocr.cpp`, native Ocr files, `packaging/ocr/`, `tests/unit/ocr_test.cpp`, image/language fixtures. **Covers:** B01–02.

**Interfaces:** `recognize(EntryId,generation,ImageInput,OcrBackend&)->Result<void>`; `OcrBackend::languages()->Result<vector<string>>`, `recognize_text(ImageInput)->Result<string>`; persist только если id/generation существует. `OcrState` Pending/Done/Unavailable/Failed, empty Done допустимо. Feature `ocr_index` включает SQLite FTS5 только для disposable `index/search.db`; main DB во всех сборках имеет одинаковые обычные OCR columns и не содержит FTS tables/triggers. `rebuild_index(Store&, index_path, data_revision)->Result<void>` читает main snapshot, индексатор подтверждает revision после обновления. `invalidate_index(index_path)->Result<void>` удаляет только известные index files/sidecars в принадлежащем приложению namespace. Base никогда не открывает FTS DB: инвалидирует её при старте/clear/import, indexed rebuilds. Single-instance lock общий для вариантов.

- [ ] Failing tests: truncate Unicode до 10 000 chars, empty result done, deleted-during-OCR, unavailable language, huge image, cancelled job, migration OCR retention, companion FTS insert/update/delete/integrity, `indexed_base_indexed_preserves_history`, `base_clear_invalidates_index`, `crash_after_main_commit_rebuilds_index`. Main DB обычные writes должны работать без FTS module после перехода с indexed build.
- [ ] `ctest --preset test -R 'ocr|fts' --output-on-failure`, затем async queue/native engines. Mac Vision; Windows.Media.Ocr только verified packaged identity lane; portable Windows/Linux pinned Tesseract/Leptonica + явно выбранные модели, как optional pack.
- [ ] В T01 зафиксировать начальные языки EN/RU и их actual engine availability; language detection и correction у разных engines не считать идентичными. Дополнительные языки — отдельный размер. Offline install closure содержит pack, нельзя назвать его нулевым.
- [ ] Проверить B01/B02 parity lane в каждой ОС. **Не создавать search field/OCR copy actions в этом task:** текущий Clipy их не предоставляет; если пользователь захочет, новый scoped task и дизайн.
- [ ] Commit `feat: add optional native and offline OCR indexing backends`.

### T17. CI, упаковка, размер и закрытие всех ID

**Files:** `.github/workflows/{ci,release}.yml`, `tools/verify-coverage.py`, `tools/size-report.py`, `packaging/{macos,windows,linux}/`, `docs/support/{feature-status,release-checklist,size-baseline}.json/md`. **Covers:** все ID, distribution.

**Interfaces:** `feature-status.json` имеет `{feature_id,platform,backend,status,evidence,limitation}`; status = verified/adapted/unsupported/pending. `verify-coverage.py --audit docs/research/2026-10-23-clipy-audit.md --status docs/support/feature-status.json` должен проваливаться при пропавшем ID или «verified» без evidence. Подтверждённые адаптации не переименовывать в verified exact parity.

- [ ] Failing CI fixtures: missing C05/S11/B02, excessive size, missing DLL/.so, wrong architecture, unsigned/wrong signer update, package contains reference/build cache. Сначала validator должен поймать эти ошибки.
- [ ] Native CMake release/test на macOS/Windows/Linux; pinned tools/third-party hashes, least-privilege CI. ASan/UBSan где доступны, bounded parser/image fuzz smoke. Cross compile не заменяет запуск на target.
- [ ] Artifacts: Mac отдельные arm64/x64 .app.zip/DMG по необходимости, signing/notarization; Windows x64 zip + installer/MSIX, arm64 отдельно после smoke; Linux deb/rpm/tar с объявленными dependencies. Native aarch64 тесты до release claim. Universal Mac optional и его размер показан отдельно.
- [ ] `ctest --preset release-test --output-on-failure`; `python tools/verify-coverage.py --audit docs/research/2026-10-23-clipy-audit.md --status docs/support/feature-status.json`; `python tools/size-report.py --dist dist --baseline docs/support/size-baseline.json --output out/size.json`. Expected: PASS, zero unclassified IDs, no silent overbudget, full dependency report. Все presets/scripts ещё предстоит создать.
- [ ] Реальные clean-machine install/uninstall и manual tests T04–16, offline operation, permission revocation, sleep/resume, desktop restart. Release checklist указывает signer/OS build/commit/actual sizes. Commit `ci: enforce platform parity and artifact size budgets`. Публиковать release/отправлять PR только в рамках соответствующей последующей команды пользователя.

## Решения, которые нужно подтвердить до реализации

1. **Решено пользователем:** сохраняем Cliptiq после сообщения о найденном Cliptic. Повторно подтверждать имя не требуется.
2. Принимаем ли предложенный native C++ подход после сравнительного T01? Если Rust измеренно лучше по сочетанию размера/поддержки — ADR меняется, не начинается незапланированная вторая реализация.
3. Разрешены ли явные адаптации: Firebase → local diagnostics, OCR/index отдельным pack/build, Wayland limited mode без compositor integration? Если цель — буквальная полнота на каждом backend, packs/integrations входят в full distribution и её размер.
4. После T01 фиксируем численный size ceiling по archive/app-owned/clean-machine total отдельно. «Маленькая сборка» без этих трёх чисел не является критерием приёмки.

Для каждого варианта указаны работа, dependency и критерий проверки. Пользователь разрешил T01: установка инструментов, prototypes, CI и загрузка документов выполнены в его рамках. T02 product scaffold не начат. Документы и эксперименты опубликованы в origin/codex/portability-plan.

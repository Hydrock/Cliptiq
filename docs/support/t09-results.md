# T09 — настройки и глобальные горячие клавиши

Автоматическая часть T09 реализована: source `3c49ba72b019c577f9c8cc39fd3b310b3ce7f58c`, [CI40056122028 — 7/7 success](https://github.com/Hydrock/Cliptiq/actions/runs/40056122028). **Ручная приёмка T04–T08 остаётся OPEN; интерактивная T09 тоже OPEN. T10 не начат.** Пользовательских релизных пакетов пока нет.

## Реализовано

Общий C++17 Settings snapshot соединяет постоянные настройки с T02 history, T03 privacy gate, T07 menu и T08 paste/modifiers. SQLite namespace `t09.*`: version/bounds/validation, атомарная запись, revision CAS против устаревшего окна, сохранение чужих ключей, немедленное pruning при снижении history count. Повреждённая/будущая schema возвращает ошибку. Успешный Apply обновляет capture/menu/paste options: capture сначала останавливается и дренируется, затем запускается с новой policy; при ошибке возвращается прежняя конфигурация. Автоматический reload из другого процесса не заявлен.

Все восемь групп представлены нативными AppKit/Win32/GTK контролами: General, Menu, Types, Excluded, Shortcuts, Updates, Beta, Donate. Defaults/ranges следуют audit. Типы/исключения/concealed/remote/unknown-source, history count/sort/duplicates, grouping/titles/tooltips/previews/tray, auto-paste/modifiers, confirm/periodic/quit cleanup сохраняются. Будущие autostart/screenshots/updater/diagnostics/donation controls отключены с reason/recovery; SDK/службы не добавлены. Generic Windows type icons остаются T07 ограничением.

Actions: main/history/snippets/settings/editor/clear/folder:<id>. Mac defaults сохранены; Windows/Linux Ctrl+Alt+V/H/B и Ctrl+Alt+Shift+V для Settings; editor/clear unset. Settings нельзя убрать; Win+V/F12, внутренние дубликаты и некорректные chords отвергаются. Logical/physical meaning явный; Windows physical не поддерживает и сообщает причину. Editor T10 action открывает Settings с объяснением.

Новая регистрация выполняется до освобождения старой и сохранения. OS conflict, ошибка записи и исключение откатывают кандидатов, сохраняя прежние registrations/preferences. Cross-action swap может требовать промежуточное свободное сочетание. Папки T02 получают маршруты; удалённые/disabled availability освобождают их. T10 schema/CRUD не добавлены.

## Платформы

- **macOS:** Carbon RegisterEventHotKey, AppKit scrollable tabs/field editor. Native registration/conflict/rebind/lifetime проходят. Recorder key-event/lifecycle проверяется без утверждения desktop activation grant; глобальный chord записывается только в key Settings window.
- **Windows:** RegisterHotKey/MOD_NOREPEAT, scrollable pages, resize/focus reveal, WM_SYSKEYDOWN recorder для Alt. Станция CI возвращает1459: capability unavailable, denial проверяется. Отдельный тест реального adapter подменяет только OS register/unregister и station fact, проверяя VK/modifier mapping, rollback и cleanup. **Реальная Windows registration/activation manual OPEN.**
- **X11:** checked XCB passive grabs, lock masks, rollback частичного grab. Xvfb проверяет настоящую активацию, конфликт другого клиента, Caps Lock, release и live logical remap. Новый keycode регистрируется до освобождения старого с сохранением registry ID. Занятый remap требует другого свободного chord; неподдержанная Latin mapping даёт recovery.
- **Wayland:** явная авторизация GlobalShortcuts; preferred chord только hint. Actual trigger descriptions/подмножество actions, ShortcutsChanged; отказ сохраняет session. Session/owner close снимает claim registered. Activation проверяет session/action и передаёт token, не выдавая его за input permission. Private D-Bus fixture проверяет response-before-return/grant/denial/rollback/changed/closed, а не реальный compositor. Нет XWayland/root/uinput обхода.

Recovery CLI: `--show-main/history/snippets/settings/editor`, `--clear-history`, `--show-folder ID`. Linux `.desktop.in` шаблон не устанавливается автоматически; сейчас он запускает второй review host, а не IPC. Собственные пути Cliptiq и контракты — [API](../../core/SETTINGS.md).

## Размер

Парные MinSizeRel probes: тот же native menu/history/paste host без T09 и с T09. Windows CRT статический; GUI/input frameworks системные. Без Electron/Qt/ICU/new codec. Байты; ZIP=один executable DEFLATE9 с фиксированной датой.

| Платформа | Baseline T08 | С T09 | Прирост executable | ZIP с T09 | Прирост ZIP |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 200 672 | 1 377 384 | 176 712 | 603 871 | 58 539 |
| macOS x86_64 | 1 183 600 | 1 358 624 | 175 024 | 626 908 | 57 400 |
| Windows x64 | 1 177 088 | 1 307 648 | 130 560 | 691 594 | 61 051 |
| Linux x86_64 | 1 039 984 | 1 161 072 | 121 088 | 575 275 | 50 850 |

Linux resolved ELF closure35381992B у обеих сборок, **новых shared-library bytes0**. Это существующие файлы+loader с real-path deduplication; fonts/themes/resources/packages не входят. Библиотеки не копировались. В headless smoke записаны GTK theme/pixbuf и AT-SPI warnings ([свидетельство](t09-measurements/ci-gui-warnings.json)); ресурсы/loaders и реальная отрисовка остаются manual gate. Полный installer/offline/update footprint пока не измерен; числа не подменяют T01. Локальные paired binary SHA256 совпали с финальным Linux CI.

## Проверки и review

CI7/7: Debug+MinSizeRel Windows2025, Ubuntu24.04, macOS15 arm64/Intel; Linux/macOS ASan+UBSan, MSVC ASan. **Linux28, Windows20, macOS17 CTest groups**, не assertions. Локально GCC Debug/MinSizeRel/ASan+UBSan28/28; MSVC build+семь безопасных групп. Hosted fixtures не подменяют clean VM.

Настройки: defaults/ranges, atomic roundtrip, stale revision, mandatory/duplicate chords, future schema read/save protection, physical meaning, immediate retention pruning без стирания старых типов, folder routing, восемь групп и unavailable reasons. Hotkeys: parse/reserved, external/internal conflicts, batch persistence Result/exception rollback, folder release, lifetime cleanup. Native UI применяет поля в настоящую SQLite; preview bitmap options обновляются live. Старые policy/native clipboard/menu/paste suites сохранены.

Один независимый review:0 Critical/4 Important/0 deferred Minor. Все четыре RED→GREEN: background recorder, X11 logical remap recovery, Windows folder scrolling и Alt event path. CI40055565156 сохраняет Alt=0/scroll=0 и Mac background-recorder RED. Дополнительные RED: persistence exception, future schema overwrite, retention/view limit, native options, portal ShortcutsChanged. Headless fixture corrections отмечены отдельно: AppKit activation не гарантируется; Sway без input seat проверяет recorder signal. Повторного review не было.

Один старый T06 X11 replay fixture упал при параллельных сборках; изолированный повтор и полный28/28 прошли без изменения T06. Оба лога сохранены, финальный CI зелёный. [JSON, compiler/flags/imports, логи и SHA256](t09-measurements/README.md), [решения](t09-ledger.md), [ручной checklist](../../tests/manual/settings.md). T04–T08 и T09 interactive OPEN; пользователь подтвердил отсутствие clean Windows/macOS VM. T10 не начат.

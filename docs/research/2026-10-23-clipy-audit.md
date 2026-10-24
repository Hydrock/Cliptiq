# Clipy → Cliptiq: аудит функций и матрица переноса

Дата: 23 октября 2026. Источник: [Clipy/Clipy](https://github.com/Clipy/Clipy), ветка `develop`, ревизия **71b0e82623d5c73b4b717d81233eb78db8bbe258**. Локальная копия: `reference/Clipy`. Это аудит исходников, не запуск Clipy и не проверка выпущенного бинарного релиза. Разработка Cliptiq ещё не начата.

Рабочее имя Cliptiq сохраняется; результаты проверки имён находятся в [naming](2026-10-23-naming.md).

## 1. Как читать матрицу

Все строки ниже относятся к исследуемой ревизии, а не к предположениям по сайту. «Общее» означает поведение общего ядра с нативным отображением на каждой ОС, а не обмен буфером между компьютерами. Linux разделён на X11 и Wayland, поскольку одинаковая ОС не означает одинаковый доступ к буферу и вводу.

В столбце «Проверка» указан наблюдаемый результат приёмки. Номер Txx связывает строку с задачей плана. Если полноценный перенос невозможен без дополнительной интеграции или увеличения поставки, это указано прямо. Никакая строка с ограничением не считается достигнутой полной эквивалентностью.

Исходные файлы в таблицах заданы относительно `reference/Clipy/Clipy/Sources/`. Постоянная база ссылок: [дерево исходников зафиксированной ревизии](https://github.com/Clipy/Clipy/tree/71b0e82623d5c73b4b717d81233eb78db8bbe258/Clipy/Sources).

## 2. Захват и форматы

| ID / источник | Функция и фактическое поведение | macOS | Windows | Linux X11 | Linux Wayland | Проверка / задача |
|---|---|---|---|---|---|---|
| C01 `Services/ClipService.swift:66` | Фоновая история; macOS опрос раз в 500 мс | NSPasteboard.changeCount, 500 мс | WM_CLIPBOARDUPDATE + номер последовательности, ограниченные повторы при занятом буфере | XFixes selection notifications, TARGETS/INCR | ext-data-control, затем wlr-data-control; проверка доступности | Копировать A/B, перезапустить, оба доступны; быстрые пропущенные изменения при polling не обещать; T02–06 |
| C02 `Models/PasteboardContent.swift:92` | Сохранение упорядоченных исходных представлений, root fallback, несколько элементов | Сохранить native type + bytes и границы элементов | EnumClipboardFormats, native bytes, CF_HDROP для группы файлов; не сериализовать указатели/handles | MIME/atoms и потоковые байты, владение selection | MIME offers, передача через fd, отмена и лимиты | Текст+HTML+RTF и два файла восстанавливаются вместе; T02,04–06 |
| C03 `Models/PasteboardAvailableType.swift:17` | Обычный текст | UTF-8 NSString | CF_UNICODETEXT, корректные UTF-16/CRLF | UTF8_STRING, text/plain; legacy STRING с преобразованием | text/plain;charset=utf-8 и text/plain | Кириллица, emoji, переводы строк; T04–06 |
| C04 тот же | RTF | public.rtf | Registered Rich Text Format | text/rtf, application/rtf по источнику | MIME при наличии | Форматированная вставка в редактор; T04–06 |
| C05 тот же | RTFD с вложениями | Сохранить нативно | Нативный blob сохранить для будущего round trip; полезная вставка только через реально доступные RTF/HTML/image/text альтернативы | Аналогично: непрозрачный тип, без обещания понимания приложениями | Аналогично | На Mac вложение сохранено; на других ОС явный fallback, без фиктивного «полного RTFD»; T04–06 |
| C06 `Models/PasteboardTypeSettings.swift:15` | HTML поддерживается, по умолчанию выключен | public.html | CF_HTML с byte-offsets StartHTML/StartFragment, не простая строка | text/html | text/html | UTF-8 offsets и fragment round trip; T04–06 |
| C07 `PasteboardAvailableType` | PDF как данные буфера | public.pdf | Registered PDF format, только если источник/приёмник его понимает | application/pdf | application/pdf | PDF байты не меняются, вставка проверяется в совместимом приложении; T04–06 |
| C08 тот же | Файлы — ссылки, не резервная копия содержимого | file URLs, legacy NSFilenames root | CF_HDROP, учитывать copy/cut-флаг; не повторять устаревшее «вырезать» автоматически | text/uri-list, gnome-copied-files при наличии | MIME списка URI, по окружению file-transfer | Несколько путей, пробелы, удалённый файл → понятная ошибка; T04–06 |
| C09 тот же | URL | public.url + text fallback | UniformResourceLocatorW и текст | text/uri-list/text/plain | MIME URL/text | HTTPS и Unicode URL, без открытия ссылки при выборе; T04–06 |
| C10 `PasteboardAvailableType.swift:126` | PNG/TIFF, при наличии PNG приоритет перед TIFF | ImageIO/AppKit | PNG registered + CF_DIB/CF_DIBV5 через WIC; alpha/top-down/stride | image/png, image/tiff через gdk-pixbuf | MIME image formats | Прозрачность, big image, повреждённый заголовок; T04–06,12 |
| C11 `PasteboardTypeSettings` | Включение каждого из 8 классов форматов отдельно; всё кроме HTML включено | Общее | Общее | Общее | Общее при доступном захвате | Отключённый класс не попадает в новую историю, старый не стирается молча; T02,09 |
| C12 `PasteboardContent.swift:29` | Пустой/пробельный primary string/RTF отбрасывается | Общее | Общее | Общее | Общее | Пробелы не создают запись, image не считается пустой строкой; T02 |
| C13 `PasteboardAvailableType.swift:33` | Transient всегда игнорируется; concealed/remote сохраняются как метаданные при разрешении | Сохранить точные NSPasteboard markers | Только известные публичные/кооперативные markers; нет универсального пароля-детектора | Кооперативные MIME hints | Кооперативные MIME hints | Transient не сохраняется, включённое sensitive exclusion реально фильтрует; T03–06 |
| C14 тот же | Игнорировать concealed — настройка off | Нативно | Адаптация известных markers, ограничение показано | Аналогично | Аналогично | Не обещать обнаружение всех паролей; T03,09 |
| C15 тот же | Игнорировать Universal Clipboard — off; удалённые primary file URLs отклоняются, secondary file URL удаляется | Сохранить Apple-specific политику | Apple-функция неприменима; системный облачный буфер не объявлять эквивалентом | Неприменимо | Неприменимо | Mac remote file payload + text/image fixtures; T03,04 |
| C16 `ClipService.swift:227` | Исключённые приложения: foreground bundle ID или marker prefix; список пуст | NSWorkspace + marker | Foreground/clipboard owner PID → exe/AUMID, без гарантии автора | _NET_ACTIVE_WINDOW / WM_CLASS / PID, best effort | Нет общего надёжного source-app API; расширение окружения либо статус недоступности | Excluded app не пишет историю на поддерживаемом backend; неизвестный source не выдать за защищённый; T03–06 |

Сохранение нескольких нативных форматов — важнее красивого превью. Для одного локального элемента не нужно преобразовывать всё во все форматы. RTFD и Apple-маркеры не имеют универсального аналога; отдельная сборка сама по себе этого не меняет.

## 3. История, меню и вставка

| ID / источник | Возможность / настройки Clipy | macOS | Windows | Linux X11 | Linux Wayland | Проверка / задача |
|---|---|---|---|---|---|---|
| H01 `AppStorage/AppStorageValues.swift` | 30 записей, диапазон 1–1000; pruning раз в 60 с, menu limit сразу | Общее SQLite | Общее | Общее | Общее | Limit=3 после 4 копий, UI сразу 3, storage pruning ≤60 с; T02 |
| H02 `Repositories/PasteboardHistoryRepository.swift:60` | Сортировка created/last-used, default last-used | Общее | Общее | Общее | Общее | A/B/use A меняет только last-used; T02,07 |
| H03 `ClipService.swift:208` | Повторы разрешены=true, replace=true; SHA256 representation hash, либо новая UUID | Общее, исправить крайний случай UUID-дубликатов | То же | То же | То же | Все комбинации duplicate flags, переключение режимов, collision-safe сравнение; T02 |
| H04 `ClipService.swift:99` | Clear history, предупреждение on, «больше не спрашивать», item on/disabled при пустоте | NSAlert | Нативный dialog | GTK dialog | GTK dialog | Отмена сохраняет данные, подтверждение удаляет assets/preview/OCR/FTS, сниппеты остаются; T02,07 |
| H05 `ClipService.swift:148` | Clear on quit off; полная периодическая очистка off, default 1h; 5m/30m/1h/4h/8h/1d/3d/7d | Общий scheduler | Общий | Общий | Общий | Это очистка всей истории, не TTL; fake clock, sleep/resume; T02 |
| H06 `Managers/MenuManager.swift:105` | Главное/history/snippets меню, рядом с курсором, первый пункт выбран | NSMenu | TrackPopupMenu/owner draw или нативный popup | GTK popup | Popup при корректном activation; координаты курсора не всегда доступны | Открытие тремя actions, Enter/Esc/arrows, без потери target; Wayland placement адаптирован; T07 |
| H07 `MenuManager.swift:392` | Tray icon standard/reversed/hidden | NSStatusItem | Shell_NotifyIcon, восстановление после Explorer restart | StatusNotifierItem + DBusMenu, host detection | То же | Hidden icon не лишает доступа: launcher/settings shortcut/CLI; T07,09 |
| H08 `MenuManager.swift:259` | Inline 0 (0–100); группы по 10 (1–100), диапазоны | Общая menu model | То же | То же | То же | 0/1/100 inline, неполная последняя группа; T07 |
| H09 `MenuManager.swift:232` | Сниппеты сверху off; separators, clear/editor/settings/quit | Общая model + native | То же | То же | То же | Порядок и действия совпадают; T07 |
| H10 `Extensions/StringExtensions.swift:17` | Обрезка до первой строки, trim, 20 символов (1–1000), `...`; type prefixes | Общая Unicode-aware title model | То же | То же | То же | Emoji/combining sequence не повреждаются; min max-title фактически 3 для ellipsis; T07 |
| H11 `MenuManager.swift:310` | Нумерация on, старт 1; zero-start off; numeric selection off, первые 10 | Нативное menu key equivalent | Локальные акселераторы popup | GTK handlers | GTK handlers | 1…9,0 либо 0…9; групповые labels и shortcuts различены; T07 |
| H12 `MenuManager.swift:334` | Icons on; tooltip on 200 (1–10000); image previews on 100×32 (1–512); hex swatches on | ImageIO/AppKit | WIC + нативная отрисовка | GdkPixbuf + GTK | То же | Aspect fit, #RGB/#RRGGBB fixtures по SwiftHEXColors, размеры и tooltip; T07,12 |
| H13 `Services/PasteService.swift:78` | Выбор восстанавливает все форматы; auto-paste on, off = только буфер | NSPasteboard + CGEvent с Accessibility | Clipboard + SendInput, UIPI limitation | Selection ownership + XTest, учитывать target | Copy доступно при backend; auto-paste только при разрешённой desktop-интеграции | Editor A → menu → A, held modifiers release, manual fallback; T08 |
| H14 `PasteService.swift:143` | Вставка учитывает клавиатурную раскладку/shortcut | Native keymap, ⌘V/target policy | Ctrl+V default, configurable target shortcut for terminals | XKB/XTest + configurable Ctrl+Shift+V | Portal/integration если доступна | EN/RU, terminal, focus change cancellation; T08 |
| H15 `Settings/BetaSettingsView.swift` | Modifier plain-text on Command; delete off; paste-delete off; Command/Shift/Control/Option selectable | Сохранить modifiers | Ctrl/Shift/Alt/Win, явное отображение замены | Ctrl/Shift/Alt/Super | То же, если compositor не перехватил | Все три действия, конфликтующие modifiers валидируются; T08 |
| H16 `PasteService.swift:82` | Delete без вставки; paste-delete исключает немедленный recapture | Общая intent pipeline | То же | То же | То же | Удалённый элемент не воскресает; при failed auto-paste не терять единственную запись; T08 |
| H17 `Services/HotKeyService.swift` | Main ⌘⇧V/history ⌘⌃V/snippets ⌘⇧B; editor/clear unset; settings ⌃⌥⇧⌘V, нельзя удалить | RegisterEventHotKey/native recorder | RegisterHotKey/native recorder | XGrabKey/XKB, lock masks | GlobalShortcuts portal; fallback desktop binding CLI | Conflict сообщает ошибку, предыдущая рабочая привязка остаётся; settings recovery сохраняется; T09 |
| H18 тот же | Отдельный shortcut каждой папки; выключенная не открывается, удалённый очищается | Общее + native registration | То же | То же | Portal, desktop может ограничить сочетание | Disable/delete folder убирает маршрут; T09,10 |

Улучшения по сравнению с буквальным поведением Clipy, предложенные ради надёжности: не вставлять пустую строку для image-only plain-text; не терять запись при отказе auto-paste; не захватывать собственную вставку повторно; пересоздавать превью при смене размера. Это не дополнительные существующие функции Clipy.

## 4. Сниппеты, интеграция и служебные возможности

| ID / источник | Возможность Clipy | macOS | Windows | Linux X11 / Wayland | Проверка / задача |
|---|---|---|---|---|---|
| S01 `Snippets/CPYSnippetsEditorWindowController.swift:90` | Одноуровневые папки; add/rename/delete folder и snippet, multiline text, сохранение по редактированию | NSOutlineView/NSTextView | TreeView/ListView + native text control | GtkTreeView + GtkTextView | Unicode/multiline сохраняется после restart; T10 |
| S02 тот же | Enable/disable folder/snippet, подтверждение удаления | Общее + native UI | То же | То же | Disabled не в popup, в editor остаётся; T10 |
| S03 `:298` | Drag reorder папок, snippets, перенос между папками | NSTable drag/drop | OLE DnD | GTK DnD | Порядок стабилен, cancelled drag не меняет БД; T10 |
| S04 `:237` | Undo/redo text editor; сброс undo при смене элемента; folder hotkey recorder | Нативно | Нативно | Нативно | Undo одного snippet не меняет соседний; T10 |
| S05 `:158` | XML import append, сохраняет whitespace, fallback title/content | Общий XML codec + file picker | То же | То же | XML Clipy → import, пробелы/CDATA/entities; ошибка атомарна; T11 |
| S06 `:195` | XML export всех папок, UTF-8 atomic, default snippets.xml | Общий codec | То же | То же | Cliptiq → Clipy import; disabled/UUID/hotkeys XML Clipy НЕ переносит; T11 |
| S07 `AppDelegate.swift:211` | Observe screenshot files off, прямо добавляет TIFF, не screen-recording | File events/проверенные directories | ReadDirectoryChangesW выбранной папки; clipboard screenshots уже C01 | GFileMonitor/inotify выбранных папок; clipboard screenshots C01 | Сохранённый скриншот появляется один раз, incomplete write ждём; T12 |
| S08 `AppDelegate.swift:198` | Launch at login off, prompt + suppress | SMAppService | HKCU Run для обычной установки; MSIX StartupTask для package | XDG autostart desktop entry, capability/status | Вкл/выкл/restart login, удаление не оставляет лишний autostart; T13 |
| S09 `Settings/SettingsPane.swift` | 8 панелей настроек: General/Menu/Types/Excluded/Shortcuts/Updates/Beta/Donate | Нативные панели | Нативные property pages | GTK notebook/sidebar | Все сохранённые настройки применяются, недоступные функции имеют причину; T09,13 |
| S10 `Settings/UpdateSettingsView.swift` | Sparkle: auto-check, daily/weekly/monthly, last-check/check-now/version | Sparkle как измеряемый отдельный dependency, либо подписанный installer route | Native HTTPS metadata + signed installer; MSIX/package manager route | Package manager для deb/rpm; tar download/check route, без перезаписи package-managed binary | Offline, invalid signature, downgrade и отмена не повреждают app; T15 |
| S11 `Dependencies/Firebase.swift` | Analytics + Crashlytics, default on, restart setting, сетевой opt-out | Предложено исключить Firebase; локальный export диагностики вместо SDK | То же | То же | Это ЯВНОЕ изменение, не parity; strict variant отдельная задача с бюджетом SDK; T15 |
| S12 `Settings/DonationSettingsView.swift` | Donate/support links | About/support со своими проектными URL после создания | То же | То же | Не выдавать пожертвования Clipy за поддержку Cliptiq; T13 |
| S13 Resources + `.lproj` | Локализация строк и snippet editor | Перенести доступные переводы с attribution; native resources | UTF-8 source → Win32 strings | gettext/встроенные каталоги | Список исходных языков фиксируется автоматически; отсутствующий перевод EN fallback; T13 |
| S14 `Database/DatabaseMigration.swift` | Realm v7/legacy archives → SQLite; настройки old archives → Codable | Отдельный macOS importer с Realm, не в основном app | Принимает нейтральный export, не обещает чтение NSKeyedArchive напрямую | То же | Fixtures v1–v4 SQLite и Realm, source read-only, repeat import idempotent; T14 |
| S15 `Database/SQLiteDataSchema.swift` | История, assets, thumbnail, snippets на диске; restart | Общая БД, отдельный namespace Cliptiq | То же | То же | Foreign keys/transaction/recovery, источник Clipy не меняется; T02,14 |

Ограничение screenshot parity: Clipy подключает Screeen. Реализация его детектора находится во внешней зависимости, а не в этом checkout; универсальный перенос его поведения ещё требует отдельной проверки пакета. На Windows/Linux нет единой папки всех screenshot-приложений. Предложены явно выбираемые папки, а не обещание перехватывать любой скриншот системы. В отличие от прямого image-пути Clipy, новые watcher-события проходят политику privacy/limits.

## 5. Реализовано внутри, но не доступно как пользовательская функция

| ID | Что обнаружено | Решение в плане |
|---|---|---|
| B01 | `OCR/TextRecognizer.swift:27`: автоматически Vision accurate, language correction/detection, max 10 000 characters, пустая строка = обработано без результата | Сохранить асинхронный backend без объявления «кнопки OCR»; macOS native Vision; Windows packaged WinRT OCR с проверкой language packs; portable Windows/Linux отдельный OCR engine/models. T16 |
| B02 | SQLite V2/V4 FTS5 indexes/triggers для title/OCR/snippets; search query API и UI не найдены | Сохранить данные и schema adapter; готовый пользовательский поиск — отдельное расширение, не обязательная фича parity. Не включать FTS5 в минимальный runtime только ради неиспользуемого UI. T16 |
| B03 | CloudKit SyncEngine создаётся при entitlement, `startImmediately:false`; настройки sync отсутствуют | Не переносить как «существующую синхронизацию»; никаких серверов/аккаунтов в базовом плане |
| B04 | `menuIconSize=16` ключ есть, действующее использование не найдено | Сохранить только в importer при необходимости, не создавать фиктивную настройку |

Не найдены в текущем пользовательском интерфейсе: pause/resume, pin/favorites, редактирование истории, history export/import, OCR copy/search, cloud sync. Они не требуются для эквивалентности этой ревизии; не подмешивать их в MVP.

## 6. Зависимости и что реально переиспользуется

Clipy написан на Swift с AppKit/SwiftUI. Shared business code использует Sharing/Dependencies/Clocks/Collections; SQLiteData/GRDB — активное хранение, Realm — миграция. Magnet/KeyHolder — shortcuts, Sauce — keyboard layout, Screeen — screenshots, AEXML — XML, SwiftHEXColors — цвета, Sparkle — updates, Firebase — diagnostics. Package.resolved содержит транзитивные Google/Realm пакеты; запись в lock-файле не доказывает вклад каждого пакета в release size.

Перенос на Windows/Linux — новая реализация платформенного слоя с сохранением поведения и совместимости, а не компиляция нынешнего AppKit кода. Переиспользуем XML-контракт, тестовые сценарии, переводы при проверке лицензий, семантику настроек, формат данных для importer. Не обещаем автоматически переносить Swift-код в C++ без переработки.

`LICENSE` и `LICENSE_CLIPMENU` — MIT notices; при использовании существенных частей исходников сохраняем оба применимых уведомления. Иконки имеют отдельное авторство: до проверки не заимствовать как новый бренд. README просит не называть производный продукт Clipy/ClipMenu. Это описание файлов проекта, не заключение о товарных знаках.

## 7. Подтверждение и ограничения аудита

Прочитаны основной поток захвата/вставки, меню, все settings, models, repositories, schema/migrations, snippets editor, OCR, dependency wrappers, package lock и соответствующие upstream tests. Тесты Clipy и GUI на этой Windows-машине не запускались. Проверки поведения ниже — будущие критерии приёмки, не результаты выполненных тестов.

Публичные внешние контракты: [Windows clipboard](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard), [SendInput/UIPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput), [WinRT package identity](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/winrt-api-desktop-app-support), [Wayland core](https://wayland.freedesktop.org/docs/html/apa.html), [ext-data-control announcement](https://lists.freedesktop.org/archives/wayland-devel/2024-December/043920.html), [GlobalShortcuts](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html), [Clipboard portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Clipboard.html), [RemoteDesktop portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html), [X11 EWMH](https://specifications.freedesktop.org/wm/latest-single/).

# Windows x64: запуск и ручная приёмка T04–T16

Статус всех пунктов: NOT RUN / OPEN. Этот документ — инструкция, а не акт приёмки. Runtime5380462ede50273817be0dbcb4a2e832fabdf93b, версия0.1.0, native CI40105477768 success16/16 attempt2. Документация более позднего commit не меняет эти бинарники.

## 1. Что скачать

- [Base: GitHub artifact](https://github.com/Hydrock/Cliptiq/actions/runs/40105477768/artifacts/12563844036) — внутри выбрать `cliptiq-0.1.0-windows-x86_64-base.zip`,865111 B (≈845KiB).
- [OCR: GitHub artifact](https://github.com/Hydrock/Cliptiq/actions/runs/40105477768/artifacts/12564726876) — внутри выбрать `cliptiq-0.1.0-windows-x86_64-ocr.zip`,5319390 B (≈5.07MiB). Нужен для T16: Tesseract + EN/RU, работает offline.

Для скачивания artifacts может потребоваться вход в GitHub. Это внешний ZIP-контейнер CI с ZIP/MSIX и отчётами: распакуйте его и найдите **внутренний ZIP с указанным именем**. MSIX для этой проверки не устанавливать. Срок хранения текущих artifacts — до22.01.2027; локальные копии подготовлены отдельно.

SHA256 именно внутренних application ZIP:

```text
365c3ee4077bbd715031a550074d5b8b0c2994b6aa653fc02c330d1cacb8d7f2  cliptiq-0.1.0-windows-x86_64-base.zip
73effd36f3a0c7bf588d3416747ee9f5e8d874a6db78d2ce2b0cea781f061ded  cliptiq-0.1.0-windows-x86_64-ocr.zip
```

PowerShell, находясь рядом со скачанными application ZIP:

```powershell
Get-FileHash .\cliptiq-0.1.0-windows-x86_64-base.zip -Algorithm SHA256
Get-FileHash .\cliptiq-0.1.0-windows-x86_64-ocr.zip -Algorithm SHA256
```

Кандидаты unsigned, без installer/production trust. При блокировке политикой Windows запишите BLOCKED; отключать системную защиту для успешной отметки не требуется. VC++/Python/Node/runtime устанавливать для запуска приложения не нужно; Windows10/11 x64 — целевые системы ручной проверки, автоматический native прогон был на Windows2025. Отсутствие developer tools на этой машине фиксируется отдельно, а не предполагается.

## 2. Распаковка и первый запуск

1. Закройте все уже запущенные Cliptiq через Quit. Base/OCR используют общий single-instance scope: отдельная БД сама по себе не создаёт отдельный экземпляр.
2. Распакуйте **внутренний** Base ZIP в отдельную папку, например `Downloads\Cliptiq-test\Base`. Не запускайте exe непосредственно из ZIP. Сохраните `licenses` и `cliptiq-package.json` рядом с exe.
3. Откройте PowerShell в этой папке и выполните:

```powershell
$testRoot = Join-Path $env:LOCALAPPDATA 'Cliptiq-Manual-5380462'
$profile = Join-Path $testRoot 'profile'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$db = Join-Path $profile 'history.sqlite3'
Start-Process -FilePath '.\Cliptiq.exe' -ArgumentList @(
    '--database', ('"{0}"' -f $db), '--no-auto-paste', '--show-main'
)
```

4. Ожидаются native launcher/menu и значок в трее (проверьте скрытые значки Windows), без консольного окна. Это фоновый менеджер, не большое окно редактора. Обычный запуск включает capture; `--demo` и `--smoke` для ручной проверки не использовать.
5. Откройте Settings → General, снимите Auto paste и нажмите Apply: так сохранённая настройка совпадёт с безопасным первым запуском. Для первых проверок выбор записи должен лишь подготовить буфер, вставляйте вручную Ctrl+V в пустой Блокнот.
6. Если горячие клавиши не сработали, при работающем приложении выполните из той же папки:

```powershell
.\Cliptiq.exe --show-settings
```

По умолчанию Main Ctrl+Alt+V, History Ctrl+Alt+H, Snippets Ctrl+Alt+B, Settings Ctrl+Shift+Alt+V. Editor shortcut изначально не задан. Возможные конфликты настройте в Settings; launcher/CLI остаются маршрутом восстановления. Обычный двойной щелчок по exe использует другой профиль `%LOCALAPPDATA%\Cliptiq\history.sqlite3`; для этой проверки повторяйте команду с `$db`.

Сначала тестируйте на обычном рабочем столе без повышения привилегий, только с искусственными данными. Во время capture не копируйте личные пароли/данные. Этот запуск действительно слушает пользовательский буфер; это ручная приёмка, а не изолированный CI-тест.

Тестовые материалы: [EN image](../../tests/fixtures/ocr/eng.png), [RU image](../../tests/fixtures/ocr/rus.png), [synthetic XML](../../tests/fixtures/xml/clipy-layout.xml). Они также подготовлены в отдельном `cliptiq-windows-x64-checklist-and-fixtures.zip`; ожидаемые OCR substrings — `Cliptiq` / `Привет`. XML hand-authored, не экспорт живого Clipy.

## 3. Как фиксировать результат

Заполните OS/build из `winver`, архитектуру, язык/раскладку, масштаб100/150/200%, версии приложений-источников/приёмников, вариант Base/OCR, runtime commit и SHA ZIP. Для каждого ID: NOT RUN / PASS / FAIL / BLOCKED / N/A, точные шаги, ожидание/факт и обезличенный скриншот/лог. N/A на Windows не закрывает macOS/Linux gate. Нет наблюдения — нет PASS. Не публикуйте тестовую БД с реальными данными.

Рекомендуемый порядок: T05 → T07–T13 → доступная часть T14 → T15 → T16. T04/T06 выполняются на соответствующих ОС отдельно.

## 4. T04 — macOS clipboard backend

- [ ] **N/A Windows, gate OPEN.** Нужны macOS ARM и Intel: Unicode/RTF/HTML/PDF/RTFD/files/images, Universal Clipboard policy, markers/exclusions, pause/restart/sleep, native приложения. Шаги: [macOS checklist](../../tests/manual/macos.md). Windows ZIP не подтверждает ни одного macOS desktop результата.

## 5. T05 — Windows clipboard / privacy

- [ ] W05.1 В Блокноте по очереди скопируйте `Cliptiq A`, `Привет 👋`, две строки с CRLF. Откройте History, выберите каждую и вручную вставьте в пустой документ. Ожидание: порядок, Unicode/emoji и переносы сохранены, после Quit/повторного запуска с `$db` история остаётся.
- [ ] W05.2 Скопируйте пробелы/переносы без текста. Ожидание: пустая primary string не создаёт запись; изображение при этом не считается пустой строкой.
- [ ] W05.3 Браузер → Word/LibreOffice: formatted text; включите HTML в Types (по умолчанию OFF), проверьте fragment/Unicode. Повторите с HTML OFF: новая HTML-representation не захватывается; альтернативные разрешённые форматы работают. Старые записи автоматически не стираются.
- [ ] W05.4 Создайте два ненужных файла с пробелами/кириллицей. Explorer Copy → history replay → Paste в другую тестовую папку. Повторите Cut: replay должен копировать, исходники не удалять. Удалённый источник — ссылка, не резервная копия содержимого.
- [ ] W05.5 Paint/Snipping Tool/браузер: скопируйте изображение, повторно вставьте в редактор. Проверьте ориентацию и поддерживаемую прозрачность. PDF — только с реальным producer/consumer, публикующим clipboard PDF; открытие PDF в браузере само по себе этого не проверяет.
- [ ] W05.6 В Types отключайте классы по одному, Apply, делайте новую копию. Ожидание: запрещённые новые данные отсутствуют, существующая история не исчезает. Верните исходные настройки.
- [ ] W05.7 Excluded: добавьте нормализованный полный путь к exe тестового источника (например, путь процесса Блокнота в нижнем регистре), Apply и скопируйте уникальную строку. Она не должна сохраняться при доступном matching owner. Неизвестный/закрывшийся owner не объявлять точно известным: проверьте явную Unknown-source policy.
- [ ] W05.8 Concealed/transient markers требуют кооперативного producer. Обычный Блокнот их не создаёт: без такого инструмента отметить BLOCKED, не PASS. Универсальный детектор паролей не заявлен. RTFD — opaque/fallback на Windows, не macOS parity.
- [ ] W05.9 Быстрые копии, занятой/закрывшийся источник, сон/пробуждение и перезапуск: приложение остаётся доступно; Busy/Unavailable не выдаются за захват. Elevated/secure desktop проверить отдельно, без автоматического повышения Cliptiq.

## 6. T06 — Linux X11 / Wayland

- [ ] **N/A Windows, gate OPEN.** На реальных X11 и выбранных Wayland compositor/version: TARGETS/INCR/MIME/files, data-control/portal grant/deny, Unknown source, privacy, tray/launcher и sleep/session restart. [Linux checklist](../../tests/manual/linux-desktops.md). Generic Wayland support из Windows результата не следует.

## 7. T07 — меню / tray / launcher

- [ ] W07.1 Откройте Main/History/Snippets мышью и shortcuts. Стрелки, Enter, Escape, подменю и включённая numeric selection первых10: правильный item, отмена ничего не вставляет и не меняет буфер.
- [ ] W07.2 Добавьте длинные кириллические/emoji названия, изображения; проверьте tooltip, thumbnail, порядок и обновление истории при открытом меню. Устаревший/удалённый item не исполняется.
- [ ] W07.3 Масштаб100/150/200%, два монитора, светлая/тёмная/контрастная тема; скрытый tray и перезапуск Explorer. Settings/launcher остаются доступны, actionable labels не обрезаны. Generic Windows type icons/options имеют известные ограничения.
- [ ] W07.4 Narrator и только клавиатура: имена, disabled states, подменю, фокус и возврат после отмены. Сам факт видимости контрола не доказывает доступность.

## 8. T08 — вставка и modifier actions

- [ ] W08.1 Сначала Auto paste OFF: выбор → только clipboard, затем Ctrl+V вручную. Затем явно включите Auto paste в Settings и проверяйте только пустой тестовый редактор/терминал: вставка в исходную цель, без лишних символов, корректные RU/EN.
- [ ] W08.2 Original/Plain (обычно Ctrl; проверьте выбранные bindings), rich text и image-only. Plain не должен опустошать clipboard при отсутствии текстовой альтернативы.
- [ ] W08.3 Назначьте отдельные свободные modifiers для DeleteOnly/PasteDelete. DeleteOnly удаляет только запись без clipboard/input; PasteDelete не удаляет при auto OFF, отказе вставки/UIPI, закрытой/изменившейся цели, удержанной клавише. Успешная dispatch ещё не доказательство receipt — проверяйте фактический текст в цели.
- [ ] W08.4 Во время popup смените/закройте target, удержите modifier, отмените/повторно откройте меню. Не должно быть вставки в постороннее окно или отложенного неожиданного retry. Сама вставка не должна немедленно воскрешать запись в истории.

## 9. T09 — настройки / hotkeys

- [ ] W09.1 Все8 групп Settings: keyboard navigation, scroll/resize, Apply/status, EN/RU. Измените history limit=3, сортировку/duplicates; сделайте4 разных копии: menu limit сразу3, pruning по политике, restart сохраняет настройки. Проверки destructive cleanup делайте в тестовой БД.
- [ ] W09.2 Для Main/History/Snippets/Settings/Editor назначьте свободные chords и проверьте после restart. Internal duplicate, Win+V/F12, chord занятый другой программой: понятный отказ; старое рабочее сочетание сохранено. Settings chord нельзя убрать. Для swap двух chords используйте временное третье.
- [ ] W09.3 Recorder active vs Settings в фоне: глобальный shortcut в другом окне выполняет действие, не меняет поле фонового recorder. Escape/cancel сохраняет старое значение. RU/EN и Alt проверить отдельно.
- [ ] W09.4 Меняйте privacy/types/Unknown source при активном capture: новые запрещённые данные не сохраняются. Clear/clear-on-quit удаляют историю, не сниппеты и не текущий clipboard.

## 10. T10 — сниппеты / редактор

- [ ] W10.1 Откройте editor через меню, `--show-editor` и заданный shortcut. Создайте две папки, пустую папку, snippets с emoji/кириллицей/многострочным body и пробелами. После сохранения и restart значения/порядок совпадают; plaintext не превращается в rich text.
- [ ] W10.2 Быстро редактируйте body, Undo/Redo, F2, Ctrl+N/Shift+Ctrl+N, Delete/cancel. Disabled folder/snippet скрывается в меню, в editor остаётся; re-enable возвращает. Не потеряйте самый новый draft при ошибке сохранения.
- [ ] W10.3 Drag между папками, reorder, Escape/drop outside; после restart порядок совпадает. Folder shortcut открывает нужную папку; disable/delete освобождает маршрут. IME, Narrator и DPI — отдельно.
- [ ] W10.4 Read-only/full-disk/конкурентный writer — только отдельное тестовое окружение. При отказе draft сохранён в UI, ошибку видно; нельзя молча перейти и потерять редактирование. Нет нужного fault environment — BLOCKED.

## 11. T11 — XML

- [ ] W11.1 Импортируйте synthetic `clipy-layout.xml` из набора fixtures, проверьте folder/snippet order, Unicode, multiline и entities; экспортируйте в новый XML, restart и повторный импорт. Повторный импорт добавляет дубликаты намеренно.
- [ ] W11.2 Создайте disabled items перед экспортом: XML не хранит UUID/enabled/hotkeys; импорт создаёт новые enabled items, прежние данные/shortcuts остаются. Это документированный формат, не lossless migration.
- [ ] W11.3 Native Open/Save/Cancel/overwrite, Unicode path, read-only target; cancel/ошибка не меняют данные/старый файл. При transfer editor остаётся заблокирован, close/reopen не обходит pending. Known Minor: Saved может появиться до завершения worker — ждите фактического результата.
- [ ] W11.4 Повреждённый XML/DTD/external entities/превышение лимитов: явная ошибка без частичного импорта. **Живой Clipy↔Cliptiq roundtrip остаётся BLOCKED до macOS с Clipy**; synthetic fixture не заменяет его.

## 12. T12 — preview / screenshot files / privacy

- [ ] W12.1 Из Paint/Snipping Tool скопируйте PNG/DIB; preview соответствует изображению, alpha/ориентация/размеры корректны. Выключите preview, измените размеры, удалите/clear во время decode: нет stale thumbnail/воскрешённой записи. Known Minor: preview-off не гарантирует отсутствие decode/cache.
- [ ] W12.2 Создайте отдельную папку скриншотов с одним PNG **до** включения watcher. В Beta задайте папку и включите наблюдение (default OFF). Старый файл и PNG вне папки не импортируются; новый корректный PNG внутри — импортируется. Проверяйте реальные названия доступных controls и capability status.
- [ ] W12.3 Unicode filename, temporary→final rename, частичная запись/повторное уведомление: без частичного decode/дубликата одного события. Clear/delete pending → нет повторного появления. Clipboard+file одного screenshot в обоих порядках — без двойной записи при соответствующей dedup policy.
- [ ] W12.4 Image OFF / Unknown Reject / Unknown-with-exclusions: новый screenshot не принимается. Источник файла не угадывается по активному окну/имени. AllowUnknown включайте лишь явно для следующего безопасного теста.
- [ ] W12.5 Удалённая/недоступная папка и реальный ACL deny: ошибка остаётся видимой, наблюдение не выглядит успешным; восстановите доступ/перезапустите watcher. Junction/replacement race — advanced fixture, без воспроизведения BLOCKED.

## 13. T13 — автозапуск / локализация / accessibility

- [ ] W13.1 Сначала завершите обычные тесты. Autostart регистрирует настоящий exe для текущего пользователя и может запускать default profile, **не custom `$db`**. Проверяйте только в отдельной тестовой Windows учётной записи; иначе отметить BLOCKED. Enable/disable, logout/login, внешний Windows disable, перемещение exe, отсутствие duplicate process; чужие startup entries не меняются.
- [ ] W13.2 При работающем экземпляре выполняйте `--show-main`, `--show-history`, `--show-snippets`, `--show-settings`, `--show-editor` из второго процесса. Один capture/store owner, правильный focus, нет неожиданной вставки. Shared scope Base/OCR означает сначала Quit перед сменой варианта.
- [ ] W13.3 EN/RU, locale restart/fallback, long labels, Narrator, tab order, high contrast/DPI и offline launch. Содержимое history/snippets не переводится. Остальные локали/RTL требуют отдельной лингвистической приёмки. `--background` пока показывает launcher — известный Minor.

## 14. T14 — миграция Clipy

- [ ] W14.1 XML-only snippets fallback уже проверяется T11; это не полная миграция history/settings.
- [ ] W14.2 **Prerequisites/BLOCKED в application ZIP:** отдельный `cliptiq-import.exe` и нейтральный export с macOS. Они не встроены в Base/OCR ZIP. [Утилита и команды](../../tools/import-clipy/README.md), [полная матрица](../../tests/manual/migration.md). Не переименовывайте обычную историю Cliptiq в neutral export.
- [ ] W14.3 Когда prerequisites готовы: Quit Cliptiq/Clipy, независимый backup; создайте новую destination-папку; SHA256 original neutral до/после; `cliptiq-import import INPUT.neutral NEW-DIRECTORY\history.sqlite3`, затем запустить Cliptiq с этой БД. Сравнить counts/order/IDs/times/Unicode/native bytes, settings по умолчанию не активировать. Повтор идентичного импорта — no-op; конфликт/ошибка — без частичного append. `--apply-settings` только осознанно, после сверки privacy/exclusions между ОС.
- [ ] W14.4 Imported history replay через реальные приложения, delete/clear/pruning удаляют связанную live provenance без resurrection; source hashes не меняются. Исторические Realm/NSKeyedArchive и исходный Clipy SQLite/preferences проверяются на macOS и остаются OPEN.

## 15. T15 — updates / local diagnostics

- [ ] W15.1 Automatic checks default OFF, нет diagnostics upload. Export Local Diagnostics: cancel не создаёт файл; save/confirmed overwrite работает. Искусственный secret в history/snippet/path не должен оказаться в whitelist report; export сам ничего не отправляет.
- [ ] W15.2 Check updates вручную online/offline: понятный status/ошибка, UI responsive. GitHub version hint не является подписью пакета; portable приложение не самообновляется и не скачивает скрытый helper. Opt-in schedule сохраняется после restart; верните OFF после теста.
- [ ] W15.3 **BLOCKED:** positive signed Cliptiq installer/MSIX update, publisher verification, rollback/cancel/relaunch/data continuity, macOS notarization/Sparkle и Linux signed repository. Нет production identity/feed/installer; unsigned ZIP не заменяет эти проверки. Не принимать fake bool/metadata за proof of trust.

## 16. T16 — OCR / FTS / privacy

1. Quit Base, распакуйте OCR ZIP в соседнюю отдельную папку `OCR`. Не смешивайте файлы двух вариантов. Сохраните `tessdata\eng.traineddata` и `rus.traineddata` рядом с exe; не задавайте `CLIPTIQ_OCR_MODELS`, если не тестируете override.
2. Запустите OCR exe той же командой с `$db`, `--no-auto-paste --show-main`. Не запускайте Base и OCR одновременно. Модели скачивать при запуске не требуется.
3. Для внутреннего результата нужен SQLite viewer/CLI с read-only режимом и поддержкой FTS5 (вспомогательный инструмент проверки, **не runtime приложения**). Без него internal OCR/FTS отметить BLOCKED: меню/картинка не доказывают распознанный текст. Пользовательского search/OCR-copy UI в T16 нет.

- [ ] W16.1 Offline: откройте fixtures `eng.png`/`rus.png` в Paint, Select All/Copy как изображение (не файл из Explorer). После обработки в history ожидаются OCR Done и строки, содержащие `Cliptiq` / `Привет`. Дайте worker завершиться, затем Quit. Read-only SQL к `history.sqlite3`:

```sql
SELECT id, ocr_state, ocr_text FROM history ORDER BY created_ms DESC LIMIT 10;
```

States:0 Pending,1 Done,2 Unavailable,3 Failed. Done с пустым текстом допустим для изображения без текста; `ocr_state IS NULL` у текстовой записи сам по себе не ошибка.

- [ ] W16.2 При работающем OCR примените Image OFF, exclusions/Unknown Reject; скопируйте новый уникальный image и создайте screenshot event. Запрещённый контент/OCR не появляется в БД. Policy change/clear/delete пока job работает — без позднего результата и resurrection; краткий Busy — явный retry пользователем.
- [ ] W16.3 Сначала штатно Quit; временно переименуйте **только папку tessdata в тестовой OCR копии**, запустите и скопируйте новый image. Ожидание: Unavailable, не ложный Done. Quit, верните tessdata, запустите: Unavailable retry после restart/60s; работу реально проверить по БД. Known Minor: capability label может заранее показывать Available — это не proof engine availability.
- [ ] W16.4 После успешной индексации Quit и откройте `profile\index\search.db` read-only. Пример проверки FTS5:

```sql
SELECT id, snippet, title, body FROM search WHERE search MATCH 'Cliptiq';
```

Проверьте также уникальный token созданного snippet. Не изменяйте SQL-схему/данные viewer'ом. Не открывайте main БД индексным engine как способ «включить FTS».

- [ ] W16.5 Quit OCR → Base с той же `$db` → Quit → OCR: main history/snippets/OCR-текст сохраняются; Base инвалидирует owned companion и не работает с FTS. OCR перестраивает companion. Для recovery сначала Quit и backup **всего тестового profile**; переименуйте только owned `index\search.db`, запустите OCR, проверьте восстановление. Не удаляйте чужие/неизвестные файлы.
- [ ] W16.6 Native WinRT OCR positive требует установленного подписанного MSIX и language packs — **BLOCKED для этих ZIP**. OCR ZIP использует Tesseract, не проверяет WinRT. macOS Vision и Linux OCR/Wayland — на своих ОС.

## 17. Завершение / что остаётся OPEN

1. Quit через меню. Если включали autostart в тестовой учётной записи, выключите в Settings или выполните там `Cliptiq.exe --remove-autostart` **до удаления exe**; проверьте своё startup состояние. Не изменяйте чужие entries.
2. Сохраните обезличенный results report. Удаление тестовой папки/профиля выполняйте только после явного выбора сохранить/удалить результаты; удалять общий `%LOCALAPPDATA%\Cliptiq` для очистки этой проверки не нужно.
3. T04/T06, live Clipy roundtrip/legacy migration, positive signed updates/WinRT, clean-system install/uninstall и непроверенные fault/race/desktop cases остаются OPEN. Даже полный Windows PASS не закрывает T04–T16 глобально. Отметки требуют наблюдений пользователя и последующего review результатов.

Подробные существующие матрицы: [Windows](../../tests/manual/windows.md), [menu](../../tests/manual/menu.md), [paste](../../tests/manual/paste.md), [settings](../../tests/manual/settings.md), [snippets](../../tests/manual/snippets.md), [XML](../../tests/manual/xml.md), [media](../../tests/manual/media.md), [lifecycle](../../tests/manual/lifecycle.md), [migration](../../tests/manual/migration.md), [updates](../../tests/manual/updates.md), [OCR/FTS](../../tests/manual/t16-ocr-fts.md).

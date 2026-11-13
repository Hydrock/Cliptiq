# Передача разработки Cliptiq на macOS

Дата: 13 ноября 2026 года. Ветка: `feature/windows-workspace`.
Предыдущий коммит: `d87b643` (иконка трея и удаление выбора её оформления).
Этот документ входит в следующий коммит с переносом интерфейса на macOS.

## Задача нового чата

Собрать текущую ветку на настоящем Mac, исправить ошибки компиляции и AppKit,
проверить интерфейс и поведение, затем измерить расход ресурсов. На Windows
перенос реализован и проверен по исходникам, но новая Mac-версия ещё ни разу
не собиралась и не запускалась. Не считать её готовым релизом.

Пользователь просит одинаковый функционал и максимально похожий, понятный
интерфейс Windows/macOS при низком потреблении ресурсов. Используем системные
Win32/AppKit и общий C++17, без Raven, WebView и нового GUI-фреймворка.
Не возвращать совместимость с Clipy: она пользователю не нужна.

## Согласованное поведение

### Главное окно

- Заголовок Cliptiq, подзаголовок, боковые разделы «История» и «Сниппеты».
- Фильтр по названию, доступному тексту списка и папке; счётчик записей.
- Таблица содержимого и типа/папки, сохранение выделения по стабильному ID.
- Справа текстовый или масштабируемый предпросмотр изображения.
- Текст только для чтения, без мигающего курсора; выделение и копирование разрешены.
- Копирование выбранного содержимого, удаление записи истории, очистка истории.
- Настройки, редактор сниппетов и выход находятся в самом окне приложения.
- Закрытие главного окна скрывает его; фоновые захват/горячие клавиши продолжают работать.
- Тяжёлые представления главного окна создаются по необходимости и освобождаются при скрытии.

### Значок и быстрое меню

- Значок подходит для светлой и тёмной темы; выбора «обычный/обратный/скрыть» в настройках нет.
- Левый клик по значку открывает главное окно.
- Правый клик открывает только служебное меню: приложение, настройки, выход.
- Горячая клавиша открывает компактное меню из двух разделов: история и сниппеты.
- Версия, диагностика, обновления и остальные служебные команды в компактное меню не добавляются.
- Положение определяется указателем мыши: меню раскрывается от него вправо/вниз,
  с системной корректировкой у края экрана. Это не привязка к текстовой каретке.
- Открытие быстрого меню не поднимает большое окно и не теряет исходное активное поле.
- Выбор отправляет содержимое в ранее активное приложение при разрешённой автоматической вставке.
  Права Accessibility и ограничения ОС не обходить.
- На macOS добавлено повторное открытие главного окна через Dock.
- Нативные элементы и миниатюры компактного меню удерживаются только во время открытия.

### Редактор сниппетов

- Отдельного поля названия справа больше нет.
- Папки и сниппеты переименовываются прямо в списке: двойной щелчок или F2.
- Enter принимает название; Esc отменяет; пустое название не заменяет существующее.
- После создания записи сразу начинается ввод названия.
- На Mac есть также кнопка переименования.
- Текст сниппета редактируется справа; сохранение локальное через существующую очередь.
- Сохраняются создание/удаление, папки, показ в меню, папочная горячая клавиша,
  undo/redo и drag-and-drop. При смене записи история отмены сбрасывается.
- Кнопки подстраиваются под перевод. Windows переносит их на следующую строку;
  Mac использует панель с горизонтальной прокруткой и собственными размерами кнопок.
- На Mac нажатие структурной команды во время переименования сначала завершает
  редактирование и ждёт сохранения названия. Ошибка сохранения отменяет отложенную
  команду. Перестроение дерева очищает ссылки на старый редактор и узел.

### Настройки и звуки

- У каждого поля есть локализованное человеческое описание: мелкий приглушённый текст,
  перенос строк, отдельное объяснение назначения горячей клавиши.
- Общая модель `SettingField.description`; русские и английские тексты в каталогах.
- Mac получил списки значений для сортировки, неизвестного источника, модификаторов,
  интервалов очистки/обновления и языка. В списках сохраняются фактические значения.
- Звуки вынесены на отдельную страницу: копирование, вставка, громкость и проверка.
- По умолчанию оба звука выключены, громкость 25%. Явные старые предпочтения сохраняются.
- Копирование сигнализирует только о сохранённой копии или операции через Cliptiq.
  Звук вставки означает отправленную Cliptiq команду, а не подтверждение приёма другим приложением.
  Обычные Ctrl/Cmd+V в других программах не отслеживаются.
- Первый захват при старте не озвучивается. Нет специального фонового потока/таймера звука.
- Windows лениво загружает winmm, Mac лениво создаёт NSSound из общего короткого PCM/WAV.
- Удалено второе дополнительное сочетание быстрого меню (`snippets` в старых настройках).
  Если основное сочетание пусто, старое переносится в него. Первое дополнительное остаётся.
- Не навязывать пользовательские Ctrl+B/Ctrl+M всем новым профилям: это были локальные назначения.

### Обновления

В Windows сейчас есть проверка GitHub Releases по расписанию и открытие страницы загрузки,
но нет автоматической установки portable EXE. В проекте есть отдельный необязательный путь
Sparkle для Mac; production-лента, ключи и подписанный выпуск не настроены в этой работе.
Не обещать рабочее автоматическое обновление без дополнительной настройки и проверки.
Подробнее: `packaging/updates/README.md`.

## Карта изменений

- `platform/MenuHost.cpp`: общие маршруты workspace включены для Apple, подключён Mac-звук;
  autoreleasepool на каждой итерации host-цикла.
- `core/include/cliptiq/native_menu.hpp`: методы workspace доступны Apple и Windows.
- `platform/macos/Menu.mm`: интеграция окна/статусного значка/Dock, транзитное быстрое меню,
  слабые ссылки в callbacks, отложенное уничтожение workspace после закрытия.
- `platform/macos/Workspace.hpp`, `Workspace.mm`: новый AppKit-контроллер главного окна.
- `platform/macos/Settings.mm`: пояснения, значения списков, звуковая проверка, адаптивная раскладка.
- `platform/macos/SnippetEditor.mm`: inline rename, защита ID/отложенных действий, раскладка и переводы.
- `platform/macos/Sound.hpp`, `Sound.mm`: необязательный ленивый NSSound; генератор WAV пока
  переиспользуется из `platform/windows/Sound.cpp`, платформенные WinAPI-вызовы там ограждены.
- `platform/windows/SnippetEditor.cpp`: нативный TVS_EDITLABELS; Enter/Esc проходят до редактора
  до IsDialogMessage. Без этого Enter перехватывался окном.
- `platform/windows/Settings.cpp`: размеры строк по реальному тексту, небольшой шрифт пояснений.
- `platform/windows/Menu.cpp`: отдельное меню трея и независимый скрытый владелец popup.
- `platform/windows/ApplicationIcon.hpp`, `resources/windows/cliptiq.ico`, `cliptiq.rc`:
  иконка EXE, заголовков, панели задач и Alt+Tab.
- `core/src/settings.cpp`: пояснения, миграция сочетания, звуки выключены по умолчанию.
- `resources/locales/{en,ru}.json`: источники переводов;
  `core/src/locale_catalogue.cpp` генерируется `python3 tools/generate-locales.py`.
- `platform/linux/Settings.cpp`: показ общего описания уменьшенным приглушённым шрифтом.
- `CMakeLists.txt`: новые платформенные источники и тесты.

При исходной передаче подписи Mac workspace были жёстко заданы на русском.
В продолжении на Mac они подключены к общему каталогу en/ru: главное окно и редактор
используют выбранный язык приложения. Пользовательские названия и содержимое не
переводятся. После изменения языка нужен перезапуск. Не заявлять полную визуальную
идентичность без реального просмотра Mac.

## Получение на Mac

Коммит создаётся локально на Windows. Если ветка ещё не отправлена на GitHub, сначала
нужно отправить её с Windows или перенести проект. Один `git pull` не переносит локальный коммит.
После появления ветки на origin в существующем checkout:

```bash
git fetch origin
git switch feature/windows-workspace
git pull --ff-only origin feature/windows-workspace
git status --short
git log -1 --oneline
```

Не использовать reset --hard и не затирать местные изменения. Не переносить Windows-папку
`out`, приватную историю и настройки пользователя. Папка `.toolchains` восстанавливается скриптом.

## Сборка и тесты на Mac

Целевой минимум CMake — macOS 13.0. Существующая матрица CI проверяет macOS 15
на Apple Silicon и Intel; новые изменения этой матрицей пока не запускались.
Понадобятся Command Line Tools, CMake, Ninja и Python 3.

```bash
xcode-select --install
# Если установлен Homebrew:
brew install cmake ninja python
```

Из корня проекта, останавливаясь при любой ошибке:

```bash
python3 tools/fetch-footprint-deps.py
cmake --preset test
cmake --build --preset test --parallel
ctest --preset test --output-on-failure

cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure
open out/core-Darwin-release/Cliptiq.app
```

Первая ошибка сборки важнее последующих: исправлять первопричину и пересобирать.
Полный Xcode для этого пути не предполагается обязательным. Существующий CI находится
в `.github/workflows/core.yml`; удалённый запуск из Windows в этой работе не выполнялся.

Новые Apple-тесты: `workspace_native` и `editor_inline_rename`.
Общий `sound_wave` теперь также подключён на Apple. Существующие проверки захвата,
вставки, настроек и редактора не удалены. Не отключать тесты ради зелёной сборки.

Стандартный профиль Mac: `~/Library/Application Support/Cliptiq/history.sqlite3`.
Для разработки лучше отдельный профиль, например:

```bash
mkdir -p "$HOME/Library/Application Support/Cliptiq-dev"
./out/core-Darwin-release/Cliptiq.app/Contents/MacOS/Cliptiq \
  --database "$HOME/Library/Application Support/Cliptiq-dev/history.sqlite3" --locale ru
```

Не запускать второй экземпляр с тем же профилем вместо остановки первого. При проверке
разрешений учитывать путь и подпись реально запущенного приложения.

## Что проверить вручную

1. Первый запуск, значок в светлой/тёмной теме, служебное меню правого клика, Dock reopen.
2. Копирование текста, URL и изображения из других приложений попадает в историю.
3. Главная история и сниппеты, фильтр, пустые состояния, сохранение выбранного ID.
4. Большой предпросмотр с пропорциями; длинный текст выделяется/копируется и не выглядит редактируемым.
5. Горячая клавиша в стороннем поле показывает только компактное меню у мыши.
   Выбор вставляет в исходное поле при выданном Accessibility; большое окно не всплывает.
6. Край экрана, несколько мониторов/Retina, изменение размеров и длинные русские подписи.
7. Inline rename папки и сниппета: двойной клик/F2/кнопка; Enter, Esc, пустое имя,
   смена выбора и нажатие Add/Reload прямо во время ввода. Перезапуск сохраняет результат.
8. Создание/удаление, undo/redo, переносы и включение/выключение показа сниппетов.
9. Все страницы настроек: пояснения читаемы, прокрутка работает, списки сохраняют значения,
   конфликт горячей клавиши не теряет прежнее рабочее назначение.
10. Звуки выключены на новом профиле; тест звучит явно по кнопке; включённые события не
    пропускаются из-за коротких интервалов; запуск приложения не издаёт звук.
11. Закрытие главного окна сохраняет работу в фоне; открытие/закрытие не наращивает память
    от неосвобождённых NSMenu, NSImage, field editors или autoreleased objects.

## Проверено на Windows и границы доказательств

- Полная MinSizeRel-сборка Windows в `out/windows-audit` завершена.
- 41 доступный CTest прошёл в чистой временной папке. 12 окруженческих проверок исключены:
  причины и имена перечислены в `docs/macos-native-parity-verification.md`.
- После окончательной правки host-цикла отдельно пересобраны приложение, настройки и
  inline rename в `out/windows-preview`; три целевых проверки прошли.
- Новая Windows-сборка запущена с прежним профилем; отдельного поля названия в редакторе нет.
- Тесты переименования работают на своей временной базе; имена реальных записей пользователя
  для тестирования не менялись. Тестовая папка «Работа» ранее удалена по просьбе пользователя.
- Mac SDK отсутствует в текущем Windows-окружении. Компиляции, исполнения и измерений новой
  AppKit-версии не было. Source review не заменяет нативный тест.

После исправления Mac-сборки измерить на release без открытых окон RSS/private footprint,
CPU за достаточно длинный интервал и размер .app. Сравнить фон до/после открытия окна и
меню, затем повторить несколько циклов. Не переносить Windows-числа на Mac и не обещать
заранее целевой объём памяти. Сохранять методику, версию ОС, архитектуру и фактические результаты.

## Короткий запрос для нового чата

«Продолжаем Cliptiq на macOS, ветка feature/windows-workspace. Прочитай
`docs/MACOS_HANDOFF.md` и `docs/macos-native-parity-verification.md`. Собери test/release,
исправь ошибки, проверь новые AppKit workspace и inline rename, затем визуально сравни
с описанным Windows-поведением и измерь фоновые ресурсы. Береги существующую историю,
не добавляй Raven/WebView и не считай исходники доказательством работающей Mac-сборки».


## LAN v1 handoff: feature/windows-workspace

See [LAN_CLIPBOARD.md](LAN_CLIPBOARD.md) for configuration and limitations.
Run `python3 tools/fetch-lan-deps.py` before CMake. Static Mbed TLS 3.6.7 is pinned;
AppKit/ImageIO/Keychain and dns_sd remain system dependencies. Build with
`CLIPTIQ_LAN_SYNC=OFF` for a control without networking.

Windows native build and loopback tests passed; macOS/Linux native builds are
not verified in this environment. On Mac, compile all Objective-C++ first, then:

- Check More → LAN draft/cancel, key masking, scrolling, save, source labels.
- Check Keychain CRUD and locked-keychain errors without background prompts.
- Check DNSServiceGetAddrInfo IPv4/IPv6, MagicDNS, cancellation on LAN shutdown.
- Run `ctest -R lan`, then Mac↔Windows text/URL/PNG, history-only, local-copy race,
  disabling the last peer, pause, restart and no echo.
- NSPasteboard changeCount is checked immediately before clear; no public atomic
  compare-and-set exists. Test races with simultaneous local copies.
- Measure memory/CPU with no peers and while connected. Raven is not used.

On Linux, validate GTK/Secret Service/GResolver. DNS cancellation drains a private
GMainContext callback after GCancellable; validate shutdown latency against a
stalled system resolver on supported distributions before release.

History schema is now 3. Back up before migration: the old executable rejects
schema 3, so rollback requires the pre-migration database.


### Inline Network settings (Windows follow-up)

Network fields now mount directly in the Network tab through the native parent
view callback. No launcher button or additional top-level window is used by the
application. Embedded Save retains the panel and refreshes the config revision;
Cancel restores the last saved draft. The Windows native test covers repeated
Save, Cancel, child-window ownership and destruction with its parent. Rebuild and
check the changed AppKit embedding on Mac; the earlier Mac acceptance predates
this UI change. Linux embedding also needs a native build/runtime check.


### Network form explanations and grouping

All network fields now use shared human-readable explanations from
LanSettingsDraft::explanation. Native renderers place them below each field in
smaller secondary text. Sections separate this computer, sharing behavior,
formats and each numbered peer. AppKit uses native separators, bold headings and
secondaryLabelColor; GTK uses themed dim-label styling. Windows wrapping and
scrolling plus existing native settings tests were verified. Recheck the updated
AppKit layout on Mac, including long Russian explanations and multiple peers.


### macOS startup permission gate (native continuation)

Normal startup now requires clipboard and automatic-paste access before capture,
hotkeys, LAN service, updates and main UI initialize. The foreground setup panel
shows independent request buttons and disables Continue until both checks pass.
On macOS 15.4+ use Always Allow for clipboard; paste requires AX/PostEvent access.
LAN is not an unconditional startup permission. Closing setup exits. Smoke/demo
fixtures bypass this interactive gate. The user confirmed foreground visibility;
Debug/release full suites passed 61/61 on macOS 27.2 arm64.

The same continuation corrected outstanding TLS-frame retention during connection
replacement; see LAN_VALIDATION.md for replay/ACK/expiry boundaries and tests.
Uncommitted permission/icon work was preserved while syncing to 7527731.

### Установка после удаления каталога Cliptiq.app (13 ноября 2026)

После удаления пакета из `out` обязательно выполнить `cmake --preset release`
перед `cmake --build --preset release`: Info.plist создаётся при конфигурации,
и один инкрементальный build может восстановить только executable и ресурсы.
Без Info.plist TCC идентифицирует процесс по пути к executable и создаёт
отдельную запись разрешений вместо bundle identity. Такой пакет не устанавливать.
Перед установкой запускать `ctest --test-dir out/core-Darwin-release -R lifecycle_bundle --output-on-failure`,
проверять CFBundleIdentifier и после подписи `codesign --verify --strict`.
Установленный пакет должен показывать `Info.plist entries`, а не `Info.plist=not bound`.
Подписывать полный пакет до запуска; не менять подпись работающего приложения.
Сейчас /Applications/Cliptiq.app восстановлен, bundle identity в журнале TCC
подтверждена как io.github.hydrock.Cliptiq. Старые записи доступа требуют
ручного удаления пользователем и выдачи доступа новому полному пакету.

Дополнительная подтверждённая причина TCC: Launch Services сохранял регистрации
Debug/Release и копий в Корзине с тем же bundle ID. При выдаче прав tccd пытался
получить staticCode из удалённого out/core-Darwin-test/Cliptiq.app, выдавая
_CFBundleCreateUnique failed. Удалены только эти регистрации через lsregister -u,
для /Applications/Cliptiq.app выполнен lsregister -f и перезапуск без смены подписи.
Проверка 13 ноября: в Launch Services осталась одна запись /Applications/Cliptiq.app;
новый процесс получил authValue=2/authReason=4 для kTCCServiceAccessibility
и kTCCServicePostEvent. Стек процесса подтверждает NativeMenu::process_events,
а не StartupPermissions::runModal. Повторная пересборка для этого не потребовалась.
При установке локальных сборок не оставлять зарегистрированные дубликаты
с production bundle ID; перед диагностикой TCC проверять именно attribution path.

### Apply во вкладке Сеть (13 ноября 2026)

Исправлена несвязанная общая кнопка Apply: раньше она сохраняла обычные
настройки и сообщала успех, оставляя сетевой черновик несохранённым. На macOS
на активной вкладке Сеть Apply теперь вызывает тот же collect/save, что
внутренняя Save. Ошибка показывается в постоянной строке окна, а поля и ключи
сохраняются для повторной попытки. После успеха обновляются revision и baseline.
Нативный EN/RU-тест воспроизводит исходный сбой общей кнопкой Apply; проверяет
невалидный порт, отказ записи, повторную попытку с сохранённым ключом и reopen.

Поиск Spotlight повторно регистрирует существующие тестовые .app, даже после
lsregister -u. После установки дубликаты пакетов (включая старые копии из Корзины)
перемещены в out/archived-apps.noindex и сняты с регистрации. На 13 ноября
в Launch Services и Spotlight оставлена установленная /Applications/Cliptiq.app.
При следующей сборке удалённых staging-пакетов сначала повторить cmake configure,
а после установки убрать staging .app обратно в .noindex; не менять установленную
подпись ради очистки поиска и не сбрасывать для этого TCC.

### Отдельная колонка источника LAN-записей (13 ноября 2026)

Таблица истории и сниппетов macOS/Windows: Содержимое, Тип / папка, Источник.
Имя source_device больше не добавляется в квадратных скобках перед item.label
в таблице; выводится в отдельной третьей колонке. Для пустого source_device
показывается «Это устройство» (This device); поиск учитывает имя устройства.
Модель записи, preview и команды копирования используют прежние payload/ID.
Нативные EN/RU-тесты macOS проверяют три колонки, исходный текст, источник PC,
локальный источник и прежнюю copy-only команду. Win32-изменение требует
проверки на Windows. Установленная /Applications/Cliptiq.app этим изменением
пока не обновлялась: сохраняем текущую подпись и выданные пользователем права.

### Правило пользователя для переустановки

Перед каждой установкой новой Mac-сборки сначала остановить и удалить предыдущий
пакет из /Applications (переместить в Корзину внутри .noindex), снять его регистрацию.
Историю, сниппеты, настройки и LAN-ключи сохранять. Проверить полный новый пакет
и подпись до установки; затем установить только одну копию и запустить её.
При изменении ad-hoc подписи сбросить Accessibility только для io.github.hydrock.Cliptiq
до запуска нового пакета, чтобы не оставалось требования старого cdhash.
Убрать staging-пакеты в .noindex и оставить единственную регистрацию /Applications/Cliptiq.app.
Сборка с третьей колонкой Источник переустановлена этим способом 13 ноября 2026;
4 Release-проверки (workspace EN/RU, workspace core, lifecycle_bundle) прошли.
Выдача системного доступа остаётся явным действием пользователя.

### Открытые вопросы перед push

Повторный вызов быстрого меню в чужом полноэкранном Space по пользовательской
проверке остаётся ненадёжным. Добавлены безопасные os_log-события hotkey received,
popup begin/owner/end без текста буфера и названий окон. Тесты повторного
AppKit tracking и owner flags проходят, но это не подтверждает устранение
пользовательского fullscreen-сбоя. Не заявлять этот сценарий исправленным.
Ad-hoc подпись меняется при обновлениях: Accessibility требует сброса старой
записи и повторного явного разрешения пользователя; доступ к существующим
LAN-ключам в login Keychain также может запрашиваться повторно. Постоянный
сертификат подписи пока не настроен. Сетевой обмен пользователь подтвердил.

### Проверка fullscreen: незавершённый запуск AppKit

В установленной сборке hotkey received и popup begin/owner/end подтверждают
доставку горячей клавиши и owner.onActiveSpace=1 даже при жалобе на невидимость
меню. Это не доказывает видимость самого NSPopupMenuWindow. Отдельный probe
с подтверждённым NSWindowDidEnterFullScreenNotification у другого тестового
процесса показал видимые NSMenu-поверхности, но не воспроизвёл пользовательский
сбой над реальными приложениями. Временные probes создавались только в /private/tmp.

Подтверждён отдельный дефект: ручной цикл AppKit не вызывал finishLaunching.
По документации Apple этот шаг выполняет NSApplication.run перед циклом:
https://developer.apple.com/documentation/appkit/nsapplication/finishlaunching()
Добавлен однократный finishLaunching после создания native menu/tray.
Регрессионный menu_native наблюдает NSApplicationWillFinishLaunchingNotification:
без исправления падает, с исправлением проходит. DidFinish зависит от доставки
системного launch-события и не подходит для синхронного assertion в CLI test.

Добавлен одноразовый tracking-mode probe уровня >= NSPopUpMenuWindowLevel:
пишет только количество visible/onActiveSpace поверхностей меню, без имён окон,
содержимого и идентификаторов целей. Таймер инвалидируется при возврате popup.
Связь исправления launch handshake с пользовательским fullscreen-сбоем требует
проверки в переустановленной сборке; не считать fullscreen исправленным заранее.

Проверочная сборка launch-handshake установлена после удаления предыдущей.
Release: 61/61, 68.43 s (/private/tmp/cliptiq-appkit-launch-release-tests.log).
Запрошена повторная проверка двух вызовов в реальном полноэкранном приложении.
При установке сброшена только старая запись Accessibility для Cliptiq; подпись
нового полного пакета проверена до запуска. Staging-пакеты убраны в .noindex.

После установки launch-handshake candidate повторная выдача доступа потребовала
различить две TCC-записи: вручную добавленная по пути /Applications/Cliptiq.app
и автоматически созданная по Bundle ID io.github.hydrock.Cliptiq. В System Settings
они отображались как Cliptiq (on) и Cliptiq.app (off). После явного подтверждения
пользователя через CUA включена именно вторая запись. tccd подтвердил
authValue=2/authReason=4 для Accessibility и PostEvent в текущем процессе.
Стартовое окно осталось в runModal, ожидая Continue (автопереход не реализован).
Повторная проверка fullscreen после Continue остаётся открытой. Не пересобирать
или повторно сбрасывать доступ до получения этой проверки.

### Сравнение fullscreen с Clipy (13 ноября 2026)

Пользователь подтвердил: один finishLaunching не устранил fullscreen-сбой.
Свежие логи показывают hotkey received, menu surfaces visible=2/activeSpace=2,
но визуальная доступность в реальных полноэкранных приложениях не подтверждена.

Изучены текущие первичные исходники Clipy develop:
- Clipy/Sources/Services/HotKeyService.swift: callback вызывает MenuManager.popUpMenu.
- Clipy/Sources/Managers/MenuManager.swift: popUp(positioning:nil, at:NSEvent.mouseLocation, in:nil).
- Clipy/Supporting Files/Info.plist: LSUIElement=true; переход в regular отсутствует.
Источник: https://github.com/Clipy/Clipy/blob/develop/Clipy/Sources/Managers/MenuManager.swift

Перенесён механизм: NSApplicationActivationPolicyAccessory в NativeMenu и startup
permissions, popup в экранных координатах с nil positioning item и nil view.
Удалён прозрачный NSPanel owner и его lifecycle. Захваченный InvocationContext,
одноразовый model generation, keyboard modifiers и paste target сохраняются.
Тест menu_fullscreen_owner теперь проверяет contract accessory/screen popup,
три повторных cancel/select, координаты, очистку и неизменность foreground/target.
RED до изменения: !popup_has_view && !popup_owner. GREEN после: 8/8 native
Debug checks. Полноэкранная пользовательская проверка новой схемы ещё требуется.

### Fullscreen подтверждён пользователем

13 ноября 2026 пользователь проверил установленную сборку со схемой Clipy:
«теперь это работает везде», повторный вызов — «Появляется оба раза».
Рабочее решение: сохранять activationPolicy accessory (LSUIElement), показывать
NSMenu в экранных координатах с positioningItem=nil и inView=nil, без прозрачного
окна-владельца. Предыдущий finishLaunching сам по себе сбой не исправил.
Release-проверка новой схемы: 61/61, 58.83 s
(/private/tmp/cliptiq-clipy-release-tests.log); 8/8 целевых Debug проверок.
Установлена одна /Applications/Cliptiq.app после удаления предыдущей версии;
оба разрешения подтверждены tccd после перезапуска того же бинарника.
Fullscreen-сбой закрыт пользовательской проверкой, включая повторное открытие.

## Поиск LAN и код сопряжения (13 ноября, новая реализация)

Реализованы opt-in обнаружение через штатный Bonjour/macOS и DNS-SD/Windows,
отдельный управляющий слушатель и EC J-PAKE сопряжение шестизначным кодом.
Главный сценарий в «Сеть»: доступность, выбор сети при необходимости, поиск,
список устройств, подтверждение на втором компьютере и код. Ручные настройки
свёрнуты; открытие окна не включает сеть. Разрешение локальной сети не добавлено
в обязательный стартовый gate буфера/вставки.

Модель/формат: docs/LAN_PAIRING_PROTOCOL.md. План и спецификация:
docs/superpowers/{plans,specs}/2026-11-13-lan-discovery-pairing*.md.
Работа ведётся в существующей feature/windows-workspace.

Проверено: локальный Bonjour без реального LAN, контроллер с поддельным системным
хранилищем, настоящий loopback PAKE→сохранение→TLS→текст/PNG→перезапуск сервиса,
EN/RU AppKit, регрессии владельца полноэкранного меню и границ вставки.
Нужна реальная приёмка Mac↔Windows; Windows SDK/рантайма и gh на этом Mac нет.
Если адрес уже сохранённого устройства изменился, его пока нужно обновить вручную;
объявление само не переписывает доверенную пару.

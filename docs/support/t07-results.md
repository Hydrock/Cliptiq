# T07 — модель меню и нативные popup/tray

Автоматическая часть T07 реализована и проверена. Source `19b93758382ae8570a377597f536e2cceaa41678`, [итоговый CI —7/7 success](https://github.com/Hydrock/Cliptiq/actions/runs/39997567519). **Ручная приёмка T04–T06 и интерактивная T07 остаются OPEN; T08 не начат.** Пользовательских релизных пакетов пока нет.

## Общая модель и связь с историей

`build_menu(Store&, MenuKind, MenuSettings)` строит main/history/snippets из одного read-only SQLite snapshot с data_revision. Оболочки получают стабильные ID и не дублируют SQL, группировку и порядок. Проверены inline0/1/100 ×folder1/10/100, zero/one numbering, независимое включение первых десяти shortcuts, snippets выше/ниже, пустые папки, отключённый Clear при пустой истории. По умолчанию inline0, folder10, title20, tooltip200, numbering1-based включён, numeric selection выключен, snippets внизу.

Заголовки UTF-8/UTF-16 очищаются от управляющих и bidi override символов, обрезаются по первой строке и границам extended grapheme cluster. Tooltip сохраняет допустимые переводы строк. Пин libgrapheme3.0.0/Unicode17, char-only subset, вместо полного ICU; сохранены исходники, хеши, ISC/Unicode notices и атрибутированная MSVC-адаптация util. Официальный GraphemeBreakTest проходит766 случаев.

Проекция ограничена1000 history rows,1000 folders,10000 snippets,8MiB metadata, depth4; длины/UTF-8/нулевые байты проверяются до передачи native API. Читаются ограниченные текстовые префиксы до65536B, полные image/PDF payload не загружаются для каждого пункта. Проверены переполнение metadata и недопустимые модели.

Каждый выбор возвращает immutable MenuCommand с ID/revision и контекстом одного открытия. Устаревший item/revision отклоняется; старый native handle не выбирает заменившую его запись. Context target/token очищается при отмене и не переносится на последующее открытие через status item, MenuButton или DBusMenu. Quit доступен даже после изменения истории. Clear выполняет проверку ревизии и удаление в одной BEGIN IMMEDIATE transaction: конкурентный capture не будет молча удалён старой командой. Clear относится к Store, а не к текущему OS clipboard.

`cliptiq_menu_host` — проверочный host: `--demo`, `--database PATH`, `--history`, `--snippets`, `--no-tray`. Опциональный `--capture` с явным database подключает существующий privacy-gated clipboard backend, изменения истории обновляют модель по revision каждые250ms. Default и `--smoke` clipboard не читают. UI не обходит privacy gate и не запускает capture автоматически. Selection пока выдаёт команду; clipboard write/auto-paste/modifiers принадлежат T08. Settings/editor отключены до T09/T10. Snippets показываются из существующей T02 schema в сохранённом порядке, без выдуманных enabled-флагов/CRUD T10.

## Нативные оболочки

- **macOS:** AppKit NSMenu/NSStatusItem, системные labels/tooltips/SF Symbols, готовые RGBA previews, template tray icon и reversed style. Видимое окно launcher сохраняется при hidden tray. Delegate и action routes ограничивают invocation одним открытием; native regression проверяет cancel → новое открытие без старого target/token.
- **Windows:** Win32 TrackPopupMenuEx, Shell_NotifyIcon, TaskbarCreated recovery, tracking tooltip и premultiplied HBITMAP previews. Launcher принимает Tab/Enter/Space через реальные focus/button messages. PerMonitorV2 layout/font и rebuild menu bitmap на WM_DPICHANGED/theme change; тесты проверяют реальные bitmap16/24/32px при96/144/192DPI. Generic type icons и полная семантика NativeMenuOptions.icons остаются отложенным Minor; RGBA previews/hex swatches работают.
- **Linux:** GTK3 MenuButton/popup и StatusNotifierItem + com.canonical.dbusmenu без GtkStatusIcon. Независимый wire peer проверяет layout/properties/events, disabled items и перерегистрацию после watcher restart. Если tray host отсутствует или игнорирует icons/tooltips, остаётся собственный launcher/popup с previews. X11 учитывает переданные coordinates либо фактический pointer. Wayland без реального input event показывает launcher; абсолютная позиция/forged serial не обещаются. Вся fixture session использует собственные Xvfb/Sway и D-Bus.

Ready-made RGBA provider ограничен512×512 и проверкой buffer length, aspect-fit и DPI scaling; hex#RGB/#RRGGBB даёт swatch без codec. Декодирование изображений и генерация thumbnail остаются T12. Все native объекты принадлежат GUI thread (AppKit main thread), borrowed handles инвалидируются update/destruction.

## Размер

MinSizeRel, один и тот же capture-capable history host: baseline → общая модель/Unicode → native UI. Динамические системные GUI библиотеки не включены в файл; Windows CRT статический. Числа ниже — байты; ZIP содержит один executable с фиксированной датой и DEFLATE9.

| Платформа | Baseline | + модель | + native UI (итог) | ZIP итога | Прирост T07 |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 983 240 | 1 059 920 | 1 100 352 | 525 748 | 117 112 |
| macOS x86_64 | 984 576 | 1 052 784 | 1 107 296 | 548 018 | 122 720 |
| Windows x64 | 1 007 104 | 1 067 008 | 1 097 728 | 590 289 | 90 624 |
| Linux x86_64 | 921 192 | 974 440 | 1 019 504 | 513 735 | 98 312 |

Приращение самой native оболочки после модели: macOS arm64 40432B, Intel54512B, Windows30720B, Linux45064B. Ядро/clipboard-only targets не линкуют GTK. Нет Electron, WebView runtime, Qt, ICU или нового image codec; общий код не дублируется по платформам.

**Linux dependency footprint отдельно:** на локальной Ubuntu24.04 deduplicated real ELF files +loader через ldd занимают6585656B для baseline и35354864B для native GTK host; добавлено28769208B (27,44MiB) системных библиотек. Это существующие shared files, не ZIP/installer/download size: сюда не входят темы, fonts, packages и clean offline closure. Библиотеки в репозиторий/пакет не копировались. Small executable не означает столь же маленькую автономную Linux установку. Полный product/offline package/update footprint T07 не измеряет; соответствующие JSON поля остаются null.

Сырые замеры, imports, source/binary SHA256, CMake flags и логи: [t07-measurements](t07-measurements/README.md). Локальные Linux release binary hashes совпали с CI для всех трёх вариантов. Последующий commit содержит только документацию/evidence.

## Проверки и пределы доказательств

Итоговый CI: Debug + MinSizeRel на Windows2025, macOS15 arm64/Intel, Ubuntu24.04; отдельные Linux Clang ASan/UBSan, macOS ASan/UBSan и Windows MSVC ASan. По17 CTest groups на Linux,9 macOS,12 Windows, включая прежние history/privacy/clipboard suites. Дополнительно локально GCC13.3 Debug/MinSizeRel/ASan+UBSan17/17; MSVC build и portable menu suites3/3. Windows local isolated station недоступна (error5), native проверка выполнена hosted CI.

Проверены Unicode, grouping, tooltip/preview bounds, snapshot integration, stale commands, atomic Clear, platform labels/enabled/selection, hidden tray fallback, context lifetime, X11 placement и bitmap DPI. Это автоматические object/protocol checks. Windows regression заменяет только modal popup display boundary в отдельной test library; AppKit context regression — только modal NSMenu boundary. Production/size hosts используют настоящие API. Такие seam tests не заменяют интерактивную keyboard/accessibility приёмку. Native generic label/accessibility properties проверены; реальный VoiceOver/Narrator/Orca ещё не прослушан.

Linux LSan имеет узкие suppression для process-global fontconfig caches и atk_bridge_adaptor_init, **только menu GUI fixtures**; ASan/UBSan и core/backend leak checks не отключены. macOS leak detection policy унаследована от T04, MSVC ASan не является UBSan. Host Linux предупреждает об отсутствующем AT-SPI service; это дополнительная причина оставить screen-reader gate открытым. PNG из private Xvfb показывают fixture в Adwaita1x, dark2x и HighContrast/font1.5; последний не является physical monitor150%.

Свежий независимый review:0 Critical,4 Important. Все четыре воспроизведены намеренно падающими native tests в source0e3432a/[RED CI](https://github.com/Hydrock/Cliptiq/actions/runs/39996984324), затем исправлены в19b9375: keyboard launcher Windows, stale invocation context на native routes, bitmap rebuild при DPI, X11 coordinates. Исторический b8d5fdb Intel job упал на GitHub artifact-service timeout после тестов; итоговый19b CI успешно сохранил все артефакты. **Один Minor отложен:** Windows generic type icons; не закрыт как реализованный. Решения и staged boundaries: [ledger](t07-ledger.md).

Открытый [manual checklist](../../tests/manual/menu.md): реальные desktop/Explorer/compositor restarts, keyboard-only маршруты, RU/EN, screen readers, multi-monitor DPI100/150/200%, dark/high contrast, GNOME/KDE/Sway/Hyprland, чистые системы. Отдельных чистых Windows/macOS VM у пользователя нет; CI не подменяет эту приёмку. T04–T06 manual OPEN, T07 interactive OPEN, T08 не начат.

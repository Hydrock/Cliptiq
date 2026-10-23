# Cliptiq: проект переноса Clipy на macOS, Windows и Linux

Статус: **дизайн продукта для ревью; разрешён и выполняется измерительный T01, product core не начат**. Дата: 2026-10-23. Рабочий репозиторий: [Hydrock/Cliptiq](https://github.com/Hydrock/Cliptiq). Пользователь повторно подтвердил имя Cliptiq после проверки фонетического соседа Cliptic. ClipClap исключён. [Первые измерения](../../support/architecture-decision.md).

## Цель и основание

Небольшой настольный менеджер буфера с поведением Clipy: история разных форматов, snippets, быстрый доступ с клавиатуры и из tray, локальное хранение, настройки и автоматическая вставка в пределах разрешений ОС. Один репозиторий, отдельные нативные сборки. Крайне малый размер поставки — требование пользователя; численный предел им пока не задан.

Основание: [полный аудит](../../research/2026-10-23-clipy-audit.md), исходники Clipy `71b0e82623d5c73b4b717d81233eb78db8bbe258`. Все функции и ограничения имеют ID Cxx/Hxx/Sxx/Bxx. [Имена](../../research/2026-10-23-naming.md). [Пошаговый план](../plans/2026-10-23-cliptiq-port.md).

## Рассмотренные подходы

| Подход | Преимущество | Цена | Вывод |
|---|---|---|---|
| C++17 core + Objective-C++/AppKit, Win32, GTK3 | Прямые OS API, маленькое число production dependencies, без отдельного UI runtime на Mac/Windows | Три UI, ручная работа с владением/FFI, Linux GTK dependencies | Предлагаю как базовый вариант; минимальность пока гипотеза |
| Rust core + те же native shells | Memory safety общего ядра; возможен такой же малый runtime | FFI/AppKit/GTK bridges, три UI всё равно нужны | Реальная альтернатива, сравнительный прототип на первом этапе |
| Qt Widgets / Tauri | Меньше повторения UI | Qt deployment plugins либо WebView2/WebKitGTK; маленький архив не равен маленькой чистой установке | Измерить при пересмотре приоритетов; не брать по умолчанию при данном требовании |

Это рекомендация автора плана, **не уже согласованный пользователем стек**. Дизайн допускает пересмотр по данным T01. Electron/Chromium, Flutter runtime, .NET self-contained, bundled browser, большие OCR-модели не входят в предложенную базовую поставку. Ничто не доказывается одной репутацией языка: сравниваем одинаковую функцию и одинаковую архитектуру CPU.

## Предлагаемая архитектура

- `core`: C++17 static library: representations/items, policy, history/snippet repositories, menu model, XML codec, scheduler, settings schema. Один исполняемый процесс; I/O и OCR вне UI thread. Linux ownership/event loop не блокируется SQLite write.
- Три оболочки: AppKit/Objective-C++ без необходимости Swift runtime; Win32/common controls/COM/WIC; GTK3 3.24 + GLib/GIO/GdkPixbuf. GTK3 — предложение ради доступности системного toolkit; T01 сравнивает GTK4 и выбирает по footprint/поддержке, без использования deprecated GtkStatusIcon.
- X11 и Wayland — разные backend одного Linux приложения. XFixes + TARGETS/INCR; ext-data-control + wlr fallback, GDBus portals. SNI/DBusMenu для tray, launcher/CLI для recovery.
- Native OS implementations: SHA256 (CommonCrypto/CNG/GLib), images (ImageIO/WIC/GdkPixbuf), HTTPS (NSURLSession/WinHTTP/пакетный updater или системный TLS transport), autostart. Не добавлять OpenSSL только ради хеша.
- SQLite amalgamation фиксированной версии, статически в app ради одинаковой schema и миграций; базовая сборка без FTS5 до появления потребителя. SQLite FTS5 включается отдельным feature `ocr_index`, если сохраняем B01/B02. Нельзя ссылаться на случайные возможности OS SQLite.
- XML: небольшой pinned pugixml; Unicode segment boundaries: pinned utf8proc либо измеренная системная реализация с одинаковыми fixtures. Учитывать эти библиотеки в size report. Third-party approvals здесь означают инженерную проверку лицензии/размера, не повторный запрос пользователю на каждую библиотеку.
- Settings в типизированных SQLite key/value rows с schema version; platform bindings храним отдельно. Данные Cliptiq не пишутся в каталог Clipy. Основной runtime не содержит Realm/Firebase/CloudKit.

## Данные и надёжность

`HistoryEntry` имеет UUID, content digest, created/last-used times, ordered item representations с `platform/native_type/canonical_kind/item_index`, trusted metadata и payload byte counts. Canonical kind не заменяет native bytes. Несколько форматов одной записи сохраняются вместе, не преобразуются принудительно. Content digest хранится отдельно от UUID, что устраняет зависимость duplicate policy от старого режима.

SHA256 input length-prefixed по type и data, порядок фиксирован, digest не считается доказательством равенства без проверки представлений. SQLite transactions + foreign keys; assets/thumbs принадлежат entry и каскадно удаляются. Настройки, папки, snippets и history разделены. XML Clipy сохраняет только titles/content/order, поэтому enable/UUID/shortcuts переносит отдельный migration format.

Основная БД одинакова во всех вариантах и **не содержит FTS virtual tables/triggers**. OCR text/state в ней — обычные nullable columns. Optional `ocr_index` хранит disposable FTS5 в отдельном `index/search.db`; indexed build открывает его только своим SQLite с FTS5 и пересоздаёт из основной БД при старте/несовпадении data_revision. БД индекса не является источником истины; inserts/updates/deletes main store увеличивают data_revision, индексатор догоняет её с повторной проверкой. Triggers допустимы только внутри index DB на её собственных shadow-source таблицах. Base при старте и clear/import инвалидирует companion index и удаляет его конкретные файлы/sidecars в собственном index namespace, не пытаясь открыть FTS без module. Single-instance lock исключает одновременно работающие Base/indexed writers. После сбоя индекс rebuildable; удалённые данные не считаются удалёнными, пока index invalidation/cleanup не завершены. Обязательны тесты indexed→Base→indexed, clear и crash между main commit и index update.

Format switches действуют до persistence: при text+HTML+RTF и HTML off сохраняются text+RTF, а не HTML «ради fidelity». Сохраняем все **разрешённые** представления из поддерживаемых классов, их порядок и item boundaries. Неизвестные native types по умолчанию не сохраняем; зарегистрированные PDF/RTFD — известные классы с opaque bytes там, где нет декодера. Metadata сохраняется только по explicit allowlist. Повторная вставка не возвращает намеренно отфильтрованный формат. Это уточняет смысл «все форматы» в матрице.

Предлагаемые защитные пределы (новые проектные решения): 32 MiB на representation, 64 MiB на clipboard entry, 256 MiB history payload quota, 40 megapixels decoded image, 8 MiB XML и 10 000 snippets/import; потоковые лимиты до allocation. При переполнении удалить самые старые допустимые history entries по текущей сортировке; snippets не трогать. Единичный слишком большой элемент пропустить с доступной причиной, не сохранять частично. Пределы configurable в advanced settings, hard ceiling проверяется отдельно. Отдельно учитывать page cache/decoder memory: квота диска не равна лимиту RAM.

Все privacy filters выполняются до persistence, thumbnails и OCR, включая screenshot watcher. Source-app на Windows/X11 — best effort, на Wayland может быть недоступен. Без надёжной идентификации нельзя показывать «приложение защищено»; режим отказа от захвата, когда источник неизвестен, предлагается как явная опция. Не обещаем все password managers и secure deletion на SSD. История — локальная, не зашифрованная приложением в базовом варианте; права каталога user-only/ACL.

Предлагаемый default `unknown_source_mode=RejectWhenExclusionsConfigured`: при пустом excluded-app list неизвестный источник допускается; при непустом и Unknown источник capture отклоняется до любой обработки. Это касается и screenshot watcher. Пользователь может явно выбрать AllowUnknown с описанием ограничения; BestEffort не показывается как гарантированная privacy. Следствие: на Wayland без source-app integration добавление исключений может остановить весь capture. Это намеренное, видимое ужесточение относительно Clipy, которое нельзя замалчивать.

Auto-paste проверяет focus target, доступность и удерживаемые modifiers; fallback — содержимое в буфере и ручная вставка. «Paste and delete» удаляет запись после принятия операции backend, а при отказе не теряет её; подтверждение фактической вставки чужим приложением обычно невозможно. Это улучшение относительно исходника и должно быть описано в интерфейсе. Clear history не равно clear текущего системного clipboard.

## Поддержка платформ и ограничения

Предлагаемый начальный support floor: macOS 13+ (arm64/x86_64 отдельно), Windows 10 22H2+/11 (x64; arm64 после smoke test), Linux glibc 2.35+ x86_64, GTK3 3.24+ (Ubuntu 22.04 baseline, Debian/Fedora текущих поддерживаемых релизов как test targets). Совместимость Windows 10 — выбор API floor, не обещание поддержки ОС её поставщиком. Linux aarch64 после нативных тестов; musl/32-bit не обещаются. Номера текущих test-image фиксируются в T01, а не выдаются за уже проверенные.

macOS auto-paste требует Accessibility, стабильной подписи; чтение буфера может требовать отдельного согласия на новых macOS — проверять при T04, не обходить. Windows elevated targets ограничены UIPI; приложение по умолчанию не повышать. Windows.Media.Ocr требует package identity — MSIX lane либо отдельный OCR pack для portable.

Wayland capability report раздельно содержит capture, write, global-hotkeys, auto-paste, source-app, tray и placement. Отсутствие data-control не исправляется одним GlobalShortcuts portal. Clipboard portal расширяет разрешённые RemoteDesktop/InputCapture sessions и не является безусловным background clipboard API. GNOME/KDE/Sway/Hyprland проверяются на конкретных версиях; не полагаться на конфликтующие таблицы поддержки. Дополнительная интеграция рабочего стола может потребоваться. Без неё ограниченный режим должен честно называться ограниченным, а не «все функции на Wayland».

Минимум для статуса **supported clipboard-history desktop**: background capture + write + постоянное хранение + snippets + доступ через launcher и хотя бы один рабочий маршрут popup. Auto-paste и tray могут иметь явно описанный manual/launcher fallback. Если background capture отсутствует, это compatibility/limited environment, не поддерживаемая полноценная история. Для такого desktop сначала нужна отдельная integration pack задача внутри T06, с version pin и GUI smoke; иначе его не включать в supported release targets.

RTFD/Universal Clipboard остаются macOS-specific. Windows/Linux сохраняют доступные альтернативы, не эмулируют Apple ecosystem. OCR Linux и portable Windows требует engine/language data: маленькая программа не делает модели бесплатными.

## Компактность и варианты поставки

Варианты функциональности — не три разных кодовых проекта:

1. **Base**: все перечисленные пользовательские функции, кроме явно обозначенных platform limits и предлагаемой замены Firebase; OCR/search backend за feature flag, потому что UI-потребитель отсутствует в Clipy. Updates не выкидываются ради маленькой цифры.
2. **OCR pack / indexed build**: B01/B02, native engine где доступен, Tesseract/Leptonica + выбранные language data на остальных системах. Размер pack отдельно и в суммарной offline-поставке. Пользовательские OCR/search actions — последующая, отдельно обозначенная новая функциональность.
3. **Migration tool**: Realm/NSKeyedArchive importer только macOS; одноразовая загрузка, не DLL в каждом запуске.

Это предложение адаптации scope, а не согласованное исключение исходных функций. Если требуется буквальная эквивалентность внутреннего OCR и сетевой диагностики в каждой минимальной поставке, full lane включает их и должна пройти собственный size budget; до принятия решения эти строки не отмечаются завершёнными.

По умолчанию план охватывает **все** функции baseline, включая separate OCR и migration lanes и исходную diagnostics возможность как открытое решение. Предлагаемая Base — вариант поставки, а не подмена полного запроса пользователя. В отчёте полной поставки строка S11 остаётся pending до принятия замены либо реализации telemetry. Ни уменьшенная Base, ни неподдерживаемый Wayland не дают основания объявить весь перенос завершённым.

Предварительные инженерные цели, не измерения и не обещания: Base compressed download ≤5 MiB, installed app-owned ≤10 MiB на одну architecture, без debug symbols и пользовательских данных. Контроль +10% к принятому baseline; любое превышение показывается в CI, исправление либо документированное инженерное решение. После T01 уточняем численные цели до разработки продукта. OCR/migration/installer отдельно; полный total обязательно показан рядом. Для Linux GTK/libstdc++/X11/Wayland/GIO и недостающие пакеты учтены как dependency delta на чистой VM. AppImage/Flatpak не объявляются основной маленькой сборкой без этой арифметики.

Release flags: MSVC `/O1 /GL /LTCG /OPT:REF /OPT:ICF`, Clang/GCC `-Os`/измеренный `-Oz`, LTO/dead strip/function sections где поддерживается. CRT статический против redistributable измеряется, а не скрывается. dSYM/PDB/debug symbols хранятся отдельными CI artifacts. UPX не основа экономии. Native system fonts/icons, без web assets/рендеринга страниц, без dev dependencies внутри dist.

## Отличия от Clipy, требующие видимого решения

- Firebase Analytics/Crashlytics предлагаю заменить локальной диагностикой без отправки clipboard contents; это потеря части служебной функциональности, а не полный перенос. Если пользователь требует её сохранить — отдельный opt-in telemetry компонент и размер отчётности.
- Пакетные updates Linux вместо встроенной замены файлов; Windows signed installer/manager вместо Sparkle. Сохраняются check schedule/version/last check, механизм установки нативный.
- OCR/FTS как отдельная lane; нет выдуманного существующего UI поиска.
- Исправление duplicate UUID lookup, безопасный failed paste-delete, guarded plain-text для images, регенерация preview и единые privacy filters вместо копирования багов.

## Критерий завершения

Каждый ID C/H/S реализован или имеет явно принятый platform-specific результат; B-ID отдельно классифицирован. CI release builds для трёх ОС, реальные GUI smoke tests, size/dependency report, round-trip XML, migration fixtures, installer/signature verification. Нельзя назвать ограниченный Wayland режим полным переносом. Нельзя назвать Base literal full parity, пока S11/B01/B02 решения не подтверждены. Репозиторий содержит исходники, документацию и release workflow; `reference/Clipy` остаётся игнорируемым исследовательским checkout.

## Оставленные исполнителю решения

Точные compiler/dependency pins и supported desktop versions выбираются и фиксируются T01 по актуальной доступности; структура внутренних private helper functions, стиль native controls и SQL query plans — исполнителю. Стек, size goals, OCR distribution и отказ от Firebase остаются предложениями для ревью. Публикация, подпись чужими сертификатами, регистрация домена, release в GitHub не выполняются этим документом.

## Принято после T01

Пользователь 23 октября 2026 подтвердил C++ с нативными оболочками и разрешил T02 по плану. Предварительные size ceilings сохраняются целями полного продукта; измеренный prototype/core probe не доказывает их достижение.

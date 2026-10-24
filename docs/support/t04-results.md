# T04: macOS clipboard backend

23 октября 2026. Реализован Objective-C++17/ARC backend и связь с C++ history/privacy gate. **Ручная macOS-приёмка остаётся открыта; T05 не начат.** Это библиотека и проверочные hosts, готового приложения с меню пока нет.

## Поведение и контракты

Общий `ClipboardBackend` предоставляет capabilities, snapshot read/replay, own-write token, start/stop. `MacClipboardBackend` использует системный AppKit, 500 ms changeCount polling и максимум три попытки согласованного чтения. Проверяется счётчик до/после metadata и bytes; foreground app не входит в sequence token. Первый tick читает существующий clipboard, как Clipy; собственная успешная запись обновляет наблюдаемый token и не зацикливает историю. Polling может пропустить промежуточные копирования между ticks.

Сохраняются несколько items, порядок и исходные bytes Text/RTF/RTFD/HTML/PDF/Files/URL/PNG/TIFF. Modern alias вытесняет legacy внутри item, PNG предпочитается TIFF внутри того же item. Отдельный TIFF-only item не теряется. Root fallback обозначен `UINT32_MAX`; полный legacy NSFilenames array сохраняется даже при синтезированном URL первого файла. При replay массив остаётся root-форматом, остальные legacy aliases публикуются через modern UTI с теми же bytes. Другие root representations прикрепляются к первому item; sparse indices уплотняются с сохранением группировки. Проверка входа и подготовка идут до clear; последующий отказ native API не превращает clipboard запись в транзакцию.

API и ownership: [backend contract](../../platform/macos/README.md), [общий интерфейс](../../core/include/cliptiq/clipboard.hpp). Создание, probing, read/write/start и policy configuration выполняются на main; callback может остановить/удалить владельца. Off-main stop использует generation guard и не останавливает новый monitor отложенным cleanup старого.

`MacCaptureSession` владеет backend и serial libdispatch worker. Только worker открывает/пишет/закрывает Store; rejected capture не создаёт даже файл БД. В UI возвращается краткое решение и retained ID. Очередь по умолчанию ограничена двумя pending snapshots и 128 MiB оценки retained capacity; overflow явно сообщает Busy и пропускает новый capture. Это предел входа очереди, не общий RSS. stop отменяет старую generation и дожидается текущего worker I/O. DB directory permissions и single-instance lock остаются обязанностью будущего application host.

## Интеграция с privacy gate

Полный набор offered types/markers собирается до filtering. T03 вызывается с metadata и пустыми payload placeholders **до dataForType**. Fetch выполняется только для разрешённых зарегистрированных типов; финальный snapshot проходит `dispatch_capture` перед настоящим SQLite sink. Нет отдельного обходного capture path. Thumbnail/OCR hooks пока не подключены; capability OCR остаётся Unavailable.

| Сценарий | Автоматическая проверка |
|---|---|
| Transient и excluded marker prefix | Reject до чтения payload, включая unknown types |
| Concealed/remote options | Общие T03 rules; native remote primary Files reject и secondary Files removal |
| Source exclusions/Unknown | NSWorkspace даёт только BestEffort; Unknown с exclusions по default reject |
| HTML off / unknown native type | ItemProvider подтверждает ноль dataForType; text/RTF остаются |
| Permission Deny | Capability Unavailable и причина, ноль payload reads |
| General Ask/default | Background Limited/paused; explicit read может запросить consent; Deny не обходится |
| Permission recovery | Injected переход к разрешённому доступу возобновляет native read |
| Rejected session | Ноль storage writes и отсутствующий файл БД |
| Oversize/invalid input | Representation/entry/type/count limits; invalid replay до clear |
| Own write/lifecycle/pressure | Нет повторной capture; stop/restart, destruction inside callback и bounded queue проверены |

Permission states и changing-owner counters инъецируются поверх реальных named pasteboards. **Это не проверка настоящего consent UI или поведения внешних приложений.** На macOS 15.4+ используется accessBehavior, до 15.4 — прежний API с явно ограниченной гарантией. Выключенные/неизвестные payload не запрашиваются; при разрешённых типах dataForType остаётся eager OS API: размер NSData проверяется перед C++ copy, но allocation/latency provider и pasteboard server не ограничиваются этим backend.

## Проверки и доказательства

[Финальный CI 39955178077](https://github.com/Hydrock/Cliptiq/actions/runs/39955178077): **6/6 jobs success**. Source `0ab616c52080d0671cec5853600cc64f983ac4f6`, clean checkout. Debug + MinSizeRel: Windows/Linux — 34 policy + 29 history scenarios и crash recovery; macOS arm64/x86_64 — дополнительно **28 native scenarios**. Отдельные Linux Clang и macOS arm64 AppleClang ASan/UBSan jobs прошли. macOS leak detection отключена; TSan/leak audit не заявляются.

AppleClang 17.0.0.17000013, SDK 15.5, Mach-O minos 13.0. macOS arm64: macOS-15.7.9-arm64-arm-64bit-Mach-O, build 24G830; macOS x86_64: macOS-15.7.9-x86_64-i386-64bit-Mach-O, build 24G830. Локально Windows Debug/MinSizeRel: 3/3 CTest PASS; tools suite 7 tests, один symlink skip по privilege. Нового runtime Windows/Linux не добавлено: core/policy probe sizes совпадают с T03.

[Raw JSON, compiler IDs, source hashes и CTest logs](t04-measurements) сохранены в репозитории, без binaries. Имя t02-size.json оставлено для совместимости workflow. В macos_clipboard находятся парные размеры/ZIP/hash, otool dependencies и minOS/SDK.

Native fixtures генерируются через AppKit: RGBA PNG/TIFF, RTFD с именованным PNG attachment, PDF, RTF/HTML/UTF-8, два file paths. [Attribution](../../tests/fixtures/macos/README.md). CI использует только уникальные named pasteboards; General не читается/не изменяется. Преднамеренные RED runs, исправления root replay, lifecycle/backpressure, startup capture и legacy aliases записаны в [ledger](t04-ledger.md). Ошибка генератора новой RTFD-фикстуры отдельно исправлена и не считается доказательством backend RED.

## Размер

| Target | Baseline, bytes | С backend, bytes | Добавка executable | ZIP с backend | Добавка ZIP |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 907,944 | 984,680 | 76,736 | 493,641 | 31,311 |
| macOS x86_64 | 892,632 | 989,704 | 97,072 | 517,336 | 33,060 |

Оба проверочных executable с backend меньше 1 MiB. Это измеренный результат конкретных fixtures, не обещание размера будущего приложения.

Парный executable fixture: одинаковый AppKit/C++ history host и тот же host с backend + timer/session/serial worker. Pinned static SQLite, MinSizeRel, dead_strip, ARC. ZIP имеет одинаковое имя единственного executable. Добавка включает связанную интеграцию; это не изолированный размер машинного кода Clipboard.mm. Нет новых third-party GUI/Swift runtime, web engine или OCR models. Системные AppKit/Foundation/libdispatch не копируются в поставку, но являются обязательными OS dependencies. Developer manual helper и test executables не входят в измеряемый payload. Это не размер полного Cliptiq, installer или offline closure; product budgets пока не доказаны.

## Воспроизведение и открытая приёмка

На Mac: `python tools/fetch-footprint-deps.py`, `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test --output-on-failure`. Повторить release; для замеров добавить `-DCLIPTIQ_BUILD_SIZE_PROBE=ON` при configure и выполнить `python tests/size/report.py`. Для ASan/UBSan — sanitize preset; на macOS CI leak detection отключена, это не leak audit.

[Manual checklist и opt-in helper](../../tests/manual/macos.md) подготовлены, но **не выполнены**. Нет интерактивного Mac для TextEdit/Safari/Finder/Preview, настоящих granted/denied/Ask dialogs, sleep/resume и внешнего owner termination. CLI helper не доказывает identity/permissions подписанного product bundle. Deployment target 13.0 проверяется в Mach-O, runtime CI на macOS 15 не доказывает macOS 13/14 compatibility. T04 нельзя объявлять полностью закрытым по ручному критерию плана. Ни меню/hotkeys/auto-paste, ни Windows backend T05, ни последующие этапы не реализовывались.

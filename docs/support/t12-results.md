# T12: preview и screenshot file observation

Автоматическая часть реализована. Runtime `132adc53f3bf6ddd9cc009dc97151de84d888269`, [CI 40077744955](https://github.com/Hydrock/Cliptiq/actions/runs/40077744955) — 7/7. Ручная T04–T11 остаётся OPEN, T12 desktop acceptance OPEN; T13 не начат. Пользовательских пакетов/релиза пока нет.

## Поведение

Готовые RGBA preview поступают в существующие нативные меню macOS/Windows/Linux; GUI не декодирует полные изображения. Aspect-fit 100×32, диапазон настроек 1–512, hex swatches сохранены. Системные ImageIO/CoreGraphics, WIC и GdkPixbuf; новый bundled codec/runtime не добавлен.

Наблюдение выключено по умолчанию. В настройках явно выбирается абсолютная папка PNG-скриншотов, только её непосредственные файлы. Начального сканирования старых файлов нет. Windows ReadDirectoryChangesW, Linux GFileMonitor, macOS NSMetadataQuery с kMDItemIsScreenCapture=1. Это наблюдение файлов, не запись экрана. Spotlight/indexing и системные permissions не подтверждены живой macOS-приёмкой.

Privacy gate проверяется до чтения и повторно перед записью. Источник файла Unknown; при стандартном режиме исключений приложения screenshot import блокируется, а не получает выдуманный foreground app ID. Файлы не несут достоверных clipboard concealed/transient/remote markers. Image-off и RejectAlways отклоняют импорт. Политика меняется с отменой старого поколения worker. Только успешно сохранённый history ID получает preview; OCR не добавлен.

Очередь ≤64 задач/64MiB encoded preview data, один worker, один готовый результат. Файл ≤32MiB, изображение ≤40MP, стабильные одинаковые данные в двух чтениях с интервалом250ms; неполный файл ждёт до10s. PNG framing/CRC и системный decode должны пройти до сохранения. Read handles закрепляют выбранную папку, final symlink/reparse файлы отвергаются. Preview disk/RAM cache по16MiB; OS decoder может дополнительно выделять память, это не заявление о RSS160MiB.

Сравниваются полные нормализованные пиксели, не lossy thumbnails; удаляются только screenshot-origin дубликаты, обычная clipboard duplicate policy сохраняется. Digest/size/generation и существование history ID проверяются транзакционно; очистка/удаление инвалидируют отложенный импорт generation-проверкой внутри ingest. Сбой watcher даёт сохраняемый статус в меню/настройках и требует перезапуска наблюдения.

## Размер

| ОС | Без T12, B | С T12, B | Добавка, B | ZIP, B | Добавка ZIP, B |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 707 040 | 1 786 192 | 79 152 | 741 013 | 31 140 |
| macOS x86_64 | 1 700 056 | 1 778 344 | 78 288 | 770 382 | 31 910 |
| Windows x64 | 1 522 688 | 1 576 448 | 53 760 | 812 359 | 27 040 |
| Linux x86_64 | 1 345 616 | 1 394 768 | 49 152 | 676 232 | 24 889 |

Это парные MinSizeRel review hosts одного source/compiler: XML host без/с media. Добавка изолирует включение T12 в текущем исходнике, не всю историческую разницу T11→T12. ZIP — один executable DEFLATE9, не installer/update. Системные библиотеки не включены. Linux resolved ELF dependency delta приведена в raw evidence. Полный installed/offline размер и размер обновления здесь не заявлены.

## Проверки и ограничения

CI Debug+MinSizeRel на Windows2025/Ubuntu24.04/macOS15 ARM+Intel, Linux/macOS ASan+UBSan, MSVC ASan. CTest groups: Linux43, Windows33, macOS30. Локально Linux Debug/MinSizeRel/ASan+UBSan43/43, Windows16 безопасных групп. Числа — группы CTest, не количество assertions. Весь прежний privacy/history/menu/paste/settings/editor/XML набор сохранён. Прежние GTK LSan и macOS detect_leaks=0 policies сохранены; новых широких suppressions нет. TSan не запускался.

Три новых review-regression теста сначала дали RED, затем GREEN: pending import после clear, замена выбранной папки, persistent monitor failure status. Atomic observe устраняет найденную data race; concurrency injection проверяет поведение, не заменяет TSan. Единственное свежее ревью нашло4 Important (исправлены одним проходом),1 Minor (отложен). Начальные native helpers не везде разрабатывались test-first, это явно записано в ledger.

Отложено: image_previews=off скрывает картинки меню, но пока не экономит decode/cache работу. T11 minor с преждевременным Saved в XML picker остаётся открытым.

[API/ограничения](../../core/MEDIA.md), [сырой evidence и SHA256](t12-measurements/README.md), [ручная приёмка](../../tests/manual/media.md), [все решения и цена ограничений](t12-ledger.md). Исторические T04–T11 отчёты не переписывались.

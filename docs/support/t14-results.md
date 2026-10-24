# T14: миграция Clipy без тяжёлого runtime в приложении

Автоматическая часть реализована. Runtime `c965d5c7251fbd1d715498502162b15c4818a7c4`, [CI40090941139](https://github.com/Hydrock/Cliptiq/actions/runs/40090941139) —9/9. Ручная T04–T13 остаётся OPEN, T14 real-backup/clean-system acceptance OPEN. T15 не начат; installer/release не опубликован.

## Реализация и сохранность

Отдельный C++ converter принимает versioned neutral SQLite на Windows/Linux/macOS. Экспортирует текущие Clipy SQLite v1–v4; нативная macOS-утилита добавляет plist/архивированные настройки. Отдельный pinned Realm-helper конвертирует schema7 и старые архивы. Realm/Swift не связываются с основным приложением.

Исходный DB/WAL/journal проверяется двумя чтениями; SQLite восстанавливает только приватную копию. Realm открывает только копию; source locks/management не удаляются. Новое neutral output публикуется после полной проверки, существующий файл не заменяется. Тесты сравнивают хеши источника/sidecars/папок до/после, включая WAL, hot journal, отказ и отсутствующий asset.

Импорт транзакционный: оригинальные IDs, порядок, enabled, время, native bytes и provenance; дубликаты сохраняются, повторный идентичный импорт не добавляет строк. Конфликт ID/лимит/неверный тип/schema отменяет всё. Исторический createdAt для v1/v2/Realm отсутствует — используется исходный update time с явной оговоркой. Обычные последующие capture/cleanup могут применить настроенные лимиты истории.

Original archives/titles/thumbnails и asset metadata принадлежат entry через ON DELETE CASCADE. Проверены delete/clear/paste-delete/pruning/periodic cleanup. Это удаление live rows, не secure erase. Общие настройки хранятся отдельно. Settings по умолчанию inactive; --apply-settings включает только проверенный совместимый subset. Старые keycode/double-modifier/reserved chords и app IDs между ОС требуют ручной сверки; исходный plist остаётся byte-identical provenance. Автозапуск/телеметрия/screenshots/updater не активируются миграцией.

macOS восстанавливает per-type occurrence item boundaries и legacy root filename offers. Для Windows/X11/Wayland добавлена адаптация поддерживаемых форматов при выборе: Unicode, RTF, HTML/CF_HTML, PDF, совместимые PNG/TIFF. Неподдерживаемые RTFD/files/multi-item данные не объявляются переносимыми; отказ/ограничение сообщает причину и сохраняет оригинал. Native tests проходят через реальный clipboard, включая точное сравнение Unicode.

## Размеры

| ОС | App executable, B | ZIP executable, B | Добавка T14 review host, B | C++ converter, B | ZIP converter, B |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 867 432 | 764 144 | 17 248 | 967 424 | 492 204 |
| macOS x86_64 | 1 849 928 | 794 176 | 8 960 | 972 280 | 515 327 |
| Windows x64 | 1 632 768 | 834 613 | 3 072 | 1 078 784 | 597 104 |
| Linux x86_64 | 1 484 880 | 703 823 | 8 192 | 865 744 | 456 378 |

App executable — фактическая production entry-сборка, ZIP — один executable DEFLATE9. Это не полный app bundle/installed/offline closure. Добавка T14 сравнивает тот же review host с сохранённым T13 CI по архитектуре; это историческое сравнение, не переключатель одного исходника. Все source/compiler reports сохранены. Converter — отдельный C++ executable; новые runtime/shared dependencies в main app не добавлены. App содержит небольшую replay adaptation, поэтому общая добавка к app не объявляется нулевой.

| ОС | Полная optional migration lane, B | ZIP lane, B | App executable + lane, B | Исключённые dSYM, B |
|---|---:|---:|---:|---:|
| macOS arm64 | 19 952 569 | 7 400 912 | 21 820 001 | 144 939 538 |
| macOS x86_64 | 20 963 761 | 7 789 110 | 22 813 689 | 140 655 801 |

Полная optional migration lane включает два C++ tools, Swift helper, Realm dylib и notices; dSYM не поставляются. App + lane складывает app executable и всю lane, без скрытого исключения Realm. Эта разовая legacy-поставка заметно больше Base и не укладывается в Base10MiB installed ориентир; она не устанавливается в main app. Для обычной текущей SQLite-миграции Realm не нужен. System frameworks/Swift OS libraries и installer не включены в эти числа; чистая ВМ не подтверждена. Перенос staged delivery проверен с недоступной build-tree Realm dylib. Повторная post-link обработка ломала Swift helper; финальная поставка сохраняет release Mach-O metadata, без dSYM.

## Проверки и ревью

Debug+MinSizeRel на Linux52/Windows41/macOS39 групп CTest;11 suite logs. Linux/macOS ASan+UBSan, MSVC ASan; отдельно реальные schema7 Realm files/NSKeyed archives, Cocoa image archive, oversized40MP header,192MiB prefs pre-read rejection и relocation на Mac ARM+Intel. Локально Linux Debug/MinSizeRel/ASan+UBSan52/52; Windows19 безопасных групп + фактический portable import suite. Local GUI WindowStation ограничен; Windows native replay проверен hosted CI. Прежние leak policies сохранены; TSan не запускался.

Одно свежее ревью:1Critical (живое содержимое оставалось после удаления),4Important (item grouping, cross-OS replay, image/preferences allocation bounds, scalar coercion). Исправлены одним проходом до зелёного CI. Есть RED→GREEN evidence для ownership, scalar types, native replay/grouping, journal recovery, archive/prefs limit separation. Security guard отказал на reviewed SHA и прошёл после устранения image expansion/whole-file reads; native valid/oversize fixtures подтверждают новую ветку. Initial native wiring не везде было test-first — абсолютный TDD не заявлен. Новых Minor ревью не выделило.

Прежние Minor сохранены: T11 generic Saved во время XML worker; T12 preview-off decode/cache; T13 --background показывает launcher. Несколько промежуточных CI отказов (Windows header order, BOOL archive decoder, смешанный legacy-root test expectation, Swift post-link relocation) устранены; отдельные старые timing assertions editor/X11 были нестабильны под нагрузкой, финальные полные suites проходят без ослабления тестов/suppressions.

Final Ubuntu regression: an old T13 fixture mishandled SQLite Busy. Only test receive/retry and a controlled lock were changed; production lifecycle is unchanged. RED/GREEN, ten stress repetitions and Release52/52 are archived.

[Контракт](../../core/MIGRATION.md), [команды](../../tools/import-clipy/README.md), [ручная матрица](../../tests/manual/migration.md), [решения ревью](t14-ledger.md), [raw evidence](t14-measurements/), [происхождение fixtures](../../tests/fixtures/migration/source-inventory.json). Поздний documentation commit не меняет исполняемый runtime.

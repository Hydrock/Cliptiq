# T11 — XML-совместимость с Clipy

Автоматическая часть реализована: source `98b3950634c650ce9b0333dba73989e37efcd2bf`, [CI40074549461 —7/7 success](https://github.com/Hydrock/Cliptiq/actions/runs/40074549461). **Ручная приёмка T04–T10 остаётся OPEN; live Clipy и interactive T11 тоже OPEN. T12 не начат.** Пользовательских установщиков пока нет.

## Импорт и экспорт

Формат pinned Clipy develop71b0e82623d5c73b4b717d81233eb78db8bbe258: `folders/folder/title/snippets/snippet/{title,content}`. UTF-8/XML1.0, порядок/пустые папки, пробелы/Unicode/multiline, CDATA/comments/standard и numeric entities. Missing/empty title получает `untitled folder`/`untitled snippet`, content — пустую строку; whitespace-only title сохраняется. Raw CRLF/CR нормализуется по XML1.0, numeric CR сохраняется. Export включает disabled элементы, не переносит UUID/enabled/revision/hotkeys. Import генерирует новые ID, включает элементы и дописывает; repeated import не deduplicate/replace.

Весь append атомарен с проверкой destination+incoming quotas; ошибка не оставляет новых папок/частичного порядка/revision. Export читает consistent SQLite snapshot, готовит bounded XML до открытия временного файла. Запись: exclusive temp в той же директории, flush, atomic replacement. До замены старый файл сохраняется; post-rename directory durability failure честно сообщает, что замена уже произошла. Snapshot может задержать concurrent writer commit; slow-disk/capture throughput guarantee не заявлен.

Нативные AppKit/Win32/GTK Import/Export file pickers, overwrite confirmation, default `snippets.xml`. XML/file/SQLite работа в одном owned background transfer с отдельным соединением. Dirty/pending save блокирует transfer; controls, custom undo/redo/edit/selection/drag/recorder, reopen/close заблокированы до UI completion. Ошибки читаемы; успешный импорт обновляет editor/catalog/menu. Если DB import committed, но refresh не удался, UI явно запрещает повторять import и предлагает Reload. Отмена picker не запускает задачу. Path-based CI идёт через тот же pipeline; физические диалоги остаются manual.

Strict schema: неизвестные/дублирующие поля, attributes/namespaces, альтернативные encodings/PI, вложенные rich fields отклоняются. DTD и внешние entities запрещены до DOM; numeric entities/UTF-8 проверяются на XML1.0 scalar. Нет внешних file/network entity reads. Unfiled legacy требуется сначала переместить в папку; XML-forbidden stored controls дают ошибку вместо потери/изменения данных.

## Компактность и пределы

TinyXML-2 11.0.0 статически закреплён по commit/SHA256, Zlib license сохранена. План предлагал pugixml; для фиксированной схемы выбран небольшой single-source parser, но абсолютный минимум среди XML-библиотек не заявлен. Одна маркированная локальная правка PEDANTIC whitespace сохраняет пробелы перед CDATA/comments; upstream RED воспроизведён. Original/compiled hashes и header CRLF→LF отдельно документированы.

64MiB wire (ранние8MiB были предложением; принятую T10 коллекцию16MiB плюс escaping нужно представлять),1000 folders/10000 snippets, title1024B/body1MiB/total body16MiB с Store quota. Preflight75000 tokens/depth5, bounded DOM/whole-file memory. Heavily escaped collection может превысить wire cap — явный отказ без усечения/записи. Постоянная streaming memory не заявлена.

Парные MinSizeRel hosts из одного source/compiler/flags, XML disabled/enabled; байты. ZIP — один executable DEFLATE9, не installer/update. Общий core/schema/filtering одинаков; добавка не равна всей исторической разнице T10→T11.

| Платформа | Без XML | С XML | Добавка executable | ZIP с XML | Добавка ZIP |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 620 200 | 1 706 896 | 86 696 | 709 113 | 33 698 |
| macOS x86_64 | 1 600 216 | 1 699 912 | 99 696 | 737 561 | 34 934 |
| Windows x64 | 1 456 640 | 1 522 176 | 65 536 | 784 898 | 28 051 |
| Linux x86_64 | 1 271 696 | 1 345 584 | 73 888 | 650 587 | 27 730 |

Linux resolved ELF closure35381992B у обеих сборок, добавка shared-library bytes0; loader включён, fonts/themes/resources/offline packages исключены. Windows добавляет вызовы системного comdlg32, RichEdit остаётся System32; macOS AppKit, Linux существующий GTK3. Нового bundled runtime/parser service нет. Полный installed/offline/update footprint не измерен. Local Linux paired executable SHA256 совпал с финальным CI.

## Проверки и review

CI7/7: Debug+MinSizeRel Windows2025/Ubuntu24.04/macOS15 ARM+Intel; Linux/macOS ASan+UBSan, MSVC ASan. **Linux38/Windows28/macOS25 CTest groups**. Локально GCC Debug/MinSizeRel/ASan+UBSan38/38, MSVC compile+11 безопасных групп. Все прежние privacy/history/menu/paste/settings/editor suites сохранены.

Roundtrip Unicode/whitespace/CDATA/entities/CR, fallback/empty folder/all-disabled export, append-not-replace, trigger-induced rollback, quotas/UTF-8/NUL/forbidden refs/depth/nodes, strict schema/DTD, nested transaction preservation, bounded file reads/atomic destination. Native controls→background transfer→SQLite→catalog/menu, repeated-transfer/draft/close guards, Unicode paths, bad import no mutation, bad stored XML control no overwrite. Independent ElementTree читает C++ export и создаёт C++ import; fixture source-derived, не captured live Clipy export. Python serializer меняет numeric CR на literal CR, поэтому тест сравнивает semantics actual generated wire; оригинальный fixture→C++ отдельно сохраняет numeric CR точно.

Один fresh review:0 Critical/2 Important исправлены одним RED→GREEN pass. Первый RED:37k text segments+63MiB comment обходили размерные limits практически квадратичным suffix scan и превышали20s. Теперь поиск ограничен текущим text range; локально Debug1.55s, полный38/38. Второй RED: AppKit Command-Z во время locked transfer мог вызвать undo и снова включить body; CI40074289793 и Linux/Windows action-path тесты воспроизвели. Guard теперь стоит на native action/edit/selection/detail/drag/recorder entry points. Повторного review не было.

**Deferred Minor:** picker start оборачивает успешный start_xml в generic status и может заменить Importing/Exporting на Saved locally раньше completion. Transfer остаётся pending, controls/close заблокированы; completion/error окончательны. Замечание явно отложено по review workflow, а не скрыто.

## Решения и оставшаяся приёмка

- Pinned static TinyXML2 вместо предложенного pugixml: цена — parser contribution и обновление dependency; локальная whitespace правка добавляет maintenance.
- Strict Clipy schema/XML1.0: цена — non-Clipy extensions/alternate encodings/PI отвергаются, forbidden stored controls не экспортируются с потерями.
- Wire64MiB/count/depth/node limits: цена — bounded whole-file/DOM memory и отказ pathological escaping; исходные данные/старый destination сохраняются на этом pre-write отказе.
- Live pinned Clipy interchange manual OPEN: fixture/independent parser не доказывают running-app acceptance.
- Physical picker/cancel на трёх desktop manual OPEN: path pipeline не доказывает native dialog UX.
- Accessibility/DPI/focus/activation/clean VM manual OPEN: hosted images не подменяют usability/clean install; чистых Mac/Windows ВМ у пользователя нет.
- Final CI проверен отдельно по точному source: review не аттестует matrix, её подтверждают raw jobs/logs/hashes.
- Размеры/upload проверяются отдельно; installer/offline/update/release не заявлены.
- T04–T10 clipboard/paste/permissions/portal/tray/hotkey manual OPEN: release readiness не заявлена.
- T12/OCR/thumbnail deletion races исключены: preview/screenshot здесь не реализованы.
- Unfiled legacy export требует реальной папки: цена — сначала организовать данные, ничего не пропускается молча.

[API](../../core/XML.md), [raw evidence/flags/imports/SHA256](t11-measurements/README.md), [manual checklist](../../tests/manual/xml.md), [ledger](t11-ledger.md). Исторические T04–T10 отчёты не переписывались.

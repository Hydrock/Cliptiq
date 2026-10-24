# T10 — сниппеты и редактор на трёх ОС

Автоматическая часть T10 реализована: runtime source `879cc511c3b6f51baeb31c71d5e629634ce1ff7d`, [CI40061508988 —7/7 success](https://github.com/Hydrock/Cliptiq/actions/runs/40061508988). **Ручная приёмка T04–T09 остаётся OPEN, интерактивная T10 тоже OPEN. T11 не начат.** Готовых пользовательских пакетов нет.

## Поведение и интеграция

Общий C++17 repository: папки/сниппеты CRUD, Unicode/multiline/whitespace, enabled, стабильные UUID, порядок, межпапочное перемещение, атомарное исправление duplicate/sparse positions, restart persistence. Owned v1→v2 migration сохраняет исходные таблицы, добавляя две таблицы enabled/revision; старые Cliptiq binaries отвергаютv2. Чужая/Clipy DB не мигрируется. Удаление папки каскадно удаляет её snippets и hotkey preference, меняет T09 revision против устаревшего Settings snapshot.

AppKit NSOutlineView/NSTextView, Win32 TreeView/system plain RichEdit, GTK3 TreeView/TextView: create/rename/body edit/enable/delete-confirm, local native DnD, common bounded undo/redo с reset при выборе, folder shortcut recorder. Содержимое plain text. Nesting/tags/templates/XML/rich-text не добавлены. Menu OpenEditor, global editor action и CLI `--show-editor` открывают редактор; editor chord исходно unset. Folder recorder использует T09 conflict rollback. Disabled preferences сохраняются, native routes освобождаются/восстанавливаются; deletion очищает их. Wayland закрывает всю granted portal session при disable/delete папки: остальные сочетания требуют явной переавторизации, без автоматического prompt.

Menu и PastePipeline повторно проверяют enabled/deleted folder/snippet и menu revision; forged current-revision disabled commands не пишут clipboard и не вызывают input. История clipboard по-прежнему проходит T03 privacy gate; явные пользовательские сниппеты — отдельные локальные данные.

Пределы:1000 папок/10000 сниппетов, title1024B UTF-8, body<=1MiB с учётом Store quota, всего16MiB. Invalid UTF-8/NUL отклоняются. Oversized legacy сохраняется, даёт явную ошибку без усечения. Native input ceilings выше repository ceilings. Ошибка загрузки любого item восстанавливает прежнее native выделение, включая пустое.

Автосохранение title/body идёт в одном owned async task через отдельное SQLite соединение. Быстрые правки объединяются в последние title/body drafts; очередная запись использует exact committed revision предыдущей. UI polling не ждёт worker. Ошибка сохраняет видимый черновик, останавливает retry; исправление повторяет запись, Reload явно отбрасывает. Пока save pending, навигация/Reload/delete/close блокируются; после ошибки закрытие по умолчанию оставляет редактор. Undo64/4MiB на каждый stack, reset при selection; coalescing меняет native grouping. Discrete CRUD/move/enable/undo остаются синхронными с no-wait SQLite busy scope: при lock возвращают Busy, но не заявлены свободными от filesystem I/O. Общая async загрузка/CRUD не реализована. Host больше не перечитывает settings/hotkeys на каждую букву; periodic reconciliation сохраняется.

## Размер сборок

Парные MinSizeRel probes из одного source/compiler/flags: тот же host без/с включённым редактором. Размеры в байтах. ZIP — один executable DEFLATE9, не installer/update. Новых bundled GUI frameworks/services нет; без Electron/Qt/GtkSourceView/ICU/new codec.

| Платформа | Без редактора | С редактором | Добавка executable | ZIP с редактором | Добавка ZIP |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 396 488 | 1 620 200 | 223 712 | 675 412 | 69 333 |
| macOS x86_64 | 1 365 376 | 1 600 216 | 234 840 | 702 645 | 74 127 |
| Windows x64 | 1 312 256 | 1 456 640 | 144 384 | 756 783 | 63 516 |
| Linux x86_64 | 1 161 072 | 1 271 696 | 110 624 | 622 897 | 45 443 |

Linux resolved ELF closure35381992B у обеих сборок, **добавка shared-library bytes0**. Это существующие файлы+loader с real-path deduplication, не fonts/themes/resources/offline package footprint. Windows использует Msftedit.dll из System32; его bytes/hash есть в JSON, файл не поставляется. macOS использует системный AppKit. Полный installed/offline/update footprint T10 не измерен: пакетов ещё нет. Local Linux paired binary SHA256 совпал с финальным CI. Headless GTK warnings о ресурсах не означают успешную отрисовку на чистом desktop.

## Проверки и review

CI7/7: Debug+MinSizeRel Ubuntu24.04, Windows2025, macOS15 arm64/Intel; Linux/macOS ASan+UBSan, MSVC ASan. **Linux33, Windows24, macOS21 CTest groups.** Локально GCC Debug/MinSizeRel/ASan+UBSan33/33; MSVC compile+10 безопасных групп. Hosted CI не подменяет clean VM; пользователь подтвердил отсутствие отдельных чистых Windows/macOS ВМ.

Repository/editor: restartv1 migration/data preservation; CRUD/title/body UTF-8/limits; duplicate/gap repair; атомарные move/reorder/delete; disabled menu/paste/hotkey filtering; stale entity/data revision; отменённый/устаревший drag без частичной записи; pending/failed draft, reopen/Reload/checkbox rollback; bounded undo и native Ctrl/Command-Z; Windows Space; real native controls→SQLite→menu и folder binding conflict сохраняют старый chord. Native drag transport реализован, но CI тестирует common commit/cancel, не физический pointer gesture.

Один fresh independent review:0 Critical/3 Important/0 Minor; все Important исправлены одним RED→GREEN pass. (1) Oversized selection прежде оставлял tree наB при sessionA: теперь selection rollback на любой ошибке. (2) Peer write между COMMIT и reread прежде незаметно принимал новую revision: exact committed entity возвращается внутри транзакции, следующая stale правка Busy. (3) Contended text callback прежде синхронно ждал до1s и возвращал Busy: owned worker/coalesced drafts и no-wait discrete commands. Common и native input тесты держат второе writer-соединение и проверяют возврат callback <150ms; normal typing/slow-disk throughput не заявлен. Normalized text/title edit больше не выполняет UPDATE/reindex каждого sibling; legacy repair остаётся. Повторного review не было; deferred Minor нет.

Дополнительные RED: native reopen draft на трёх ОС (CI40059113529), Windows Space (CI40060120818), folder reorder race внутри BEGIN, sparse positions, checkbox rollback, disabled portal session. Lifetime teardown experiment прошёл ещё до гипотетического исправления; UAF не заявлен. Raw logs сохраняют реальные failure/success и не подменяют ручную приёмку.

## Решения и остающиеся gates

- Extension metadata сохраняет форму legacy tables; цена — две небольшие таблицы/joins и несовместимость старых Cliptiq binaries сv2.
- Явные count/text limits сохраняют маленькую память/меню; цена — очень большие коллекции пока неподдержаны, оригиналы не усекаются.
- Portal session закрывается целиком при disabled/deleted folder; цена — явная переавторизация остальных shortcuts.
- Common bounded undo избегает GtkSourceView; цена — grouping отличается от native Clipy, особенно при coalescing.
- Discrete CRUD/move/enable/undo оставлены синхронными с no-wait busy handling; цена — возможна filesystem latency, общий async loading/CRUD пока отсутствует.
- Reviewer не оценивал physical drag/desktop activation/accessibility/DPI/external apps: оставлены manual OPEN; цена — usability/desktop acceptance ещё требуется.
- Реальные OS grants/portal consent UX не доказаны fixtures: manual OPEN; цена — разрешения проверять на настоящих desktop.
- Final CI/size/upload исключены из review verdict: подтверждены отдельно evidence exact source; цена — публикация только после этих проверок.
- T11 XML/later исключены: цена — import/export пока отсутствует. T04–T09 manual OPEN по запросу; цена — release readiness не заявлена.

[API](../../core/SNIPPETS.md), [raw JSON/logs/flags/imports/SHA256](t10-measurements/README.md), [ledger](t10-ledger.md), [ручной checklist](../../tests/manual/snippets.md). Исторические T04–T09 отчёты не переписывались.

Scope note: both paired hosts share current v2 schema and enabled/menu/hotkey filtering. The delta isolates enabling/linking the editor and its repository/controller; it is not the entire historical T09→T10 change.

# T16: OCR и отдельный FTS backend

Автоматическая часть T16 реализована. Runtime `a4deefee84bc14aacae88e63c2d94c59dbb502d8`,
[CI40101383632](https://github.com/Hydrock/Cliptiq/actions/runs/40101383632) —16/16.
Ручная приёмка T04–T15 и интерактивная T16 OPEN; T17 не начат.

Base не включает OCR engine, модели или FTS5. Опции `ocr_index=ON` и
`CLIPTIQ_OCR_ENGINE=native|tesseract` независимы. Индекс находится только в
`index/search.db` профиля; main SQLite connections отключают FTS modules,
схема history сохраняет обычные OCR-поля. Все main-транзакции удерживают общий gate до COMMIT/ROLLBACK;
известные companion файлы удаляются до изменения данных.
Startup Base не открывает индекс, но инвалидирует его. Namespace имеет
owner marker; чужие файлы и symlinks не удаляются. Перестройка использует read snapshot и ограниченную in-memory FTS БД,
завершает snapshot, проверяет revision под gate и публикует disk backup; потерянный индекс
восстанавливается из main. Single-instance scope T13 общий для вариантов.

OCR: один bounded worker, декодирование через T12 codecs; проверки privacy
до декодирования и перед публикацией. ID + digest + generation + nonce
исключают публикацию удалённой/заменённой задачи. Pending/Done/Unavailable/
Failed, пустой Done допустим. Unavailable повторяется при перезапуске
и в bounded60s retry epochs; Pending-результат сохраняется при временном Busy. Текст до 10 000 Unicode grapheme clusters,
выход engine до 1 MiB, image до 40 MP. Ошибка не уничтожает импортированный
OCR; migration NULL-state/non-NULL-text читается как Done. Очередь получает
только retained history и повторно проверяет текущую capture policy.

Vision использует accurate recognition, language correction и automatic
language detection, как закреплённый Clipy. WinRT
распознаёт только при package identity; для EN/RU выполняет отдельные
одноязычные проходы и объединяет результаты (возможны повторы).
Hosted portable CI проверяет отказ,
положительная MSIX приёмка OPEN. Portable Win/Linux используют pinned
Tesseract 5.5.1 / Leptonica 1.85.0, LSTM-only, без OpenMP, curl, archive и
лишних image codecs. Изображение уже декодировано системным codec. EN/RU
модели tessdata_fast 4.1.0: 7 974 826 B, проверяются по SHA256. Никаких
runtime downloads. EN/RU availability native engines зависит от ОС;
correction/detection и качество разных engines не эквивалентны.

Синтетические EN/RU PNG: ожидаются `clipboard` / `Привет`; Tesseract читает
бренд «Cliptiq» как «Cliptig» на английской фикстуре. Это наблюдаемое
ограничение точности, не обход ошибки исполнения. Никакие пользовательские
данные в fixtures или логах не используются.

Публикация держит gate только при записи/rename; интерактивные main writes
при конфликте сразу возвращают Busy, а не ждут перестройки. Main использует
WAL после проверки identity/schema, чужие БД не изменяются. Recovery
проверяется для rollback journal и WAL. Цена in-memory перестройки — до128MiB
индексной памяти сверх остальных данных; максимальный RSS не замерен.

FTS: unicode61, literal phrase, query ≤1024 B, ≤1000 результатов, ≤100000
history/10000 snippets, общий текст ≤64 MiB, database ≤128 MiB. Text history
берётся из первых 64 KiB UTF8/UTF16 representation, до 10000 clusters;
OCR до 10000 clusters; disabled snippets/folders не индексируются.
Search field и OCR copy actions не добавлены (вне T16).


## B01/B02 coverage

| ID | Классификация и evidence |
|---|---|
| B01 | Native Vision accurate/correction/automatic detection, async state и10k limit проверены EN/RU на macOS ARM/Intel; Windows portable/Linux адаптированы на pinned Tesseract; WinRT package-negative проверен, positive MSIX pending. Privacy/state/queue tests и5 native lanes. |
| B02 | Адаптация: main ordinary schema + disposable companion вместо FTS triggers в main. History text/OCR и snippets проверены через rebuild/query, invalidation, race, corruption и Base roundtrip. Ограничения размера/длины явные; Base исключает FTS; нового UI нет. |

## Проверки

Итоговый CI: Base Debug/MinSizeRel на Linux57/57, Windows47/47,
macOS ARM/Intel44/44; ASan/UBSan Linux57/57, macOS44/44 и Windows ASan47/47.
Пять optional OCR lanes прошли по 5/5 tests. Vision ARM/Intel и Tesseract
Linux/Windows распознали EN/RU; native Windows проверяет отказ без package
identity. Cross-process indexed→Base→indexed, Base clear и crash recovery
прошли на всех optional lanes. Linux CMake installed layout дополнительно
проверен с реальным распознаванием из установленных моделей. Два прежних
Realm migration и два Sparkle source-build задания также зелёные.

Privacy tests: request off, transient, concealed, excluded app, unknown source,
Image off, remote macOS, unavailable capability, cancel, delete, supersession,
privacy revocation before publish, engine UTF8 error, empty Done, Unicode limit.
FTS: snippets enable/delete, UTF16 history, OCR migration retention, corruption,
stale revision, unowned file retention, transaction clear/rebuild race и быстрый
Busy вместо ожидания gate. Неподходящая чужая БД сохраняется byte-for-byte.
Crash fixtures проверяют rollback journal и WAL.

Свежий reviewer нашёл 1 Critical + 3 Important: stale index after deletion,
UI wait, неверный installed model path, отсутствие retry Unavailable.
Каждый воспроизведён RED и исправлен в одном pass, затем полная регрессия
зелёная; повторный review не запускался. Один Minor отложен: capability
label объявляет Available до фактического probe (engine корректно возвращает
Unsupported). Все dispositions review сохранены отдельно.

[Evidence](t16-measurements/), [сводка размеров](t16-measurements/summary.json),
[CI metadata](t16-measurements/ci-run.json), [проверка исходников](t16-measurements/source-verification.json).
Проверены 1306 SHA256 исходников из 9 отчётов против 181 Git blob указанного runtime.
Локальные логи — дополнительная RED/GREEN evidence; канонические размеры ниже — CI.
CI не заменяет чистые потребительские ВМ: таких ВМ пользователь не имеет.

## Размеры (байты)

Base production executable, без installer/system dependencies:

| ОС | Executable | ZIP executable | Добавка к T15 |
|---|---:|---:|---:|
| macOS ARM64 | 1,898,472 | 775,416 | 22,144 |
| macOS x64 | 1,881,264 | 806,369 | 14,088 |
| Windows x64 | 1,685,504 | 858,535 | 16,384 |
| Linux x64 | 1,517,680 | 716,281 | 8,192 |

Полный app-owned OCR/index pack: приложение/bundle, EN/RU где нужны,
license/notices. OS framework/system package closure сюда не входит.

| Вариант | Все файлы pack | ZIP pack |
|---|---:|---:|
| macOS ARM64 Vision+FTS | 2,125,070 | 885,427 |
| macOS x64 Vision+FTS | 2,115,542 | 920,976 |
| Windows x64 Tesseract+FTS+EN/RU | 11,370,726 | 5,311,293 |
| Linux x64 Tesseract+FTS+EN/RU | 12,697,014 | 5,780,186 |
| Windows x64 WinRT+FTS, identity-negative CI only | 1,910,667 | 979,921 |

EN/RU модели занимают 7 974 826 B; не скрыты из общего размера. Windows
Tesseract imports только OS DLL, без VC redistributable/DLL OCR runtime.
Linux использует прежние desktop/system libraries; Tesseract/Leptonica статические.
Mac использует установленный Vision; OS language availability нельзя считать
нулевым clean-machine cost. Signing/notarization, MSIX identity, реальные
install/uninstall, совмещённые release-пакеты optional migration/Sparkle и
обновления этих пакетов остаются соответствующими manual/T17 gates.

## Сборка и проверка

Base: существующие presets. Optional:
`python tools/test-ocr.py --engine native` (Mac/Windows) или
`python tools/test-ocr.py --engine tesseract` (portable Windows/Linux).
Скрипт собирает pinned offline dependencies, отдельные Base/indexed варианты,
проверяет fixtures/roundtrip и формирует полный pack/report. `--reuse-deps`
предназначен для повторной локальной проверки hash-checked development prefix.
Прямые CMake опции: `-Docr_index=ON -DCLIPTIQ_OCR_ENGINE=tesseract`;
предварительно `python packaging/ocr/build.py`. Base: оба feature выключены.
Модели рядом с executable в `tessdata`; Linux install — `bin/tessdata`.
`CLIPTIQ_OCR_MODELS` — явный путь к тому же проверяемому EN/RU pack.
Никаких downloads при запуске приложения.

Источники: [Vision](https://developer.apple.com/documentation/vision/vnrecognizetextrequest),
[Windows OCR package identity](https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr),
[Tesseract API](https://tesseract-ocr.github.io/tessapi/5.x/a02438.html),
[SQLite FTS5](https://www.sqlite.org/fts5.html).

Свежий review и полный список ограничений: [review](t16-review.md).
Ручная T16: [чеклист](../../tests/manual/t16-ocr-fts.md), OPEN.

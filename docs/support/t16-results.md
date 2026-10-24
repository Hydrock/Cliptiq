# T16: OCR и отдельный FTS backend

Работа в процессе проверки CI. Ручная приёмка T04–T15 OPEN; T17 не начат.

Base не включает OCR engine, модели или FTS5. Опции `ocr_index=ON` и
`CLIPTIQ_OCR_ENGINE=native|tesseract` независимы. Индекс находится только в
`index/search.db` профиля; main SQLite connections отключают FTS modules,
схема history сохраняет обычные OCR-поля. Все main revision writes перед
commit удаляют известные файлы companion под общим межпроцессным lock.
Startup Base не открывает индекс, но инвалидирует его. Namespace имеет
owner marker; чужие файлы и symlinks не удаляются. Перестройка использует
read snapshot и проверку revision перед публикацией; потерянный индекс
восстанавливается из main. Single-instance scope T13 общий для вариантов.

OCR: один bounded worker, декодирование через T12 codecs; проверки privacy
до декодирования и перед публикацией. ID + digest + generation + nonce
исключают публикацию удалённой/заменённой задачи. Pending/Done/Unavailable/
Failed, пустой Done допустим. Текст до 10 000 Unicode grapheme clusters,
выход engine до 1 MiB, image до 40 MP. Ошибка не уничтожает импортированный
OCR; migration NULL-state/non-NULL-text читается как Done. Очередь получает
только retained history и повторно проверяет текущую capture policy.

Vision использует accurate recognition и language correction. WinRT
распознаёт только при package identity; hosted portable CI проверяет отказ,
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

FTS: unicode61, literal phrase, query ≤1024 B, ≤1000 результатов, ≤100000
history/10000 snippets, общий текст ≤64 MiB, database ≤128 MiB. Text history
берётся из первых 64 KiB UTF8/UTF16 representation, до 10000 clusters;
OCR до 10000 clusters; disabled snippets/folders не индексируются.
Search field и OCR copy actions не добавлены (вне T16).

## Проверки

Локально Base Linux: 57/57 regression. Focused indexed Linux OCR/FTS/queue/
native OCR: 4/4; EN/RU распознаны. Windows native: focused 4/4, package
identity rejection, indexed→Base→indexed / clear / crash recovery прошли.
Размеры и итоговые CI ссылки будут добавлены после завершения проверки.
CI не заменяет чистые потребительские ВМ: таких ВМ пользователь не имеет.

Источники: [Vision](https://developer.apple.com/documentation/vision/vnrecognizetextrequest),
[Windows OCR package identity](https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr),
[Tesseract API](https://tesseract-ocr.github.io/tessapi/5.x/a02438.html),
[SQLite FTS5](https://www.sqlite.org/fts5.html).

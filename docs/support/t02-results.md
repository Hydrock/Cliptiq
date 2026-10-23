# T02: постоянная история и общее ядро

23 октября 2026. **T02 завершён. C++ с нативными оболочками принят пользователем. T03–T17 не начаты.**

Реализованы C++17 static core, pinned SQLite 3.53.4, schema v1 и CMake presets. История сохраняет ordered representations, item boundaries, platform/native type, encoding и исходные bytes, source confidence/flags, отдельные UUID/digest и created/last-used. Текст, RTF/HTML и бинарные representations не сводятся к одной строке. Сниппеты/настройки отделены; history clear каскадно удаляет representations/assets и не трогает их.

Все duplicate modes работают после переключения настроек; SHA256 — length-prefixed, collision-safe comparison выполняется по реальным representations. Добавлены count/payload limits, pruning по выбранной сортировке, touch без изменения created и persisted whole-history cleanup. Периодическая очистка — не TTL. Перед записью и материализацией сохранённых данных проверяются лимиты. Native adapters должны проверять внешние clipboard streams до allocation.

## Проверки

[Финальный CI 39941083332](https://github.com/Hydrock/Cliptiq/actions/runs/39941083332), source `004a0d53aa7ac0a484525d2a379737d9d3191e39`: **5/5 jobs PASS** — Windows, Ubuntu, macOS arm64/x86_64, Clang ASan/UBSan. На каждой ОС выполнены Debug и MinSizeRel CTest: 29 history scenarios + process interruption recovery. Отдельно local Windows Debug/Release и Linux GCC ASan/UBSan PASS; 7 tool tests Linux PASS, Windows symlink case skipped по privilege.

Проверены reopen, SQLite_FULL через реальный max_page_count, ReadOnly через query_only, corrupt/foreign/future DB без автоматической замены, rollback после ошибки вставки и после аварийного выхода процесса с flushed dirty pages/nonempty rollback journal. Это fault-injection и hosted CI; stock clean VM, аппаратные сбои и реальные ACL/permissions всех ОС этим не подменяются.

Независимое ревью нашло три дефекта: чтение persisted payload до проверки лимитов, UTF-16 whitespace и возврат уже удалённого при pruning ID. Исправлены с регрессионными тестами; полный suite повторён. SHA256 проверен стандартными vectors empty/abc/million-a. [Подробный ledger](t02-ledger.md).

## Размер связанного ядра

Это `core_probe`: executable, вызывающий весь T02 public API, со статическим SQLite. Без UI и clipboard capture; не полный продукт, не installer и не чистый incremental cost к T01. ZIP содержит один executable, не release package/licenses. Размеры static archives в JSON — промежуточные файлы сборки и не суммируются с executable.

| Target | Probe bytes | ZIP bytes |
|---|---:|---:|
| Windows x64, static CRT | 843,776 | 481,955 |
| Linux x64 | 789,648 | 426,680 |
| macOS arm64 | 881,800 | 454,297 |
| macOS x86_64 | 867,024 | 475,048 |

Фактические JSON с source hashes/toolchain flags и raw CTest logs: [t02-measurements](t02-measurements). MinSizeRel, static CRT Windows; без FTS5, browser runtime, сетевых библиотек, XML/Unicode framework и crypto DLL. SHA256 добавлен небольшим внутренним кодом. Финальные product budgets не объявлены достигнутыми; полная offline closure здесь неизвестна.

## Контракты и границы

[Core API и воспроизведение](../../core/README.md). No-op отфильтрованного/blank/duplicate capture возвращает пустой optional; также пустой результат, если текущая сортировка/квота сразу вытеснила принятый элемент. Публичный возвращённый ID всегда существует после commit. Любая ошибка записи возвращает Error и откатывает транзакцию.

Cleanup хранит last/interval в БД; первое включение/смена интервала/перевод часов назад переустанавливает отсчёт. При просроченном tick после sleep очищается вся история, включая свежие entries. Ограничения metadata и числа representations дополняют payload quota; квота payload не является лимитом точных SQLite disk bytes. Компактация — отдельный worker-call, secure erase не обещается.

Native shells, user-directory ACL, process single-instance lock и background scheduling потребителей остаются последующим этапам. Privacy capture filtering — T03; snippet repository/editor — T09; companion FTS index и OCR — T16. T02 лишь резервирует data_revision/обычные nullable OCR fields, не добавляет index/FTS. Никаких сетевых операций в runtime ядре нет.

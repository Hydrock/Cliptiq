# T01: native footprint spike

Это эксперимент, не готовый Cliptiq. Пользователь подтвердил имя Cliptiq. Один и тот же нативный C++ UI host связан с двумя альтернативными core: C++17 и Rust через C ABI. Так сравнивается вклад ядра, а не неизвестное количество функций разных UI frameworks.

Одинаковая нагрузка: SQLite 3.53.4, 30 UTF-8 text entries, PNG, HTML+text, RTF+text (33 записи / 35 representations), два snippets, экранированный совместимый XML export. CTest проверяет raw-byte readback и обе реализации; build runner сравнивает XML byte-for-byte. FTS5 lane реально создаёт virtual table и выполняет MATCH. SQLite single-threaded только для spike; это не production-конфигурация.

Native shell: Win32, AppKit, GTK3/GTK4. Windows interactive имеет tray, shortcut Ctrl+Alt+Shift+F12, popup, fixture copy. Mac interactive имеет status menu и fixture copy. Linux interactive пока только окно и text fixture copy; tray/portal/Wayland capture/hotkeys не реализованы. У всех нет фонового мониторинга реального буфера. Автотесты не заменяют содержимое пользовательского clipboard. `--interactive` меняет его только по нажатию пользователя.

## Запуск

Нужны C++17 compiler, Rust >=1.75, CMake >=3.22 и Ninja; Windows MSVC Build Tools, Linux GTK3/GTK4 dev packages/pkg-config, Mac Xcode tools. Python >=3.10, без pip dependencies. SQLite загружается в `.toolchains` с проверкой SHA256; vendored source в Git не добавляется.

```text
python -m unittest discover -s tests/tools -v
python tools/build-footprint.py
python tools/build-footprint.py --fts5
python tools/build-footprint.py --dynamic-crt       # Windows only
python tools/build-footprint.py --gtk4              # Linux only
python tools/build-footprint.py --lto               # compare C/C++ LTO separately
python tools/build-footprint.py --ocr               # generated-image OCR probe
xvfb-run -a python3 tools/build-footprint.py         # Linux headless
```

На Windows runner сам загружает VsDevCmd environment в память процесса. Путь с пробелами поддержан. MSVC /O1 + linker dead stripping; Rust opt-level=z, panic=abort, thin LTO, codegen-units=1. C++ cross-unit LTO не включён — это честно отражённый первый baseline, не доказанный минимум каждого языка. Rust std staticlib не поставляется целиком: измеряется связанный executable. Windows CRT сравнивается отдельно /MT и /MD.

`out/footprint/<lane>`: cpp/rust JSON, ZIP, distribution directories, dependency lists и build log. `build/footprint/<lane>`: экспериментальные executables, XML fixtures и intermediates. Все generated/build/dependency directories игнорируются Git; исходники и отобранные JSON результатов сохраняются отдельно.

Для GUI на Windows: `build/footprint/windows-x86_64-base-mt/probe_cpp.exe --interactive`. Для Rust заменить имя на probe_rust.exe. Для Linux/Mac аналогичный executable без .exe. Это демонстрация fixtures, не менеджер пользовательской истории.

## Что означают измерения

- `app_bytes`: executable + одинаковый NOTICE, без данных пользователя, compiler, object files, symbols.
- `archive_bytes`: реально созданный ZIP/deflate9, фиксированные timestamps.
- `idle_memory_bytes`: медиана пяти RSS/working-set samples после setup и 300 ms; UI hidden, не размер приватной памяти. OCR lane использует два samples в локальной проверке и пять по умолчанию в CI; это host RSS после OCR, без завершившегося Tesseract child process и без памяти внешних OS OCR services. Поэтому это не полная RAM OCR-системы.
- `startup_ms`: медиана main→SQLite fixture+hidden native window. **Не cold start:** загрузка executable/OS loader до main не учтена. Это не обещание скорости готового продукта.
- `linked_library_bytes` Linux: сумма реальных файлов ELF closure `ldd`, включая системные библиотеки, без fonts/plugins/data. Это **не** clean-machine dependency delta.
- `dependency_delta_bytes` и `total_offline_bytes`: null, пока clean-machine installation не измерена. Ноль вместо неизвестного запрещён.
- `ocr_bytes=0` в Base означает «OCR отсутствует», не «OCR бесплатно». В OCR lane стоимость внешних engine/models неизвестна и `ocr_bytes=null`; добавленный native integration code уже учтён в app_bytes. Generated-image OCR probe использует Windows.Media.Ocr / macOS Vision / внешний Linux Tesseract. Полная упаковка OCR, updater/migration и полный UI отсутствуют. Windows OCR lane использует C++20 из-за современного C++/WinRT SDK; базовые lanes остаются C++17.

У каждой ОС отдельные compiler/version данные; WSL/Xvfb не полноценная сессия GNOME/KDE и не proof Wayland support. macOS lane подготовлен в CI, но результат существует только после фактического запуска на Mac. FTS5/CRT/GTK измерения — компоненты решения T01; финальная архитектура и size ceiling остаются предварительными до полного closure и OCR lanes.

Эксперимент не переносится в core автоматически. Для product code действует отдельный T02+ план после закрытия архитектурных вопросов.

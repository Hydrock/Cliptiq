# ADR-T01: первые сравнительные прототипы Cliptiq

Дата: 23 октября 2026. **Имя Cliptiq подтверждено пользователем.** Статус архитектуры: предварительная рекомендация, не завершённый production benchmark. Исследование и код доступны в ветке `codex/portability-plan` репозитория [Hydrock/Cliptiq](https://github.com/Hydrock/Cliptiq/tree/codex/portability-plan).

## Что действительно сделано

Созданы и собраны два взаимозаменяемых ядра C++17/Rust, одинаковый SQLite 3.53.4 и общий нативный UI host. Windows: MSVC + Win32; Linux: GCC + GTK3/GTK4; macOS arm64: AppleClang + AppKit в GitHub Actions. Общий UI намеренно написан на C++/Objective-C++, чтобы измерять разницу ядер с одинаковой нагрузкой, а не сравнивать неодинаковые UI frameworks.

Оба ядра проходят raw-byte round trip для 33 fixture entries / 35 representations, PNG, HTML/RTF, Unicode и snippets XML. XML экспорты совпадают побайтно. FTS lane выполняет реальный MATCH. Автоматические проверки не читают и не меняют пользовательский clipboard. Native lifecycle smoke закрывает окно и требует завершения процесса за 8 sec. Отдельная OCR lane распознаёт сгенерированное слово Cliptiq на Windows, Linux и Mac.

Полного приложения ещё нет: нет наблюдения пользовательской истории, всех настроек, полноценного snippet editor, migration/updater и фоновой Linux tray/portal/Wayland integration. Маленький prototype executable не доказывает размер будущего feature-complete приложения.

## Первые локальные размеры и macOS CI

Значения включают executable + одинаковый NOTICE; MiB = 1 048 576 bytes. Windows/Linux — локальные отчёты, macOS — CI arm64; размеры между разными ОС/CPU нельзя трактовать как сравнение качества компиляторов. Точные toolchain versions и flags сохранены в JSON evidence.

| Среда / Base | C++ app | Rust app | C++ ZIP | Rust ZIP |
|---|---:|---:|---:|---:|
| Windows 11 x64, static CRT | 0,898 MiB | 0,996 MiB | 0,507 MiB | 0,558 MiB |
| Ubuntu 24.04 WSL x64, GTK3 | 0,695 MiB | 1,011 MiB | 0,378 MiB | 0,528 MiB |
| macOS 15 arm64, AppKit, CI | 0,800 MiB | 1,166 MiB | 0,417 MiB | 0,565 MiB |

Незначительные изменения lifecycle/CRT/toolchain и повторная упаковка меняют последние байты. Финальный CI run на `265d6f0` и его JSON — отдельный воспроизводимый набор, а не попытка представить цифры разных запусков одним экспериментом. [Исправленная CI-матрица](https://github.com/Hydrock/Cliptiq/actions/runs/39931524164), [первый run с обнаруженной GTK-регрессией](https://github.com/Hydrock/Cliptiq/actions/runs/39931197789).

### Что сильнее влияет на поставку

- **Windows CRT:** /MD C++ executable+NOTICE = 688 038 bytes, /MT = 941 478 bytes в первом локальном наборе. Но только три непосредственно требуемых MSVC DLL из установленного redist занимают ещё **872 240 bytes** (msvcp140.dll 643 512; vcruntime140.dll 178 616; vcruntime140_1.dll 50 112). EXE+эти DLL уже больше /MT. Это инвентарь конкретного toolchain, не обещание полного installer closure для любой Windows. Поэтому исходная рекомендация — static CRT, без требования VC runtime download.
- **FTS5:** добавляет около 140–170 KiB app-owned к этому workload в зависимости от target/toolchain; код с индексом не бесплатный. Base пока не требует пользовательского поиска. Пересоздаваемый companion index остаётся отдельным решением дизайна.
- **GTK3/GTK4:** app binary почти одинаков, поскольку toolkit динамический. Локальная ELF `ldd` closure: GTK3 **35 319 848 bytes**, GTK4 **37 021 200 bytes**, включая OS libs, без fonts/data/plugins. Это не package-manager installation delta, не полная offline-поставка и не универсальный победитель GTK3; в GNOME GTK4 может уже присутствовать, в KDE может отсутствовать оба.
- **C/C++ LTO:** отдельная lane измерена. На Windows уменьшение небольшое, на Linux в этом workload практически отсутствует. Rust thin-LTO включён в обоих вариантах. Нельзя объявлять первый baseline теоретическим минимумом каждого языка.
- **OCR:** native интеграция увеличила C++ app до ~0,925 MiB Windows и ~0,802 MiB Mac; Linux использует внешний Tesseract. Уже только EN+RU model files занимают **7 974 826 bytes**, без engine/Leptonica и прочих runtime dependencies. Локальный tesseract executable — 39 320 bytes, потому что основной код находится в .so: выдавать эту цифру за полный OCR size нельзя.

## OCR: наблюдение и контракт поддержки

На текущей Windows 11 portable-проба Windows.Media.Ocr **успешно распознала текст**. В исходном плане риск был сформулирован через официальный список WinRT API, для которых требуется package identity. Практический успех на одном build не отменяет [этот опубликованный контракт Microsoft](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/winrt-api-desktop-app-support). Production decision: проверять engine/language availability, не обещать весь Windows 10/11 portable support без дополнительных тестов; packaged lane или fallback pack сохраняются как варианты. Это уточнение исследования, а не повод включить модели в Base автоматически.

macOS Vision и Linux Tesseract также распознали generated fixture. OCR status отражает только эту операцию; одинаковой точности, language detection, RU quality и полной offline deployment ещё не доказано. RAM в OCR JSON — host process RSS после операции; память Tesseract child process и внешних OS OCR services в неё не входит. Startup — main→fixture+window, не cold launch, не GUI latency готового продукта.

## Предварительное решение

Продолжать проектирование **C++ core + native shells**. Во всех измеренных Base вариантах C++ core дал меньший app-owned footprint, Rust не устранил необходимость native adapters. Это основание для данного workload, не утверждение «Rust всегда больше». Для Linux пока нет достаточного основания навсегда выбрать GTK3 вместо GTK4: реальный target desktop и runtime delta важнее 0 KiB разницы EXE. Windows предпочесть static CRT как исходный self-contained вариант; Mac — отдельные CPU builds, не universal по умолчанию.

Предложенные пределы 5 MiB download / 10 MiB app-owned остаются **целями**, не достигнутым размером полного продукта. Не замораживать final size acceptance по маленькому spike.

## Что ещё нужно закрыть в T01

1. Clean-machine offline closure: ОС + missing toolkit/CRT/model packages отдельно, включая данные и plugins; текущие GitHub runner/WSL — не чистые целевые desktop VM.
2. Mac Sparkle/update integration footprint; подписанная установленная сборка, macOS x86_64 lane. Сейчас только AppKit/Vision arm64 prototype.
3. Native Linux tray/hotkey/Wayland capability spike; Xvfb/GTK window не доказывает GNOME/KDE/Sway compatibility.
4. OCR full pack/reproducible redistribution, RU/EN engine availability и quality; Windows supported packaging policy.
5. Стабилизировать dependency/toolchain pins для product build и утвердить численные budgets по всем трём категориям: download, app-owned и offline total.

T01 начат и дал реальные comparative builds на трёх ОС, но **не отмечен целиком завершённым**. T02 product core не начат. Snapshot reports, checksum/flags и dependency inventories находятся в `docs/support/measurements/`; скрипты воспроизведения — в `tools/`.

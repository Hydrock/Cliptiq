# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md

## Scope

Пользователь 2026-10-23 подтвердил имя Cliptiq, разрешил загрузку документов в GitHub и выполнение первого этапа T01. T02–T17 не входят в текущую реализацию.

- BASE: 515beec (документы). Ветка codex/portability-plan, origin Hydrock/Cliptiq.
- Ruling: работать в существующей выделенной feature branch — checkout содержит только эту задачу, пользователь запросил продолжение здесь; дополнительный worktree не нужен для разрешённого spike.
- Ruling: T01 — throwaway measurement spike, не product scaffold; минимальные тесты fixture/core parity и строгие tests измерительного инструмента. C++ UI host одинаковый в C++/Rust вариантах, чтобы изолировать вклад ядра; не выдавать результат за сравнение всех возможных Rust UI frameworks.
- Pre-flight T01→T17: size-report JSON поля сохраняют имена плана; неизвестное измерение null, не 0. T02+ контракты пока не реализуются.
- Документы committed 515beec и после подтверждённого пользователем входа отправлены в origin/codex/portability-plan. Учётные данные не сохраняются в репозитории/логах.
- Доступны MSVC 14.51 Build Tools и Rust 1.99 MSVC; CMake/Ninja находятся в VS, не в PATH. SQLite 3.53.4 загружен из sqlite.org, SHA256 1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d.

Historical checkpoint — Task T01: in progress. Windows и Ubuntu 24.04 WSL prototypes выполнены; macOS arm64 Base/FTS/OCR выполнены в CI. Clean-machine closure/Sparkle/native Linux capability tests ещё не выполнены.

## Проверки и решения

- Tools RED: size-report.py отсутствовал — 5 tests failed; GREEN: реализован реальный ZIP/size/null accounting. Regression RED/GREEN: aggregation self-reference и unknown OCR cost. Текущий suite 7 tests: Linux все PASS, Windows 1 symlink skip из-за отсутствия соответствующей привилегии.
- Native core: SQLite 33 entries / 35 representations, PNG/HTML/RTF bytes, Unicode snippets/XML, C++=Rust XML, FTS MATCH. CTest четыре cases в каждой текущей lane: fixture_cpp/lifecycle_cpp/fixture_rust/lifecycle_rust.
- Ruling: CLI build runner вместо root product CMake presets — T01 isolated spike, не scaffolding будущего core. Эквивалентные CMake flags и exact versions включены в evidence.
- Ruling: Windows C++/WinRT OCR lane использует C++20, Base core C++17 — новый MSVC отвергает experimental coroutine header старого пути SDK. Это видно только в OCR lane, не скрытая смена общего стандарта.
- Ruling: Linux apt Rust1.75 и Windows Rust1.99 — разные toolchains в локальных наборах, не делать межплатформенных performance conclusions; GitHub runner версии записаны в отдельных reports.
- Review: свежий gpt-6-astra обнаружил GTK window-close hang и macOS release-on-close dangling pointer. Исправлено; added timeout lifecycle regression. Regression впервые обнаружил, что gtk_window_close игнорирует hidden unrealized window; smoke теперь реализует окно в Xvfb, закрывает настоящий surface. GTK3/GTK4 PASS после исправления.
- Первый CI af3dc97 / run39931197789: Mac 3 lanes PASS, Linux failed lifecycle; локально исправлено и pushed 265d6f0. Повторный run39931524164 относится к исправлению.
- OCR generated image: локальные Windows WinRT и Linux Tesseract recognized; Mac Vision recognized в CI. Успех Windows portable на конкретной Windows11 не расширяет автоматически официальный support contract; scope уточнён в ADR.
- Независимый follow-up review: серьёзных новых дефектов не найдено; обновлена README трактовка OCR host memory (не включает child/service memory).
- Historical checkpoint: Task T01 НЕ complete: остаются пункты ADR. T02–T17 не выполнялись.

## Closure 2026-10-23

T01 complete within user-accepted measurement scope; T02–T17 not started. User explicitly accepted no stock Windows/macOS VM, requiring clear CI limitation. No clean VM PASS claimed.

Final footprint CI 39936634738: 12/12 success; profile CI 39936634716: 3/3 success; Sparkle CI 39934838419: both CPU success. Previous apt failures diagnosed as slow Azure mirror, bounded official-source/lane-specific bootstrap fixes verified by final green matrix.

Independent review P2: cached old runtime DEBs could inflate download totals after revisions. Fixed unique per-run directories; GTK3/GTK4/OCR fresh offline Docker measurements repeated. GTK3 DEB install/self-test/lifecycle/remove verified without network. Raw package hashes retained. Summary and explicit deferred production gates: t01-results.md.

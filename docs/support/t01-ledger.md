# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md

## Scope

Пользователь 2026-10-23 подтвердил имя Cliptiq, разрешил загрузку документов в GitHub и выполнение первого этапа T01. T02–T17 не входят в текущую реализацию.

- BASE: 515beec (документы). Ветка codex/portability-plan, origin Hydrock/Cliptiq.
- Ruling: работать в существующей выделенной feature branch — checkout содержит только эту задачу, пользователь запросил продолжение здесь; дополнительный worktree не нужен для разрешённого spike.
- Ruling: T01 — throwaway measurement spike, не product scaffold; минимальные тесты fixture/core parity и строгие tests измерительного инструмента. C++ UI host одинаковый в C++/Rust вариантах, чтобы изолировать вклад ядра; не выдавать результат за сравнение всех возможных Rust UI frameworks.
- Pre-flight T01→T17: size-report JSON поля сохраняют имена плана; неизвестное измерение null, не 0. T02+ контракты пока не реализуются.
- Документы committed, push пока не выполнен: GitHub credential отсутствует, пользователь уведомлён. Не создавать токены и не сохранять секреты в репозитории.
- Доступны MSVC 14.51 Build Tools и Rust 1.99 MSVC; CMake/Ninja находятся в VS, не в PATH. SQLite 3.53.4 загружен из sqlite.org, SHA256 1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d.

Task T01: in progress. macOS/Linux/clean-machine measurements ещё не выполнены.

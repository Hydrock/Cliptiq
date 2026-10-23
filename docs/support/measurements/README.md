# Измерительные evidence snapshots

- `ci-265d6f0`: JSON и dependency inventories из [GitHub Actions run 39931524164](https://github.com/Hydrock/Cliptiq/actions/runs/39931524164), source commit `265d6f097d0a6a552085cb2030f1329b4c8d411e`. Это основной сравнимый набор CI для каждой платформы.
- `local-windows-wsl`: промежуточные локальные Windows 11/MSVC/Rust1.99 и Ubuntu24.04 WSL/GCC/Rust1.75 измерения, включая отдельную C/C++ LTO lane. Конкретные версии и flags внутри JSON. Не смешивать их в один performance benchmark с CI или между разными архитектурами.

`app_bytes` содержит executable+NOTICE, `archive_bytes` — фактический ZIP. Hashes относятся к сгенерированным artifacts, они не хранятся в Git. `dependency_delta_bytes` / `total_offline_bytes` неизвестны до clean desktop deployment и остаются null. Linux ELF library closure не равно installed package delta. OCR costs null в OCR lane, поскольку native OS models/external Tesseract pack не включены в сам executable.

Startup — main-to-fixture-and-hidden-window, не cold start; RSS/working set — host process, не private bytes/полная OCR-system RAM. Core fixtures не тестируют реальный пользовательский clipboard. Отчёт: [ADR](../architecture-decision.md). Ни один файл здесь не является готовой сборкой Cliptiq.

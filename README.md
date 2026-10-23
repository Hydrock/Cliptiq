# Cliptiq

Планируемый компактный менеджер буфера обмена для macOS, Windows и Linux на основе поведения [Clipy](https://github.com/Clipy/Clipy).

Реализовано C++17 ядро постоянной истории и privacy/capability gate (T02–T03). Добавлен macOS clipboard backend (T04); пользовательский интерфейс и готовые пользовательские сборки ещё не созданы.

[Исследование, матрица функций и план переноса](docs/README.md)

[Сборка ядра и контракты](core/README.md) · [Результаты T02](docs/support/t02-results.md)

[Privacy/capability contract](core/POLICY.md) · [Результаты T03](docs/support/t03-results.md)

[macOS backend и границы проверки](platform/macos/README.md) · [Результаты T04](docs/support/t04-results.md)

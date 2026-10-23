# Cliptiq

Планируемый компактный менеджер буфера обмена для macOS, Windows и Linux на основе поведения [Clipy](https://github.com/Clipy/Clipy).

Реализовано C++17 ядро постоянной истории и privacy/capability gate (T02–T03). Добавлены macOS, Windows и Linux X11/Wayland clipboard backends (T04–T06). Добавлены модель меню и нативные popup/tray/launcher (T07); готовых пользовательских пакетов пока нет.

[Исследование, матрица функций и план переноса](docs/README.md)

[Сборка ядра и контракты](core/README.md) · [Результаты T02](docs/support/t02-results.md)

[Privacy/capability contract](core/POLICY.md) · [Результаты T03](docs/support/t03-results.md)

[macOS backend и границы проверки](platform/macos/README.md) · [Результаты T04](docs/support/t04-results.md)

[Windows backend и границы проверки](platform/windows/README.md) · [Результаты T05](docs/support/t05-results.md)

[Linux backend и ограничения](platform/linux/README.md) · [Результаты T06](docs/support/t06-results.md)

[T07: меню, native UI, проверки и размеры](docs/support/t07-results.md). Ручная приёмка T04–T06 и интерактивная T07 открыта. T08 не начат.

# Cliptiq: исследование и план

Рабочий репозиторий: [Hydrock/Cliptiq](https://github.com/Hydrock/Cliptiq). Дата: 23 октября 2026. Исследован Clipy `develop` на ревизии `71b0e82623d5c73b4b717d81233eb78db8bbe258`.

1. [Проверка Cliptiq и ClipClap](research/2026-10-23-naming.md): произношение, смысл, совпадения, что ещё не подтверждено.
2. [Полная матрица переноса](research/2026-10-23-clipy-audit.md): каждая обнаруженная функция, source reference, macOS/Windows/X11/Wayland, критерий приёмки и задача.
3. [Предлагаемый дизайн](superpowers/specs/2026-10-23-cliptiq-design.md): архитектура, альтернативы, данные, ограничения, размер и варианты поставки.
4. [План реализации: 17 задач](superpowers/plans/2026-10-23-cliptiq-port.md): файлы, контракты, проверки, зависимости, этапы и release gate.

T02 создаёт общее C++ ядро истории (см. [контракты](../core/README.md)). Пользователь подтвердил Cliptiq и разрешил этап T01: [прототипы](../experiments/footprint/README.md), [результаты и решение](support/architecture-decision.md). В документах дизайна числа size ceiling — предложенные цели; реальные измерения вынесены отдельно. Команды T10–T17 остаются инструкциями будущему исполнителю.

Имя Cliptiq подтверждено. Открытые решения: точные size budgets; OCR/diagnostics distribution; поддерживаемые Wayland desktops и необходимые integrations. ClipClap не принят как замена.

Исходная исследовательская копия `reference/Clipy` исключена из Git. Она не входит в поставку и не должна превращаться в nested repository внутри Cliptiq.

T01 закрыт в согласованных границах: [итоговые замеры и ограничения](support/t01-results.md). Пользователь принял C++ с нативными оболочками и разрешил T02.

T02 завершён: [постоянная история, CI и размеры](support/t02-results.md). Последующие этапы отражены ниже.

T03 завершён: [privacy policy, capability contract, CI и добавка размера](support/t03-results.md). T04 описан ниже.

T04: [macOS backend, privacy integration и замеры](support/t04-results.md). Автоматическая часть реализована; ручная приёмка TextEdit/Safari/Finder/Preview и реальных permission dialogs остаётся открыта. T05 описан ниже.

T05: [Windows backend, privacy integration и размеры](support/t05-results.md). Автоматическая реализация подготовлена; ручная Windows app-to-app приёмка открыта. T04 manual остаётся открытой.

T06: [Linux X11/Wayland, privacy, CI и размеры](support/t06-results.md). Автоматическая часть реализована; Linux desktop/clean-install manual gates открыты. T04/T05 manual OPEN. T07 автоматическая часть реализована: [меню, native UI, CI и размеры](support/t07-results.md). T04–T06 manual и T07 interactive OPEN; T08 автоматическая часть реализована: [вставка, CI и размеры](support/t08-results.md); ручная app-to-app приёмка OPEN. T09 автоматическая часть реализована: [настройки, shortcuts, конфликты, CI и размер](support/t09-results.md). Ручная T04–T09 OPEN. T10 не начат.

# Cliptiq: исследование и план

Рабочий репозиторий: [Hydrock/Cliptiq](https://github.com/Hydrock/Cliptiq). Дата: 23 октября 2026. Исследован Clipy `develop` на ревизии `71b0e82623d5c73b4b717d81233eb78db8bbe258`.

1. [Проверка Cliptiq и ClipClap](research/2026-10-23-naming.md): произношение, смысл, совпадения, что ещё не подтверждено.
2. [Полная матрица переноса](research/2026-10-23-clipy-audit.md): каждая обнаруженная функция, source reference, macOS/Windows/X11/Wayland, критерий приёмки и задача.
3. [Предлагаемый дизайн](superpowers/specs/2026-10-23-cliptiq-design.md): архитектура, альтернативы, данные, ограничения, размер и варианты поставки.
4. [План реализации: 17 задач](superpowers/plans/2026-10-23-cliptiq-port.md): файлы, контракты, проверки, зависимости, этапы и release gate.

Product code ещё не создаётся. Пользователь подтвердил Cliptiq и разрешил этап T01: [прототипы](../experiments/footprint/README.md), [результаты и решение](support/architecture-decision.md). В документах дизайна числа size ceiling — предложенные цели; реальные измерения вынесены отдельно. Команды T02–T17 по-прежнему инструкции будущему исполнителю, не выполненные проверки.

Имя Cliptiq подтверждено. Открытые решения: окончательный native stack после T01; точные size budgets; OCR/diagnostics distribution; поддерживаемые Wayland desktops и необходимые integrations. ClipClap не принят как замена.

Исходная исследовательская копия `reference/Clipy` исключена из Git. Она не входит в поставку и не должна превращаться в nested repository внутри Cliptiq.

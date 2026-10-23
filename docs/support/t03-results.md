# T03: privacy policy и capability contract

23 октября 2026. **T03 завершён; T04 не начат.** C++17 core, без новых runtime/third-party dependencies.

## Реализовано

Pure `evaluate_capture` возвращает Accept/Reject, структурированную причину и только отфильтрованный snapshot. `dispatch_capture` передаёт storage только принятый результат; thumbnail/OCR получают его лишь после подтверждённого retained ID. Storage error и пустой ID не запускают производные операции. OCR дополнительно требует explicit request и capability. T02 `ingest` остаётся низкоуровневым repository API; все будущие capture/screenshot пути должны использовать T03 gate.

Capabilities содержит независимые capture/write/hotkeys/auto-paste/source-app/tray/pointer-placement/OCR с Available/Limited/Unavailable и reason. По умолчанию всё Unavailable/Not probed. Native backend discovery не реализован и не подменяется именем ОС. Настоящее обнаружение/permissions относится к T04–06.

Trusted backend registry сопоставляет platform/native type/class/encoding. Неизвестный формат не может пройти, объявив себя Text. PDF/RTFD сохраняют исходные bytes в своих известных классах, произвольный Opaque не принимается. Capture-only список native offers нужен для marker checks до filtering и не меняет storage schema/digest T02.

## Сценарии фильтрации

| Группа | Проверенный результат |
|---|---|
| Transient flag и cooperative marker | Всегда Reject; marker payload не сохраняется |
| Concealed option off/on | Сохранение Yes metadata либо Reject |
| Apple remote option off/on | Правила только в Mac context; Windows/X11/Wayland hints не объявлены Universal Clipboard |
| Remote primary file | Reject до enabled-kind filtering; исходный primary offer не меняется |
| Remote secondary file | Только Files удаляются, text/image и исходные item indices остаются |
| App exclusions | Exact normalized ID для Known/BestEffort; marker prefix именно excluded_id.starts_with(type), включая unknown/disabled types |
| Unknown source | Default reject с непустым списком исключений; AllowUnknown/RejectAlways проверены со списком и без |
| Source capability | Unavailable удаляет неподтверждённый ID и даёт Unknown; Limited понижает Known до BestEffort; повышения доверия нет |
| Kind switches | Каждый из восьми классов отдельно выключен; все выключены → Reject; HTML default off сохраняет text+RTF |
| Native type/encoding | Unknown, spoofed kind, незарегистрированная platform и несовпадающая encoding не сохраняются |
| Metadata/order/fidelity | Flags, confidence, bytes, encoding, порядок и item boundaries сохранены; входной snapshot не изменён |
| Screenshot input | Image snapshot проходит тот же gate; неизвестный source с исключениями не пишет историю |
| Side effects | Rejected capture: 0 storage/thumbnail/OCR calls; принятый filtered snapshot проверен через реальный SQLite counting sink |
| Post-storage conditions | Full error и пустой retained ID: 0 derivatives; OCR только request+capability; отключённые capabilities не включаются автоматически |
| Malformed input | Некорректные enums/IDs и превышение metadata/payload bounds отклоняются до копирования разрешённого результата |

**34 policy scenarios**, **29 history scenarios** и process interruption recovery проходят в Debug и MinSizeRel на Windows, Linux и обоих CPU macOS. [Финальный CI 39943798856](https://github.com/Hydrock/Cliptiq/actions/runs/39943798856): **5/5 jobs success**, включая отдельный Clang ASan/UBSan. Source `6824d00d9c129e482ca0ed309163fd708f5a2d28`. Локально также Windows Debug/Release, Linux GCC ASan/UBSan/Release PASS. Tools suite Linux 7 PASS; Windows один symlink skip по privilege.

До реализации подтверждён RED: 24 из 27 начальных policy cases падали на stub; три sanity/rejection tests уже проходили на fail-closed stub. После реализации все 34 cases и существующие regression suites зелёные. Независимое source review gpt-6-astra: нет critical/important defects; замечание об attribution замера учтено ниже. [Ledger](t03-ledger.md).

## Парный замер размера

Два native-free API fixtures собраны одним commit/compiler/MinSizeRel/link flags. `core_probe` — история+SQLite, `core_policy_probe` — тот же workload с policy dispatch, trusted registry/capability setup, sink wiring и result recovery. **Добавка — стоимость этой связанной интеграции, не размер policy machine code в изоляции.** ZIP содержит одинаково названный одиночный executable для корректного сравнения контейнерного overhead.

| Target | Baseline bytes | С policy dispatch, bytes | Добавка executable | Добавка ZIP |
|---|---:|---:|---:|---:|
| Windows x64, /MT | 844,800 | 856,576 | 11,776 | 5,424 |
| Linux x64 | 789,648 | 801,936 | 12,288 | 4,293 |
| macOS arm64 | 883,608 | 889,728 | 6,120 | 5,841 |
| macOS x86_64 | 872,896 | 887,496 | 14,600 | 6,947 |

Итого около **6–14,3 KiB** executable и **4,2–6,8 KiB** ZIP. Новые сторонние библиотеки/сервисы не подключены; ни clipboard decode framework, ни сетевой runtime в policy нет. Вариант baseline использует текущую T03 модель snapshot; это парное сравнение в одном commit, не вычитание исторических T02 binaries. Размеры полного продукта и offline closure остаются неизвестными, будущие 5/10 MiB budgets не объявлены достигнутыми.

[Raw JSON, source hashes, compiler versions и CTest logs](t03-measurements). Имя `t02-size.json` сохранено для совместимости workflow; внутри добавлены policy_probe_* и policy_added_* поля.

## Границы и воспроизведение

[API, defaults и backend obligations](../../core/POLICY.md). `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test -R policy --output-on-failure`. Для всех regressions: `ctest --preset test`; sanitizer и release presets сохранены. Для paired sizes: `cmake --preset release -DCLIPTIQ_BUILD_SIZE_PROBE=ON`, build, `python tests/size/report.py`.

Screenshot test означает общий policy path для предоставленного image snapshot; сам watcher/decoder/OCR engine не создавались. OS clipboard, реальные приложения-источники, permissions и registry mapping adapters будут проверяться в следующих platform задачах. Native adapters обязаны предоставлять полный ordered offer/marker list до фильтрации и корректную confidence; normalized app IDs должны совпадать с настройками исключений.

Unknown flags не становятся No. Cooperative markers не обнаруживают все пароли/удалённый clipboard; Known/BestEffort/Unknown нельзя выдавать за универсальную защиту приложения. При отсутствии source API список исключений по default может остановить весь захват — это явное поведение. Политика меняет только новый capture, не удаляет прежнюю историю и не изменяет системный clipboard. Фиксированные reason messages не содержат содержимое, marker payload или app IDs; hidden telemetry/default network events не добавлены.

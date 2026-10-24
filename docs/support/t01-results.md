# T01: итог сравнительных прототипов

23 октября 2026. **T01 закрыт в согласованных границах измерительного этапа. T02 не начат.** Это сравнение прототипов с одинаковым workload, не готовый Cliptiq и не замер всех будущих функций.

## CI и воспроизводимость

[Последняя полная матрица: 12/12 jobs успешно](https://github.com/Hydrock/Cliptiq/actions/runs/39936634738), [установка в профиле: 3/3](https://github.com/Hydrock/Cliptiq/actions/runs/39936634716), [Sparkle: 2/2 CPU](https://github.com/Hydrock/Cliptiq/actions/runs/39934838419). JSON содержит фактические toolchain, flags, hashes и CPU. Последние footprint snapshots лежат в measurements/final-ci.

Предыдущие Linux CI падали на скачивании CMake через медленный Azure mirror, до сборки. Подтверждено логом и timeout exit 124. Bootstrap переведён на официальные Ubuntu mirrors, ограничен по времени, ставит зависимости только выбранной lane и использует уже имеющиеся build tools. После исправления вся матрица зелёная.

## Размеры и решение

В изолированном профиле CI C++ Base с NOTICE: Windows 942 502 bytes / ZIP 534 869; macOS arm64 859 848 / 438 385; macOS x86_64 858 392 / 465 965. Это отдельный эксперимент от Sparkle baseline; небольшие различия упаковки/подписи отражены исходными JSON. C++ core оказался компактнее Rust на измеренном workload. Рекомендация: C++17 core, native UI, static CRT Windows, отдельные CPU сборки Mac; финальное утверждение стека остаётся решением до T02.

[Чистый Linux userland](clean-install/README.md): GTK3 runtime добавляет 141 422 111 bytes, GTK4 — 147 262 609; OCR EN/RU с GTK3 — 171 250 572. Маленький EXE не означает маленький автономный Linux-дистрибутив. GTK3 пока разумный baseline для дальнейшей проверки, desktop package manager может переиспользовать существующие библиотеки.

[Обновления](update-footprint/README.md): Windows staging/verification helper /MT — 145 920 bytes; Linux полный .deb — 344 624 bytes, tar.xz — 344 108. Mac full Sparkle добавляет 1 644 671 bytes arm64 / 1 616 159 x86_64 к app, около 0,55–0,60 MiB к ZIP. Итог app с full Sparkle около 2,4 MiB. Это full payload и стоимость интеграции; binary delta между настоящими releases не измерена и не принимается за ноль.

[Linux-интеграции](linux-integration/README.md): X11 clipboard/hotkey прошли в Xvfb; SNI/dbusmenu — протокол с mock watcher; clipboard — реальный headless Sway data-control v2. У измеренного portal нет GlobalShortcuts/Clipboard интерфейсов. Универсальная поддержка Wayland и визуальная работа GNOME/KDE не заявляются.

## Принятые ограничения и последующие release-проверки

Пользователь ответил: «Нет — явно зафиксировать ограничение CI» на вопрос о чистых Windows/macOS ВМ. Поэтому проверены свежие профили hosted runners; полноценная установка на чистой ОС остаётся непроверенной, поле clean_vm_verified=false. Linux-контейнер также не заменяет desktop VM.

Production Developer ID/notarization/EdDSA appcast, подписанный установленный update end-to-end, Windows download/install transaction/rollback, подписанный Linux repository, реальные GNOME/KDE/physical-key проверки и OCR quality/support matrix относятся к последующим задачам, в частности T15/T17. Для них нет выдуманных размеров или PASS. Предварительные цели 5 MiB download / 10 MiB app-owned не объявлены достигнутыми для полного продукта и требуют отдельного offline dependency budget.

Проверки инструмента: 7 unit tests; на Windows один symlink case пропущен из-за привилегии, на Linux все проходят. Docker lanes GTK3/GTK4/OCR: offline install, XML parity, lifecycle PASS; GTK3 DEB install/run/remove PASS. Независимое ревью выявило риск старого DEB cache: исправлен уникальным каталогом на каждый запуск, замеры повторены. Все T02–T17 остаются не начатыми.

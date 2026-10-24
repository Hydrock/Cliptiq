# T15: обновления и локальная диагностика

Автоматическая часть T15 реализована и проверена. Runtime `3ac983131e1a167908f4c11ab43db3a62bbc20e7`, [CI40094637399](https://github.com/Hydrock/Cliptiq/actions/runs/40094637399) —11/11. Ручная приёмка T04–T14 остаётся OPEN, T15 native release/desktop acceptance OPEN. T16 не начат. Production release/installer не опубликован.

Пользователь явно принял замену Firebase Analytics/Crashlytics локальным экспортом без отправки данных. Отчёт строится по фиксированному белому списку без доступа к Store, clipboard, snippets, путям, app IDs, environment или текстам ошибок. Экспорт через native save picker; автоматический crash reporting и удалённая аналитика намеренно исключены.

## Обновления и интеграция

Windows/Linux: ограниченный background HTTPS request к собственному GitHub Atom feed; manual check и opt-in интервалы86400/604800/2592000 сек. Version, last-attempt и статус видны в меню/настройках. Last-attempt переживает Apply настроек. Проверка метаданных не скачивает/исполняет пакет и не является проверкой подлинности. Live native WinHTTP и GIO/TLS проверки собственного feed локально прошли: опубликованных releases нет, корректно показано отсутствие новой версии.

Windows signed installer/MSIX manager и Linux authenticated package manager остаются маршрутами явной установки. Portable не перезаписывается автоматически: скрытого runtime/helper/rollback нет. Отдельный Windows verifier использует WinVerifyTrust + точный SHA256 сертификата издателя, отклоняет unsigned/tampered/wrong publisher/invalid pin/oversize/reparse и ничего не запускает. Реальный Cliptiq installer/repository ещё не предоставлен.

macOS Base: явная страница собственных releases; automatic checking недоступен без настроенного Sparkle. Optional lane включает полную source-built Sparkle2.10.0 framework с XPC/installer helpers/resources/license. Требуются свои feed/Ed25519 public key; чужие keys/feed не используются. Startup не перезаписывает provider preferences; scheduler принадлежит Sparkle, automatic install запрещён. System profiling/release notes выключены.

Свежий review выявил1Important: исходный SDK проверяет advertised size, но не ограничивает реальные response bytes. Один fix pass заменил только downloader в закреплённых исходниках:128KiB appcast/5MiB package, проверка до append, uncached ephemeral session,20s resource timeout, bounded private staging. Installer и Ed25519 verifier upstream не менялись. CMake отвергает unpatched framework identity. Native fixture сначала воспроизводит дефект на исходном downloader (RED), затем проверяет valid/unknown-length appcast/oversized archive на том же patched source (GREEN). Это тест byte handling с synthetic transport, не реальная установка.

## Размеры

| ОС | Base executable, B | ZIP executable, B | Добавка к T14, B |
|---|---:|---:|---:|
| macOS arm64 | 1 876 328 | 770 310 | 8 896 |
| macOS x86_64 | 1 867 176 | 799 962 | 17 248 |
| Windows x64 | 1 669 120 | 850 317 | 36 352 |
| Linux x86_64 | 1 509 488 | 712 780 | 24 608 |

Base — фактический production executable. ZIP — только executable; это не полный bundle/installer/installed closure. Добавка — историческое сравнение с сохранённым T14 CI той же архитектуры, не переключатель одного исходника. Общий runtime без Sparkle/Firebase/Realm; TLS использует системные WinHTTP/GIO. Linux TLS backend может быть недоступен — ошибка сообщается явно; он не скрыто скачивается.

| ОС | App + полный optional Sparkle bundle, B | ZIP bundle, B |
|---|---:|---:|
| macOS arm64 | 3 561 474 | 1 354 543 |
| macOS x86_64 | 3 533 842 | 1 454 641 |

Эта таблица включает app, framework, все её runtime helpers/localizations и license; вырезаны только лишние CPU slices, установщик/system frameworks сюда не входят. Ad-hoc signatures нужны для CI, не являются Developer ID/notarization. Перенос проверен с недоступным build-tree SDK. Windows optional Authenticode utility: **144,896B**, отдельно от Base. Полные per-file hashes/source/compiler evidence сохранены; один generated local Python cache исключён из сравнения с Git blobs, исходный raw report сохранён и четыре clean CI maps проверены полностью; prototype T01 не подменяет эту production-host lane.

## Проверки и открытые gates

Linux54/Windows44/macOS41 CTest groups в Debug и MinSizeRel,11 suite logs; Linux/macOS ASan+UBSan, MSVC ASan. Отдельно обе macOS архитектуры: настоящий SDK Ed25519 verifier (valid/wrong identity/missing/malformed/tampered), native downloader RED→GREEN, source-built framework, complete app packaging/relocation. Windows реальный Authenticode проверен на подписанном Microsoft CRT, не Cliptiq release. T14 legacy migration suites сохранены и прошли. TSan не запускался, clean VM отсутствует.

Промежуточные отказы: Objective-C id property, неверная форма SDK fixture key (нужен32-byte seed), framework marker LF/CRLF и старый X11 fixture atom query под нагрузкой. Финальный полный CI проходит; X11 assertions/suppressions не ослаблялись. Native wiring не везде было test-first; абсолютный TDD не заявлен.

Два новых Minor отложены по review workflow: macOS Base показывает доступный automatic checkbox при unavailable provider; ошибка записи local export видна только в stderr GUI-запуска. Дополнительно отложена фильтрация generated Python cache в source-size collector; аудит явно классифицирует этот локальный non-source entry. Прежние Minor сохранены: T11 Saved при XML worker, T12 preview-off decode/cache, T13 --background launcher. Полные решения с ценой ограничений — [ledger](t15-ledger.md).

Production Ed25519 feed/key, Developer ID/notarization, actual signed Windows installer/MSIX, signed Linux repository, real update/cancel/relaunch/rollback, desktop preferences, picker/file failures, proxy/offline/clean-system closure остаются OPEN. Это ограничение приёмки, не PASS по mock provider. Ручные T04–T14 не закрыты; T16 не реализуется.

[Контракт](../../core/UPDATES.md) · [Privacy](privacy.md) · [Delivery](../../packaging/updates/README.md) · [Manual](../../tests/manual/updates.md) · [Raw evidence](t15-measurements/)

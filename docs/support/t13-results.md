# T13: автозапуск, локализация и доступность

Автоматическая часть реализована. Runtime `45d2349441ae51a36d78c68d94e7d789db687c0a`, [CI40083243041](https://github.com/Hydrock/Cliptiq/actions/runs/40083243041) — 7/7. Ручная T04–T12 и T13 desktop acceptance OPEN. T14 не начат. Готовых installers/release пока нет.

## Поведение и размер

Нативный SMAppService, HKCU Run и XDG autostart; настройки сохраняются с откатом к реальному состоянию регистрации. Один владелец capture/store на user-session, ограниченный mailbox с пятью маршрутами открытия, без shell/path команд; Settings доступны через повторный запуск. Есть собственные Cliptiq.app/GUI Cliptiq.exe/Linux desktop actions, About/version/support своего проекта.

EN/RU132 ключа, семь частичных переводов Clipy с attribution/source inventory. Единая генерация C++ каталога, fallback English, системный auto locale; пользовательские данные не переводятся. Нативные label associations/accessibility names и перенос длинных подписей добавлены. Реальные screen readers, DPI, RTL, login approval и logout/login остаются ручными.

| ОС | Без T13, B | С T13, B | Добавка, B | ZIP, B | Добавка ZIP, B |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 786 768 | 1 850 200 | 63 432 | 760 920 | 17 781 |
| macOS x86_64 | 1 786 840 | 1 840 968 | 54 128 | 791 824 | 18 104 |
| Windows x64 | 1 582 080 | 1 639 936 | 57 856 | 837 712 | 22 520 |
| Linux x86_64 | 1 398 832 | 1 476 688 | 77 856 | 701 348 | 24 293 |

Парные MinSizeRel media hosts одного исходника/компилятора без/с lifecycle+locale. ZIP — один executable DEFLATE9, не installer/update. Нового bundled runtime/library нет: существующая SQLite + системные APIs + статические строки. Linux dependency delta указана в raw evidence; это не полный offline installed closure. macOS ServiceManagement — системный framework. Производственные app/GUI targets компилируются и проверяются отдельно; размеры таблицы относятся к review hosts.

## Проверки

CI Debug+MinSizeRel на Ubuntu24.04/Windows2025/macOS15 ARM+Intel, Linux/macOS ASan+UBSan, MSVC ASan. CTest groups Linux48, Windows38, macOS35; всего11 suite logs. Локально Linux Debug/MinSizeRel/ASan+UBSan48/48, Windows19 безопасных групп и GUI PE subsystem check. Остальные Windows GUI группы проверяет hosted CI; локальная WindowStation не позволяет их запуск. Нет чистых Windows/macOS ВМ; CI не подменяет их. Прежние leak policies/suppressions сохранены, TSan не запускался.

Проверены separate-process forwarding/crash-restart, bounded queue, fixed routes, чужая SQLite без перезаписи, пустая interrupted initialization, locale fallback, неизменность history/user text, RU native settings и OS-isolated autostart success/failure/rollback. Test registration не изменяет настоящий login list. Общие privacy/history/media/paste/hotkeys/XML тесты проходят; новые UI routes не обходят privacy gate.

Единственное свежее ревью нашло2 Important: initialization race теряла уже пересланный Settings; rollback восстанавливал preference вместо actual registration. Оба исправлены одним проходом, регрессии RED→GREEN. Initial CI выявил macOS SQLite NOFOLLOW на /var symlink ancestor: воспроизведено на Linux, исправлено canonicalization после проверки private directory с повторной проверкой. Raw RED/финальные logs сохранены. Некоторые initial native wiring изменения появились до полного test-first цикла; это не заявление об абсолютном TDD.

## Ограничения и решения ревью

- Minor T13 отложен: --background пока не скрывает launcher; login запускает видимое окно. Доступ к настройкам сохраняется, скрытый старт не принят.
- Прежние Minor: T11 generic Saved во время locked XML worker; T12 preview-off продолжает decode/cache. Они не исправлялись в T13.
- IPC решение: OS owner/init locks + уже имеющаяся SQLite. Цена —100ms UI polling, bounded startup/busy waits и отсутствие durable exactly-once после crash.
- macOS решение: настоящий SMAppService требует собственного app bundle/OS approval. Цена — тестовые probes не подтверждают login/signing; exact approval grant не гарантируется rollback.
- Declined owner-change capture: transport T04–T06 не менялся, принят сохранённый автоматический suite; цена — реальная смена владельца остаётся manual OPEN.
- Declined deletion/OCR/thumbnail: T12 media не менялся, старые cancellation/generation tests сохранены; цена — ручная T12 OPEN, OCR/T16 не начаты.
- Declined denied/lost-target paste: сохранены boundary tests/manual fallback; цена — реальная app-to-app T08 OPEN.
- Declined INCR/image/XML bounds: прежние bounded parser/transport suites проходят, IPC bounded отдельно; цена — hostile live producer manual не закрыт.
- Declined desktop combinations/login/signing/GUI/DPI/screen readers: формальные ручные gates оставлены OPEN; цена — нельзя заявить desktop/assistive-tech acceptance или clean-install.
- Declined final size/exact-code CI: закрыт четырьмя hashed clean-source reports и7/7 CI этого runtime; цена — измерен review host, не installer/update/full distribution.
- Declined T14 migration: исключено по указанию пользователя; цена — миграция отсутствует, следующий этап не начат.

[Контракт](../../core/LIFECYCLE.md), [ручная матрица](../../tests/manual/lifecycle.md), [raw evidence](t13-measurements/), [locale inventory](../../resources/locales/source-inventory.json), [notices](../../THIRD_PARTY_NOTICES.md). Source manifests проверены по Git blobs; локальные CRLF/LF differences нормализованы отдельно, Linux paired binary hashes сопоставлены с CI. Документационный commit после runtime не меняет исполняемый код.

# T17: CI, упаковка и контроль размера

Автоматическая часть T17 реализована: runtime `5380462ede50273817be0dbcb4a2e832fabdf93b`, [CI40105477768](https://github.com/Hydrock/Cliptiq/actions/runs/40105477768) —16/16, attempt2 (повтор только Linux fixture job). Ручная приёмка T04–T16 и production release OPEN.

Реализованы отдельные Base/OCR кандидаты: macOS app ZIP по архитектурам; Windows ZIP и unsigned MSIX; Linux ZIP для сравнения, tar.xz, DEB и RPM. Приложение сохраняет C++/нативные оболочки; дополнительных GUI/runtime зависимостей упаковка не вводит. Лицензии зависимостей включены, лицензия самого проекта пока не назначена.

`feature-status.json` классифицирует53 ID ×4 backend =212 строк. `verified` означает автоматический контракт, не ручную desktop-приёмку. Адаптации и dormant upstream B03/B04 выделены; S11 — принятая локальная диагностика, B02 — отдельный индекс без нового поискового UI. `verify-coverage.py` проверяет полноту, scope и существование evidence; содержательную достаточность доказательств проверяет review, а не наличие файла само по себе.

`distribution.py` читает PE imports/delay imports, ELF DT_NEEDED/RPATH и Mach-O load commands, проверяет реальную архитектуру и достижимую локальную closure. Package manifest содержит точный файловый inventory SHA256, размер, symlink inventory и системные imports. Упаковка запрещает cache/reference/object/debug/user databases. Тесты покрывают неверный signer, unsigned proof, отсутствующие DLL/.so, неверную архитектуру, лишние файлы и unsafe links. Проверка production использует native trust + fingerprint; текущие кандидаты не подписаны publisher identity.

Release-test включает регрессионные тесты прошлых этапов, новые policy/inventory/coverage/entry tests и bounded fuzz smoke. ASan/UBSan/Windows ASan lanes сохранены. Отдельный `--version` выходит до открытия профиля/clipboard и не показывает Windows MessageBox.

Минимизация: MinSizeRel, static MSVC runtime, dead-code elimination и IPO/LTO только в MinSizeRel; Debug/sanitizer остаются отдельными. Контрольный Linux до/после LTO:1517680→1427336 B (−90344 B,−5.95%). Полные staged bytes включают лицензии/manifest; optional модели не считаются дважды.

Ограничения и процедура ручной приёмки: [release-checklist](release-checklist.md). Чистые ВМ отсутствуют; production signing/notarization, реальная установка/удаление и полный offline dependency delta не проверены. Автоматическая блокировка выпуска сохранена.

Независимое review (`8b8d7df..fe95fdb`) нашло два Important, оба исправлены в `5380462`: абсолютные/CWD-dependent ELF/Mach-O loader paths отклоняются, неподдерживаемые Mach-O32/big-endian/FAT64 magic не пропускаются как данные. Новые regression tests RED→GREEN; tools25 tests (Windows3 symlink privilege skips), Linux release62/62, Windows новые T17 tests5/5. Critical/Minor от reviewer нет; прежние отложенные замечания T04–T16 не закрывались.

Локальный полный Windows suite не green: изолированная WindowStation возвращает Win32 Access denied5; повтор под другим контекстом также столкнулся с ACL общего temp index gate. Native Windows CI проходит в свежем окружении. Эти локальные ограничения не скрыты: лог будет приложен. Linux X11 fixture atom query — известный с T15 периодический отказ; локальные30 повторов прошли, исходный CI-отказ сохраняется отдельно. Автоматические suppressions/assertion weakening/retry-on-pass в тесты не добавлялись.

Итоговые native Debug/MinSizeRel suites: macOS49/49 на обеих архитектурах, Windows52/52, Linux62/62. Sanitizer lanes49/52/62 и пять OCR suites по5/5. Подтверждены1322 source digests по185 путям относительно runtime commit;4 core +5 OCR reports. Проверены8 staging inventories и8 size gates, все PASS. Policy CI и старые footprint/update/profile workflows также green; [метаданные](t17-measurements/ancillary-ci.json).

Все размеры ниже в байтах, app-owned включает staged metadata/licenses, модели уже внутри optional; OS frameworks/libraries отдельно, их дополнительная установка неизвестна. ZIP — одинаковый сравнительный формат. Абсолютные/growth gates применяются к app/ZIP; дополнительные контейнеры измеряются отдельно, отдельного исторического growth baseline DEB/RPM/MSIX пока нет.

| ОС | Архитектура | Вариант | App-owned B | ZIP B |
|---|---|---|---:|---:|
| linux | x86_64 | base | 1439613 | 682540 |
| macos | aarch64 | base | 1871442 | 771189 |
| macos | x86_64 | base | 1876130 | 814852 |
| windows | x86_64 | base | 1702510 | 865111 |
| linux | x86_64 | ocr | 12607297 | 5740750 |
| macos | aarch64 | ocr | 2092176 | 878680 |
| macos | x86_64 | ocr | 2101936 | 931849 |
| windows | x86_64 | ocr | 11390743 | 5319390 |

| Контейнер | Вариант | Размер B |
|---|---|---:|
| cliptiq-x86_64-base.tar.xz | t17-linux-x86_64-base | 573608 |
| cliptiq-x86_64-base.deb | t17-linux-x86_64-base | 610834 |
| cliptiq-0.1.0-0.t17.5380462.x86_64.rpm | t17-linux-x86_64-base | 691680 |
| cliptiq-0.1.0-linux-x86_64-base.zip | t17-linux-x86_64-base | 682540 |
| cliptiq-x86_64-ocr.tar.xz | t17-linux-x86_64-ocr | 4623212 |
| cliptiq-x86_64-ocr.deb | t17-linux-x86_64-ocr | 5149362 |
| cliptiq-0.1.0-0.t17.5380462.x86_64.rpm | t17-linux-x86_64-ocr | 5750578 |
| cliptiq-0.1.0-linux-x86_64-ocr.zip | t17-linux-x86_64-ocr | 5740750 |
| cliptiq-0.1.0-macos-aarch64-base.zip | t17-macos-aarch64-base | 771189 |
| cliptiq-0.1.0-macos-aarch64-ocr.zip | t17-macos-aarch64-ocr | 878680 |
| cliptiq-0.1.0-macos-x86_64-base.zip | t17-macos-x86_64-base | 814852 |
| cliptiq-0.1.0-macos-x86_64-ocr.zip | t17-macos-x86_64-ocr | 931849 |
| cliptiq-x86_64-base.msix | t17-windows-x86_64-base | 884540 |
| cliptiq-0.1.0-windows-x86_64-base.zip | t17-windows-x86_64-base | 865111 |
| cliptiq-x86_64-ocr.msix | t17-windows-x86_64-ocr | 5443436 |
| cliptiq-0.1.0-windows-x86_64-ocr.zip | t17-windows-x86_64-ocr | 5319390 |

Отдельные существующие optional lanes не входят в Base/OCR-таблицу:

| Компонент | App-owned/closure B | ZIP B |
|---|---:|---:|
| legacy-migration-macos-15 (полная migration closure) | 19943837 | 7403645 |
| legacy-migration-macos-15-intel (полная migration closure) | 20967989 | 7792480 |
| sparkle-update-macos-15 (приложение + Sparkle) | 3506770 | 1349198 |
| sparkle-update-macos-15-intel (приложение + Sparkle) | 3490642 | 1449732 |

Миграция существенно тяжелее основного приложения, поэтому остаётся отдельной утилитой. Swift debug symbols исключены из shipping closure и их стоимость указана отдельно в raw reports. Union всех опций не измерен: приведённые варианты нельзя выдавать за полный объединённый пакет.

[Полные измерения и SHA256 контейнеров](t17-measurements/summary.json), [source verification](t17-measurements/source-verification.json), [журналы и test counts](t17-measurements/test-counts.json), [промежуточные отказы](t17-measurements/intermediate-failures.md), [решения и review](t17-review-ledger.md), [сборка кандидатов](packaging.md). Бинарные кандидаты доступны в artifacts указанного CI, в Git не добавлены. Нового release/merge нет.

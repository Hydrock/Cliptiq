# T01: стоимость обновлений (23 октября 2026)

Это измерение delivery spike, не готовый updater и не T02. Сравниваются app-owned bytes, сжатый payload и внешние OS services отдельно.

| Измерено локально | App-owned добавка / полный payload |
|---|---:|
| Windows x64 standalone native staging/verification helper, /MT | 145 920 bytes; ZIP около 74,3 KiB |
| Тот же helper, /MD | 13 312 bytes EXE + 178 616 bytes vcruntime140.dll = **191 928 bytes** |
| Linux C++ GTK3 Base, полный tar.xz | **344 108 bytes** download |
| Та же Linux fixture, полный .deb (xz9) | **344 624 bytes** download |
| Sparkle 2.10.0 original universal framework, все regular files | **5 634 056 bytes**, до thinning/embedding/re-sign |

Точные хеши и последние ZIP bytes — в JSON рядом. Windows helper — отдельный процесс, поэтому /MT число является фактической добавкой к Base, включая собственный CRT. Интеграция того же кода в основной EXE может переиспользовать CRT; такой incremental link пока не измерен. /MD closure здесь требует только vcruntime140.dll: helper не использует C++ stdlib. UCRT — OS component на целевой Windows 10/11. Для основного C++ приложения могут дополнительно требоваться msvcp140/vcruntime140_1; не переносить этот helper closure на весь продукт. Никакого собственного сетевого/crypto runtime DLL нет: WinHTTP, WinTrust, Crypt32 — системные библиотеки.

Native helper реально вызывает WinHTTP HTTPS download, ограничивает ответ 256 MiB, запрещает redirects, создаёт новый staging file и проверяет embedded Authenticode через WinVerifyTrust + SHA-256 pin сертификата издателя. Выполнены проверки: unsigned helper отклонён `0x800b0100`; настоящий Microsoft embedded-signed vcruntime140.dll принят с верным pin и отклонён с неверным. Catalog-only подписи системных EXE этим spike не поддержаны. Download path собран, но сетевой end-to-end transfer не проверен. Installer не запускается. Feed/version policy, installer transaction, rollback, locking/TOCTOU hardening, recovery и подпись Cliptiq остаются работой product updater. Это **не обещание**, что production updater стоит всего 146 KB.

Linux package manager может владеть download, проверкой подписанного repository metadata и установкой: тогда встроенный updater добавляет 0 bytes, но обновление скачивает полный .deb и использует установленный пакетный менеджер. Измеренный .deb — packaging fixture без репозитория и подписи; установка, запуск и удаление проверены offline в чистом Ubuntu-контейнере (см. ../clean-install/linux-gtk3/report.json); список Depends не является доказанным clean-machine closure. Tar lane — ручная/внешняя замена полного архива; автоматический updater для tar не реализован. GTK/shared runtime скачивается отдельно, если отсутствует. Размер delta **не измерен**: нет пары настоящих releases с patch/apply/verification. Нельзя подменять full-update payload размером короткой version string или обещать tiny delta.

## macOS CI

Официальный latest stable на дату проверки — **Sparkle 2.10.0**, минимум macOS **12.0**. Пин binary distribution: SHA-256 `c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c`, download **16 319 840 bytes**. Это архив всех development tools, не app update download. Локально архив скачан и hash проверен; inventory framework в `sparkle-release.json`, отдельно license 2.10.0 включается в app Resources.

Dedicated workflow `update-footprint.yml` строит C++ Base и тот же AppKit host с созданным `SPUStandardUpdaterController` (автозапуск update checks отключён), копирует framework со всеми helpers/locales, делает CPU thinning всех Mach-O. Варианты full и documented non-sandbox no-XPC. Сохраняются file inventory, before/after app/ZIP deltas и оригинальная framework signature validation. macos-15 = arm64, macos-15-intel = x86_64; JSON фиксирует фактическую architecture.

После thinning код подписывается **ad hoc** inside-out, затем `codesign --verify --deep --strict` и lifecycle smoke. Настоящий Developer ID / notarization / EdDSA-signed appcast / установленное signed-update end-to-end **не проверены**: нет release credentials. Ad hoc размер signature учитывается; его нельзя выдавать за production signed size. Обе lane успешно выполнены: [CI 39934838419](https://github.com/Hydrock/Cliptiq/actions/runs/39934838419). CPU-specific integrated results сохранены рядом в JSON.

| CPU | Base app / ZIP, bytes | Full Sparkle app / ZIP | Добавка app / ZIP |
|---|---:|---:|---:|
| arm64 | 860 089 / 439 699 | 2 504 760 / 1 018 619 | 1 644 671 / 578 920 |
| x86_64 | 858 681 / 468 034 | 2 474 840 / 1 100 777 | 1 616 159 / 632 743 |

No-XPC: arm64 2 297 187 / 972 977 bytes; x86_64 2 319 811 / 1 051 784. Full framework остаётся исходным вариантом; небольшая экономия no-XPC не заменяет проверку production updater. Delta-патчи между releases не измерены.

## Воспроизведение

Windows: `python experiments/update-footprint/measure.py` (MSVC через существующий toolchain helper). Linux после Base build: `python3 experiments/update-footprint/linux-payload.py`; staging использует temporary directory на Linux FS, не DrvFS. macOS: `python3 tools/fetch-footprint-deps.py`, затем `python3 experiments/update-footprint/measure.py`.

Локально выполнены оба Windows build, import inventory, три signature-gate сценария, Linux dpkg-deb/tar packaging и Python bytecode syntax check. Время и размер относятся к fixture source hashes из JSON, не к будущему complete app.

Официальные источники: [Sparkle release](https://github.com/sparkle-project/Sparkle/releases/tag/2.10.0), [embedding/signing/EdDSA guidance](https://sparkle-project.org/documentation/), [optional XPC removal](https://sparkle-project.org/documentation/sandboxing/#removing-xpc-services), [WinVerifyTrust contract](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust). GitHub release API digest дополнительно проверен на скачанном файле.

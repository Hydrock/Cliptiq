# T06: Linux X11 и Wayland clipboard backends

23 октября 2026. Автоматическая часть T06 реализована и загружена в существующую ветку/draft PR. **Ручная приёмка T04/T05 остаётся OPEN. Linux desktop acceptance также OPEN. T07 не начат.** Готового приложения с меню, launcher, snippets и установщиком пока нет.

## Реализация

C++17, XCB/XFixes и системный libwayland-client. GTK/Qt/web runtime и новые кодеки не добавлены. XML ext-data-control-v1 (wayland-protocols1.45) и wlr-data-control-v1/v2 закреплены SHA-256 и проверяются при configure; генерация только при сборке, notices сохранены. XCB выбран вместо Xlib, чтобы не ставить глобальный error handler и обрабатывать гибель owner как обычную ошибку.

X11: TARGETS → metadata privacy gate → выбранные bytes, normal/INCR, XFixes notifications, ownership/replay и подавление собственных записей. Повторное копирование тем же owner/timestamp считается новым событием. Уникальные requestor windows, ожидание единственного ответа на conversion и финальная owner/generation проверка не допускают смешивания снимков. PRIMARY не захватывается.

Wayland: ext-data-control предпочитается; wlr fallback только при отсутствии ext. Реальные registry/seat определяют capability. Без data-control, при finished/revoked device или неоднозначном seat backend явно Unavailable; XWayland не подменяет native Wayland. FD streams неблокирующие, имеют generation/deadline/byte limits. Ограниченная FD send queue безопасно обрабатывает SIGPIPE. Источник приложения Unknown, а не предположение по активному окну.

LinuxCaptureSession работает на owner event thread; serial worker один владеет SQLite, открывая БД только после dispatch_capture. Очередь имеет count/retained-byte budgets; overload возвращает Busy. Stop отменяет будущие callbacks/queued jobs и ждёт in-flight SQLite; уже выполняющаяся транзакция может завершиться. Нет thumbnail/OCR до соответствующих этапов. [Контракт и все ограничения](../../platform/linux/README.md).

Форматы: UTF-8, X11 Latin-1 STRING→UTF8_STRING, binary RTF/RTFD/PDF, HTML, raw PNG/TIFF, Mozilla URL UTF-16LE, URI/GNOME file lists. Generic URI list консервативно Files; cut replay становится copy. Изображения здесь передаются как bounded native bytes, не декодируются — это T12. Один selection соответствует item0. Неизвестные типы участвуют в metadata privacy, payload не запрашивается.

## Privacy и ошибки

| Сценарий | Проверенное поведение |
|---|---|
| Transient | Reject до любых content requests, без БД |
| Concealed | Apple marker и x-kde-passwordManagerHint учитываются до content при ignore_concealed=true; KDE hint presence трактуется консервативно, даже если значение не secret |
| Defaults | HTML off, ignore_concealed=false по контракту T03; отсутствие hint не доказывает публичность |
| Source exclusions | X11 WM_CLASS — BestEffort, не удостоверенная identity; Wayland Unknown; default Unknown с exclusions отвергается |
| HTML off / unknown type | Wire counters подтверждают отсутствие content requests |
| Owner death / offer cancel/change / timeout | Полученные partial bytes отбрасываются; отменённый FD не открывает SQLite |
| Oversize | INCR объявление и actual Wayland 32MiB+1 отвергаются; общие limits 32MiB/representation,64MiB/entry,4096 offers |
| Lifecycle | Own-write suppression, stop/restart, callback уничтожает session; старые completions не доставляются |
| Backpressure | Byte queue overflow не создаёт БД; outgoing count/bytes bounded, sends удерживают только запрошенную representation |
| Remote | Apple Universal Clipboard не эмулируется; Linux remote metadata Unknown |

Граница протокола: Wayland FD EOF не содержит заявленной длины. Если внешний provider молча завершает усечённый opaque stream без cancellation, отличить его от полного payload невозможно. Это не обещание полной защиты от ложных native данных. Общий RSS процесса не равен byte budget очереди.

## Проверки и review

[Финальный CI39989123550](https://github.com/Hydrock/Cliptiq/actions/runs/39989123550), source **da0d6cc20a77749bdfb1c9067ce92efea74f0e9c**, clean checkout: **7/7 jobs success**. Linux **9/9 CTest**, **134 PASS**:14 protocol/stream cases,14 independent Wayland wire cases,12 X11 wire cases,14 native X11 и14 native Sway cases,2 external-client cases,34 policy,29 history и crash recovery. Debug/MinSizeRel и отдельный Clang ASan/UBSan. Локальный WSL/GCC13.3 Debug/MinSizeRel/ASan+UBSan также9/9. macOS arm64/Intel —92 PASS, Windows и отдельный MSVC ASan —100 PASS; предыдущие backend не менялись. macOS leak detection по-прежнему отключена; MSVC ASan не объявляется UBSan.

Изоляция: private Xvfb, headless Sway + private DBus/runtime; ambient DISPLAY/WAYLAND_DISPLAY очищены, fallback запрещён. Independent XCB owner и настоящий libwayland-server peer проверяют wire faults/ext preference; peer не является compositor. Native wlr roundtrip и SQLite проверены на Sway1.9/wlroots0.17.1. Независимые wl-copy/wl-paste2.2.1 проверяют UTF-8 capture/replay. Эта версия не имеет --sensitive: реальный клиент предлагает KDE MIME явно, а combined text+hint проверен wire peers. GUI браузер/editor/file-manager interop этим не подменяется.

Независимый final review gpt-6-astra нашёл3 Important,0 Critical. Все3 воспроизведены RED до исправлений: ASan OOB на duplicate X11 notify, KDE sensitive hint при включённом concealed filter, удержание всего multi-format snapshot маленькой outgoing representation. Исправлены за один TDD pass; regressions GREEN. Один Minor: completion admission threshold64 не резервирует pending jobs; фактический верхний предел64+max_pending_count (максимум80 lean completions) теперь явно документирован. Дополнительно ранее воспроизведены и исправлены same-owner timestamp и queued XFixes/final-owner races. [Ledger](t06-ledger.md), [fixtures](../../tests/fixtures/linux/README.md).

## Размер

Одинаковый GCC13.3, -Os, -DNDEBUG, --gc-sections, strip; SQLite статически. Локальная и CI Ubuntu24.04 сборки совпали по SHA-256 исполняемых файлов.

| Linux x86_64 paired fixture | Executable, bytes | ZIP, bytes |
|---|---:|---:|
| Core/policy/history baseline |801,936|431,773|
| Оба backend + monitor/session/worker |**925,288**|**487,225**|
| Добавка |**123,352**|**55,452**|

Добавка120.46KiB executable/54.15KiB ZIP; весь проверочный хост903.60KiB. Это связанный функциональный host с SQLite, а не полный продукт и не изолированные machine-code bytes backend. Native-free core probes не выросли. Windows/macOS размеры сохранены в тех же CI JSON для regression comparison.

Динамические зависимости нового host: libxcb, libxcb-xfixes, libwayland-client и их системные transitives; нет GTK/Qt/libwayland-server в imports. Разрешённые через ldd shared objects:6,349,040bytes против5,857,288baseline, delta491,752bytes. Это сумма файлов существующей системы; ELF loader и package metadata/resources здесь не подсчитаны. **Не full offline closure и не размер установки на чистой системе.** Dev headers, scanner, compositor, wire server, wl-copy и sanitizer не входят в ship payload. Полные installers/update deltas/full-product budgets остаются будущим release gate.

## Desktop matrix и открытые gates

| Среда | Наблюдение | Статус |
|---|---|---|
| Xvfb21.1.12 X11 | Native capture/write/INCR/monitor/storage | Automated backend only |
| Sway1.9/wlroots0.17.1 headless | Native wlr capture/write, independent CLI interop | Automated backend only |
| GNOME Shell46.0 headless | Реальный registry не объявил ext/wlr data-control; native background backend здесь Unavailable по registry inference | LIMITED, без integration pack |
| KDE/KWin, Hyprland | Исполняемые compositor недоступны в среде, проверки не выполнялись | OPEN/unverified |

GNOME probe — отдельная private headless session без X11; raw advertised interfaces сохранены. Он не доказывает свойства других версий GNOME. Нет чистых desktop ВМ: CI и WSL developer images явно не заменяют clean install/interactive acceptance. Без capture+write+persistence+snippets+launcher+working popup route ни один desktop не объявляется полностью поддержанным. Plugins/portal обходы не добавлялись. [Ручная матрица](../../tests/manual/linux-desktops.md).

[Raw JSON, hashes, versions, CTest и RED evidence](t06-measurements) сохранены без binaries. Последующие коммиты меняют только отчёты и статусы. **T04/T05 manual OPEN, Linux desktop manual OPEN, T07 не начат.**

# T05: Windows clipboard backend

23 октября 2026. Реализован C++17/Win32 backend с системным WIC и связью с privacy gate/SQLite worker. **T04 manual acceptance остаётся открытой. T06 не начат.** Windows app-to-app manual acceptance также не выполнялась; автоматические тесты не подменяют её.

## Реализация

Message-only window + AddClipboardFormatListener, coalesced WM_CLIPBOARDUPDATE и первая capture существующего clipboard. Snapshot проверяет sequence/owner до и после metadata/data с тремя ограниченными попытками. Собственная replay запись обновляет token и не создаёт цикл истории. Busy retry listener выполняется таймерами; GetClipboardData/delayed rendering остаётся native вызовом, который нельзя прервать этими ограничениями.

Поддержаны CF_UNICODETEXT (UTF-16LE, emoji, CRLF), Rich Text Format, CF_HTML с настоящими UTF-8 byte offsets, зарегистрированные PDF/RTFD bytes, Unicode URL, CF_HDROP с несколькими путями и безопасным COPY replay, PNG/DIBV5/DIB. Сохраняются исходные transferable representations, не HANDLE/HBITMAP/OLE pointers. Windows форматный набор относится к одному item; несколько файлов — одна DROPFILES группа. Virtual files/OLE-only и неподдерживаемые DIB layouts явно ограничены. [Полный контракт и ограничения](../../platform/windows/README.md).

Все memory handles освобождаются/передаются согласно Win32 ownership. Read ограничивает GlobalSize до lock/copy, replay подготавливает данные до EmptyClipboard. Ошибка native записи после clear может оставить partial clipboard; транзакционность не обещается. WIC декодирует только отфильтрованное изображение на worker, до storage, с payload/dimension/pixel limits; нативные bytes сохраняются без перекодирования. No thumbnail/OCR implementation.

## Privacy integration

Полный ordered offer/marker list сохраняется до T03. Metadata preflight выполняется до content GetClipboardData, затем dispatch_capture контролирует путь в lazy SQLite sink. Rejected capture и ошибочный WIC decode не создают БД. Очередь ограничена count и retained-input bytes, overflow сообщает Busy вместо роста памяти. Generation guards отменяют старые completion и queued jobs.

| Сценарий | Результат |
|---|---|
| ExcludeClipboardContentFromMonitorProcessing | Reject presence-only до чтения payload |
| CanIncludeInClipboardHistory | DWORD 0, malformed/unreadable hint → transient reject; valid 1 разрешает дальнейшую policy |
| Cooperative transient/concealed | Общие T03 правила; отсутствие concealed hint остаётся Unknown |
| Unknown/excluded source | Lowercase full owner exe path — только BestEffort; inaccessible/exited owner Unknown; default reject с exclusions |
| Marker prefix | Проверяется на полном списке, включая unknown native formats |
| HTML off / unknown format | Delayed WM_RENDERFORMAT spies подтверждают отсутствие чтения payload |
| Windows cloud privacy | Replay всегда публикует CanUploadToCloudClipboard=0; исходный producer/OS sync до capture не контролируется |
| Apple Universal Clipboard | Не эмулируется Windows cloud hints; remote остаётся Unknown |
| Image | WIC только после privacy dispatch, pixel/stride/alpha проверены; malformed image не пишет storage |
| Queue/lifecycle | Count/byte overflow, off-thread backend stop/restart, уничтожение backend/session из callback |

## Проверки

[Финальный CI 39972287168](https://github.com/Hydrock/Cliptiq/actions/runs/39972287168): **7/7 jobs success**, source `c33256583e51eff549d790bd48b5d9b42a7ef107`, clean checkout.

Windows: **35 native scenarios**, отдельный no-clipboard format/WIC test, 34 policy и 29 history scenarios + crash recovery; **5/5 CTest** в Debug/MinSizeRel и отдельном **MSVC AddressSanitizer**. macOS arm64/Intel — все 28 T04 native cases + core regression; Linux core regression и отдельные macOS/Linux Clang ASan/UBSan jobs проходят. Windows ASan не объявляется UBSan/LeakSanitizer; macOS leak detection отключена.

Windows runner: `Windows-2025Server-10.0.26100-SP0`, x64, MSVC 19.51.36260.0; это Server 2025 build 26100, не доказательство Windows 10/11 app compatibility. Локально после исправлений: Debug/MinSizeRel parser/WIC/core/history/crash 4/4 и ASan 4/4; OS clipboard test исключён только из локального запуска по известному station error5, в CI он включён. Tools suite: 7 tests, один symlink skip по privilege.

[Raw JSON, compiler IDs, source hashes, CTest logs](t05-measurements) сохранены без binaries. Имя t02-size.json оставлено для совместимости workflow, внутри добавлен windows_clipboard. Runtime code после проверенного commit не менялся; дальнейший commit содержит отчёты/документацию.

Нативные tests и size hosts создают **отдельную noninteractive window station/desktop до любого clipboard access**. Windows station содержит собственный clipboard; WinSta0 пользователя не затрагивается. Изоляция не удалась → abort, не fallback. Local executor возвращает error5 на CreateWindowStation даже при unrestricted exec: local native acceptance не заявляется. Parser/WIC/core ASan выполняются локально, native clipboard — на Windows CI. [Фикстуры](../../tests/fixtures/windows/README.md), [ledger RED→GREEN](t05-ledger.md).

Windows ASan lane сначала потеряла /EHsc при замене CMAKE_CXX_FLAGS: это воспроизводимо ломало RAII rollback. Исправлена конфигурация, не ослаблен transaction test и не изменён T02. Listener restart дефект также был воспроизведён тестом и исправлен. Независимое gpt-6-astra review выявило три Important дефекта: cloud-upload hint, binary RTF NUL truncation и replay unlock race. Все три воспроизведены отдельными regression tests до исправлений. Replay теперь явно запрещает cloud upload, RTF сохраняется целиком, own sequence подтверждается под clipboard lock с проверкой owner. Номер фиксируется Windows на CloseClipboard, поэтому подтверждение использует второй lock; чужой owner не подавляется. Minor/Critical замечаний не было.

## Размер

| Fixture, Windows x64 | Executable, bytes | ZIP, bytes |
|---|---:|---:|
| Win32/core baseline | 866,816 | 491,603 |
| С backend/session/worker/WIC | 947,200 | 529,181 |
| Добавка | **80,384** | **37,578** |

Добавка около **78.5 KiB** executable и **36.7 KiB** ZIP. Полный проверочный host — **925 KiB**, меньше 1 MiB. PE imports: USER32.dll, ole32.dll, KERNEL32.dll; WIC активируется через COM как системный компонент и не обязан фигурировать в static imports. Dependency JSON фиксирует dumpbin output, а не обещает полную offline closure.

Native-free core/policy probes Windows/Linux/macOS по размерам не выросли относительно T03/T04: новые Windows объекты не линкуются на других платформах.

Парные hosts одного source/compiler/MinSizeRel/static CRT: Win32 + C++ history/SQLite baseline и тот же workload с clipboard listener, session/worker и WIC. ZIP содержит одинаково названный executable. Добавка относится ко всей связанной интеграции, не к машинному коду Clipboard.cpp отдельно. Ни bundled GUI/web runtime, ни отдельные codecs, ни OCR models не добавлены. User32/Shell32/COM/WIC — обязательные системные зависимости, не app-owned bytes. Full product/installer/offline closure и Windows 10 compatibility этим не доказаны.

## Воспроизведение и ручная приёмка

Pinned SQLite: `python tools/fetch-footprint-deps.py`; в MSVC developer environment: `cmake --preset test`, build, `ctest --preset test --output-on-failure`. Для MinSizeRel repeat release preset. Парный размер: release configure с `-DCLIPTIQ_BUILD_SIZE_PROBE=ON`, build и `python tests/size/report.py`; нужен доступ к созданию новой window station. ASan commands зафиксированы в core.yml; /EHsc обязателен.

[Windows manual checklist](../../tests/manual/windows.md) открыт: Notepad, browser/Word/LibreOffice, Explorer, image editor/PDF consumer, elevated/secure-desktop boundaries, sleep/resume и owner termination. Hosted isolated Win32/API tests не доказывают реальную совместимость этих приложений или установку на stock clean system. [T04 macOS manual](../../tests/manual/macos.md) не закрывался. Полноценные settings/menu/hotkeys/auto-paste, launch-at-login и T06 Linux backend не начинались.

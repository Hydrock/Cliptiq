# T08 — вставка и modifier actions

Автоматическая часть T08 реализована. Финальный runtime source `affd2d7b9bcd5296ce064cd1efb042e8c7a8e611`, [CI40005189737 —7/7 success](https://github.com/Hydrock/Cliptiq/actions/runs/40005189737). **Ручная приёмка T04–T07 и реальная app-to-app T08 остаётся OPEN. T09 не начат.** Пользовательских релизных пакетов пока нет.

## Что работает

Общий `execute_selection` соединяет T07 MenuCommand с T02 Store и существующим ClipboardBackend. Команда/ID/revision и payload проверяются в короткой SQLite transaction, которая завершается до внешних clipboard/input вызовов. Оболочки передают контекст одного открытия и modifiers на момент выбора; контроллер выполняет selection после возврата из menu event pump. Устаревший выбор не меняет clipboard или историю.

- **Original:** воспроизводит сохранённые нативные представления через прежние allowlists. Auto-paste off только копирует и обновляет used_ms.
- **Plain:** оставляет Text representations, сохраняя encoding/данные. Image-only/rich-only без plain text отменяется без пустой перезаписи; новый codec не добавлен.
- **DeleteOnly:** атомарно удаляет выбранную запись, не пишет clipboard и не отправляет клавиши. `history_deleted=true`; Cancelled в текущем enum означает отсутствие copy/input, явный reason сообщает об удалении.
- **PasteDelete:** удаляет history только после принятого backend dispatch. Permission/UIPI/focus/held-key отказ и auto off оставляют запись. Dispatch не подтверждает, что чужое приложение приняло текст.

После OS вызовов повторно проверяется revision: конкурентное изменение сохраняет запись и возвращает history_error, без автоматического повтора. Clipboard write failure не запускает input/delete. Snippets читаются из T02 schema и превращаются в native text; Windows UTF-16LE input/output limits проверяются до выходной allocation, включая surrogate pairs/NUL и representation/entry quotas. Destructive snippet actions отменяются и не стирают определения.

NativeCaptureSession использует тот же writer instance, поэтому подавление собственного capture и privacy gate T03 сохраняются. Writer-only режим не вызывает start: default review host не начинает capture без `--capture`. Проверены menu → issued target → selection → history и реальный X11 write без немедленного self-recapture. Ранее реализованные native clipboard/privacy/history suites остаются в полном CI.

Modifiers настраиваются в review host CLI: Plain по умолчанию Command на Mac, Ctrl на Windows/Linux; Delete/PasteDelete выключены. Допустимы Ctrl/Shift/Alt/Super, нулевой binding отключён; конфликтующие/многобитные bindings и одновременные конфликтующие действия отвергаются. Удерживаемая клавиша даёт немедленный manual fallback: не отпускаем пользовательские modifiers синтетически и не повторяем paste неожиданно после задержки. T09 persistence/settings/hotkeys этим не реализованы.

## Платформы и безопасный fallback

**Windows:** SendInput одним batch Ctrl(+Shift)+V, scan code по target keymap. Проверяются HWND/thread/PID/process birth, generation жизни окна через destroy WinEvent, integrity/UIPI, foreground и удерживаемые клавиши. Недоступный lifetime tracker запрещает auto-paste. Возврат foreground только штатным SetForegroundWindow из Cliptiq; нет AttachThreadInput/elevation/UIAccess. При partial submission возвращается manual fallback и key-up только для ещё нажатых клавиш принятого prefix. Receipt чужого приложения не подтверждается.

**macOS:** AX/input permission проверяется без prompt, captured app launch date и AX focused window — до и после обычной activation. Нет перехвата чужого foreground. ASCII-capable keymap находит V; оба Paste/TerminalPaste используют **Command+V**. Shift+Command+V в Apple Terminal означает Paste Selection и не используется. Контекст status menu живёт через close-before-action и истекает после отмены с guard по generation.

**X11:** реальный XTest/XKB; текущая Latin V, затем Latin alternate group, включая Dvorak/remap. Для single-RU fallback допускается AB04 только с фактическим Cyrillic em; неизвестная mapping даёт manual fallback. Проверяются focus, destruction, собственный launcher по PID ancestry и held/latched modifiers. Короткий scoped server grab содержит только X requests до постановки событий, без DB/clipboard/приложенческих callback. Фокус не захватывается у другого окна.

**Wayland:** авторизованной input-интеграции/session в репозитории нет. `NativePasteBackend::capability` честно unavailable; содержимое доступно для ручной вставки, без root/uinput/XWayland bypass или автоматического RemoteDesktop prompt. Activation token не выдаётся за input permission. Приложение не объявлено полностью поддержанным на любом Wayland desktop.

Input capabilities сообщаются отдельным NativePasteBackend, чтобы clipboard-only targets не получили input framework зависимости. Сам launcher, уже владеющий focus, не угадывает прошлое приложение. Независимый DBusMenu item event без captured target идёт в copy/manual. Оpaque target одноразовый и заменяется новым capture.

Review host: `--no-auto-paste`, `--terminal-paste`, `--plain-modifier|--delete-modifier|--paste-delete-modifier none|ctrl|shift|alt|super`. Мappings проверяются до показа меню; результаты пока stdout/reason, не готовая product UX. Default/`--smoke` не читают clipboard, smoke не выбирает пункты/не пишет clipboard/не отправляет input. [Полный API-контракт и первичные platform references](../../core/PASTE.md).

## Минимальный размер

Парные MinSizeRel probes из одного исходника host: native menu/capture-capable baseline против того же host с selection/modifiers/native paste. Windows CRT статический, GUI/input frameworks системные, без Electron/Qt/ICU/new codec. Байты; ZIP — один executable, DEFLATE9, фиксированная дата.

| Платформа | Baseline | С T08 | Прирост executable | ZIP с T08 | Прирост ZIP |
|---|---:|---:|---:|---:|---:|
| macOS arm64 | 1 101 312 | 1 200 672 | 99 360 | 545 297 | 18 666 |
| macOS x86_64 | 1 108 160 | 1 183 488 | 75 328 | 569 351 | 20 538 |
| Windows x64 | 1 099 264 | 1 177 600 | 78 336 | 630 653 | 39 770 |
| Linux x86_64 | 1 019 504 | 1 039 984 | 20 480 | 524 102 | 9 917 |

Linux системная libXtst добавляет **27128B** существующих resolved ELF files: общий closure35354864 →35381992B (loader включён, real paths deduplicated). Это не пакет, download или полная offline установка: GTK/fonts/themes/resources отдельно. Все system dependencies не копировались в поставку. Новые Windows user32/advapi32 и macOS ApplicationServices/Carbon используются из ОС; imports сохранены. Full product/offline/update footprint не измерялся, поля остаются null.

Локальные Linux baseline/with_paste binary SHA256 точно совпадают с CI. [Сырые JSON, CMake flags, imports, hashes и логи](t08-measurements/README.md). После измеренного runtime commit публикация меняет только документацию/evidence.

## Проверки и review

Финальный CI —7/7: Debug+MinSizeRel на Windows2025, macOS15 arm64/Intel, Ubuntu24.04; отдельные Linux/macOS ASan+UBSan, Windows MSVC ASan. **Linux21, Windows15, macOS13 CTest groups**; это группы, а не количество assertions. Дополнительно локально GCC Debug/MinSizeRel/ASan+UBSan21/21 и MSVC build/5 безопасных portable+boundary групп. Предыдущая local Windows isolated station недоступна (error5); hosted CI используется для station fixtures.

Core16 scenarios включают полный replay, plain/image-only, auto off, delete/paste-delete, failed writes/dispatch, stale и concurrent revisions, snippets, UTF16 encoded bounds, modifiers/shortcut validation. Xvfb получает настоящие Ctrl+V/Ctrl+Shift+V events, EN/RU/Dvorak/remap, held Ctrl без forced release, changed/closed/self target, one-shot token, native writer self-suppression. GTK bridge использует реальные MenuButton/SQLite, writer/input — in-memory boundaries.

Windows/macOS дополнительно проверяются **выданные targets**, а не только forged tokens: OS facts/submission scripted через NativePasteCalls; фактическая adapter decision logic и core history исполняются. Windows проверяет каждый partial SendInput prefix, generation смену при одинаковых HWND/PID/thread/birth, focus/UIPI/held/restoration, input arrays и отсутствие лишних key-up. Mac создаёт настоящие CGEvent и инспектирует flags/keycodes, перехватывая posting; проверяются permission revocation, window/focus/key/allocation/restoration. Native denial на обеих ОС интегрирован с реальной SQLite PasteDelete retention. AppKit lifecycle воспроизводит native delegate close-before-action без AX permission. Это не реальные permission grants или receipt браузера/editor/terminal.

Свежий независимый review:0 Critical,6 Important,0 новых Minor. Все6 исправлены в одном fix pass с RED→GREEN: AppKit context; Mac terminal chord; XKB layout/remap; encoded quotas; HWND lifetime; native decision coverage. Первая намеренно падающая ревизия65c8b4d/[CI40003977152](https://github.com/Hydrock/Cliptiq/actions/runs/40003977152) воспроизводит lifecycle/bounds и отсутствующий safe seam; fac81fa/[CI40004663133](https://github.com/Hydrock/Cliptiq/actions/runs/40004663133) отдельно воспроизводит старый Mac Shift chord и Windows reuse/partial cleanup через рабочий seam. Исторические failures сохранены отдельно от итогового green CI. T07 Windows generic icons остаются прежним отложенным Minor.

Унаследованные GUI-only LSan suppressions fontconfig/AT-SPI относятся только к GTK menu fixtures; core/backend leak tests не ослаблены. macOS leak policy и ограничения MSVC ASan прежние. [Ledger решений и review](t08-ledger.md), [открытый manual checklist](../../tests/manual/paste.md). Реальные browser/editor/terminal, elevated targets, Window/Explorer/compositor lifecycle, multi-monitor/accessibility и clean systems остаются OPEN вместе с T04–T07. T09 не начат.

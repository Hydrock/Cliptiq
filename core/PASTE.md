# Selection and paste contract (T08)

`execute_selection(Store&, SelectionIntent, ClipboardBackend&, PasteBackend&)`
validates a T07 history/snippet MenuCommand and revision under a short SQLite
transaction, loads its bounded payload, then releases the DB lock before any OS
clipboard/input operation. The controller serializes each Store connection.

Actions:

| Action | Clipboard | Input | History |
|---|---|---|---|
| Original | Replay all supported stored representations | If auto-paste enabled | Touch after successful write |
| Plain | Retain Text representations only | If enabled and text exists | Touch after successful write |
| DeleteOnly | Unchanged | None | Atomic revision-checked delete |
| PasteDelete | Replay original representations | Attempt if enabled | Delete only after accepted dispatch |

Missing plain text cancels without an empty overwrite. Rich-only/image-only
content is not converted using a newly bundled codec. Snippets are UTF-8 text,
translated to the native text format (UTF-16LE for Windows) using the writer's
platform capability. UTF-8 input and encoded output (including Windows NUL) are
checked against both representation and entry bounds before output allocation. Destructive modifiers on snippets cancel: they never delete
the user's saved snippet definitions. Snippet editor/CRUD stays T10.

Results distinguish Copied, Dispatched, NeedsManualPaste and Cancelled; the
history_deleted flag independently reports DeleteOnly/PasteDelete mutation.
DeleteOnly currently uses Cancelled to indicate no copy/input, with
history_deleted=true and an explicit deleted reason. Dispatched means OS input
submission was accepted, **not** that another application received or inserted
the payload. PasteDelete with auto-paste disabled copies and retains history.
Permission/UIPI/focus/held-key denial retains the record and returns a manual
fallback reason. Clipboard errors abort before input/deletion. A revision change
during OS calls preserves the record and reports history_error, even after
accepted dispatch; the controller must refresh, not automatically retry.

The exact writer instance from a capture session is reused, preserving native
self-write suppression. The writer-only review-host path does not call start
and never captures ambient clipboard. Privacy capture still goes exclusively
through the T03 gate. Existing native writer format/size/marker allowlists apply;
no privacy exemption is introduced for later external copies.

`ModifierBindings` maps one optional Control/Shift/Alt/Super bit to each action.
Zero disables a binding; duplicate/multi-bit/unknown bindings and simultaneous
conflicting action modifiers are rejected. Unbound bits do not select an action.
The review host defaults Plain to Command on macOS, Control elsewhere; delete
and paste-delete are disabled. T09 settings persistence/global shortcuts are not
implemented by this CLI configuration.

`NativeMenuOptions::invocation_provider` captures a target on the GUI thread
before native popup activation, and activation records current modifier flags.
The controller defers selection until the menu event pump returns. Independent
DBusMenu item events carry no fabricated target and use manual paste. A launcher
which already owns focus can only copy/manual paste; it does not guess a prior
application. Native target tokens are service-local, single use, superseded by
new captures and not parsed as arbitrary window IDs supplied by the menu.

`NativePasteBackend::capability()` describes the input adapter separately from
ClipboardBackend capture/write capabilities; clipboard-only programs do not gain
input library dependencies. All methods/destruction belong to the owner GUI
thread (main thread for AppKit). A physically held modifier or paste key causes
manual fallback; no forced user-key releases or delayed surprise paste retry.

- Windows checks HWND/thread/PID/process creation time, target integrity/UIPI,
  foreground and physical key state; a destruction WinEvent hook invalidates
  window lifetime generations, with manual fallback if tracking is unavailable.
  It restores focus only by normal
  SetForegroundWindow from Cliptiq foreground, never AttachThreadInput. SendInput
  submits a single Ctrl(+Shift)+V batch using the target keymap's scan code;
  partial submission reports failure and releases only batch-introduced keys.
- macOS checks AX/input permission without prompting, running app launch date,
  AX focused window, foreground and physical modifiers. Normal activation is
  allowed only from Cliptiq foreground. ASCII-capable layout locates V; CGEvent
  submits Command+V for both standard and terminal policies: Terminal
  Shift+Command+V means Paste Selection and is deliberately not used. Focused
  window is checked again after normal activation. No receipt acknowledgement.
- X11 checks XTEST, XKB keymap, held/latched modifiers, actual focus, destroyed
  target and own launcher PID ancestry. A short scoped server grab keeps target
  validation and XTest event queuing together; it makes no focus change. The current XKB group resolves Latin V, including Dvorak/remaps, then a Latin
  alternate group. A single Russian layout falls back to AB04 only when its
  actual symbol is Cyrillic em; unknown mappings fail to manual paste. The scoped grab
  contains only local X requests, no application/DB callback or clipboard IO.
- Native Wayland has no authorized input session/integration in this stage.
  It returns unavailable/manual paste; no uinput/root/XWayland substitution or
  automatic RemoteDesktop prompt. An activation token is not input permission.

The native host accepts `--no-auto-paste`, `--terminal-paste`, and
`--plain-modifier|--delete-modifier|--paste-delete-modifier`
`none|ctrl|shift|alt|super`. It validates mappings before showing a menu.
Only an explicit selection writes/sends input; `--smoke` never does either.
Outcomes are printed by this review host; it is not finished product UX.

Platform references checked2026-10-23: [SendInput and UIPI/held keys](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput),
[foreground restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow),
[Apple AX trust](https://developer.apple.com/documentation/applicationservices/1460720-axisprocesstrusted),
[CGEventPost](https://developer.apple.com/documentation/coregraphics/cgevent/post%28tap%3A%29?language=objc),
[XTEST](https://www.x.org/releases/X11R7.5/doc/Xext/xtestlib.html),
[XKB keymap](https://www.x.org/docs/XKB/XKBlib.pdf),
[RemoteDesktop authorized session/input methods](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html).

Automated evidence and interactive limitations: [T08 manual checklist](../tests/manual/paste.md).

Native Win/mac adapters expose `PasteNative.hpp` OS-call boundaries to verify
issued targets and decisions without interactive permission or actual injection.
The injected constructor requires a trusted, fully populated NativePasteCalls
object. Windows tests script identity/lifetime, focus, integrity, held keys,
restoration and every partial SendInput prefix. macOS tests create actual CGEvent
objects, inspect Command+V flags, and intercept posting; AX/foreground facts are
scripted. Both compose native denial with real SQLite PasteDelete retention.
These tests prove adapter decision/event construction, not actual OS permission
behavior or target-app receipt. AppKit lifecycle regression invokes native
delegate close-before-action ordering and checks deferred context expiry.

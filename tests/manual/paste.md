# T08 real desktop acceptance — OPEN

T04–T07 manual gates remain OPEN. No clean Windows/macOS VMs are available.
CI is not app-to-app GUI acceptance. T09 is not started.

For each OS/version, record application/version, keyboard layout, target
privilege, modifiers, permission state, invocation route and result. Preserve
history/clipboard until the application actually consumes data when evaluating
receipt; the API's Dispatched alone cannot prove receipt.

- Browser, editor and terminal: text+rich text, files/image, multiline snippets,
  Original/Plain, auto on/off; inspect all supported clipboard representations.
- Normal/terminal shortcut, EN/RU, mouse and numeric selection, Command/Ctrl,
  Shift/Alt/Super mappings, disabled/conflicting modifiers. Hold then release
  each modifier; denial must retain history, with no forced releases or delayed
  unsolicited retry. Plain image-only must not empty the clipboard.
- DeleteOnly: no clipboard write/input. PasteDelete: retain sole record on write
  failure, unavailable permission/UIPI, changed/closed target, held key or auto
  off. Successful accepted dispatch may delete, but is not receipt confirmation.
- Open popup, change focus to another application/window, close target, then
  select. No paste to an unrelated application or own launcher. Repeat after
  cancellation, new opening, native tray or DBusMenu activation and stale item.
- macOS: unsigned/signed build, Accessibility and input access denied/granted,
  document/window changes, app restart, Command+V and selected shortcut. In
  Apple Terminal set terminal selection different from clipboard; both standard
  and terminal policies must paste the clipboard via Command+V. No
  silent permission prompt/bypass. Hosted runner tests do not grant AX access.
- Windows10/11: normal/elevated target, foreground lock denial, Explorer restart,
  handle/process reuse, destruction-hook continuity, and actual SendInput receipt. No elevation/UIAccess or
  AttachThreadInput workaround. A private noninteractive station does not prove
  real keyboard delivery; local station creation may be unavailable (error5).
- X11: actual desktop/window manager, focus restored after menu grabs, custom
  XKB layouts/remaps/locks and target destruction, including Dvorak. Xvfb tests verify real key
  events in a fixture, not browser/editor/terminal consumption.
- Wayland GNOME/KDE/Sway/Hyprland: without authorized input integration, copy and
  explicit manual fallback. Blocked portal/no session must not silently inject
  via XWayland/uinput. An authorized integration/session is not present yet and
  must not be advertised as tested.
- Run capture and select from menu repeatedly, including PasteDelete: no
  immediate self-recapture resurrection. External subsequent copies still pass
  privacy policy normally. Concurrent capture/history revision cannot erase a
  replacement item; report a preserved-record cleanup condition.

Keep these checks OPEN until actual observations are recorded. Prior backend and
menu object/property/protocol suites remain separate evidence.

# T13 manual acceptance — OPEN

T04–T12 acceptance remains OPEN. CI is not a clean Windows/macOS VM, interactive login session, permission dialog or screen-reader test. No clean VMs were available. Record build/source, OS/version/architecture, desktop/session/display scale, exact steps and observed result. Do not check these boxes from automated tests.

- [ ] macOS13+ proper signed Cliptiq.app: enable/disable Launch at login; cancel/deny approval, later approve in Login Items; readonly startup status and Settings reflect the actual state. Logout/login; no duplicates. Repeat after bundle move/removal. Unbundled development executable must report unsupported registration.
- [ ] Windows current-user install: enable/disable own HKCU Run value, logout/login, external OS disable, failure writing settings and registry, unrelated value untouched. GUI binary has no console window. Run actual unicode executable paths. Verify remove-autostart before removing files.
- [ ] Linux GNOME/KDE X11 and Wayland: owned XDG autostart enable/disable, logout/login, executable path spaces/quotes/percent, hidden/unowned desktop entry, read-only config, no tray, no portal, no data-control, unknown source. Capability reasons must be precise; manual copy and Settings stay reachable.
- [ ] Launch Main/History/Snippets/Settings/Editor from a second process or desktop action while first is running/backgrounded. One capture/store owner per user session; check simultaneous startup, kill/restart, startup failure and several consecutive Settings requests. Inconsistent launch environment must not accidentally create duplicate Linux scope.
- [ ] Tray hidden/unavailable and all global shortcuts unbound/conflicting: native launcher and `cliptiq --show-settings` recover access. Verify focus returns correctly, no unintended auto-paste from forwarded menu routes. Background launch must remain hidden even without a tray.
- [ ] EN/RU all tabs, editor and menus: long labels, keyboard-only traversal, visible focus, Enter/Escape, disabled reason/status, locale restart, missing/unknown locale English fallback. Clipboard/snippet/folder text unchanged, including strings identical to UI keys. Partial seven locales and RTL Persian need separate linguistic/visual acceptance.
- [ ] VoiceOver, Narrator and Orca: window/control names, label associations, checkbox state, hotkey capture, tab order, menu selection and status announcement. DPI100/150/200%, macOS Retina, long RU text, Linux fractional scaling; no clipped actionable labels.
- [ ] Offline launch/settings/editor/history/autostart state inspection. No network needed. No support/donation redirect to upstream project.
- [ ] Explicit --remove-autostart, then remove app/desktop entry; no dangling own startup registration. Installer/update lifecycle remains future work.

## First-run autostart (Windows/macOS)

- Fresh default profile, normal launch: enable Launch at login and save the
  preference automatically, without an application window. Disable it in Settings,
  restart and confirm it remains disabled.
- Restart and update in the same installation: do not show setup or re-register.
  A persisted disabled login preference from older releases is already configured.
  External OS disable must not be silently reversed on a later application launch.
- Windows: quoted stable EXE path with --background; opt-out removes an existing
  registration owned by this same executable. Disk-write failure restores prior OS
  registration; no successful choice is recorded. Native EN/RU checkbox tests never
  call the real registration backend.
- macOS 13+: use the real Cliptiq.app bundle. If approval is required, reflect it in Settings; keep the saved choice while
  the user approves it. Confirm actual logout/login and repeat after an update.
- Smoke/demo, background invocations and explicit database fixtures show no setup.
  Linux behavior is unchanged. First-run completion uses presence of t09.login;
  there is no schema-version reset or reapplication of a new default on upgrade.

## Quiet desktop launch

- Windows/macOS normal launch starts in the tray/menu bar without showing the main
  window. Only required system permission dialogs may interrupt the first launch.
- Starting the same app again (including --background or Finder reopen) must not
  open the main window or enqueue Main, and must not start a second capture owner.
- Tray click, hotkeys and --show-main/--show-history/--show-settings remain usable.
  An unavailable tray must not reveal a window. On Windows, Explorer becoming ready restores the tray icon; explicit --show-main remains available.

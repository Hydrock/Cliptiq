# T07 interactive acceptance — OPEN

T04-T06 manual gates remain OPEN; T08 is not started. No clean desktop VMs are available. Automated native object/protocol tests are not full desktop or accessibility acceptance.

Automated: common model grouping/numbering/order/Unicode17 conformance; bounded previews and96/144/192DPI raster calculations; native label/enable/tooltip/preview handle/selection/stale-node checks; independent SNI/DBusMenu layout/events and watcher restart in private bus; visible launcher with hidden tray. Linux screenshot uses only owned Xvfb root. Wayland no-input-event route is visible launcher, not arbitrary global coordinates.

For macOS13+ arm64/Intel, Windows10/11 and real Linux X11/GNOME/KDE/Sway/Hyprland record exact versions and verify:

- Main/history/snippets routes; initial enabled item, arrows/submenus, Enter/Esc, first-ten digits, layout RU/EN, native mnemonic escaping, no unintended selection on popup dismissal.
- VoiceOver/Narrator/Orca item labels, disabled-state announcements, submenu and launcher discovery; real screen-reader output is required.
- DPI100/150/200%, moving across monitors, light/dark/high contrast, grayscale/image previews, long Unicode titles and tooltip bounds.
- Tray standard/reversed/hidden, no tray host, host ignoring icons/tooltips, Explorer/SNI watcher restart and compositor restart. Launcher remains available even when popup activation is denied.
- History changes while popup is open, stale/deleted item, snippet folder order, Clear against concurrent capture. Settings/editor/paste are owned by future tasks and must not be mistaken for finished features.

Do not mark this checklist complete from a fixture screenshot or isolated native property test. Full support requires the later snippets/launcher/integration routes from the design.

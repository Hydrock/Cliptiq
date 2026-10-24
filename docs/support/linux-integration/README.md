# T01: Linux integration evidence, 2026-10-23

Executed locally in Ubuntu 24.04.3 under WSL2 x86_64, kernel 6.6.87.2, GCC 13.3.0. Reproduction: `bash experiments/linux-integration/run.sh` from the repository root. Final successful command printed `PASS: isolated X11 selection/hotkey, mock SNI/menu, headless Wayland data-control clipboard` and exited 0. `environment.txt` contains exact package versions; `source-sha256.txt` ties evidence to probe sources.

| Measurement | Executed result | Boundary |
|---|---|---|
| X11 clipboard | Actual selection ownership and UTF-8 byte roundtrip PASS | Xvfb 21.1.12, two independent client connections; no desktop session |
| X11 global hotkey | Root passive Ctrl+Shift+F12 grab PASS | Another client focused; synthetic XTest input, no physical keyboard/conflict policy test |
| StatusNotifierItem registration | Mock watcher wire call PASS | Private D-Bus, not GNOME/KDE shell rendering |
| Menu interaction | Get(Menu), GetLayout, clicked Event and Activate PASS | Minimal dbusmenu service, not full host conformance or visual menu |
| Native Wayland clipboard | Registry advertisement + exact UTF-8 copy/read PASS | Sway 1.9 / wlroots 0.17.1, headless pixman, real data-control v2, wl-clipboard 2.2.1 |
| Portal detection | Desktop portal activated; GlobalShortcuts=false, Clipboard=false | Actual private-session introspection of installed xdg-desktop-portal 1.18.4; unavailable interfaces in this session |
| Wayland global hotkey | **Unavailable through the measured portal** | No BindShortcuts or activation event was claimed; compositor-specific configuration not implemented |

`x11.json`, `dbus.json`, and `wayland.json` are outcomes. `dbus.json.portal.xml` is actual portal introspection; `wayland-info.txt` is the actual compositor registry. Private portal warnings and fallback choice are retained in `dbus.log`; having GNOME/GTK portal packages installed does not mean a GNOME session was tested. Sway created a seat with no physical input capabilities. No physical-key, compositor security/policy, locked-screen or hotkey conflict behavior was measured.

## Footprint attribution

The optimized/stripped X11 **test probe** is 14,472 bytes; its `ldd` closure is 4,117,152 bytes in this environment, including loader/libc and the XTest test-input library. This is neither a production adapter size nor incremental cost relative to the existing GTK host. Exact paths, sizes and executable hash are in `x11-size.json`; `app_owned_production_bytes` and `offline_install_total_bytes` remain null.

The SNI/menu test uses system Python, python3-dbus and PyGObject; production C++ could use the existing GIO stack, but that incremental implementation footprint has not been measured. wl-copy/wl-paste and wayland-info are test utilities, Sway is the test compositor, and Xvfb is the test X server. They are not proposed Cliptiq runtime payload. `environment.txt` gives package Installed-Size values in KiB for selected test components; those are package metadata, not clean-machine dependency delta, compressed download, or full offline closure.

Dependencies added for this run were requested using `apt-get install -y sway wl-clipboard wayland-utils python3-dbus`; Python D-Bus was already installed, and apt installed recommended/transitive test dependencies. This modified the WSL test environment, not product packaging. It is not a clean target image. The scripts require no pip packages and leave no binaries in the repository.

## Decision and remaining scope

T01 now has executed Linux protocol measurements beyond GTK/Xvfb window launch: the X11 ownership/key path, SNI wire plumbing, and native wlroots data-control path work in the stated isolated environments. A Wayland backend must detect compositor protocol and portal capabilities at runtime and expose an explicit unsupported/degraded state when unavailable. This evidence does not justify a generic Linux, GNOME, KDE, or all-Wayland support promise.

Still unmeasured: real GNOME/KDE tray host behavior (including extensions), portal shortcut consent/binding/activation on supporting desktops, actual desktop-shell UX, compositor-specific hotkey integration, physical keys, image/multiple-MIME capture, long-running ownership monitoring, retention after owner exits, reconnect/shutdown races and clean-machine deployment closure. The CLI tools are proof of protocol behavior, not finished native C++ Wayland adapters. T02 remains untouched.

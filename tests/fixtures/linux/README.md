# Linux isolation and fault fixtures

run-linux-isolated.py clears ambient display/session endpoints and owns private Xvfb or headless Sway + private DBus/runtime. No fallback to user clipboard. No PRIMARY access. Tests assert the isolation flag.

X11Peer is an independent XCB owner; WaylandPeer is a real libwayland-server wire peer implementing ext-data-control, not a compositor or desktop. It also advertises a wlr manager to verify ext preference, without implementing wlr exchange; actual wlr exchange is tested against Sway. Fault modes cover source death, stalled INCR/FD, cancellation, changed offer, finished device, missing data control, seats and byte limits. X11 test-only linker wrappers inject real native notifications at deterministic reply boundaries; no production hooks. Pure queue lifetime tests ensure stalled representation sends cannot retain unrelated snapshot formats. Raw synthetic image bytes test transport, not a decoder.

linux_wl_clipboard.py uses independent installed wl-copy/wl-paste inside Sway for UTF-8/replay and KDE secret MIME. For versions with no --sensitive flag, it explicitly offers the KDE MIME instead; the combined text+hint case belongs to independent wire tests. No app-to-app GUI or full Linux desktop acceptance is implied.

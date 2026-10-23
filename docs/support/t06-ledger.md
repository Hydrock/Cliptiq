# T06 execution — 2026-10-23

BASE 4f6239d9f40ba5d70ef15bc90bb800dadeaa8990. User authorizes Linux X11/Wayland backend, privacy gate tests, footprint measurements and upload. T04/T05 manual gates remain OPEN; T07+ excluded. Reuse clean dedicated codex/portability-plan checkout/draft PR. Approved C++ native architecture remains binding.

Pre-flight T02/T03/T04 -> T06: common ClipboardBackend, native bytes + complete offered markers, trusted type registry, source confidence, mandatory dispatch_capture before Store/derivatives. Backend event ownership stays on caller event thread; serial worker alone owns SQLite. No GTK menu/tray/launcher implementation in T06.

Ruling: XCB + XFixes provides X11 native protocol with checked/asynchronous replies and no global Xlib error handler. It is the XFixes design from plan, with XCB transport instead of Xlib; owner death must not terminate the process. Wayland uses system libwayland-client, pinned ext-data-control v1 preferred and wlr v2 fallback only when ext is absent. No GTK focus workaround or portal consent bypass. No PRIMARY capture.

Ruling: native tests/size hosts launch isolated Xvfb/headless compositor/private runtime directory; never connect to ambient user's DISPLAY/WAYLAND_DISPLAY. Protocol fault fixtures and a libwayland-server test peer are labelled wire fixtures, not a real GNOME/KDE/Hyprland desktop. Real compositor versions/capabilities are recorded separately. Desktop support floor also requires future snippets/UI/launcher (T07+), so backend capability is not a claim of supported full history desktop.

Ruling: libwayland/XCB are system dynamic dependencies, no GTK/web runtime addition in this backend. Decoders/thumbnails belong to T12; T06 preserves bounded native image bytes and does not claim pixel decode safety. Capture stream has per-representation/entry/count/deadline limits before persistence; source sends have bounded count/retained bytes/deadlines and safe SIGPIPE handling.

Ruling: text/uri-list and GNOME copied-files are file/reference list representations; URL-specific text/x-moz-url plus plain-text alternatives supported. Ambiguous generic URI lists are conservatively Files for kind filtering rather than misclassifying file references as URLs. This format-level limitation is documented, not hidden parity. GNOME cut lists replay as copy. X11 STRING converts Latin-1 to UTF8_STRING; raw native RTF/PDF/RTFD remains opaque bytes.

Status: tests/stubs and environment setup first; native RED, implementation, review, CI and measurements pending.

Local RED verified: out/t06-red.log has 12/13 initial protocol cases fail (one generic unknown-type sanity already passes). Shared streams/format code now passes 12 cases, remaining protocol failure is intentionally absent Wayland capability implementation. Six X11 + six real Sway wlr native cases all fail on Not implemented before OS adapter code. Core policy/history/crash remain green. Dev header addition in WSL: libxcb-xfixes0-dev + libxcb-shape0-dev, 171 kB installed, build environment only.

#include "cliptiq/paste.hpp"
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>
namespace cliptiq {
namespace {
struct ServerGuard {
  Display *display;
  explicit ServerGuard(Display *d) : display(d) { XGrabServer(display); }
  ~ServerGuard() {
    XUngrabServer(display);
    XFlush(display);
  }
};
bool own_window(Display *display, Window window) {
  auto atom = XInternAtom(display, "_NET_WM_PID", True);
  for (unsigned depth = 0;
       window && window != DefaultRootWindow(display) && depth < 32; ++depth) {
    if (atom) {
      Atom type = 0;
      int format = 0;
      unsigned long n = 0, remaining = 0;
      unsigned char *data = nullptr;
      auto result =
          XGetWindowProperty(display, window, atom, 0, 1, False, XA_CARDINAL,
                             &type, &format, &n, &remaining, &data);
      bool own = result == Success && type == XA_CARDINAL && format == 32 &&
                 n == 1 && data &&
                 *reinterpret_cast<unsigned long *>(data) ==
                     static_cast<unsigned long>(getpid());
      if (data)
        XFree(data);
      if (own)
        return true;
    }
    Window root, parent, *children = nullptr;
    unsigned count = 0;
    auto result =
        XQueryTree(display, window, &root, &parent, &children, &count);
    if (children)
      XFree(children);
    if (!result || parent == window)
      break;
    window = parent;
  }
  return false;
}
} // namespace

struct NativePasteBackend::State {
  std::thread::id owner = std::this_thread::get_id();
  Display *display = nullptr;
  Window target = 0;
  TargetToken token;
  std::uint64_t serial = 0;
  bool wayland = false, xtest = false;
  State() {
    wayland = getenv("WAYLAND_DISPLAY") ||
              (getenv("XDG_SESSION_TYPE") &&
               std::string(getenv("XDG_SESSION_TYPE")) == "wayland");
    if (!wayland) {
      display = XOpenDisplay(nullptr);
      if (display) {
        int event, error, major, minor;
        xtest = XTestQueryExtension(display, &event, &error, &major, &minor);
      }
    }
  }
  ~State() {
    if (display)
      XCloseDisplay(display);
  }
};
NativePasteBackend::NativePasteBackend() : state_(std::make_unique<State>()) {}
NativePasteBackend::~NativePasteBackend() = default;
Capability NativePasteBackend::capability() const {
  auto &s = *state_;
  if (s.wayland)
    return {CapabilityStatus::Unavailable,
            "No authorized Wayland input session; manual paste"};
  if (!s.display || !s.xtest)
    return {CapabilityStatus::Unavailable,
            "X11/XTEST unavailable; manual paste"};
  return {CapabilityStatus::Limited,
          "XTest/XKB; captured target and released modifiers required; no "
          "receipt confirmation"};
}
TargetToken NativePasteBackend::capture_target() {
  auto &s = *state_;
  s.token.clear();
  s.target = 0;
  if (s.owner != std::this_thread::get_id() || !s.display || !s.xtest)
    return {};
  ServerGuard server(s.display);
  int revert;
  XGetInputFocus(s.display, &s.target, &revert);
  if (!s.target || s.target == PointerRoot ||
      s.target == DefaultRootWindow(s.display)) {
    s.target = 0;
    return {};
  }
  if (own_window(s.display, s.target)) {
    s.target = 0;
    return {};
  }
  XSelectInput(s.display, s.target, StructureNotifyMask);
  XSync(s.display, False);
  while (XPending(s.display)) {
    XEvent event;
    XNextEvent(s.display, &event);
    if (event.type == DestroyNotify &&
        event.xdestroywindow.window == s.target) {
      s.target = 0;
      return {};
    }
  }
  s.token = "x11-target:" + std::to_string(++s.serial) + ":" +
            std::to_string(s.target);
  return s.token;
}
Result<void> NativePasteBackend::dispatch(const TargetToken &token,
                                          Shortcut shortcut) {
  auto &s = *state_;
  auto expected = std::move(s.token);
  s.token.clear();
  auto fail = [](const char *message) -> Result<void> {
    return Error{ErrorCode::Unsupported, message};
  };
  if (s.owner != std::this_thread::get_id())
    return fail("Paste requires owner GUI thread");
  if (s.wayland)
    return fail(
        "Wayland input requires an authorized desktop session; paste manually");
  if (!s.display || !s.xtest)
    return fail("X11 XTEST unavailable; paste manually");
  if (token.empty() || token != expected || static_cast<unsigned>(shortcut) > 1)
    return fail("Expired or invalid paste target");
  ServerGuard server(s.display);
  XSync(s.display, False);
  while (XPending(s.display)) {
    XEvent event;
    XNextEvent(s.display, &event);
    if (event.type == DestroyNotify && event.xdestroywindow.window == s.target)
      return fail("Paste target closed");
  }
  Window focus;
  int revert;
  XGetInputFocus(s.display, &focus, &revert);
  if (focus != s.target)
    return fail("Focus changed; paste manually");
  XkbStateRec keyboard{};
  if (XkbGetState(s.display, XkbUseCoreKbd, &keyboard) != Success ||
      keyboard.base_mods || keyboard.latched_mods)
    return fail("Release held modifiers before pasting");
  auto map = XkbGetKeyboard(s.display, XkbAllComponentsMask, XkbUseCoreKbd);
  if (!map)
    return fail("XKB keymap unavailable");
  unsigned v = 0;
  if (map->names && map->names->keys)
    for (unsigned code = map->min_key_code; code <= map->max_key_code; ++code)
      if (std::memcmp(map->names->keys[code].name, "AB04", 4) == 0) {
        v = code;
        break;
      }
  XkbFreeKeyboard(map, 0, True);
  auto control = XKeysymToKeycode(s.display, XK_Control_L),
       shift = XKeysymToKeycode(s.display, XK_Shift_L);
  char keys[32];
  XQueryKeymap(s.display, keys);
  if (!v || !control || !shift || (keys[v / 8] & (1 << (v % 8))))
    return fail("Paste key unavailable or held");
  // Recheck at the dispatch boundary; never set focus to an unrelated window.
  XGetInputFocus(s.display, &focus, &revert);
  if (focus != s.target)
    return fail("Focus changed before dispatch");
  bool terminal = shortcut == Shortcut::TerminalPaste;
  bool sent = XTestFakeKeyEvent(s.display, control, True, CurrentTime);
  if (terminal)
    sent = XTestFakeKeyEvent(s.display, shift, True, CurrentTime) && sent;
  sent = XTestFakeKeyEvent(s.display, v, True, CurrentTime) && sent;
  sent = XTestFakeKeyEvent(s.display, v, False, CurrentTime) && sent;
  if (terminal)
    sent = XTestFakeKeyEvent(s.display, shift, False, CurrentTime) && sent;
  sent = XTestFakeKeyEvent(s.display, control, False, CurrentTime) && sent;
  XSync(s.display, False);
  return sent ? Result<void>{}
              : fail("XTEST rejected input; application receipt unknown");
}
} // namespace cliptiq

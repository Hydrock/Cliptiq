#include "cliptiq/hotkeys.hpp"
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#include "../fixtures/windows/IsolatedClipboard.hpp"
#elif defined(__APPLE__)
#import <Carbon/Carbon.h>
#else
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <chrono>
#include <thread>
#include <xcb/xcb.h>
#endif
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
int run() {
  try {
#ifdef _WIN32
    IsolatedClipboard station;
#endif
    unsigned events = 0;
    ActionId selected;
    NativeHotkeys native([&](ActionId a, InvocationContext) {
      ++events;
      selected = std::move(a);
    });
#if !defined(_WIN32) && !defined(__APPLE__)
    if (getenv("WAYLAND_DISPLAY")) {
      auto result = native.acquire("main", take(parse_chord("Ctrl+Alt+Q")));
      CHECK(!result && result.error().code == ErrorCode::Unsupported);
      CHECK(native.capability().status != CapabilityStatus::Available);
      std::cout << "PASS wayland_no_unapproved_global_grab\n";
      return 0;
    }
    auto display = XOpenDisplay(nullptr);
    CHECK(display);
    auto window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      100, 100, 0, 0, 0);
    XMapWindow(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XSync(display, False);
    auto peer = xcb_connect(nullptr, nullptr);
    auto root = xcb_setup_roots_iterator(xcb_get_setup(peer)).data->root;
    auto q = XKeysymToKeycode(display, XK_q),
         w = XKeysymToKeycode(display, XK_w);
    auto conflict = xcb_grab_key_checked(
        peer, 0, root, XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1, w,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    CHECK(!xcb_request_check(peer, conflict));
    {
      HotkeyRegistry registry(native);
      CHECK(take(registry.bind_hotkey("main", take(parse_chord("Ctrl+Alt+Q"))))
                .registered);
      auto blocked =
          registry.bind_hotkey("main", take(parse_chord("Ctrl+Alt+W")));
      CHECK(!blocked && blocked.error().code == ErrorCode::Busy &&
            registry.registered("main"));
      auto emit = [&] {
        auto ctrl = XKeysymToKeycode(display, XK_Control_L),
             alt = XKeysymToKeycode(display, XK_Alt_L);
        XTestFakeKeyEvent(display, ctrl, True, 0);
        XTestFakeKeyEvent(display, alt, True, 0);
        XTestFakeKeyEvent(display, q, True, 0);
        XTestFakeKeyEvent(display, q, False, 0);
        XTestFakeKeyEvent(display, alt, False, 0);
        XTestFakeKeyEvent(display, ctrl, False, 0);
        XSync(display, False);
        for (int i = 0; i < 20; ++i) {
          CHECK(native.dispatch());
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
      };
      emit();
      CHECK(events == 1 && selected == "main");
      XkbLockModifiers(display, XkbUseCoreKbd, LockMask, LockMask);
      XSync(display, False);
      emit();
      CHECK(events == 2);
      XkbLockModifiers(display, XkbUseCoreKbd, LockMask, 0);
      XSync(display, False);
      std::cout
          << "PASS "
             "native_x11_conflict_rollback_real_activation_and_caps_lock\n";
    }
    auto reusable = xcb_grab_key_checked(
        peer, 0, root, XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1, q,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    CHECK(!xcb_request_check(peer, reusable));
    xcb_disconnect(peer);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
#else
    {
      HotkeyRegistry registry(native);
      CHECK(take(registry.bind_hotkey("main", take(parse_chord("Ctrl+Alt+Q"))))
                .registered);
      NativeHotkeys peer([](auto, auto) {});
      auto conflict = peer.acquire("history", take(parse_chord("Ctrl+Alt+Q")));
      CHECK(!conflict && conflict.error().code == ErrorCode::Busy);
      CHECK(registry.bind_hotkey("main", take(parse_chord("Ctrl+Alt+W"))));
      CHECK(!registry.bind_hotkey("settings", {}));
    }
    CHECK(native.dispatch());
    std::cout
        << "PASS_native_registration_conflict_rebind_and_lifetime_cleanup\n";
#endif
    std::cout << "PASS no_ambient_key_injection\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL native_hotkeys: " << e.what() << '\n';
    return 1;
  }
}
int main() {
#ifdef __APPLE__
  @autoreleasepool {
    return run();
  }
#else
  return run();
#endif
}

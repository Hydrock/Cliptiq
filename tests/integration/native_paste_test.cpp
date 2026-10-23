#include "Clipboard.hpp"
#include "cliptiq/paste.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#elif defined(_WIN32)
#include "../fixtures/windows/IsolatedClipboard.hpp"
#else
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <cstdlib>
#include <unistd.h>
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
    IsolatedClipboard isolated;
#endif
    NativePasteBackend paste;
    CHECK(!paste.dispatch("forged:42", Shortcut::Paste));
    CHECK(!paste.dispatch("", Shortcut::Paste));
    std::cout << "PASS forged_and_missing_targets_never_dispatch\n";
#if !defined(__APPLE__) && !defined(_WIN32)
    if (getenv("WAYLAND_DISPLAY")) {
      CHECK(paste.capture_target().empty());
      CHECK(paste.capability().status == CapabilityStatus::Unavailable);
      CHECK(!paste.dispatch("portal-without-authorized-session",
                            Shortcut::Paste));
      std::cout << "PASS wayland_without_authorized_session_manual_fallback\n";
      return 0;
    }
    CHECK(paste.capability().status == CapabilityStatus::Limited);
    auto d = XOpenDisplay(nullptr);
    CHECK(d);
    auto window =
        XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 200, 100, 0, 0, 0);
    auto other =
        XCreateSimpleWindow(d, DefaultRootWindow(d), 250, 0, 200, 100, 0, 0, 0);
    XSelectInput(d, window, KeyPressMask | KeyReleaseMask);
    XMapWindow(d, window);
    XMapWindow(d, other);
    XSetInputFocus(d, window, RevertToParent, CurrentTime);
    XSync(d, False);
    auto drain = [&] {
      XSync(d, False);
      while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
      }
    };
    auto pid_atom = XInternAtom(d, "_NET_WM_PID", False);
    unsigned long self = getpid();
    XChangeProperty(d, window, pid_atom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&self), 1);
    XSync(d, False);
    CHECK(paste.capture_target().empty());
    XDeleteProperty(d, window, pid_atom);
    XSync(d, False);
    std::cout << "PASS self_launcher_target_rejected\n";
    auto target = paste.capture_target();
    CHECK(!target.empty());
    CHECK(paste.dispatch(target, Shortcut::Paste));
    XSync(d, False);
    unsigned presses = 0, releases = 0, vcode = 0;
    bool ctrl = false;
    while (XPending(d)) {
      XEvent e;
      XNextEvent(d, &e);
      if (e.type == KeyPress) {
        ++presses;
        if (e.xkey.state & ControlMask) {
          ctrl = true;
          vcode = e.xkey.keycode;
        }
      }
      if (e.type == KeyRelease)
        ++releases;
    }
    CHECK(presses == 2 && releases == 2 && ctrl && vcode);
    CHECK(!paste.dispatch(target, Shortcut::Paste));
    std::cout << "PASS real_xtest_ctrl_v_and_single_use_token\n";
    target = paste.capture_target();
    CHECK(paste.dispatch(target, Shortcut::TerminalPaste));
    XSync(d, False);
    bool shifted = false;
    while (XPending(d)) {
      XEvent e;
      XNextEvent(d, &e);
      if (e.type == KeyPress && unsigned(e.xkey.keycode) == vcode)
        shifted = (e.xkey.state & (ControlMask | ShiftMask)) ==
                  (ControlMask | ShiftMask);
    }
    CHECK(shifted);
    std::cout << "PASS terminal_ctrl_shift_v\n";
    target = paste.capture_target();
    XSetInputFocus(d, other, RevertToParent, CurrentTime);
    XSync(d, False);
    CHECK(!paste.dispatch(target, Shortcut::Paste));
    drain();
    std::cout << "PASS unrelated_focus_change_rejected\n";
    XSetInputFocus(d, window, RevertToParent, CurrentTime);
    XSync(d, False);
    target = paste.capture_target();
    auto control = XKeysymToKeycode(d, XStringToKeysym("Control_L"));
    CHECK(control);
    XTestFakeKeyEvent(d, control, True, CurrentTime);
    XSync(d, False);
    drain();
    CHECK(!paste.dispatch(target, Shortcut::Paste));
    char held[32];
    XQueryKeymap(d, held);
    CHECK(held[control / 8] & (1 << (control % 8)));
    XTestFakeKeyEvent(d, control, False, CurrentTime);
    XSync(d, False);
    drain();
    std::cout << "PASS held_modifier_not_released_or_used\n";
    CHECK(system("setxkbmap -layout ru") == 0);
    XSync(d, False);
    target = paste.capture_target();
    CHECK(paste.dispatch(target, Shortcut::Paste));
    XSync(d, False);
    unsigned ru_code = 0;
    while (XPending(d)) {
      XEvent e;
      XNextEvent(d, &e);
      if (e.type == KeyPress && (e.xkey.state & ControlMask))
        ru_code = e.xkey.keycode;
    }
    CHECK(ru_code == vcode);
    CHECK(system("setxkbmap -layout us") == 0);
    std::cout << "PASS ru_layout_physical_paste_key\n";
    CHECK(system("setxkbmap -layout us -variant dvorak") == 0);
    XSync(d, False);
    target = paste.capture_target();
    CHECK(paste.dispatch(target, Shortcut::Paste));
    XSync(d, False);
    unsigned dvorak = 0;
    while (XPending(d)) {
      XEvent e;
      XNextEvent(d, &e);
      if (e.type == KeyPress && (e.xkey.state & ControlMask))
        dvorak = e.xkey.keycode;
    }
    CHECK(dvorak == XKeysymToKeycode(d, XStringToKeysym("v")));
    std::cout << "PASS dvorak_effective_v_shortcut\n";
    CHECK(system("setxkbmap -layout us -variant ''") == 0);
    XSync(d, False);
    auto old_v = XKeysymToKeycode(d, XStringToKeysym("v")),
         new_v = XKeysymToKeycode(d, XStringToKeysym("p"));
    KeySym replacement = XStringToKeysym("x"), sym_v = XStringToKeysym("v");
    XChangeKeyboardMapping(d, old_v, 1, &replacement, 1);
    XChangeKeyboardMapping(d, new_v, 1, &sym_v, 1);
    XSync(d, False);
    target = paste.capture_target();
    CHECK(paste.dispatch(target, Shortcut::Paste));
    XSync(d, False);
    unsigned remapped = 0;
    while (XPending(d)) {
      XEvent e;
      XNextEvent(d, &e);
      if (e.type == KeyPress && (e.xkey.state & ControlMask))
        remapped = e.xkey.keycode;
    }
    CHECK(remapped == new_v);
    std::cout << "PASS remapped_effective_v_shortcut\n";
    CHECK(system("setxkbmap -layout us -variant ''") == 0);
    XSync(d, False);
    target = paste.capture_target();
    XDestroyWindow(d, window);
    XSync(d, False);
    CHECK(!paste.dispatch(target, Shortcut::Paste));
    XDestroyWindow(d, other);
    XCloseDisplay(d);
    std::cout << "PASS closed_target_rejected\n";
    // Real backend write/capture ownership path; no ambient display/clipboard.
    linux_backend::ClipboardX11 clipboard;
    unsigned captures = 0;
    CHECK(clipboard.start([&](auto) { ++captures; }));
    auto store = take(open_store(":memory:"));
    ClipboardSnapshot snap;
    snap.representations = {
        {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'s', 'a', 'f', 'e'}}};
    auto id = *take(ingest(store, snap, {}, 1));
    SelectionIntent i;
    i.command = {MenuAction::SelectHistory, id, take(data_revision(store))};
    i.auto_paste = false;
    auto result = take(execute_selection(store, i, clipboard, paste));
    CHECK(result.status == SelectionStatus::Copied);
    for (int n = 0; n < 10; ++n)
      CHECK(clipboard.dispatch(5));
    CHECK(captures == 0 && take(list_history(store)).size() == 1);
    clipboard.stop();
    std::cout << "PASS selection_native_write_self_capture_suppressed\n";
#else
    // Hosted macOS has no Accessibility approval; noninteractive Windows
    // station cannot be used as evidence of target-app receipt. Only failure
    // safety here.
    CHECK(!paste.dispatch("stale-native-target", Shortcut::TerminalPaste));
    std::cout << "PASS native_denial_safe_no_permission_prompt\n";
#endif
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL native_paste: " << e.what() << '\n';
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

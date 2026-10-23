#include "PasteNative.hpp"
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <thread>
namespace cliptiq {
namespace {
AXUIElementRef focused(pid_t pid) {
  auto app = AXUIElementCreateApplication(pid);
  CFTypeRef value = nullptr;
  auto error =
      AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, &value);
  CFRelease(app);
  if (error != kAXErrorSuccess || !value)
    return nullptr;
  if (CFGetTypeID(value) != AXUIElementGetTypeID()) {
    CFRelease(value);
    return nullptr;
  }
  return static_cast<AXUIElementRef>(const_cast<void *>(value));
}
int paste_key() {
  auto source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
  if (!source)
    return -1;
  auto data = static_cast<CFDataRef>(
      TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
  int result = -1;
  if (data) {
    auto layout =
        reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
    for (unsigned key = 0; key < 128; ++key) {
      UInt32 dead = 0;
      UniChar text[4];
      UniCharCount size = 0;
      if (UCKeyTranslate(layout, key, kUCKeyActionDisplay, 0, LMGetKbdType(),
                         kUCKeyTranslateNoDeadKeysBit, &dead, 4, &size,
                         text) == noErr &&
          size == 1 && (text[0] == 'v' || text[0] == 'V')) {
        result = key;
        break;
      }
    }
  }
  CFRelease(source);
  return result;
}

struct MacTarget {
  NSRunningApplication *application = nil;
  NSDate *launched = nil;
  AXUIElementRef window = nullptr;
  ~MacTarget() {
    if (window)
      CFRelease(window);
  }
  bool capture() {
    if (window) {
      CFRelease(window);
      window = nullptr;
    }
    application = nil;
    launched = nil;
    auto app = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!app ||
        app.processIdentifier == NSProcessInfo.processInfo.processIdentifier)
      return false;
    auto focused_window = focused(app.processIdentifier);
    if (!focused_window || !app.launchDate) {
      if (focused_window)
        CFRelease(focused_window);
      return false;
    }
    application = app;
    launched = app.launchDate;
    window = focused_window;
    return true;
  }
  bool alive() {
    return application && !application.terminated &&
           [application.launchDate isEqualToDate:launched];
  }
  bool same() {
    if (!alive())
      return false;
    auto current = focused(application.processIdentifier);
    bool result = current && window && CFEqual(current, window);
    if (current)
      CFRelease(current);
    return result;
  }
};
std::shared_ptr<NativePasteCalls> native_calls() {
  auto c = std::make_shared<NativePasteCalls>();
  auto target = std::make_shared<MacTarget>();
  c->trusted = [] {
    return AXIsProcessTrusted() && CGPreflightPostEventAccess();
  };
  c->capture = [target] { return target->capture(); };
  c->alive = [target] { return target->alive(); };
  c->same_window = [target] { return target->same(); };
  c->foreground = [target] {
    auto pid =
        NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
    if (target->application && pid == target->application.processIdentifier)
      return PasteForeground::Target;
    if (pid == NSProcessInfo.processInfo.processIdentifier)
      return PasteForeground::Self;
    return PasteForeground::Other;
  };
  c->activate = [target] {
    return [target->application activateWithOptions:0] != NO;
  };
  c->held_flags = [] {
    return CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  };
  c->paste_key = [] { return paste_key(); };
  c->key_down = [](int key) {
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                 static_cast<CGKeyCode>(key));
  };
  c->create = [](CGKeyCode key, bool down) {
    return CGEventCreateKeyboardEvent(nullptr, key, down);
  };
  c->post = [](CGEventRef event) { CGEventPost(kCGHIDEventTap, event); };
  return c;
}
} // namespace
struct NativePasteBackend::State {
  std::thread::id owner = std::this_thread::get_id();
  std::shared_ptr<NativePasteCalls> calls;
  TargetToken token;
  std::uint64_t serial = 0;
};
NativePasteBackend::NativePasteBackend() : NativePasteBackend(native_calls()) {}
NativePasteBackend::NativePasteBackend(std::shared_ptr<NativePasteCalls> calls)
    : state_(std::make_unique<State>()) {
  state_->calls = std::move(calls);
}
NativePasteBackend::~NativePasteBackend() = default;
Capability NativePasteBackend::capability() const {
  if (!state_->calls->trusted())
    return {CapabilityStatus::Unavailable,
            "Accessibility/input permission not granted; no automatic prompt; "
            "manual paste"};
  return {CapabilityStatus::Limited,
          "CGEvent/keymap; captured window and released modifiers required; no "
          "receipt confirmation"};
}
TargetToken NativePasteBackend::capture_target() {
  auto &s = *state_;
  s.token.clear();
  if (s.owner != std::this_thread::get_id() || !NSThread.isMainThread ||
      !s.calls->trusted() || !s.calls->capture())
    return {};
  s.token = "mac-target:" + std::to_string(++s.serial);
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
  if (s.owner != std::this_thread::get_id() || !NSThread.isMainThread ||
      token.empty() || token != expected || static_cast<unsigned>(shortcut) > 1)
    return fail("Expired or invalid paste target");
  if (!s.calls->trusted())
    return fail("Accessibility/input permission unavailable; paste manually");
  if (!s.calls->alive())
    return fail("Paste target process closed or replaced");
  auto held = s.calls->held_flags();
  if (held & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl |
              kCGEventFlagMaskShift | kCGEventFlagMaskAlternate))
    return fail("Release held modifiers before pasting");
  if (!s.calls->same_window())
    return fail("Target focused window changed");
  auto foreground = s.calls->foreground();
  if (foreground != PasteForeground::Target) {
    if (foreground != PasteForeground::Self)
      return fail("Focus changed; paste manually");
    if (!s.calls->activate())
      return fail("macOS denied normal application activation");
  }
  int key = s.calls->paste_key();
  if (key < 0 || s.calls->foreground() != PasteForeground::Target ||
      !s.calls->same_window() || !s.calls->alive())
    return fail("Paste key/focus unavailable");
  if (s.calls->key_down(key))
    return fail("Release held paste key");
  auto down = s.calls->create(static_cast<CGKeyCode>(key), true),
       up = s.calls->create(static_cast<CGKeyCode>(key), false);
  if (!down || !up) {
    if (down)
      CFRelease(down);
    if (up)
      CFRelease(up);
    return fail("Cannot allocate input events");
  }
  // macOS Terminal uses Command+V for the clipboard too; Shift+Command+V is
  // Paste Selection and must never substitute unrelated terminal text.
  CGEventSetFlags(down, kCGEventFlagMaskCommand|(shortcut==Shortcut::TerminalPaste?kCGEventFlagMaskShift:0));
  CGEventSetFlags(up, kCGEventFlagMaskCommand|(shortcut==Shortcut::TerminalPaste?kCGEventFlagMaskShift:0));
  s.calls->post(down);
  s.calls->post(up);
  CFRelease(down);
  CFRelease(up);
  return {};
}
} // namespace cliptiq

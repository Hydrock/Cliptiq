#include "cliptiq/paste.hpp"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include <thread>
namespace cliptiq {
struct NativePasteBackend::State {
  std::thread::id owner = std::this_thread::get_id();
  NSRunningApplication *application = nil;
  NSDate *launched = nil;
  AXUIElementRef window = nullptr;
  TargetToken token;
  std::uint64_t serial = 0;
  ~State() {
    if (window)
      CFRelease(window);
  }
  void clear() {
    token.clear();
    application = nil;
    launched = nil;
    if (window) {
      CFRelease(window);
      window = nullptr;
    }
  }
};
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
} // namespace
NativePasteBackend::NativePasteBackend() : state_(std::make_unique<State>()) {}
NativePasteBackend::~NativePasteBackend() = default;
Capability NativePasteBackend::capability() const {
  if (!AXIsProcessTrusted() || !CGPreflightPostEventAccess())
    return {CapabilityStatus::Unavailable,
            "Accessibility/input permission not granted; no automatic prompt; "
            "manual paste"};
  return {CapabilityStatus::Limited,
          "CGEvent/keymap; captured window and released modifiers required; no "
          "receipt confirmation"};
}
TargetToken NativePasteBackend::capture_target() {
  auto &s = *state_;
  s.clear();
  if (s.owner != std::this_thread::get_id() || !NSThread.isMainThread ||
      !AXIsProcessTrusted() || !CGPreflightPostEventAccess())
    return {};
  auto app = NSWorkspace.sharedWorkspace.frontmostApplication;
  if (!app ||
      app.processIdentifier == NSProcessInfo.processInfo.processIdentifier)
    return {};
  auto window = focused(app.processIdentifier);
  if (!window || !app.launchDate) {
    if (window)
      CFRelease(window);
    return {};
  }
  s.application = app;
  s.launched = app.launchDate;
  s.window = window;
  s.token = "mac-target:" + std::to_string(++s.serial) + ":" +
            std::to_string(app.processIdentifier);
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
  if (!AXIsProcessTrusted() || !CGPreflightPostEventAccess())
    return fail("Accessibility/input permission unavailable; paste manually");
  if (!s.application || s.application.terminated ||
      ![s.application.launchDate isEqualToDate:s.launched])
    return fail("Paste target process closed or replaced");
  auto held = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  if (held & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl |
              kCGEventFlagMaskShift | kCGEventFlagMaskAlternate))
    return fail("Release held modifiers before pasting");
  auto window = focused(s.application.processIdentifier);
  bool same = window && s.window && CFEqual(window, s.window);
  if (window)
    CFRelease(window);
  if (!same)
    return fail("Target focused window changed");
  auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication;
  if (foreground.processIdentifier != s.application.processIdentifier) {
    if (foreground.processIdentifier !=
        NSProcessInfo.processInfo.processIdentifier)
      return fail("Focus changed; paste manually");
    if (![s.application activateWithOptions:0])
      return fail("macOS denied normal application activation");
  }
  int key = paste_key();
  if (key < 0 ||
      NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier !=
          s.application.processIdentifier)
    return fail("Paste key/focus unavailable");
  if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                            static_cast<CGKeyCode>(key)))
    return fail("Release held paste key");
  auto down =
      CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), true);
  auto up =
      CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), false);
  if (!down || !up) {
    if (down)
      CFRelease(down);
    if (up)
      CFRelease(up);
    return fail("Cannot allocate input events");
  }
  auto flags =
      kCGEventFlagMaskCommand |
      (shortcut == Shortcut::TerminalPaste ? kCGEventFlagMaskShift : 0);
  CGEventSetFlags(down, flags);
  CGEventSetFlags(up, flags);
  CGEventPost(kCGHIDEventTap, down);
  CGEventPost(kCGHIDEventTap, up);
  CFRelease(down);
  CFRelease(up);
  return {};
}
} // namespace cliptiq

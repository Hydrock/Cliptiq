#include "cliptiq/hotkeys.hpp"
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <atomic>
#include <cctype>
#include <thread>
namespace cliptiq {
namespace {
int code_for(const KeyChord &c) {
  if (c.meaning == KeyMeaning::Physical)
    return c.physical_code < 128 ? int(c.physical_code) : -1;
  if (c.key.size() > 1) {
    const int codes[] = {122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103};
    auto n = std::stoi(c.key.substr(1));
    return n >= 1 && n <= 11 ? codes[n - 1] : -1;
  }
  auto source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
  if (!source)
    return -1;
  auto data = static_cast<CFDataRef>(
      TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
  int out = -1;
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
          size == 1 && text[0] < 128 &&
          std::toupper(int(text[0])) == c.key[0]) {
        out = key;
        break;
      }
    }
  }
  CFRelease(source);
  return out;
}
} // namespace
struct NativeHotkeys::State {
  std::thread::id owner = std::this_thread::get_id();
  EventHandlerRef handler = nullptr;
  std::function<void(ActionId, InvocationContext)> callback;
  struct Bound {
    EventHotKeyRef handle;
    ActionId action;
    unsigned key, mods;
  };
  std::map<unsigned, Bound> actions;
  static OSStatus event(EventHandlerCallRef, EventRef event, void *data) {
    auto s = static_cast<State *>(data);
    EventHotKeyID id{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(id), nullptr, &id) != noErr ||
        id.signature != 0x43746971)
      return eventNotHandledErr;
    auto it = s->actions.find(id.id);
    if (it == s->actions.end())
      return eventNotHandledErr;
    auto callback = s->callback;
    auto action = it->second.action;
    InvocationContext ctx;
    ctx.keyboard = true;
    if (callback)
      callback(std::move(action), std::move(ctx));
    return noErr;
  }
  State(std::function<void(ActionId, InvocationContext)> cb)
      : callback(std::move(cb)) {
    [NSApplication sharedApplication];
    EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
    InstallEventHandler(GetApplicationEventTarget(), event, 1, &type, this,
                        &handler);
  }
  ~State() {
    if (handler)
      RemoveEventHandler(handler);
    for (auto &p : actions)
      UnregisterEventHotKey(p.second.handle);
  }
};
NativeHotkeys::NativeHotkeys(
    std::function<void(ActionId, InvocationContext)> cb)
    : state_(std::make_unique<State>(std::move(cb))) {}
NativeHotkeys::~NativeHotkeys() = default;
Capability NativeHotkeys::capability() const {
  return state_->handler
             ? Capability{CapabilityStatus::Limited,
                          "Carbon RegisterEventHotKey; ASCII-capable keymap / "
                          "explicit physical keycodes"}
             : Capability{CapabilityStatus::Unavailable,
                          "macOS hotkey handler unavailable; use launcher"};
}
Result<unsigned> NativeHotkeys::acquire(const ActionId &a, const KeyChord &c) {
  auto &s = *state_;
  if (s.owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Hotkeys require main thread"};
  if (!s.handler)
    return Error{ErrorCode::Unsupported, capability().reason};
  auto code = code_for(c);
  if (code < 0)
    return Error{ErrorCode::Unsupported,
                 "Shortcut key is unavailable in native layout"};
  unsigned mods = ((c.modifiers & Control) ? controlKey : 0) |
                  ((c.modifiers & Shift) ? shiftKey : 0) |
                  ((c.modifiers & Alt) ? optionKey : 0) |
                  ((c.modifiers & Super) ? cmdKey : 0);
  for (auto &p : s.actions)
    if (p.second.key == unsigned(code) && p.second.mods == mods)
      return Error{ErrorCode::Busy,
                   "Native key mapping conflicts with another action"};
  static std::atomic<unsigned> next{0};
  unsigned id = ++next;
  EventHotKeyRef handle = nullptr;
  auto status = RegisterEventHotKey(unsigned(code), mods, {0x43746971, id},
                                    GetApplicationEventTarget(), 0, &handle);
  if (status != noErr || !handle)
    return Error{ErrorCode::Busy,
                 "macOS shortcut conflict; previous binding retained"};
  s.actions[id] = {handle, a, unsigned(code), mods};
  return id;
}
void NativeHotkeys::release(unsigned id) {
  auto &s = *state_;
  auto p = s.actions.find(id);
  if (p != s.actions.end()) {
    UnregisterEventHotKey(p->second.handle);
    s.actions.erase(p);
  }
}
Result<void> NativeHotkeys::dispatch() {
  if (state_->owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Hotkeys require main thread"};
  return {};
}
void NativeHotkeys::reconcile_folders(const std::map<FolderId, bool> &) {}
Result<void> NativeHotkeys::configure_portal(const HotkeyMap &) {
  return Error{ErrorCode::Unsupported, "Portal integration not implemented"};
}
std::map<ActionId, BindingStatus> NativeHotkeys::portal_statuses() const {
  return {};
}
} // namespace cliptiq

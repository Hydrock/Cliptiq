#include "cliptiq/hotkeys.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <windows.h>
namespace cliptiq {
struct NativeHotkeys::State {
  DWORD owner = GetCurrentThreadId();
  HWND window = nullptr;
  std::function<void(ActionId, InvocationContext)> callback;
  std::map<unsigned, ActionId> actions;
  static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM id, LPARAM lp) {
    auto self = reinterpret_cast<State *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<State *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == WM_HOTKEY) {
      auto it = self->actions.find(unsigned(id));
      if (it != self->actions.end()) {
        auto callback = self->callback;
        auto action = it->second;
        InvocationContext ctx;
        ctx.keyboard = true;
        if (callback)
          callback(std::move(action), std::move(ctx));
      }
      return 0;
    }
    return DefWindowProcW(w, message, id, lp);
  }
  State(std::function<void(ActionId, InvocationContext)> cb)
      : callback(std::move(cb)) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Cliptiq.Hotkeys.v1";
    RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, this);
  }
  ~State() {
    for (auto &p : actions)
      UnregisterHotKey(window, int(p.first));
    if (window)
      DestroyWindow(window);
  }
};
NativeHotkeys::NativeHotkeys(
    std::function<void(ActionId, InvocationContext)> cb)
    : state_(std::make_unique<State>(std::move(cb))) {}
NativeHotkeys::~NativeHotkeys() = default;
Capability NativeHotkeys::capability() const {
  return state_->window
             ? Capability{CapabilityStatus::Limited,
                          "RegisterHotKey; conflicts retained, reserved OS "
                          "chords unavailable"}
             : Capability{CapabilityStatus::Unavailable,
                          "Global hotkey dispatcher unavailable; use launcher"};
}
Result<unsigned> NativeHotkeys::acquire(const ActionId &a, const KeyChord &c) {
  auto &s = *state_;
  if (GetCurrentThreadId() != s.owner)
    return Error{ErrorCode::InvalidArgument, "Hotkeys require owner thread"};
  if (!s.window)
    return Error{ErrorCode::Unsupported, capability().reason};
  if (c.meaning != KeyMeaning::Logical)
    return Error{ErrorCode::Unsupported,
                 "RegisterHotKey uses logical VK meaning; physical bindings "
                 "require a layout-specific integration"};
  static std::atomic<unsigned> next{1000};
  unsigned id = ++next;
  if (id >= 0xbfff)
    return Error{ErrorCode::TooLarge, "Hotkey registration IDs exhausted"};
  unsigned key = c.key.size() == 1
                     ? unsigned(c.key[0])
                     : VK_F1 + unsigned(std::stoi(c.key.substr(1)) - 1);
  unsigned mods = MOD_NOREPEAT | ((c.modifiers & Control) ? MOD_CONTROL : 0) |
                  ((c.modifiers & Shift) ? MOD_SHIFT : 0) |
                  ((c.modifiers & Alt) ? MOD_ALT : 0) |
                  ((c.modifiers & Super) ? MOD_WIN : 0);
  if (!RegisterHotKey(s.window, int(id), mods, key))
    return Error{ErrorCode::Busy, "Windows shortcut is reserved or owned by "
                                  "another app; previous binding retained"};
  s.actions[id] = a;
  return id;
}
void NativeHotkeys::release(unsigned id) {
  auto &s = *state_;
  if (s.actions.erase(id))
    UnregisterHotKey(s.window, int(id));
}
Result<void> NativeHotkeys::dispatch() {
  auto &s = *state_;
  if (GetCurrentThreadId() != s.owner)
    return Error{ErrorCode::InvalidArgument, "Hotkeys require owner thread"};
  MSG message{};
  for (unsigned i = 0;
       i < 64 && PeekMessageW(&message, s.window, 0, 0, PM_REMOVE); ++i) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return {};
}
Result<void> NativeHotkeys::configure_portal(const HotkeyMap &) {
  return Error{ErrorCode::Unsupported, "Portal integration not implemented"};
}
std::map<ActionId, BindingStatus> NativeHotkeys::portal_statuses() const {
  return {};
}
} // namespace cliptiq

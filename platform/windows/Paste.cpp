#include "PasteNative.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <map>
#include <thread>
#include <windows.h>
namespace cliptiq {
namespace {
struct Handle {
  HANDLE h = nullptr;
  ~Handle() {
    if (h)
      CloseHandle(h);
  }
};
std::uint64_t birth(HANDLE process) {
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
    return 0;
  return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
DWORD integrity(HANDLE process) {
  Handle token;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token.h))
    return MAXDWORD;
  DWORD bytes = 0;
  GetTokenInformation(token.h, TokenIntegrityLevel, nullptr, 0, &bytes);
  if (bytes > 4096 || !bytes)
    return MAXDWORD;
  Bytes data(bytes);
  if (!GetTokenInformation(token.h, TokenIntegrityLevel, data.data(), bytes,
                           &bytes))
    return MAXDWORD;
  auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(data.data())->Label.Sid;
  return *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
}
struct WindowLifetime {
  HWND watched = nullptr;
  std::uint64_t generation = 0;
  HWINEVENTHOOK hook = nullptr;
  static std::map<HWINEVENTHOOK, WindowLifetime *> &registry() {
    static thread_local std::map<HWINEVENTHOOK, WindowLifetime *> value;
    return value;
  }
  static void CALLBACK event(HWINEVENTHOOK hook, DWORD, HWND window,
                             LONG object, LONG child, DWORD, DWORD) {
    auto found = registry().find(hook);
    if (found != registry().end() && object == OBJID_WINDOW &&
        child == CHILDID_SELF && found->second->watched == window) {
      ++found->second->generation;
      found->second->watched = nullptr;
    }
  }
  WindowLifetime() {
    hook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr,
                           event, 0, 0,
                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook)
      registry()[hook] = this;
  }
  ~WindowLifetime() {
    if (hook) {
      registry().erase(hook);
      UnhookWinEvent(hook);
    }
  }
  std::uint64_t track(HWND window) {
    if (!hook)
      return 0;
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    if (watched != window) {
      watched = window;
      ++generation;
    }
    return generation;
  }
};
std::shared_ptr<NativePasteCalls> native_calls() {
  auto c = std::make_shared<NativePasteCalls>();
  auto lifetime = std::make_shared<WindowLifetime>();
  c->foreground = [] { return GetForegroundWindow(); };
  c->alive = [](HWND w) { return IsWindow(w) != FALSE; };
  c->window_thread = [](HWND w, DWORD *pid) {
    return GetWindowThreadProcessId(w, pid);
  };
  c->process_birth = [](DWORD pid) {
    Handle h;
    h.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    return h.h ? birth(h.h) : 0;
  };
  c->integrity = [](DWORD pid) {
    Handle h;
    h.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    return h.h ? integrity(h.h) : MAXDWORD;
  };
  c->key_state = [](int key) { return GetAsyncKeyState(key); };
  c->restore = [](HWND w) { return SetForegroundWindow(w) != FALSE; };
  c->scan = [](DWORD thread) {
    return MapVirtualKeyExW('V', MAPVK_VK_TO_VSC, GetKeyboardLayout(thread));
  };
  c->send = [](UINT n, INPUT *input) {
    return SendInput(n, input, sizeof(INPUT));
  };
  c->lifetime = [lifetime](HWND w) { return lifetime->track(w); };
  return c;
}
} // namespace
struct NativePasteBackend::State {
  DWORD owner = GetCurrentThreadId(), pid = 0, thread = 0;
  HWND window = nullptr;
  std::uint64_t created = 0, serial = 0, lifetime = 0;
  std::shared_ptr<NativePasteCalls> calls;
  TargetToken token;
};
NativePasteBackend::NativePasteBackend() : NativePasteBackend(native_calls()) {}
NativePasteBackend::NativePasteBackend(std::shared_ptr<NativePasteCalls> calls)
    : state_(std::make_unique<State>()) {
  state_->calls = std::move(calls);
}
NativePasteBackend::~NativePasteBackend() = default;
Capability NativePasteBackend::capability() const {
  return {CapabilityStatus::Limited,
          "SendInput is subject to foreground rules, held modifiers and UIPI; "
          "no receipt confirmation"};
}
TargetToken NativePasteBackend::capture_target() {
  auto &s = *state_;
  s.token.clear();
  s.window = nullptr;
  if (GetCurrentThreadId() != s.owner)
    return {};
  auto window = s.calls->foreground();
  if (!window)
    return {};
  s.thread = s.calls->window_thread(window, &s.pid);
  if (!s.thread || s.pid == GetCurrentProcessId())
    return {};
  if (!(s.created = s.calls->process_birth(s.pid)) ||
      !(s.lifetime = s.calls->lifetime(window)))
    return {};
  s.window = window;
  s.token =
      "win-target:" + std::to_string(++s.serial) + ":" + std::to_string(s.pid);
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
  if (GetCurrentThreadId() != s.owner || token.empty() || token != expected ||
      static_cast<unsigned>(shortcut) > 1)
    return fail("Expired or invalid paste target");
  DWORD pid = 0;
  if (!s.calls->alive(s.window) ||
      s.calls->window_thread(s.window, &pid) != s.thread || pid != s.pid)
    return fail("Paste target closed or replaced");
  if (s.calls->process_birth(s.pid) != s.created)
    return fail("Paste target process changed");
  auto ours = s.calls->integrity(GetCurrentProcessId()),
       theirs = s.calls->integrity(s.pid);
  if (ours == MAXDWORD || theirs == MAXDWORD || theirs > ours)
    return fail("Target integrity/UIPI blocks paste; paste manually");
  for (int key : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, int('V')})
    if (s.calls->key_state(key) & 0x8000)
      return fail("Release held modifiers/paste key before pasting");
  auto foreground = s.calls->foreground();
  if (foreground != s.window) {
    DWORD current = 0;
    s.calls->window_thread(foreground, &current);
    if (current != GetCurrentProcessId())
      return fail("Focus changed; paste manually");
    if (!s.calls->restore(s.window) || s.calls->foreground() != s.window)
      return fail("Windows denied normal focus restoration");
  }
  auto scan = s.calls->scan(s.thread);
  if (!scan || s.calls->foreground() != s.window)
    return fail("Paste key/focus unavailable");
  INPUT input[6]{};
  unsigned count = 0;
  auto key = [&](WORD code, bool up, bool physical = false) {
    auto &i = input[count++];
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = physical ? 0 : code;
    i.ki.wScan = physical ? code : 0;
    i.ki.dwFlags =
        (up ? KEYEVENTF_KEYUP : 0) | (physical ? KEYEVENTF_SCANCODE : 0);
  };
  key(VK_CONTROL, false);
  bool terminal = shortcut == Shortcut::TerminalPaste;
  if (terminal)
    key(VK_SHIFT, false);
  key(static_cast<WORD>(scan), false, true);
  key(static_cast<WORD>(scan), true, true);
  if (terminal)
    key(VK_SHIFT, true);
  key(VK_CONTROL, true);
  UINT sent = s.calls->send(count, input);
  if (sent != count) {
    // Release only modifiers actually introduced by this batch, never a held
    // user key.
    INPUT release[3]{};
    unsigned n = 0;
    auto up = [&](WORD k, bool physical = false) {
      release[n].type = INPUT_KEYBOARD;
      release[n].ki.wVk = physical ? 0 : k;
      release[n].ki.wScan = physical ? k : 0;
      release[n++].ki.dwFlags =
          KEYEVENTF_KEYUP | (physical ? KEYEVENTF_SCANCODE : 0);
    };
    if(sent>unsigned(terminal?2:1))up(static_cast<WORD>(scan),true);
    if(terminal&&sent>1)up(VK_SHIFT);
    if(sent)up(VK_CONTROL);
    if (n)
      s.calls->send(n, release);
    return fail("SendInput rejected or partially dispatched (UIPI possible); "
                "paste receipt unknown");
  }
  return {};
}
} // namespace cliptiq

#include "cliptiq/paste.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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
} // namespace
struct NativePasteBackend::State {
  DWORD owner = GetCurrentThreadId(), pid = 0, thread = 0;
  HWND window = nullptr;
  std::uint64_t created = 0, serial = 0;
  TargetToken token;
};
NativePasteBackend::NativePasteBackend() : state_(std::make_unique<State>()) {}
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
  auto window = GetForegroundWindow();
  if (!window)
    return {};
  s.thread = GetWindowThreadProcessId(window, &s.pid);
  if (!s.thread || s.pid == GetCurrentProcessId())
    return {};
  Handle process;
  process.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, s.pid);
  if (!process.h || !(s.created = birth(process.h)))
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
  if (!IsWindow(s.window) ||
      GetWindowThreadProcessId(s.window, &pid) != s.thread || pid != s.pid)
    return fail("Paste target closed or replaced");
  Handle process;
  process.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, s.pid);
  if (!process.h || birth(process.h) != s.created)
    return fail("Paste target process changed");
  auto ours = integrity(GetCurrentProcess()), theirs = integrity(process.h);
  if (ours == MAXDWORD || theirs == MAXDWORD || theirs > ours)
    return fail("Target integrity/UIPI blocks paste; paste manually");
  for (int key : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN, int('V')})
    if (GetAsyncKeyState(key) & 0x8000)
      return fail("Release held modifiers/paste key before pasting");
  auto foreground = GetForegroundWindow();
  if (foreground != s.window) {
    DWORD current = 0;
    GetWindowThreadProcessId(foreground, &current);
    if (current != GetCurrentProcessId())
      return fail("Focus changed; paste manually");
    if (!SetForegroundWindow(s.window) || GetForegroundWindow() != s.window)
      return fail("Windows denied normal focus restoration");
  }
  auto scan =
      MapVirtualKeyExW('V', MAPVK_VK_TO_VSC, GetKeyboardLayout(s.thread));
  if (!scan || GetForegroundWindow() != s.window)
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
  UINT sent = SendInput(count, input, sizeof(INPUT));
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
    if (sent > unsigned(terminal ? 2 : 1))
      up(static_cast<WORD>(scan), true);
    if (terminal && sent > 1)
      up(VK_SHIFT);
    if (sent)
      up(VK_CONTROL);
    if (n)
      SendInput(n, release, sizeof(INPUT));
    return fail("SendInput rejected or partially dispatched (UIPI possible); "
                "paste receipt unknown");
  }
  return {};
}
} // namespace cliptiq

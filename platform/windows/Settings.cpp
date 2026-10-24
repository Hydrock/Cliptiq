#include "cliptiq/settings.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
namespace cliptiq {
namespace {
std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  std::wstring out(n, 0);
  if (n)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                        out.data(), n);
  return out;
}
std::string text(HWND w) {
  int n = GetWindowTextLengthW(w);
  std::wstring raw(n + 1, 0);
  GetWindowTextW(w, raw.data(), n + 1);
  int count = WideCharToMultiByte(CP_UTF8, 0, raw.data(), n, nullptr, 0,
                                  nullptr, nullptr);
  std::string out(count, 0);
  if (count)
    WideCharToMultiByte(CP_UTF8, 0, raw.data(), n, out.data(), count, nullptr,
                        nullptr);
  return out;
}
} // namespace
struct NativeSettings::State : std::enable_shared_from_this<State> {
  DWORD owner = GetCurrentThreadId();
  HWND window = nullptr, tabs = nullptr, message = nullptr;
  Settings settings;
  std::function<Result<void>(Settings)> apply;
  std::function<Result<void>(const HotkeyMap &)> authorize;
  struct Control {
    std::string key;
    HWND window;
    bool boolean;
  };
  std::vector<Control> controls;
  std::vector<HWND> pages[8];
  ~State() {
    if (window)
      DestroyWindow(window);
  }
  static LRESULT CALLBACK recorder(HWND w, UINT msg, WPARAM key, LPARAM lp,
                                   UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
      unsigned mods =
          ((GetKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u) |
          ((GetKeyState(VK_SHIFT) & 0x8000) ? 2u : 0u) |
          ((GetKeyState(VK_MENU) & 0x8000) ? 4u : 0u) |
          (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? 8u : 0u);
      if (!mods && key == VK_BACK) {
        SetWindowTextW(w, L"");
        return 0;
      }
      if (mods) {
        std::string name;
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
          name = char(key);
        else if (key >= VK_F1 && key <= VK_F11)
          name = "F" + std::to_string(key - VK_F1 + 1);
        if (!name.empty()) {
          auto value = format_chord({mods, name});
          if (parse_chord(value)) {
            SetWindowTextW(w, wide(value).c_str());
            return 0;
          }
        }
      }
    }
    if (msg == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000))
      return 0;
    return DefSubclassProc(w, msg, key, lp);
  }
  void save() {
    auto keep = shared_from_this();
    std::map<std::string, std::string> values;
    for (auto &c : controls)
      values[c.key] =
          c.boolean
              ? (SendMessageW(c.window, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1"
                                                                          : "0")
              : text(c.window);
    auto draft = edit_settings(settings, values);
    if (!draft) {
      SetWindowTextW(message, wide(draft.error().message).c_str());
      return;
    }
    auto result = apply(draft.value());
    if (!result) {
      SetWindowTextW(message, wide(result.error().message).c_str());
      return;
    }
    settings = std::move(draft.value());
    ++settings.revision;
    SetWindowTextW(message, L"Saved. Unsupported functions remain disabled.");
  }
  void page() {
    int selected = TabCtrl_GetCurSel(tabs);
    for (int p = 0; p < 8; ++p)
      for (auto child : pages[p])
        ShowWindow(child, p == selected ? SW_SHOW : SW_HIDE);
  }
  static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = reinterpret_cast<State *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      s = static_cast<State *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (s) {
      if (msg == WM_COMMAND && LOWORD(wp) == 100) {
        s->save();
        return 0;
      }
      if (msg == WM_NOTIFY &&
          reinterpret_cast<NMHDR *>(lp)->code == TCN_SELCHANGE) {
        s->page();
        return 0;
      }
      if (msg == WM_CLOSE) {
        ShowWindow(w, SW_HIDE);
        return 0;
      }
    }
    return DefWindowProcW(w, msg, wp, lp);
  }
};
NativeSettings::NativeSettings() : state_(std::make_shared<State>()) {}
NativeSettings::~NativeSettings() = default;
Result<void>
NativeSettings::show(Settings settings, const Capabilities &caps,
                     std::function<Result<void>(Settings)> callback,
                     std::function<Result<void>(const HotkeyMap &)> authorize) {
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Settings require owner thread"};
  auto valid = validate_settings(settings);
  if (!valid)
    return valid;
  if (s->window) {
    DestroyWindow(s->window);
    s->window = nullptr;
  }
  s->controls.clear();
  for (auto &v : s->pages)
    v.clear();
  s->settings = std::move(settings);
  s->apply = std::move(callback);
  s->authorize = std::move(authorize);
  INITCOMMONCONTROLSEX init{sizeof(init), ICC_TAB_CLASSES};
  InitCommonControlsEx(&init);
  WNDCLASSW wc{};
  wc.lpfnWndProc = State::proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Cliptiq.Settings.v1";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  s->window =
      CreateWindowExW(0, wc.lpszClassName, L"Cliptiq Settings",
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 920,
                      800, nullptr, nullptr, wc.hInstance, s.get());
  if (!s->window)
    return Error{ErrorCode::Io, "Cannot create settings window"};
  s->tabs = CreateWindowExW(
      0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, 8, 884,
      690, s->window, reinterpret_cast<HMENU>(200), wc.hInstance, nullptr);
  const wchar_t *names[] = {L"General",   L"Menu",    L"Types", L"Excluded",
                            L"Shortcuts", L"Updates", L"Beta",  L"Donate"};
  int rows[8]{};
  for (int p = 0; p < 8; ++p) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(names[p]);
    TabCtrl_InsertItem(s->tabs, p, &item);
  }
  unsigned id = 1000;
  auto font = GetStockObject(DEFAULT_GUI_FONT);
  for (auto &f : settings_fields(s->settings, caps)) {
    unsigned p = unsigned(f.group);
    int y = 44 + rows[p];
    auto label =
        CreateWindowExW(0, L"STATIC", wide(f.label).c_str(), WS_CHILD, 24, y,
                        440, 23, s->window, nullptr, wc.hInstance, nullptr);
    bool multi = f.key == "excluded";
    auto control = CreateWindowExW(
        f.boolean ? 0 : WS_EX_CLIENTEDGE, f.boolean ? L"BUTTON" : L"EDIT",
        f.boolean ? L"" : wide(f.value).c_str(),
        WS_CHILD | WS_TABSTOP | (f.boolean ? BS_AUTOCHECKBOX : ES_AUTOHSCROLL) |
            (multi ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0),
        470, y, 390, multi ? 76 : 25, s->window,
        reinterpret_cast<HMENU>(std::uintptr_t(id++)), wc.hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (f.boolean)
      SendMessageW(control, BM_SETCHECK,
                   f.value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    else
      SendMessageW(control, EM_SETLIMITTEXT, 65536, 0);
    EnableWindow(control, f.enabled);
    if (f.shortcut)
      SetWindowSubclass(control, State::recorder, 1, 0);
    s->pages[p].push_back(label);
    s->pages[p].push_back(control);
    s->controls.push_back({f.key, control, f.boolean});
    rows[p] += multi ? 84 : 30;
    if (!f.reason.empty()) {
      auto reason = CreateWindowExW(0, L"STATIC", wide(f.reason).c_str(),
                                    WS_CHILD, 24, 44 + rows[p], 840, 40,
                                    s->window, nullptr, wc.hInstance, nullptr);
      SendMessageW(reason, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      s->pages[p].push_back(reason);
      rows[p] += 42;
    }
  }
  s->message =
      CreateWindowExW(0, L"STATIC",
                      L"Shortcut fields record chords; Backspace clears "
                      L"optional binding. Conflicts retain old settings.",
                      WS_CHILD | WS_VISIBLE, 16, 706, 730, 40, s->window,
                      nullptr, wc.hInstance, nullptr);
  auto apply = CreateWindowExW(
      0, L"BUTTON", L"Apply",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 780, 710, 100, 30,
      s->window, reinterpret_cast<HMENU>(100), wc.hInstance, nullptr);
  SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessageW(s->message, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  s->page();
  ShowWindow(s->window, SW_SHOW);
  return {};
}
Result<void> NativeSettings::process_events() {
  auto s = state_;
  if (GetCurrentThreadId() != s->owner)
    return Error{ErrorCode::InvalidArgument, "Settings require owner thread"};
  MSG msg{};
  for (int n = 0;
       n < 64 && s->window && PeekMessageW(&msg, s->window, 0, 0, PM_REMOVE);
       ++n) {
    if (!IsDialogMessageW(s->window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return {};
}
std::uintptr_t NativeSettings::native_handle() const {
  return reinterpret_cast<std::uintptr_t>(state_->window);
}
bool NativeSettings::confirm_clear() const {
  return MessageBoxW(state_->window,
                     L"Clear Cliptiq history? Snippets and current system "
                     L"clipboard remain.",
                     L"Cliptiq", MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}
void NativeSettings::status(const std::string &value) {
  if (state_->message)
    SetWindowTextW(state_->message, wide(value).c_str());
}
bool NativeSettings::record_chord(const KeyChord &chord) {
  auto focus = GetFocus();
  for (auto &c : state_->controls)
    if (c.window == focus && c.key.rfind("hotkey:", 0) == 0) {
      SetWindowTextW(focus, wide(format_chord(chord)).c_str());
      return true;
    }
  return false;
}
} // namespace cliptiq

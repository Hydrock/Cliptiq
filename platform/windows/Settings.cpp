#include "cliptiq/settings.hpp"
#include "../LocaleNative.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
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
  struct Page {
    HWND window = nullptr;
    int height = 0, offset = 0;
    void scroll(int position) {
      RECT area{};
      GetClientRect(window, &area);
      int next =
          std::clamp(position, 0, std::max(0, height - int(area.bottom)));
      ScrollWindowEx(window, 0, offset - next, nullptr, nullptr, nullptr,
                     nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
      offset = next;
      SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
      info.nMin = 0;
      info.nMax = std::max(0, height - 1);
      info.nPage = unsigned(area.bottom);
      info.nPos = offset;
      SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
      auto p = reinterpret_cast<Page *>(GetWindowLongPtrW(w, GWLP_USERDATA));
      if (msg == WM_NCCREATE) {
        p = static_cast<Page *>(
            reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        p->window = w;
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
      }
      if (p) {
        if (msg == WM_SIZE) {
          p->scroll(p->offset);
          return 0;
        }
        if (msg == WM_VSCROLL) {
          SCROLLINFO info{sizeof(info), SIF_ALL};
          GetScrollInfo(w, SB_VERT, &info);
          int position = p->offset;
          switch (LOWORD(wp)) {
          case SB_TOP:
            position = 0;
            break;
          case SB_BOTTOM:
            position = p->height;
            break;
          case SB_LINEUP:
            position -= 30;
            break;
          case SB_LINEDOWN:
            position += 30;
            break;
          case SB_PAGEUP:
            position -= int(info.nPage);
            break;
          case SB_PAGEDOWN:
            position += int(info.nPage);
            break;
          case SB_THUMBTRACK:
            position = info.nTrackPos;
            break;
          }
          p->scroll(position);
          return 0;
        }
        if (msg == WM_MOUSEWHEEL) {
          p->scroll(p->offset - GET_WHEEL_DELTA_WPARAM(wp) * 90 / WHEEL_DELTA);
          return 0;
        }
        if (msg == WM_COMMAND && HIWORD(wp) == EN_SETFOCUS && lp) {
          RECT child{}, area{};
          GetWindowRect(reinterpret_cast<HWND>(lp), &child);
          MapWindowPoints(nullptr, w, reinterpret_cast<POINT *>(&child), 2);
          GetClientRect(w, &area);
          if (child.top < 0)
            p->scroll(p->offset + child.top);
          else if (child.bottom > area.bottom)
            p->scroll(p->offset + child.bottom - area.bottom);
        }
      }
      return DefWindowProcW(w, msg, wp, lp);
    }
  } pages[8];
  HWND apply_button = nullptr;
  void layout() {
    RECT area{};
    GetClientRect(window, &area);
    int width = std::max(320, int(area.right)),
        height = std::max(200, int(area.bottom));
    MoveWindow(tabs, 8, 8, width - 16, height - 76, TRUE);
    for (auto &p : pages)
      if (p.window)
        MoveWindow(p.window, 16, 38, width - 32, height - 114, TRUE);
    if (message)
      MoveWindow(message, 16, height - 60, width - 150, 48, TRUE);
    if (apply_button)
      MoveWindow(apply_button, width - 120, height - 52, 100, 30, TRUE);
  }
  ~State() {
    if (window)
      DestroyWindow(window);
  }
  static LRESULT CALLBACK recorder(HWND w, UINT msg, WPARAM key, LPARAM lp,
                                   UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
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
    if (msg == WM_SYSCHAR ||
        (msg == WM_CHAR && (GetKeyState(VK_CONTROL) & 0x8000)))
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
    SetWindowTextW(message,
                   lw(L"Saved. Unsupported functions remain disabled."));
  }
  void page() {
    int selected = TabCtrl_GetCurSel(tabs);
    for (int p = 0; p < 8; ++p)
      ShowWindow(pages[p].window, p == selected ? SW_SHOW : SW_HIDE);
  }
  static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = reinterpret_cast<State *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      s = static_cast<State *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (s) {
      if (msg == WM_SIZE) {
        s->layout();
        return 0;
      }
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
    v = {};
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
  WNDCLASSW page_class = wc;
  page_class.lpfnWndProc = State::Page::proc;
  page_class.lpszClassName = L"Cliptiq.Settings.Page.v1";
  RegisterClassW(&page_class);
  s->window =
      CreateWindowExW(0, wc.lpszClassName, lw(L"Cliptiq Settings"),
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 920,
                      800, nullptr, nullptr, wc.hInstance, s.get());
  if (!s->window)
    return Error{ErrorCode::Io, "Cannot create settings window"};
  s->tabs = CreateWindowExW(
      0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, 8, 884,
      690, s->window, reinterpret_cast<HMENU>(200), wc.hInstance, nullptr);
  const wchar_t *names[] = {lw(L"General"),  lw(L"Menu"),      lw(L"Types"),
                            lw(L"Excluded"), lw(L"Shortcuts"), lw(L"Updates"),
                            lw(L"Beta"),     lw(L"About")};
  int rows[8]{};
  for (int p = 0; p < 8; ++p) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(names[p]);
    TabCtrl_InsertItem(s->tabs, p, &item);
    s->pages[p].window =
        CreateWindowExW(WS_EX_CONTROLPARENT, page_class.lpszClassName, L"",
                        WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 16, 38, 868,
                        650, s->window, nullptr, wc.hInstance, &s->pages[p]);
    if (!s->pages[p].window)
      return Error{ErrorCode::Io, "Cannot create settings page"};
  }
  unsigned id = 1000;
  auto font = GetStockObject(DEFAULT_GUI_FONT);
  for (auto &f : settings_fields(s->settings, caps)) {
    unsigned p = unsigned(f.group);
    int y = 6 + rows[p];
    auto caption = wide(f.label);
    RECT measured{0, 0, 440, 0};
    auto dc = GetDC(s->pages[p].window);
    auto old_font = SelectObject(dc, font);
    DrawTextW(dc, caption.c_str(), int(caption.size()), &measured,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(s->pages[p].window, dc);
    int label_height = std::max(23, int(measured.bottom));
    auto label = CreateWindowExW(0, L"STATIC", wide(f.label).c_str(), WS_CHILD,
                                 24, y, 440, label_height, s->pages[p].window,
                                 nullptr, wc.hInstance, nullptr);
    bool multi = f.key == "excluded";
    auto control = CreateWindowExW(
        f.boolean ? 0 : WS_EX_CLIENTEDGE, f.boolean ? L"BUTTON" : L"EDIT",
        f.boolean ? L"" : wide(f.value).c_str(),
        WS_CHILD | WS_TABSTOP | (f.boolean ? BS_AUTOCHECKBOX : ES_AUTOHSCROLL) |
            (multi ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0),
        470, y, 390, multi ? 76 : 25, s->pages[p].window,
        reinterpret_cast<HMENU>(std::uintptr_t(id++)), wc.hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (f.boolean)
      SendMessageW(control, BM_SETCHECK,
                   f.value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    else
      SendMessageW(control, EM_SETLIMITTEXT, 65536, 0);
    ShowWindow(label, SW_SHOW);
    ShowWindow(control, SW_SHOW);
    EnableWindow(control, f.enabled);
    if (f.shortcut)
      SetWindowSubclass(control, State::recorder, 1, 0);
    s->controls.push_back({f.key, control, f.boolean});
    rows[p] += std::max(label_height, multi ? 76 : 25) + 6;
    if (!f.reason.empty()) {
      auto reason = CreateWindowExW(
          0, L"STATIC", wide(f.reason).c_str(), WS_CHILD, 24, 6 + rows[p], 820,
          40, s->pages[p].window, nullptr, wc.hInstance, nullptr);
      SendMessageW(reason, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

      ShowWindow(reason, SW_SHOW);
      rows[p] += 42;
    }
  }
  for (int p = 0; p < 8; ++p)
    s->pages[p].height = rows[p] + 12;
  s->message =
      CreateWindowExW(0, L"STATIC",
                      L"Shortcut fields record chords; Backspace clears "
                      L"optional binding. Conflicts retain old settings.",
                      WS_CHILD | WS_VISIBLE, 16, 706, 730, 40, s->window,
                      nullptr, wc.hInstance, nullptr);
  auto apply = s->apply_button = CreateWindowExW(
      0, L"BUTTON", lw(L"Apply"),
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 780, 710, 100, 30,
      s->window, reinterpret_cast<HMENU>(100), wc.hInstance, nullptr);
  SendMessageW(apply, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  SendMessageW(s->message, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  s->layout();
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

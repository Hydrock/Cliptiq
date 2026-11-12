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
using Choices = std::vector<std::pair<std::string, std::wstring>>;
Choices choices(const std::string &key) {
  if (key == "sort") return {{"0", L"\u041f\u043e \u0432\u0440\u0435\u043c\u0435\u043d\u0438 \u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u044f"}, {"1", L"\u041f\u043e \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u043d\u0438\u044e"}};
  if (key == "tray") return {{"0", L"\u041e\u0431\u044b\u0447\u043d\u044b\u0439"}, {"1", L"\u041e\u0431\u0440\u0430\u0442\u043d\u044b\u0439"}, {"2", L"\u0421\u043a\u0440\u044b\u0442\u044c"}};
  if (key == "unknown_source") return {{"0", L"\u041e\u0442\u043a\u043b\u043e\u043d\u044f\u0442\u044c \u043f\u0440\u0438 \u0438\u0441\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f\u0445"}, {"1", L"\u0420\u0430\u0437\u0440\u0435\u0448\u0430\u0442\u044c"}, {"2", L"\u041e\u0442\u043a\u043b\u043e\u043d\u044f\u0442\u044c"}};
  if (key.find("modifier") != std::string::npos) return {{"0", L"\u041d\u0435 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u0442\u044c"}, {"1", L"Ctrl"}, {"2", L"Shift"}, {"4", L"Alt"}, {"8", L"Win"}};
  if (key == "cleanup_seconds") return {{"300", L"5 \u043c\u0438\u043d\u0443\u0442"}, {"1800", L"30 \u043c\u0438\u043d\u0443\u0442"}, {"3600", L"1 \u0447\u0430\u0441"}, {"14400", L"4 \u0447\u0430\u0441\u0430"}, {"28800", L"8 \u0447\u0430\u0441\u043e\u0432"}, {"86400", L"1 \u0434\u0435\u043d\u044c"}, {"259200", L"3 \u0434\u043d\u044f"}, {"604800", L"1 \u043d\u0435\u0434\u0435\u043b\u044f"}};
  if (key == "updates_interval") return {{"86400", L"\u041a\u0430\u0436\u0434\u044b\u0439 \u0434\u0435\u043d\u044c"}, {"604800", L"\u041a\u0430\u0436\u0434\u0443\u044e \u043d\u0435\u0434\u0435\u043b\u044e"}, {"2592000", L"\u041a\u0430\u0436\u0434\u044b\u0435 30 \u0434\u043d\u0435\u0439"}};
  if (key == "locale") return {{"auto", L"\u041a\u0430\u043a \u0432 \u0441\u0438\u0441\u0442\u0435\u043c\u0435"}, {"ru", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}, {"en", L"English"}};
  return {};
}
} // namespace
struct NativeSettings::State : std::enable_shared_from_this<State> {
  DWORD owner = GetCurrentThreadId();
  HWND window = nullptr, tabs = nullptr, message = nullptr;
  Settings settings;
  HFONT font = nullptr;
  std::function<void(bool, int)> preview;
  HWND preview_copy = nullptr, preview_paste = nullptr;
  void test_sound(bool paste) {
    for (const auto &c : controls) if (c.key == "sound_volume") {
      auto draft = edit_settings(settings, {{"sound_volume", text(c.window)}});
      if (!draft) { SetWindowTextW(message, wide(draft.error().message).c_str()); return; }
      if (preview) preview(paste, std::stoi(draft.value().values.at("sound_volume")));
    }
  }
  std::function<Result<void>(Settings)> apply;
  std::function<Result<void>(const HotkeyMap &)> authorize;
  struct Control {
    std::string key;
    HWND window;
    bool boolean;
    Choices options;
  };
  std::vector<Control> controls;
  struct Page {
    HWND window = nullptr;
    int height = 0, offset = 0;
    State *state = nullptr;
    struct Placement { HWND child; RECT rect; };
    std::vector<Placement> placements;
    void arrange() {
      RECT area{}; GetClientRect(window, &area);
      for (auto &v : placements) {
        int left = MulDiv(v.rect.left, area.right, 868);
        int right = MulDiv(v.rect.right, area.right, 868);
        auto dc = GetDC(window); int dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(window, dc);
        MoveWindow(v.child, left, MulDiv(v.rect.top, dpi, 96) - offset, right - left,
                   MulDiv(v.rect.bottom - v.rect.top, dpi, 96), TRUE);
      }
    }
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
      if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT) {
        auto dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? RGB(32, 34, 38) : GetSysColor(COLOR_GRAYTEXT));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      }

        if (msg == WM_COMMAND && (LOWORD(wp) == 300 || LOWORD(wp) == 301)) { p->state->test_sound(LOWORD(wp) == 301); return 0; }
        if (msg == WM_SIZE) {
          p->scroll(p->offset);
          p->arrange();
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
  } pages[9];
  HWND apply_button = nullptr;
  void layout() {
    RECT area{};
    GetClientRect(window, &area);
    int width = std::max(320, int(area.right)),
        height = std::max(200, int(area.bottom));
    MoveWindow(tabs, 8, 8, width - 16, height - 76, TRUE);
    for (auto &p : pages)
      if (p.window)
        {
          RECT content{0, 0, width - 16, height - 76};
          TabCtrl_AdjustRect(tabs, FALSE, &content);
          MoveWindow(p.window, 8 + content.left, 8 + content.top, content.right - content.left, content.bottom - content.top, TRUE);
        }
    if (message)
      MoveWindow(message, 16, height - 60, width - 150, 48, TRUE);
    if (apply_button)
      MoveWindow(apply_button, width - 120, height - 52, 100, 30, TRUE);
  }
  ~State() {
    if (window)
      DestroyWindow(window);
    if (font) DeleteObject(font);
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
              : (!c.options.empty() ? c.options.at(std::size_t(SendMessageW(c.window, CB_GETCURSEL, 0, 0))).first : text(c.window));
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
    for (int p = 0; p < 9; ++p)
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
      if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT) {
        auto dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? RGB(32, 34, 38) : GetSysColor(COLOR_GRAYTEXT));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      }

      if (msg == WM_GETMINMAXINFO) {
        auto limits = reinterpret_cast<MINMAXINFO *>(lp);
        limits->ptMinTrackSize = {800, 540}; return 0;
      }
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
        s->layout();
        s->page();
        return 0;
      }
      if (msg == WM_CLOSE) {
        // show() already reloads the committed snapshot on each opening.
        // Keeping a hidden tree of controls serves no draft-preservation role.
        DestroyWindow(w);
        s->window = s->tabs = s->message = s->apply_button = nullptr;
        s->controls.clear();
        s->preview_copy = s->preview_paste = nullptr;
        for (auto &page : s->pages)
          page = {};
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
      0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_MULTILINE, 8, 8, 884,
      690, s->window, reinterpret_cast<HMENU>(200), wc.hInstance, nullptr);
  const wchar_t *names[] = {L"\u041e\u0431\u0449\u0438\u0435", L"\u041c\u0435\u043d\u044e", L"\u0424\u043e\u0440\u043c\u0430\u0442\u044b", L"\u0418\u0441\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f", L"\u041a\u043b\u0430\u0432\u0438\u0448\u0438", L"\u041e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u044f", L"\u0415\u0449\u0451", L"\u041e \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u0435", L"\u0417\u0432\u0443\u043a\u0438"};
  int rows[9]{};
  for (int p = 0; p < 9; ++p) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(names[p]);
    SendMessageW(s->tabs, TCM_INSERTITEMW, WPARAM(p), reinterpret_cast<LPARAM>(&item));
    s->pages[p].state = s.get();
    s->pages[p].window =
        CreateWindowExW(WS_EX_CONTROLPARENT, page_class.lpszClassName, L"",
                        WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 16, 38, 868,
                        650, s->window, nullptr, wc.hInstance, &s->pages[p]);
    if (!s->pages[p].window)
      return Error{ErrorCode::Io, "Cannot create settings page"};
  }
  unsigned id = 1000;
  if (s->font) DeleteObject(s->font);
  auto dc_font = GetDC(s->window);
  s->font = CreateFontW(-MulDiv(11, GetDeviceCaps(dc_font, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  ReleaseDC(s->window, dc_font);
  auto font = s->font;
  SendMessageW(s->tabs, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  for (auto &f : settings_fields(s->settings, caps)) {
    unsigned p = unsigned(f.group);
    int y = 6 + rows[p];
    auto options = choices(f.key);
    if (!options.empty() && std::none_of(options.begin(), options.end(), [&](const auto &o) { return o.first == f.value; })) options.push_back({f.value, wide(f.value)});
    bool boolean = f.boolean && options.empty();
    auto caption = wide(f.label);
    if (f.key == "sort") caption = L"\u041f\u043e\u0440\u044f\u0434\u043e\u043a \u0438\u0441\u0442\u043e\u0440\u0438\u0438";
    if (f.key == "tray") caption = L"\u041c\u0435\u043d\u044e \u0432 \u0442\u0440\u0435\u0435";
    if (f.key == "unknown_source") caption = L"\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e\u0435 \u043f\u0440\u0438\u043b\u043e\u0436\u0435\u043d\u0438\u0435";
    if (f.key == "locale") caption = L"\u042f\u0437\u044b\u043a \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430";
    if (f.key == "plain_modifier") caption = L"\u0412\u0441\u0442\u0430\u0432\u0438\u0442\u044c \u0431\u0435\u0437 \u0444\u043e\u0440\u043c\u0430\u0442\u0430";
    if (f.key == "sound_copy") caption = L"\u0417\u0432\u0443\u043a \u043f\u0440\u0438 \u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0438";
    if (f.key == "sound_paste") caption = L"\u0417\u0432\u0443\u043a \u043f\u0440\u0438 \u0432\u0441\u0442\u0430\u0432\u043a\u0435";
    if (f.key == "sound_volume") caption = L"\u0413\u0440\u043e\u043c\u043a\u043e\u0441\u0442\u044c, % (0-100)";
    RECT measured{0, 0, 440, 0};
    auto dc = GetDC(s->pages[p].window);
    auto old_font = SelectObject(dc, font);
    DrawTextW(dc, caption.c_str(), int(caption.size()), &measured,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(s->pages[p].window, dc);
    int label_height = std::max(30, int(measured.bottom));
    auto label = CreateWindowExW(0, L"STATIC", caption.c_str(), WS_CHILD,
                                 24, y, 440, label_height, s->pages[p].window,
                                 nullptr, wc.hInstance, nullptr);
    bool multi = f.key == "excluded";
    auto control = CreateWindowExW(
        boolean ? 0 : WS_EX_CLIENTEDGE, boolean ? L"BUTTON" : (!options.empty() ? L"COMBOBOX" : L"EDIT"),
        boolean ? L"" : wide(f.value).c_str(),
        WS_CHILD | WS_TABSTOP | (boolean ? BS_AUTOCHECKBOX : (!options.empty() ? CBS_DROPDOWNLIST | WS_VSCROLL : ES_AUTOHSCROLL)) |
            (multi ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0),
        470, y, 390, multi ? 90 : (!options.empty() ? 300 : 30), s->pages[p].window,
        reinterpret_cast<HMENU>(std::uintptr_t(id++)), wc.hInstance, nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (!options.empty()) {
      for (std::size_t i = 0; i < options.size(); ++i) {
        SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(options[i].second.c_str()));
        if (options[i].first == f.value) SendMessageW(control, CB_SETCURSEL, i, 0);
      }
    } else if (boolean)
      SendMessageW(control, BM_SETCHECK,
                   f.value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    else
      SendMessageW(control, EM_SETLIMITTEXT, 65536, 0);
    ShowWindow(label, SW_SHOW);
    ShowWindow(control, SW_SHOW);
    EnableWindow(control, f.enabled);
    if (f.shortcut)
      SetWindowSubclass(control, State::recorder, 1, 0);
    s->controls.push_back({f.key, control, boolean, options});
    rows[p] += std::max(label_height, multi ? 90 : 30) + 14;
    if (!f.reason.empty()) {
      auto reason = CreateWindowExW(
          0, L"STATIC", wide(f.reason).c_str(), WS_CHILD, 24, 6 + rows[p], 820,
          40, s->pages[p].window, nullptr, wc.hInstance, nullptr);
      SendMessageW(reason, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

      ShowWindow(reason, SW_SHOW);
      rows[p] += 42;
    }
  }
  for (int p = 0; p < 9; ++p)
    s->pages[p].height = rows[p] + 12;
  auto make_preview = [&](const wchar_t *title, int id, int x) {
    auto button = CreateWindowExW(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      x, rows[8] + 20, 280, 36, s->pages[8].window, reinterpret_cast<HMENU>(std::uintptr_t(id)), wc.hInstance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnableWindow(button, bool(s->preview)); return button;
  };
  s->preview_copy = make_preview(L"\u041f\u0440\u043e\u0432\u0435\u0440\u0438\u0442\u044c \u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435", 300, 24);
  s->preview_paste = make_preview(L"\u041f\u0440\u043e\u0432\u0435\u0440\u0438\u0442\u044c \u0432\u0441\u0442\u0430\u0432\u043a\u0443", 301, 320);
  s->pages[8].height = rows[8] + 90;
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
  for (auto &p : s->pages) {
    for (auto child = GetWindow(p.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
      RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, p.window, reinterpret_cast<POINT *>(&r), 2);
      wchar_t kind[32]{}; GetClassNameW(child, kind, 32);
      if (std::wstring(kind) == L"ComboBox") r.bottom = r.top + 300;
      p.placements.push_back({child, r});
    }
    auto dc = GetDC(p.window); p.height = MulDiv(p.height, GetDeviceCaps(dc, LOGPIXELSY), 96); ReleaseDC(p.window, dc);
  }
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
void NativeSettings::sound_preview(std::function<void(bool, int)> callback) {
  state_->preview = std::move(callback);
  if (state_->preview_copy) EnableWindow(state_->preview_copy, bool(state_->preview));
  if (state_->preview_paste) EnableWindow(state_->preview_paste, bool(state_->preview));
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

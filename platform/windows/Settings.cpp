#include "cliptiq/settings.hpp"
#include "../LocaleNative.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include "ApplicationIcon.hpp"
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
  std::function<void(void*)> advanced;
  HFONT font = nullptr, help_font = nullptr;
  std::function<void(bool, int)> preview;
  HWND preview_copy = nullptr, preview_paste = nullptr;
  void test_sound(bool paste) {
    for (const auto &c : controls) if (c.key == "sound_volume") {
      auto draft = edit_settings(settings, {{"sound_volume", text(c.window)}});
      if (!draft) { set_message(wide(draft.error().message)); return; }
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
    struct Row { HWND label, control, help, reason; int control_height; bool combo; };
    std::vector<Row> rows;
    int rows_bottom = 0;
    void arrange() {
      RECT area{}; GetClientRect(window, &area);
      auto dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
      auto px=[&](int n){return MulDiv(n,dpi,96);};
      auto dc=GetDC(window);
      auto measure=[&](HWND child,int width){
        std::wstring value(GetWindowTextLengthW(child)+1,L'\0');
        int length=GetWindowTextW(child,value.data(),int(value.size()));
        auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0)));
        RECT text{0,0,std::max(1,width),0};
        DrawTextW(dc,value.c_str(),length,&text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,previous);return int(text.bottom);
      };
      int left=px(24),right=std::max(left+1,int(area.right)-px(24));
      int control_left=MulDiv(area.right,470,868),label_width=std::max(1,control_left-left-px(20));
      int y=px(12);
      for(auto &row:rows){
        int label_height=std::max(px(30),measure(row.label,label_width));
        MoveWindow(row.label,left,y-offset,label_width,label_height,TRUE);
        MoveWindow(row.control,control_left,y-offset,std::max(1,right-control_left),px(row.combo?300:row.control_height),TRUE);
        y+=std::max(label_height,px(row.control_height))+px(5);
        for(auto child:{row.help,row.reason})if(child){
          int h=measure(child,right-left);
          MoveWindow(child,left,y-offset,right-left,h,TRUE);y+=h+px(5);
        }
        y+=px(16);
      }
      for(auto &v:placements){
        int x=MulDiv(v.rect.left,area.right,868),r=MulDiv(v.rect.right,area.right,868);
        MoveWindow(v.child,x,y+px(v.rect.top-rows_bottom)-offset,r-x,px(v.rect.bottom-v.rect.top),TRUE);
      }
      ReleaseDC(window,dc);
      int extra=12;for(auto &v:placements)extra=std::max(extra,int(v.rect.bottom)-rows_bottom+12);
      height=y+px(extra);
      RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
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
        auto child=reinterpret_cast<HWND>(lp);
        bool help=GetPropW(child,L"Cliptiq.Help")!=nullptr;
        SetTextColor(dc, help ? (GetSysColor(COLOR_WINDOW)==RGB(255,255,255)?RGB(100,105,112):GetSysColor(COLOR_GRAYTEXT)) : (IsWindowEnabled(child) ? RGB(32,34,38) : GetSysColor(COLOR_GRAYTEXT)));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
      }

        if (msg == WM_COMMAND && LOWORD(wp) == 302) { if(p->state->advanced) p->state->advanced(w); return 0; }
        if (msg == WM_COMMAND && (LOWORD(wp) == 300 || LOWORD(wp) == 301)) { p->state->test_sound(LOWORD(wp) == 301); return 0; }
        if (msg == WM_SIZE) {
          if(auto child=GetDlgItem(w,303)){RECT r{};GetClientRect(w,&r);MoveWindow(child,0,0,r.right,r.bottom,TRUE);ShowScrollBar(w,SB_VERT,FALSE);return 0;}
          p->arrange();
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
  } pages[10];
  HWND apply_button = nullptr;
  void set_message(const std::wstring& value) {
    SetWindowTextW(message,value.c_str());
    layout();
  }
  void layout() {
    RECT area{};
    GetClientRect(window, &area);
    int width = std::max(320, int(area.right)),
        height = std::max(200, int(area.bottom));
    auto dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
    auto px=[&](int n){return MulDiv(n,dpi,96);};
    const int margin=px(16),gap=px(16);
    auto dc=GetDC(window);auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
    RECT button_text{0,0,0,0};
    auto caption=std::wstring(lw(L"Apply"));
    DrawTextW(dc,caption.c_str(),int(caption.size()),&button_text,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
    int button_width=std::max(px(120),int(button_text.right)+px(32));
    int button_height=std::max(px(36),int(button_text.bottom)+px(16));
    RECT message_text{0,0,std::max(1,width-2*margin-gap-button_width),0};
    if(message){
      std::wstring value(GetWindowTextLengthW(message)+1,L'\0');
      int length=GetWindowTextW(message,value.data(),int(value.size()));
      DrawTextW(dc,value.c_str(),length,&message_text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    }
    SelectObject(dc,previous);ReleaseDC(window,dc);
    int footer_height=std::max(button_height,int(message_text.bottom))+2*margin;
    int tabs_height=std::max(px(80),height-footer_height-px(8));
    MoveWindow(tabs, px(8), px(8), width-px(16), tabs_height, TRUE);
    for (auto &p : pages)
      if (p.window)
        {
          RECT content{0, 0, width-px(16), tabs_height};
          TabCtrl_AdjustRect(tabs, FALSE, &content);
          MoveWindow(p.window, px(8)+content.left, px(8)+content.top, content.right-content.left, content.bottom-content.top, TRUE);
        }
    if (message)
      MoveWindow(message,margin,height-footer_height+margin,message_text.right,std::max(1,int(message_text.bottom)),TRUE);
    if (apply_button)
      MoveWindow(apply_button,width-margin-button_width,height-footer_height+margin,button_width,button_height,TRUE);
    // Repaint embedded pages after the entire resize, not midway through child moves.
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
  }
  ~State() {
    if (window)
      DestroyWindow(window);
    if (font) DeleteObject(font);
    if (help_font) DeleteObject(help_font);
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
      set_message(wide(draft.error().message));
      return;
    }
    auto result = apply(draft.value());
    if (!result) {
      set_message(wide(result.error().message));
      return;
    }
    settings = std::move(draft.value());
    ++settings.revision;
    set_message(lw(L"Saved. Unsupported functions remain disabled."));
  }
  void page() {
    int selected = TabCtrl_GetCurSel(tabs);
    for (int p = 0; p < 10; ++p)
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
        auto child=reinterpret_cast<HWND>(lp);
        bool help=GetPropW(child,L"Cliptiq.Help")!=nullptr;
        SetTextColor(dc, help ? (GetSysColor(COLOR_WINDOW)==RGB(255,255,255)?RGB(100,105,112):GetSysColor(COLOR_GRAYTEXT)) : (IsWindowEnabled(child) ? RGB(32,34,38) : GetSysColor(COLOR_GRAYTEXT)));
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
  windows::set_application_icons(s->window);
  s->tabs = CreateWindowExW(
      0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_MULTILINE, 8, 8, 884,
      690, s->window, reinterpret_cast<HMENU>(200), wc.hInstance, nullptr);
  const wchar_t *names[] = {L"\u041e\u0431\u0449\u0438\u0435", L"\u041c\u0435\u043d\u044e", L"\u0424\u043e\u0440\u043c\u0430\u0442\u044b", L"\u0418\u0441\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f", L"\u041a\u043b\u0430\u0432\u0438\u0448\u0438", L"\u041e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u044f", L"\u0415\u0449\u0451", L"\u041e \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c\u0435", L"\u0417\u0432\u0443\u043a\u0438", lw(L"Network")};
  int rows[10]{};
  for (int p = 0; p < (s->advanced ? 10 : 9); ++p) {
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
  if(s->help_font)DeleteObject(s->help_font);
  s->help_font=CreateFontW(-MulDiv(9,GetDpiForWindow(s->window),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  auto font = s->font;
  SendMessageW(s->tabs, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  // Give each native tab a comfortable hit area; TCS_MULTILINE wraps on narrow windows.
  const auto tab_dpi=GetDpiForWindow(s->window);
  TabCtrl_SetPadding(s->tabs,MulDiv(18,tab_dpi,96),MulDiv(6,tab_dpi,96));
  for (auto &f : settings_fields(s->settings, caps)) {
    unsigned p = unsigned(f.group);
    int y = 6 + rows[p];
    auto options = choices(f.key);
    if (!options.empty() && std::none_of(options.begin(), options.end(), [&](const auto &o) { return o.first == f.value; })) options.push_back({f.value, wide(f.value)});
    bool boolean = f.boolean && options.empty();
    auto caption = wide(f.label);
    if (f.key == "sort") caption = L"\u041f\u043e\u0440\u044f\u0434\u043e\u043a \u0438\u0441\u0442\u043e\u0440\u0438\u0438";
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
    auto help=CreateWindowExW(0,L"STATIC",wide(f.description).c_str(),WS_CHILD|WS_VISIBLE|SS_NOPREFIX,
      24,6+rows[p],820,40,s->pages[p].window,nullptr,wc.hInstance,nullptr);
    SetPropW(help,L"Cliptiq.Help",reinterpret_cast<HANDLE>(1));
    SendMessageW(help,WM_SETFONT,reinterpret_cast<WPARAM>(s->help_font),TRUE);
    HWND reason_control=nullptr;
    rows[p]+=48;
    if (!f.reason.empty()) {
      auto reason = CreateWindowExW(
          0, L"STATIC", wide(f.reason).c_str(), WS_CHILD, 24, 6 + rows[p], 820,
          40, s->pages[p].window, nullptr, wc.hInstance, nullptr);
      SendMessageW(reason, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

      reason_control=reason;
      ShowWindow(reason, SW_SHOW);
      rows[p] += 42;
    }
    s->pages[p].rows.push_back({label,control,help,reason_control,multi?90:30,!options.empty()});
  }
  for (int p = 0; p < 10; ++p) {
    s->pages[p].height = rows[p] + 12;
    s->pages[p].rows_bottom=rows[p];
  }
  if(s->advanced) s->advanced(s->pages[9].window);
  auto version=std::wstring(L"Cliptiq ")+wide(CLIPTIQ_APP_VERSION);
  auto about=CreateWindowExW(0,L"STATIC",version.c_str(),WS_CHILD|WS_VISIBLE,
    24,rows[7]+12,800,30,s->pages[7].window,nullptr,wc.hInstance,nullptr);
  SendMessageW(about,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
  s->pages[7].height=rows[7]+54;
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
      if(GetDlgCtrlID(child)==303)continue;
      bool field=false;for(auto &row:p.rows)if(child==row.label||child==row.control||child==row.help||child==row.reason)field=true;
      if(field)continue;
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
void NativeSettings::advanced(std::function<void(void*)> cb) { state_->advanced=std::move(cb); }
void NativeSettings::sound_preview(std::function<void(bool, int)> callback) {
  state_->preview = std::move(callback);
  if (state_->preview_copy) EnableWindow(state_->preview_copy, bool(state_->preview));
  if (state_->preview_paste) EnableWindow(state_->preview_paste, bool(state_->preview));
}
void NativeSettings::status(const std::string &value) {
  if (state_->message)
    state_->set_message(wide(value));
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

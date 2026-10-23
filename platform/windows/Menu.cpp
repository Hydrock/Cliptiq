#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
// clang-format on
#include "cliptiq/native_menu.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef CLIPTIQ_TEST_POPUP_BOUNDARY
extern "C" UINT cliptiq_test_track_popup(HMENU,UINT,int,int,HWND,LPTPMPARAMS);
#define TrackPopupMenuEx cliptiq_test_track_popup
#endif
namespace cliptiq {
namespace {
std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  if (!n && !s.empty())
    return L"[Invalid text]";
  std::wstring out(n, 0);
  if (n)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                        out.data(), n);
  return out;
}
std::wstring caption(const MenuItem &i) {
  std::string label;
  for (char c : i.label) {
    label += c;
    if (c == '&')
      label += '&';
  }
  std::string prefix;
  if (i.number)
    prefix = std::to_string(*i.number) + ". ";
  return wide(prefix + label + (i.shortcut.empty() ? "" : "\t" + i.shortcut));
}
HBITMAP bitmap(const MenuImage &i) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = i.width;
  bi.bmiHeader.biHeight = -LONG(i.height);
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *p = nullptr;
  auto b = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
  if (!b)
    return nullptr;
  auto out = static_cast<unsigned char *>(p);
  for (size_t j = 0; j < i.rgba.size(); j += 4) {
    unsigned a = i.rgba[j + 3];
    out[j] = i.rgba[j + 2] * a / 255;
    out[j + 1] = i.rgba[j + 1] * a / 255;
    out[j + 2] = i.rgba[j] * a / 255;
    out[j + 3] = a;
  }
  return b;
}
} // namespace
struct NativeMenu::State : std::enable_shared_from_this<State> {
  NativeMenuOptions options;
  std::function<void(MenuCommand)> callback;
  MenuPreviewProvider provider;
  std::thread::id owner = std::this_thread::get_id();
  HWND window = nullptr, tip = nullptr;
  HMENU menu = nullptr;
  HICON icon = nullptr;
  HFONT font = nullptr;
  std::vector<HBITMAP> bitmaps;
  MenuModel model;
  InvocationContext invocation;
  std::map<UINT, MenuItem> commands;
  UINT next_id = 100, taskbar_created = 0, pending = 0;
  bool closing = false;
  NativeMenuStatus status;
  std::wstring tooltip;
  State(NativeMenuOptions o, std::function<void(MenuCommand)> c,
        MenuPreviewProvider p)
      : options(o), callback(std::move(c)), provider(std::move(p)) {}
  ~State() {
    closing = true;
    remove_icon();
    if (window)
      DestroyWindow(window);
    if (font)
      DeleteObject(font);
    if (menu)
      DestroyMenu(menu);
    for (auto b : bitmaps)
      DeleteObject(b);
  }
  bool thread() const { return owner == std::this_thread::get_id(); }
  void invoke(UINT id) {
    auto keep = shared_from_this();
    auto it = commands.find(id);
    if (it == commands.end() || !it->second.enabled ||
        it->second.action == MenuAction::None || it->second.separator)
      return;
    auto cb = callback;
    MenuCommand c{it->second.action, it->second.id, model.revision};
    c.invocation = invocation;
    if (cb)
      cb(std::move(c));
  }
  HMENU build(const std::vector<MenuItem> &items) {
    auto result = CreatePopupMenu();
    if (!result)
      throw std::bad_alloc();
    for (const auto &i : items) {
      MENUITEMINFOW info{};
      info.cbSize = sizeof(info);
      info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID;
      info.wID = next_id++;
      info.fType = i.separator ? MFT_SEPARATOR : MFT_STRING;
      info.fState = i.enabled ? MFS_ENABLED : MFS_DISABLED;
      auto label = caption(i);
      if (!i.separator) {
        info.fMask |= MIIM_STRING;
        info.dwTypeData = label.data();
        if (!i.children.empty()) {
          info.fMask |= MIIM_SUBMENU;
          info.hSubMenu = build(i.children);
        } else
          commands[info.wID] = i;
        auto image = menu_preview(i, options, provider);
        if (image) {
          auto dpi = GetDpiForWindow(window);
          auto b = bitmap(scale_menu_image(*image, dpi ? dpi : 96));
          if (b) {
            bitmaps.push_back(b);
            info.fMask |= MIIM_BITMAP;
            info.hbmpItem = b;
          }
        }
      }
      if (!InsertMenuItemW(result, UINT(-1), TRUE, &info)) {
        DestroyMenu(result);
        throw std::runtime_error("InsertMenuItem failed");
      }
    }
    return result;
  }
  Result<void> rebuild() {
    EndMenu();
    if (menu)
      DestroyMenu(menu);
    menu = nullptr;
    for (auto b : bitmaps)
      DeleteObject(b);
    bitmaps.clear();
    commands.clear();
    try {
      menu = build(model.items);
    } catch (...) {
      return Error{ErrorCode::Io, "Cannot build native menu"};
    }
    return {};
  }
  void layout() {
    auto dpi = GetDpiForWindow(window);
    if (!dpi)
      dpi = 96;
    auto button = GetDlgItem(window, 1);
    if (!button)
      return;
    MoveWindow(button, MulDiv(16, dpi, 96), MulDiv(16, dpi, 96),
               MulDiv(240, dpi, 96), MulDiv(42, dpi, 96), TRUE);
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                   &metrics, 0, dpi)) {
      auto next = CreateFontIndirectW(&metrics.lfMessageFont);
      if (next) {
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        if (font)
          DeleteObject(font);
        font = next;
      }
    }
  }
  void remove_icon() {
    if (window) {
      NOTIFYICONDATAW n{};
      n.cbSize = sizeof(n);
      n.hWnd = window;
      n.uID = 1;
      Shell_NotifyIconW(NIM_DELETE, &n);
    }
    if (icon) {
      DestroyIcon(icon);
      icon = nullptr;
    }
    status.tray_available = false;
  }
  void tray() {
    remove_icon();
    if (options.tray_style == TrayStyle::Hidden) {
      status.tray_reason = "Tray hidden; launcher visible";
      return;
    }
    MenuImage pixels{32, 32, Bytes(32 * 32 * 4)};
    unsigned color = options.tray_style == TrayStyle::Reversed ? 240 : 32;
    for (unsigned y = 3; y < 29; ++y)
      for (unsigned x = 7; x < 25; ++x)
        if (x < 10 || x > 21 || y < 6 || y > 25) {
          auto p = (y * 32 + x) * 4;
          pixels.rgba[p] = pixels.rgba[p + 1] = pixels.rgba[p + 2] = color;
          pixels.rgba[p + 3] = 255;
        }
    auto b = bitmap(pixels);
    auto mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = b;
    info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
    DeleteObject(b);
    DeleteObject(mask);
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = window;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_APP + 1;
    n.hIcon = icon;
    wcscpy_s(n.szTip, L"Cliptiq");
    status.tray_available = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
    if (status.tray_available) {
      n.uVersion = NOTIFYICON_VERSION_4;
      Shell_NotifyIconW(NIM_SETVERSION, &n);
    }
    status.tray_reason = status.tray_available
                             ? "Shell_NotifyIcon registered"
                             : "Explorer tray unavailable; launcher visible";
  }
  void popup(const InvocationContext &ctx = {}) {
    invocation = ctx;
    auto keep = shared_from_this();
    if (!menu)
      return;
    POINT p{};
    GetCursorPos(&p);
    if (ctx.x && ctx.y)
      p = {*ctx.x, *ctx.y};
    SetForegroundWindow(window);
    pending = 0;
    auto id = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y,
                               window, nullptr);
    if (tip)
      SendMessageW(tip, TTM_TRACKACTIVATE, FALSE, 0);
    PostMessageW(window, WM_NULL, 0, 0);
    if (pending)
      id = pending;
    if (id)
      invoke(id);
  }
  static LRESULT CALLBACK procedure(HWND w, UINT message, WPARAM wp,
                                    LPARAM lp) {
    auto raw = reinterpret_cast<State *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      raw = static_cast<State *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(raw));
      raw->window = w;
    }
    if (!raw)
      return DefWindowProcW(w, message, wp, lp);
    try {
      auto s = raw->shared_from_this();
      if (message == s->taskbar_created && s->taskbar_created) {
        s->tray();
        return 0;
      }
      switch (message) {
      case WM_CREATE: {
        CreateWindowW(L"BUTTON", L"Clipboard History",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 16,
                      16, 240, 42, w, reinterpret_cast<HMENU>(1),
                      GetModuleHandleW(nullptr), nullptr);
        return 0;
      }
      case WM_SIZE:
        s->layout();
        return 0;
      case WM_CLOSE: {
        if (!s->closing && s->callback) {
          auto cb = s->callback;
          cb({MenuAction::Quit, "", s->model.revision});
        }
        return 0;
      }
      case WM_DESTROY:
        s->window = nullptr;
        return 0;
      case WM_COMMAND:
        if (LOWORD(wp) == 1)
          s->popup();
        else
          s->invoke(LOWORD(wp));
        return 0;
      case WM_MENUCOMMAND: {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_ID;
        if (GetMenuItemInfoW(reinterpret_cast<HMENU>(lp), UINT(wp), TRUE,
                             &info))
          s->invoke(info.wID);
        return 0;
      }
      case WM_MENUCHAR: {
        char digit = char(LOWORD(wp));
        for (const auto &entry : s->commands)
          if (entry.second.enabled &&
              entry.second.shortcut == std::string(1, digit)) {
            s->pending = entry.first;
            EndMenu();
            return MAKELRESULT(0, MNC_CLOSE);
          }
        break;
      }
      case WM_APP + 1:
        if (LOWORD(lp) == NIN_SELECT || LOWORD(lp) == NIN_KEYSELECT ||
            LOWORD(lp) == WM_CONTEXTMENU)
          s->popup();
        return 0;
      case WM_DPICHANGED: {
        auto rect = reinterpret_cast<RECT *>(lp);
        SetWindowPos(w, nullptr, rect->left, rect->top,
                     rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_THEMECHANGED:
      case WM_SETTINGCHANGE:
        s->layout();
        s->rebuild();
        s->tray();
        return 0;
      case WM_MENUSELECT: {
        auto found = s->commands.find(LOWORD(wp));
        if (found != s->commands.end() && !found->second.tooltip.empty()) {
          s->tooltip = wide(found->second.tooltip);
          TOOLINFOW info{sizeof(info)};
          info.hwnd = w;
          info.uId = 1;
          info.lpszText = s->tooltip.data();
          SendMessageW(s->tip, TTM_UPDATETIPTEXTW, 0,
                       reinterpret_cast<LPARAM>(&info));
          POINT p;
          GetCursorPos(&p);
          SendMessageW(s->tip, TTM_TRACKPOSITION, 0,
                       MAKELPARAM(p.x + 12, p.y + 16));
          SendMessageW(s->tip, TTM_TRACKACTIVATE, TRUE,
                       reinterpret_cast<LPARAM>(&info));
        } else
          SendMessageW(s->tip, TTM_TRACKACTIVATE, FALSE, 0);
        return 0;
      }
      }
    } catch (...) {
      return 0;
    }
    return DefWindowProcW(w, message, wp, lp);
  }
};
NativeMenu::NativeMenu(NativeMenuOptions o, std::function<void(MenuCommand)> c,
                       MenuPreviewProvider p)
    : state_(std::make_shared<State>(o, std::move(c), std::move(p))) {}
NativeMenu::~NativeMenu() = default;
Result<void> NativeMenu::initialize() {
  auto s = state_;
  if (!s->thread() || s->window)
    return Error{ErrorCode::InvalidArgument, "Invalid Win32 menu initialize"};
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  WNDCLASSW type{};
  type.lpfnWndProc = State::procedure;
  type.hInstance = GetModuleHandleW(nullptr);
  type.lpszClassName = L"Cliptiq.NativeMenu";
  type.hCursor = LoadCursor(nullptr, IDC_ARROW);
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return Error{ErrorCode::Io, "Cannot register menu window"};
  s->window = CreateWindowExW(0, type.lpszClassName, L"Cliptiq",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, 300, 150, nullptr, nullptr,
                              type.hInstance, s.get());
  if (!s->window)
    return Error{ErrorCode::ReadOnly, "Cannot create native launcher window"};
  s->status.launcher_visible = true;
  s->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  s->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                           WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           s->window, nullptr, type.hInstance, nullptr);
  if (s->tip) {
    TOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    info.hwnd = s->window;
    info.uId = 1;
    info.lpszText = const_cast<wchar_t *>(L"");
    SendMessageW(s->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    SendMessageW(s->tip, TTM_SETMAXTIPWIDTH, 0, 400);
  }
  s->tray();
  return update({});
}
Result<void> NativeMenu::update(MenuModel m) {
  auto valid = validate_menu_model(m);
  if (!valid)
    return valid;
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "Win32 menu not initialized"};
  s->model = std::move(m);
  return s->rebuild();
}
Result<void> NativeMenu::show(const InvocationContext &c) {
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "Win32 menu not initialized"};
  s->popup(c);
  return {};
}
Result<void> NativeMenu::set_tray_style(TrayStyle t) {
  auto s = state_;
  if (!s->thread() || static_cast<unsigned>(t) > 2)
    return Error{ErrorCode::InvalidArgument, "Invalid tray style"};
  s->options.tray_style = t;
  s->tray();
  return {};
}
Result<void> NativeMenu::process_events(unsigned wait) {
  auto s = state_;
  if (!s->thread())
    return Error{ErrorCode::InvalidArgument,
                 "Win32 menu requires owner thread"};
  MSG message;
  for (unsigned i = 0;
       i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (wait)
    MsgWaitForMultipleObjects(0, nullptr, FALSE, std::min(wait, 10u),
                              QS_ALLINPUT);
  return {};
}
NativeMenuStatus NativeMenu::status() const { return state_->status; }
NativeMenuHandles NativeMenu::native_handles() const {
  return {reinterpret_cast<uintptr_t>(state_->window),
          reinterpret_cast<uintptr_t>(state_->menu)};
}
} // namespace cliptiq

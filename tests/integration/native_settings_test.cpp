#include "cliptiq/native_menu.hpp"
#include "cliptiq/settings.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include <commctrl.h>
HWND child(HWND parent, int id) {
  struct Search {
    int id;
    HWND found;
  } search{id, nullptr};
  EnumChildWindows(
      parent,
      [](HWND w, LPARAM data) -> BOOL {
        auto s = reinterpret_cast<Search *>(data);
        if (GetDlgCtrlID(w) == s->id) {
          s->found = w;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
GtkWidget *find(GtkWidget *w, const char *name) {
  if (std::string(gtk_widget_get_name(w)) == name)
    return w;
  if (GTK_IS_CONTAINER(w)) {
    auto children = gtk_container_get_children(GTK_CONTAINER(w));
    GtkWidget *out = nullptr;
    for (auto p = children; p && !out; p = p->next)
      out = find(GTK_WIDGET(p->data), name);
    g_list_free(children);
    return out;
  }
  return nullptr;
}
#endif
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
#ifdef __APPLE__
NSView *find(NSView *view, NSString *key) {
  if ([view.identifier isEqualToString:key])
    return view;
  for (NSView *child in view.subviews) {
    auto out = find(child, key);
    if (out)
      return out;
  }
  return nil;
}
#endif
int run() {
  try {
#ifdef _WIN32
    IsolatedClipboard station;
#endif
    auto store = take(open_store(":memory:"));
    auto settings = default_settings(CapturePlatform::X11);
#ifdef _WIN32
    for (unsigned n = 0; n < 30; ++n)
      settings.hotkeys["folder:existing-" + std::to_string(n)] = {};
#endif
    Capabilities caps;
    caps.platform = CapturePlatform::X11;
    caps.capture = {CapabilityStatus::Available, "fixture"};
    caps.auto_paste = {CapabilityStatus::Limited, "fixture"};
    unsigned applied = 0;
    NativeSettings ui;
    CHECK(ui.show(settings, caps, [&](Settings s) {
      ++applied;
      return save_settings(store, s);
    }));
    CHECK(ui.native_handle());
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(ui.native_handle());
    CHECK(TabCtrl_GetItemCount(GetDlgItem(window, 200)) == 8);
    CHECK(SetWindowTextW(child(window, 1000), L"5"));
    auto fields = settings_fields(settings, caps);
    unsigned index = 0;
    while (index < fields.size() && fields[index].key != "hotkey:main")
      ++index;
    TabCtrl_SetCurSel(GetDlgItem(window, 200), 4);
    NMHDR notify{GetDlgItem(window, 200), 200, UINT(TCN_SELCHANGE)};
    SendMessageW(window, WM_NOTIFY, 200, reinterpret_cast<LPARAM>(&notify));
    SetFocus(child(window, 1000 + index));
    auto shortcut = child(window, 1000 + index);
    BYTE old_keys[256], keys[256]{};
    CHECK(GetKeyboardState(old_keys));
    keys[VK_MENU] = 0x80;
    CHECK(SetKeyboardState(keys));
    SendMessageW(shortcut, WM_SYSKEYDOWN, 'Q', 0);
    CHECK(SetKeyboardState(old_keys));
    wchar_t recorded[128]{};
    GetWindowTextW(shortcut, recorded, 128);
    bool alt_recorded = std::wstring(recorded) == L"Alt+Q";
    SCROLLINFO scroll{sizeof(scroll), SIF_ALL};
    auto panel = GetParent(shortcut);
    bool scrollable = GetScrollInfo(panel, SB_VERT, &scroll) &&
                      scroll.nMax > int(scroll.nPage);
    std::cout << "Alt recorder=" << alt_recorded
              << " folder page scrollable=" << scrollable << '\n';
    CHECK(alt_recorded && scrollable);
    auto recovery =
        std::find_if(fields.begin(), fields.end(),
                     [](const auto &f) { return f.key == "hotkey:settings"; });
    auto recovery_window = child(window, 1000 + int(recovery - fields.begin()));
    SendMessageW(panel, WM_VSCROLL, SB_BOTTOM, 0);
    RECT recovered{}, viewport{}, apply_before{}, apply_after{};
    GetWindowRect(recovery_window, &recovered);
    GetWindowRect(panel, &viewport);
    GetWindowRect(GetDlgItem(window, 100), &apply_before);
    CHECK(recovered.top >= viewport.top && recovered.bottom <= viewport.bottom);
    SendMessageW(panel, WM_VSCROLL, SB_TOP, 0);
    GetWindowRect(GetDlgItem(window, 100), &apply_after);
    CHECK(EqualRect(&apply_before, &apply_after));
    SetFocus(shortcut);
    CHECK(ui.record_chord(take(parse_chord("Ctrl+Alt+Q"))));
    SendMessageW(window, WM_COMMAND, 100, 0);
#elif defined(__APPLE__)
    auto window = (__bridge NSWindow *)(void *)ui.native_handle();
    auto tabs = find(window.contentView, @"settings-tabs");
    CHECK([tabs isKindOfClass:NSTabView.class] &&
          [(NSTabView *)tabs numberOfTabViewItems] == 8);
    auto field = (NSTextField *)find(window.contentView, @"history_count");
    CHECK(field);
    field.stringValue = @"5";
    [(NSTabView *)tabs selectTabViewItemAtIndex:4];
    auto shortcut = (NSTextField *)find(window.contentView, @"hotkey:main");
    CHECK(shortcut);
    [window makeFirstResponder:shortcut];
    [window resignKeyWindow];
    CHECK(!ui.record_chord(take(parse_chord("Ctrl+Alt+W"))));
    // CLI-hosted AppKit can lack activation rights. Test the native field-editor
    // event path without asserting an actual key-window grant from the desktop.
    auto editor = [window fieldEditor:YES forObject:shortcut];
    CHECK(editor);
    auto event = [NSEvent keyEventWithType:NSEventTypeKeyDown
      location:NSZeroPoint modifierFlags:NSEventModifierFlagControl | NSEventModifierFlagOption
      timestamp:0 windowNumber:window.windowNumber context:nil characters:@"q"
      charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
    [editor keyDown:event];
    CHECK([shortcut.stringValue isEqualToString:@"Ctrl+Alt+Q"]);
    [(NSButton *)find(window.contentView, @"apply") performClick:nil];
#else
    auto window = reinterpret_cast<GtkWidget *>(ui.native_handle());
    auto tabs = find(window, "settings-tabs");
    CHECK(tabs && gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)) == 8);
    auto field = find(window, "history_count");
    CHECK(GTK_IS_ENTRY(field));
    gtk_entry_set_text(GTK_ENTRY(field), "5");
    auto apply = find(window, "apply");
    CHECK(GTK_IS_BUTTON(apply));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), 4);
    auto shortcut = find(window, "hotkey:main");
    CHECK(shortcut);
    gtk_window_set_focus(GTK_WINDOW(window), shortcut);
    for (int n = 0; n < 30; ++n) {
      CHECK(ui.process_events());
      g_usleep(2000);
    }
    if (gtk_window_is_active(GTK_WINDOW(window)))
      CHECK(ui.record_chord(take(parse_chord("Ctrl+Alt+Q"))));
    else {
      // Headless Sway has no input seat; exercise the owned recorder event
      // path.
      GdkEventKey key{};
      key.type = GDK_KEY_PRESS;
      key.keyval = GDK_KEY_q;
      key.state = GdkModifierType(GDK_CONTROL_MASK | GDK_MOD1_MASK);
      gboolean handled = FALSE;
      g_signal_emit_by_name(shortcut, "key-press-event", &key, &handled);
      CHECK(handled && std::string(gtk_entry_get_text(GTK_ENTRY(shortcut))) ==
                           "Ctrl+Alt+Q");
    }
    // A background window retains its remembered shortcut-field focus.
    auto other = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_show_all(other);
    gtk_window_present(GTK_WINDOW(other));
    gdk_window_focus(gtk_widget_get_window(other), GDK_CURRENT_TIME);
    for (int n = 0; n < 30; ++n) {
      CHECK(ui.process_events());
      g_usleep(2000);
    }
    CHECK(!gtk_window_is_active(GTK_WINDOW(window)));
    CHECK(!ui.record_chord(take(parse_chord("Ctrl+Alt+W"))));
    gtk_widget_destroy(other);
    gtk_button_clicked(GTK_BUTTON(apply));
    auto future = find(window, "login");
    CHECK(future && !gtk_widget_get_sensitive(future));
#endif
    CHECK(ui.process_events());
    CHECK(applied == 1 &&
          settings_history(take(load_settings(store))).max_count == 5);
    CHECK(format_chord(*take(load_settings(store)).hotkeys.at("main")) ==
          "Ctrl+Alt+Q");
    std::cout << "PASS "
                 "eight_native_panels_apply_to_real_store_and_disabled_future_"
                 "controls\n";
    NativeMenuOptions options;
    options.tray_style = TrayStyle::Hidden;
    NativeMenu menu(options, [](auto) {});
    CHECK(menu.initialize());
    MenuModel model;
    MenuItem item;
    item.label = "Swatch";
    item.action = MenuAction::SelectHistory;
    item.id = "r";
    item.preview = PreviewRef{"r", 0xff8800ff};
    model.items.push_back(item);
    CHECK(menu.update(model));
    options.preview_width = 8;
    options.preview_height = 8;
    CHECK(menu.set_options(options));
#ifdef _WIN32
    auto native_menu = reinterpret_cast<HMENU>(menu.native_handles().menu);
    MENUITEMINFOW info{sizeof(info)};
    info.fMask = MIIM_BITMAP;
    CHECK(GetMenuItemInfoW(native_menu, 0, TRUE, &info) && info.hbmpItem);
    BITMAP image{};
    CHECK(GetObjectW(info.hbmpItem, sizeof(image), &image));
    CHECK(image.bmWidth == 8 && image.bmHeight == 8);
#elif defined(__APPLE__)
    auto native_menu = (__bridge NSMenu *)(void *)menu.native_handles().menu;
    CHECK([native_menu itemAtIndex:0].image.size.width == 8 &&
          [native_menu itemAtIndex:0].image.size.height == 8);
#else
    auto native_menu =
        reinterpret_cast<GtkWidget *>(menu.native_handles().menu);
    auto children = gtk_container_get_children(GTK_CONTAINER(native_menu));
    CHECK(children);
    auto row = GTK_WIDGET(children->data);
    auto box = gtk_bin_get_child(GTK_BIN(row));
    auto contents = gtk_container_get_children(GTK_CONTAINER(box));
    GtkWidget *image = nullptr;
    for (auto item = contents; item; item = item->next)
      if (GTK_IS_IMAGE(item->data))
        image = GTK_WIDGET(item->data);
    CHECK(image);
    auto pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(image));
    CHECK(pixbuf && gdk_pixbuf_get_width(pixbuf) == 8 &&
          gdk_pixbuf_get_height(pixbuf) == 8);
    g_list_free(contents);
    g_list_free(children);
#endif
    std::cout << "PASS live_native_options_rebuild_preview_bitmap\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL native_settings: " << e.what() << '\n';
    return 1;
  }
}
int main() {
#ifdef __APPLE__
  @autoreleasepool {
    return run();
  }
#else
  return run();
#endif
}

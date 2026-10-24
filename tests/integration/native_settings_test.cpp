#include "cliptiq/native_menu.hpp"
#include "cliptiq/settings.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include <commctrl.h>
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
    CHECK(SetWindowTextW(GetDlgItem(window, 1000), L"5"));
    auto fields = settings_fields(settings, caps);
    unsigned index = 0;
    while (index < fields.size() && fields[index].key != "hotkey:main")
      ++index;
    TabCtrl_SetCurSel(GetDlgItem(window, 200), 4);
    NMHDR notify{GetDlgItem(window, 200), 200, UINT(TCN_SELCHANGE)};
    SendMessageW(window, WM_NOTIFY, 200, reinterpret_cast<LPARAM>(&notify));
    SetFocus(GetDlgItem(window, 1000 + index));
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
    CHECK(ui.record_chord(take(parse_chord("Ctrl+Alt+Q"))));
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
    CHECK(ui.record_chord(take(parse_chord("Ctrl+Alt+Q"))));
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

#include "cliptiq/native_menu.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
static unsigned popup_calls = 0;
static bool pick_item = false;
extern "C" UINT cliptiq_test_track_popup(HMENU menu, UINT, int, int, HWND,
                                         LPTPMPARAMS) {
  ++popup_calls;
  return pick_item ? GetMenuItemID(menu, 0) : 0;
}
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
static bool pick_item = false;
@interface NSMenu (CliptiqPopupBoundary)
- (BOOL)cliptiq_testPopup:(NSMenuItem *)item
               atLocation:(NSPoint)p
                   inView:(NSView *)view;
@end
@implementation NSMenu (CliptiqPopupBoundary)
- (BOOL)cliptiq_testPopup:(NSMenuItem *)item
               atLocation:(NSPoint)p
                   inView:(NSView *)view {
  (void)item;
  (void)p;
  (void)view;
  auto d = self.delegate;
  if ([d respondsToSelector:@selector(menuWillOpen:)])
    [d menuWillOpen:self];
  if (pick_item)
    [self performActionForItemAtIndex:0];
  if ([d respondsToSelector:@selector(menuDidClose:)])
    [d menuDidClose:self];
  return pick_item;
}
@end
#else
#include <gtk/gtk.h>
#endif
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
void ok(Result<void> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
}
int run(int argc, char **argv) {
  try {
    CHECK(argc == 2);
    std::string which = argv[1];
#ifdef _WIN32
    IsolatedClipboard isolate;
#elif !defined(__APPLE__)
    CHECK(getenv("CLIPTIQ_TEST_ISOLATED"));
#endif
    MenuCommand command;
    unsigned calls = 0;
    NativeMenuOptions options;
    options.tray_style = TrayStyle::Hidden;
    NativeMenu ui(options, [&](MenuCommand c) {
      command = std::move(c);
      ++calls;
    });
    ok(ui.initialize());
    MenuModel model;
    model.revision = 55;
    MenuItem item;
    item.label = "History";
    item.id = "row";
    item.action = MenuAction::SelectHistory;
    item.preview = PreviewRef{"row", 0xff8800ff};
    model.items.push_back(item);
    ok(ui.update(model));
    auto handles = ui.native_handles();
    InvocationContext ctx;
    ctx.target = "old-target";
    ctx.activation_token = "old-token";
    ctx.x = 40;
    ctx.y = 60;
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(handles.window);
    if (which == "keyboard") {
      SetFocus(window);
      PostMessageW(window, WM_KEYDOWN, VK_TAB, 0);
      ok(ui.process_events(2));
      CHECK(GetFocus() == GetDlgItem(window, 1));
      PostMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
      ok(ui.process_events(2));
      CHECK(popup_calls == 1);
    }
    if (which == "dpi") {
      for (auto dpi : {96u, 144u, 192u}) {
        RECT rect{100, 100, 500, 400};
        SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi),
                     reinterpret_cast<LPARAM>(&rect));
        auto menu = reinterpret_cast<HMENU>(ui.native_handles().menu);
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_BITMAP;
        CHECK(GetMenuItemInfoW(menu, 0, TRUE, &info) && info.hbmpItem);
        BITMAP actual{};
        CHECK(GetObjectW(info.hbmpItem, sizeof(actual), &actual));
        CHECK(actual.bmWidth == LONG(16 * dpi / 96) &&
              actual.bmHeight == LONG(16 * dpi / 96));
      }
    }
    if (which == "context") {
      pick_item = false;
      ok(ui.show(ctx));
      auto menu = reinterpret_cast<HMENU>(ui.native_handles().menu);
      SendMessageW(window, WM_COMMAND, GetMenuItemID(menu, 0), 0);
      CHECK(calls == 1 && command.invocation.target.empty() &&
            command.invocation.activation_token.empty());
      pick_item = true;
      ok(ui.show(ctx));
      CHECK(calls == 2 && command.invocation.target == ctx.target &&
            command.invocation.activation_token == ctx.activation_token);
    }
#elif defined(__APPLE__)
    CHECK(which == "context");
    Method original = class_getInstanceMethod(
        NSMenu.class, @selector(popUpMenuPositioningItem:atLocation:inView:));
    Method seam = class_getInstanceMethod(
        NSMenu.class, @selector(cliptiq_testPopup:atLocation:inView:));
    struct Guard {
      Method a, b;
      ~Guard() { method_exchangeImplementations(a, b); }
    } guard{original, seam};
    method_exchangeImplementations(original, seam);
    pick_item = false;
    ok(ui.show(ctx));
    auto menu = (__bridge NSMenu *)(void *)ui.native_handles().menu;
    if ([menu.delegate respondsToSelector:@selector(menuWillOpen:)])
      [menu.delegate menuWillOpen:menu];
    [menu performActionForItemAtIndex:0];
    CHECK(calls == 1 && command.invocation.target.empty() &&
          command.invocation.activation_token.empty());
    pick_item = true;
    ok(ui.show(ctx));
    CHECK(calls == 2 && command.invocation.target == ctx.target &&
          command.invocation.activation_token == ctx.activation_token);
#else
    auto menu = GTK_WIDGET((void *)handles.menu);
    bool wayland = std::string(getenv("CLIPTIQ_TEST_ISOLATED")) == "wayland";
    auto activate = [&] {
      auto children = gtk_container_get_children(GTK_CONTAINER(menu));
      gtk_menu_item_activate(GTK_MENU_ITEM(children->data));
      g_list_free(children);
    };
    if (which == "context") {
      ok(ui.show(ctx));
      activate();
      CHECK(calls == 1);
      if (!wayland)
        CHECK(command.invocation.target == ctx.target);
      else
        CHECK(command.invocation.target.empty());
      gtk_menu_popdown(GTK_MENU(menu));
      ok(ui.process_events(2));
      ok(ui.show(ctx));
      gtk_menu_popdown(GTK_MENU(menu));
      ok(ui.process_events(2));
      if (!wayland) {
        auto button = gtk_bin_get_child(GTK_BIN((void *)handles.window));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
      }
      activate();
      CHECK(calls == 2 && command.invocation.target.empty() &&
            command.invocation.activation_token.empty());
    }
    if (which == "coordinates") {
      CHECK(!wayland);
      auto position = [&] {
        for (int n = 0; n < 8; ++n)
          ok(ui.process_events(2));
        auto top = gtk_widget_get_toplevel(menu);
        int x = 0, y = 0;
        gdk_window_get_origin(gtk_widget_get_window(top), &x, &y);
        return std::pair{x, y};
      };
      ok(ui.show(ctx));
      auto a = position();
      gtk_menu_popdown(GTK_MENU(menu));
      ctx.x = 120;
      ctx.y = 140;
      ok(ui.show(ctx));
      auto b = position();
      CHECK(b.first - a.first >= 60 && b.second - a.second >= 60);
      gtk_menu_popdown(GTK_MENU(menu));
    }
#endif
    std::cout << "PASS native_menu_review_" << which << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL menu_review: " << e.what() << '\n';
    return 1;
  }
}
int main(int argc, char **argv) {
#ifdef __APPLE__
  @autoreleasepool {
    return run(argc, argv);
  }
#else
  return run(argc, argv);
#endif
}

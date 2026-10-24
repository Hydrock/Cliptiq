#include "cliptiq/native_menu.hpp"
#include "cliptiq/paste.hpp"
#include <gtk/gtk.h>
#include <iostream>
#include <stdexcept>
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
struct Copy : ClipboardBackend {
  ClipboardSnapshot output;
  unsigned writes = 0;
  Capabilities capabilities() const override { return {}; }
  Result<ClipboardSnapshot> read_snapshot() override { return output; }
  Result<std::string> write_snapshot(const ClipboardSnapshot &s) override {
    output = s;
    ++writes;
    return std::string("self:1");
  }
  Result<void> start(CaptureCallback) override { return {}; }
  void stop() override {}
};
struct Input : PasteBackend {
  TargetToken target;
  Result<void> dispatch(const TargetToken &t, Shortcut) override {
    target = t;
    return {};
  }
};
int main() {
  try {
    auto store = take(open_store(":memory:"));
    ClipboardSnapshot snapshot;
    snapshot.representations = {
        {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'a'}}};
    auto id = *take(ingest(store, snapshot, {}, 1));
    Copy copy;
    Input input;
    unsigned openings = 0, selected = 0;
    NativeMenuOptions options;
    options.tray_style = TrayStyle::Hidden;
    options.invocation_provider = [&] {
      ++openings;
      return InvocationContext{"issued-target"};
    };
    NativeMenu menu(options, [&](MenuCommand c) {
      CHECK(c.id == id);
      SelectionIntent intent;
      intent.command = c;
      auto result = take(execute_selection(store, intent, copy, input));
      CHECK(result.status == SelectionStatus::Dispatched);
      ++selected;
    });
    CHECK(menu.initialize());
    MenuSettings settings;
    settings.inline_count = 10;
    CHECK(menu.update(take(build_menu(store, MenuKind::History, settings))));
    auto win = reinterpret_cast<GtkWidget *>(menu.native_handles().window);
    auto child = gtk_bin_get_child(GTK_BIN(win));
    CHECK(GTK_IS_MENU_BUTTON(child));
    auto button = GTK_MENU_BUTTON(child);
    auto popup = GTK_WIDGET(gtk_menu_button_get_popup(button));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    CHECK(menu.process_events());
    CHECK(openings == 1);
    auto items = gtk_container_get_children(GTK_CONTAINER(popup));
    CHECK(items);
    gtk_menu_item_activate(GTK_MENU_ITEM(items->data));
    g_list_free(items);
    CHECK(selected == 1 && copy.writes == 1 &&
          input.target == "issued-target" &&
          take(list_history(store)).size() == 1);
    std::cout << "PASS native_menu_provider_to_history_selection_pipeline\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL menu_paste_bridge: " << e.what() << '\n';
    return 1;
  }
}

#include "cliptiq/editor.hpp"
#include "cliptiq/menu.hpp"
#include "cliptiq/xml.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
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
Bytes bytes(const std::string &s) { return {s.begin(), s.end()}; }
#ifdef __APPLE__
NSView *find_xml(NSView *view, NSString *name) {
  if ([view.identifier isEqualToString:name])
    return view;
  for (NSView *child in view.subviews) {
    auto found = find_xml(child, name);
    if (found)
      return found;
  }
  return nil;
}
#elif !defined(_WIN32)
GtkWidget *find_xml(GtkWidget *view, const char *name) {
  if (std::string(gtk_widget_get_name(view)) == name)
    return view;
  if (GTK_IS_CONTAINER(view)) {
    auto children = gtk_container_get_children(GTK_CONTAINER(view));
    GtkWidget *found = nullptr;
    for (auto c = children; c && !found; c = c->next)
      found = find_xml(GTK_WIDGET(c->data), name);
    g_list_free(children);
    return found;
  }
  return nullptr;
}
#endif
int run() {
  try {
#ifdef _WIN32
    IsolatedClipboard station;
#endif
    struct Temp {
      std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          ("cliptiq-native-xml-" +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
      Temp() { std::filesystem::create_directory(path); }
      ~Temp() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
      }
    } temp;
    auto db = take(open_store((temp.path / "store.db").u8string()));
    auto f = take(create_folder(db, "existing"));
    unsigned changes = 0;
    SnippetEditorCallbacks callbacks;
    callbacks.changed = [&] { ++changes; };
    auto model = std::make_shared<SnippetEditorSession>(db, callbacks);
    CHECK(model->reload());
    CHECK(model->select(SnippetItemId{SnippetItemKind::Folder, f}));
    take(model->add_snippet());
    CHECK(model->edit_content("first"));
    CHECK(model->edit_content("second"));
    CHECK(model->can_undo());
    changes = 0;
    NativeSnippetEditor ui;
    CHECK(ui.show(model));
    auto input = (temp.path / u8"импорт.xml").u8string(),
         output = (temp.path / u8"экспорт.xml").u8string();
    CHECK(write_snippet_xml_atomic(
        input, bytes(u8"<folders><folder><title>Новая "
                     u8"😀</title><snippets><snippet><title>A</title><content> "
                     u8" &lt;hello&gt;\nworld  "
                     u8"</content></snippet></snippets></folder></folders>")));
    auto wait = [&] {
      for (unsigned i = 0; i < 1000 && ui.xml_transfer_busy(); ++i) {
        CHECK(ui.process_events());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      CHECK(!ui.xml_transfer_busy());
    };
    CHECK(ui.import_xml_path(input));
    CHECK(ui.xml_transfer_busy());
    // Let the worker commit, but deliberately do not poll the editor: the
    // transfer lock is still held until UI completion is processed.
    for (unsigned i = 0; i < 1000 && take(list_folders(db)).size() != 2; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(take(list_folders(db)).size() == 2 && ui.xml_transfer_busy());
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(ui.native_handle());
    SendMessageW(window, WM_COMMAND, 510, 0);
    CHECK(!IsWindowEnabled(window));
#elif defined(__APPLE__)
    auto window = (__bridge NSWindow *)(void *)ui.native_handle();
    auto body = (NSTextView *)find_xml(window.contentView, @"editor-content");
    CHECK(body);
    auto event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                  location:NSZeroPoint
                             modifierFlags:NSEventModifierFlagCommand
                                 timestamp:0
                              windowNumber:window.windowNumber
                                   context:nil
                                characters:@"z"
               charactersIgnoringModifiers:@"z"
                                 isARepeat:NO
                                   keyCode:6];
    [body performKeyEquivalent:event];
    CHECK(!body.editable);
#else
    auto window = reinterpret_cast<GtkWidget *>(ui.native_handle());
    auto undo = find_xml(window, "editor-undo");
    CHECK(undo);
    g_signal_emit_by_name(undo, "clicked");
    CHECK(!gtk_widget_get_sensitive(window));
#endif
    CHECK(model->content() == "second" && changes == 0);

    CHECK(!ui.export_xml_path(output));
    CHECK(!take(ui.request_close()));
    wait();
    auto folders = take(list_folders(db));
    CHECK(folders.size() == 2 && folders[0].id == f);
    CHECK(changes == 1 && model->folders().size() == 2);
    auto menu = take(build_folder_menu(db, folders[1].id));
    CHECK(!menu.items.empty() && menu.items[0].enabled);
    CHECK(ui.export_xml_path(output));
    wait();
    auto decoded = take(decode_clipy_xml(take(read_snippet_xml(output))));
    CHECK(decoded.size() == 2 && decoded[1].title == u8"Новая 😀" &&
          decoded[1].snippets[0].content == "  <hello>\nworld  ");
    auto before = take(read_snippet_xml(output));
    auto header = take(list_snippet_headers(db))[0];
    auto incompatible = take(get_snippet(db, header.id));
    auto original = incompatible.content;
    incompatible.content = std::string(1, char(1));
    CHECK(save_snippet(db, incompatible));
    CHECK(ui.export_xml_path(output));
    wait();
    CHECK(take(read_snippet_xml(output)) == before);
    incompatible = take(get_snippet(db, header.id));
    incompatible.content = original;
    CHECK(save_snippet(db, incompatible));
    auto rev = take(data_revision(db));
    CHECK(write_snippet_xml_atomic(
        input,
        bytes("<!DOCTYPE folders SYSTEM 'file:///not-readable'><folders/>")));
    CHECK(ui.import_xml_path(input));
    wait();
    CHECK(take(data_revision(db)) == rev && changes == 1);
    CHECK(model->select(SnippetItemId{SnippetItemKind::Snippet,
                                      take(list_snippet_headers(db))[0].id}));
    CHECK(!model->edit_content(std::string(1024 * 1024 + 1, 'x')));
    CHECK(!ui.import_xml_path(input));
    CHECK(!ui.export_xml_path(output));
    CHECK(model->reload());
    CHECK(ui.request_close());
    std::cout << "PASS "
                 "native_xml_background_append_export_menu_invalid_atomic_"
                 "dirty_gate\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL native_xml: " << e.what() << '\n';
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

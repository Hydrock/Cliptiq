#include "cliptiq/editor.hpp"
#include "cliptiq/menu.hpp"
#include "cliptiq/settings.hpp"
#include <iostream>
#include "sqlite3.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include <commctrl.h>
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
#ifndef _WIN32
#ifdef __APPLE__
NSView *find(NSView *w, NSString *name) {
  if ([w.identifier isEqualToString:name])
    return w;
  for (NSView *c in w.subviews) {
    auto r = find(c, name);
    if (r)
      return r;
  }
  return nil;
}
#else
GtkWidget *find(GtkWidget *w, const char *name) {
  if (std::string(gtk_widget_get_name(w)) == name)
    return w;
  if (GTK_IS_CONTAINER(w)) {
    auto children = gtk_container_get_children(GTK_CONTAINER(w));
    GtkWidget *r = nullptr;
    for (auto c = children; c && !r; c = c->next)
      r = find(GTK_WIDGET(c->data), name);
    g_list_free(children);
    return r;
  }
  return nullptr;
}
#endif
#endif
struct Backend : HotkeyBackend {
  unsigned serial = 0;
  std::map<unsigned, std::string> held;
  Result<unsigned> acquire(const ActionId &, const KeyChord &c) override {
    auto v = format_chord(c);
    if (v == "Ctrl+Alt+W")
      return Error{ErrorCode::Busy, "fixture external conflict"};
    for (auto &p : held)
      if (p.second == v)
        return Error{ErrorCode::Busy, "duplicate"};
    held[++serial] = v;
    return serial;
  }
  void release(unsigned i) override { held.erase(i); }
  Result<void> dispatch() override { return {}; }
  Capability capability() const override {
    return {CapabilityStatus::Available, "fixture"};
  }
};
int run() {
  try {
#ifdef _WIN32
    IsolatedClipboard station;
#endif
    struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("cliptiq-native-editor-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db");~Temp(){std::error_code ec;std::filesystem::remove(path,ec);}} temp;
    auto store = take(open_store(temp.path.u8string()));
    Backend backend;
    HotkeyRegistry registry(backend);
    auto f = take(create_folder(store, "Folder A"));
    auto other = take(create_folder(store, "Folder B"));
    SnippetEditorCallbacks cb;
    unsigned changes = 0;
    cb.changed = [&] { ++changes; };
    cb.bind_folder = [&](const FolderId &id, auto chord) {
      auto s = take(load_settings(store));
      s.hotkeys["folder:" + id] = chord;
      return apply_settings(store, registry, s);
    };
    cb.folder_chord = [&](const FolderId &id) {
      return take(load_settings(store)).hotkeys.at("folder:" + id);
    };
    auto session = std::make_shared<SnippetEditorSession>(store, cb);
    CHECK(session->reload());
    CHECK(session->select(SnippetItemId{SnippetItemKind::Folder, f}));
    NativeSnippetEditor ui;
    CHECK(ui.show(session));
    CHECK(ui.native_handle());
    auto settle=[&] {
      for(unsigned i=0;i<400 && session->saving();++i) {CHECK(ui.process_events());std::this_thread::sleep_for(std::chrono::milliseconds(5));}
      CHECK(!session->saving());
    };
    auto peer=take(open_store(temp.path.u8string()));
    std::chrono::steady_clock::time_point input_start;
    auto lock_input=[&] {CHECK(sqlite3_exec(peer.connection(),"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK);input_start=std::chrono::steady_clock::now();};
    auto release_input=[&] {
      auto elapsed=std::chrono::steady_clock::now()-input_start;
      CHECK(sqlite3_exec(peer.connection(),"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK);
      CHECK(elapsed<std::chrono::milliseconds(150));
    };
#ifdef _WIN32
    auto window = reinterpret_cast<HWND>(ui.native_handle());
    auto click = [&](int id) {
      SendMessageW(window, WM_COMMAND, id, 0);
      CHECK(ui.process_events());
    };
    click(508);
    auto selected = session->selected();
    CHECK(selected && selected->kind == SnippetItemKind::Snippet);
    auto id = selected->value;
    auto body = GetDlgItem(window, 504);
    CHECK(body);
    lock_input();
    CHECK(SetWindowTextW(body, L"  Привет\r\nline2 😀  "));
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(504, EN_CHANGE),
                 reinterpret_cast<LPARAM>(body));
    release_input();settle();
    CHECK(session->content() == u8"  Привет\nline2 😀  ");
    BYTE saved_keys[256], pressed[256]{};CHECK(GetKeyboardState(saved_keys));pressed[VK_CONTROL]=0x80;CHECK(SetKeyboardState(pressed));
    SendMessageW(body,WM_KEYDOWN,'Z',0);SendMessageW(body,WM_CHAR,26,0);CHECK(SetKeyboardState(saved_keys));
    CHECK(session->content().empty());
    click(511);
    settle();
    CHECK(session->content() == u8"  Привет\nline2 😀  ");
    auto outline=GetDlgItem(window,501);SendMessageW(outline,WM_KEYDOWN,VK_SPACE,0);CHECK(ui.process_events());
#elif defined(__APPLE__)
    auto window = (__bridge NSWindow *)(void *)ui.native_handle();
    auto click = [&](NSString *name) {
      auto b = (NSButton *)find(window.contentView, name);
      CHECK(b);
      [b performClick:nil];
    };
    click(@"editor-add-snippet");
    auto selected = session->selected();
    CHECK(selected && selected->kind == SnippetItemKind::Snippet);
    auto id = selected->value;
    auto body = (NSTextView *)find(window.contentView, @"editor-content");
    CHECK(body);
    lock_input();
    body.string = @"  Привет\nline2 😀  ";
    [body.delegate
        textDidChange:[NSNotification
                          notificationWithName:NSTextDidChangeNotification
                                        object:body]];
    release_input();settle();
    CHECK(session->content() == u8"  Привет\nline2 😀  ");
    auto undo_event=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:window.windowNumber context:nil characters:@"z" charactersIgnoringModifiers:@"z" isARepeat:NO keyCode:6];
    CHECK([body performKeyEquivalent:undo_event]);
    CHECK(session->content().empty());
    click(@"editor-redo");
    auto enabled = (NSButton *)find(window.contentView, @"editor-enabled");
    enabled.state = NSControlStateValueOff;
    [NSApp sendAction:enabled.action to:enabled.target from:enabled];
#else
    auto window = reinterpret_cast<GtkWidget *>(ui.native_handle());
    auto click = [&](const char *name) {
      auto b = find(window, name);
      CHECK(GTK_IS_BUTTON(b));
      gtk_button_clicked(GTK_BUTTON(b));
    };
    click("editor-add-snippet");
    auto selected = session->selected();
    CHECK(selected && selected->kind == SnippetItemKind::Snippet);
    auto id = selected->value;
    auto body = find(window, "editor-content");
    CHECK(GTK_IS_TEXT_VIEW(body));
    lock_input();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(body)),
                             u8"  Привет\nline2 😀  ", -1);
    release_input();settle();
    CHECK(session->content() == u8"  Привет\nline2 😀  ");
    GdkEventKey undo_event{};undo_event.type=GDK_KEY_PRESS;undo_event.keyval=GDK_KEY_z;undo_event.state=GDK_CONTROL_MASK;gboolean handled=FALSE;
    g_signal_emit_by_name(body,"key-press-event",&undo_event,&handled);CHECK(handled);
    CHECK(session->content().empty());
    click("editor-redo");
    auto enabled = find(window, "editor-enabled");
    CHECK(GTK_IS_TOGGLE_BUTTON(enabled));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), FALSE);
#endif
    CHECK(!take(get_snippet(store, id)).enabled);
    auto menu = take(build_folder_menu(store, f));
    CHECK(menu.items.size() == 1 && !menu.items[0].enabled);
    CHECK(session->select(SnippetItemId{SnippetItemKind::Folder, other}));
    CHECK(!session->can_undo());
    CHECK(ui.show(session));
#ifdef _WIN32
    window = reinterpret_cast<HWND>(ui.native_handle());
    CHECK(SetWindowTextW(GetDlgItem(window, 505), L"Ctrl+Alt+Q"));
    SendMessageW(window, WM_COMMAND, 506, 0);
#elif defined(__APPLE__)
    window = (__bridge NSWindow *)(void *)ui.native_handle();
    auto shortcut = (NSTextField *)find(window.contentView, @"editor-shortcut");
    CHECK(shortcut);
    shortcut.stringValue = @"Ctrl+Alt+Q";
    auto bind = (NSButton *)find(window.contentView, @"editor-bind");
    CHECK(bind);
    [bind performClick:nil];
#else
    window = reinterpret_cast<GtkWidget *>(ui.native_handle());
    auto shortcut = find(window, "editor-shortcut");
    CHECK(GTK_IS_ENTRY(shortcut));
    gtk_entry_set_text(GTK_ENTRY(shortcut), "Ctrl+Alt+Q");
    gtk_button_clicked(GTK_BUTTON(find(window, "editor-bind")));
#endif
    CHECK(format_chord(
              *take(load_settings(store)).hotkeys.at("folder:" + other)) ==
          "Ctrl+Alt+Q");
    CHECK(registry.registered("folder:" + other));
    auto failed = session->bind_folder(take(parse_chord("Ctrl+Alt+W")));
    CHECK(!failed &&
          format_chord(
              *take(load_settings(store)).hotkeys.at("folder:" + other)) ==
              "Ctrl+Alt+Q");
    auto drag = take(session->begin_drag({SnippetItemKind::Snippet, id}));
    auto rev = take(data_revision(store));
    CHECK(session->finish_drag(drag, other, 0, false));
    CHECK(take(data_revision(store)) == rev);
    drag = take(session->begin_drag({SnippetItemKind::Snippet, id}));
    CHECK(session->finish_drag(drag, other, 0, true));
    CHECK(take(get_snippet(store, id)).folder_id == other);
    CHECK(changes >= 5);
    CHECK(ui.show(session));
    auto external = take(get_snippet(store, id));
    external.content = "external";
    CHECK(save_snippet(store, external));
#ifdef _WIN32
    window = reinterpret_cast<HWND>(ui.native_handle());
    body = GetDlgItem(window, 504);
    CHECK(SetWindowTextW(body, L"unsaved draft"));
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(504, EN_CHANGE),
                 reinterpret_cast<LPARAM>(body));
#elif defined(__APPLE__)
    window = (__bridge NSWindow *)(void *)ui.native_handle();
    body = (NSTextView *)find(window.contentView, @"editor-content");
    body.string = @"unsaved draft";
    [body.delegate
        textDidChange:[NSNotification
                          notificationWithName:NSTextDidChangeNotification
                                        object:body]];
#else
    window = reinterpret_cast<GtkWidget *>(ui.native_handle());
    body = find(window, "editor-content");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(body)),
                             "unsaved draft", -1);
#endif
    settle();
    CHECK(session->has_unsaved());
#ifdef _WIN32
    SendMessageW(GetDlgItem(window, 503), BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(window, WM_COMMAND, 503, 0);
    CHECK(SendMessageW(GetDlgItem(window, 503), BM_GETCHECK, 0, 0) ==
          BST_UNCHECKED);
#elif defined(__APPLE__)
    enabled = (NSButton *)find(window.contentView, @"editor-enabled");
    enabled.state = NSControlStateValueOn;
    [NSApp sendAction:enabled.action to:enabled.target from:enabled];
    CHECK(enabled.state == NSControlStateValueOff);
#else
    enabled = find(window, "editor-enabled");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), TRUE);
    CHECK(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled)));
#endif
#ifdef _WIN32
    auto tree = GetDlgItem(window, 501);
    auto previous = TreeView_GetSelection(tree);
    TreeView_SelectItem(tree, TreeView_GetRoot(tree));
    CHECK(TreeView_GetSelection(tree) == previous);
    wchar_t retained[64]{};
    GetWindowTextW(body, retained, 64);
    CHECK(std::wstring(retained) == L"unsaved draft");
#elif defined(__APPLE__)
    auto tree = (NSOutlineView *)find(window.contentView, @"editor-tree");
    auto previous = tree.selectedRow;
    [tree selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
        byExtendingSelection:NO];
    CHECK(tree.selectedRow == previous &&
          [body.string isEqualToString:@"unsaved draft"]);
#else
    auto tree = find(window, "editor-tree");
    auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    auto path = gtk_tree_path_new_from_string("0");
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_path_free(path);
    GtkTreeIter chosen;
    CHECK(gtk_tree_selection_get_selected(selection, nullptr, &chosen));
    gchar *chosen_id = nullptr;
    gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), &chosen, 1,
                       &chosen_id, -1);
    CHECK(chosen_id && id == chosen_id);
    g_free(chosen_id);
    GtkTextIter first, last;
    auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(body));
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    auto retained = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    CHECK(std::string(retained) == "unsaved draft");
    g_free(retained);
#endif
    CHECK(session->selected()->value == id);
    auto handle = ui.native_handle();
    CHECK(!ui.show(session));
    CHECK(ui.native_handle() == handle &&
          take(get_snippet(store, id)).content == "external");
    CHECK(session->reload());
    // A failed load must restore the highlighted item, otherwise Delete/edit
    // acts on a different document from the one the tree advertises.
    Snippet large;large.folder_id=other;large.title="Oversized";
    auto large_id=take(save_snippet(store,large));
    sqlite3_stmt* oversized=nullptr;
    CHECK(sqlite3_prepare_v2(store.connection(),"UPDATE snippets SET content=zeroblob(1048577) WHERE id=?",-1,&oversized,nullptr)==SQLITE_OK);
    sqlite3_bind_text(oversized,1,large_id.c_str(),-1,SQLITE_TRANSIENT);
    CHECK(sqlite3_step(oversized)==SQLITE_DONE);sqlite3_finalize(oversized);CHECK(session->reload());
    CHECK(ui.show(session));
#ifdef _WIN32
    window=reinterpret_cast<HWND>(ui.native_handle());
    tree=GetDlgItem(window,501);previous=TreeView_GetSelection(tree);
    auto target=TreeView_GetNextSibling(tree,previous);CHECK(target);
    TreeView_SelectItem(tree,target);CHECK(TreeView_GetSelection(tree)==previous);
#elif defined(__APPLE__)
    window=(__bridge NSWindow*)(void*)ui.native_handle();
    tree=(NSOutlineView*)find(window.contentView,@"editor-tree");previous=tree.selectedRow;
    CHECK(previous+1<tree.numberOfRows);
    [tree selectRowIndexes:[NSIndexSet indexSetWithIndex:previous+1] byExtendingSelection:NO];
    CHECK(tree.selectedRow==previous);
#else
    window=reinterpret_cast<GtkWidget*>(ui.native_handle());
    tree=find(window,"editor-tree");selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    CHECK(gtk_tree_selection_get_selected(selection,nullptr,&chosen));
    auto target=gtk_tree_model_get_path(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),&chosen);
    gtk_tree_path_next(target);gtk_tree_selection_select_path(selection,target);gtk_tree_path_free(target);
    CHECK(gtk_tree_selection_get_selected(selection,nullptr,&chosen));chosen_id=nullptr;
    gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),&chosen,1,&chosen_id,-1);
    CHECK(chosen_id && id==chosen_id);g_free(chosen_id);
#endif
    CHECK(session->selected()->value==id);
    CHECK(session->edit_title("Still targets highlighted A"));
    CHECK(take(get_snippet(store,id)).title=="Still targets highlighted A");
#if !defined(_WIN32) && !defined(__APPLE__)
    auto transient = std::make_unique<NativeSnippetEditor>();
    SnippetEditorCallbacks lifetime_callbacks;
    lifetime_callbacks.changed = [&] { transient.reset(); };
    auto lifetime_session = std::make_shared<SnippetEditorSession>(store,lifetime_callbacks);
    CHECK(lifetime_session->reload());CHECK(lifetime_session->select(SnippetItemId{SnippetItemKind::Folder,f}));
    CHECK(transient->show(lifetime_session));
    auto retained_button=find(reinterpret_cast<GtkWidget*>(transient->native_handle()),"editor-add-snippet");
    CHECK(retained_button);g_object_ref(retained_button);
    gtk_button_clicked(GTK_BUTTON(retained_button));CHECK(!transient);
    gtk_button_clicked(GTK_BUTTON(retained_button));
    g_object_unref(retained_button);
#endif
    std::cout << "PASS "
                 "native_editor_autosave_unicode_undo_enabled_menu_folder_"
                 "binding_conflict_and_drag_contract\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL native_editor: " << e.what() << '\n';
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

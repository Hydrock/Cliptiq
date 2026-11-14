#include "cliptiq/editor.hpp"
#include "cliptiq/locale.hpp"
#import <AppKit/AppKit.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class T> T take(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().message);
  return std::move(result.value());
}
NSView *find(NSView *view, NSString *identifier) {
  if ([view.identifier isEqualToString:identifier]) return view;
  for (NSView *child in view.subviews) if (auto found = find(child, identifier)) return found;
  return nil;
}
int main(int argc, char **argv) { @autoreleasepool {
  const bool russian=argc>1 && std::string(argv[1])=="--ru";
  activate_locale(russian?"ru":"en");
  struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
      ("cliptiq-macos-rename-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
    ~Temp() { std::error_code error; std::filesystem::remove(path, error); }
  } temp;
  try {
    auto store = take(open_store(temp.path.u8string()));
    auto folder = take(create_folder(store, "Original folder"));
    auto session = std::make_shared<SnippetEditorSession>(store, SnippetEditorCallbacks{});
    CHECK(session->reload());
    CHECK(session->select(SnippetItemId{SnippetItemKind::Folder, folder}));
    NativeSnippetEditor ui;
    CHECK(ui.show(session));
    auto window = (__bridge NSWindow *)(void *)ui.native_handle();
    auto tree = (NSOutlineView *)find(window.contentView, @"editor-tree");
    CHECK(tree);
    // Reopening the same editor session must reuse its outline. AppKit retains
    // disclosure buttons through notification blocks after discarded outlines.
    auto originalNode=[tree itemAtRow:0];
    CHECK(take(ui.request_close()));
    CHECK(ui.show(session));
    CHECK((__bridge NSWindow *)(void *)ui.native_handle()==window);
    CHECK(find(window.contentView,@"editor-tree")==tree);
    CHECK([tree itemAtRow:0]==originalNode);

    CHECK([window.title isEqualToString:russian?@"Сниппеты Cliptiq":@"Cliptiq Snippets"]);
    CHECK([((NSButton *)find(window.contentView,@"editor-add-folder")).title
        isEqualToString:russian?@"Добавить папку":@"Add Folder"]);
    CHECK([((NSButton *)find(window.contentView,@"editor-rename")).title
        isEqualToString:russian?@"Переименовать":@"Rename"]);
    CHECK(!find(window.contentView, @"editor-title"));
    auto settle = [&] {
      for (unsigned n = 0; n < 400 && session->saving(); ++n) {
        CHECK(ui.process_events());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      CHECK(!session->saving());
      CHECK(ui.process_events());
    };
    auto rename = [&](NSString *value, SEL command) {
      auto button = (NSButton *)find(window.contentView, @"editor-rename");
      CHECK(button); [button performClick:nil];
      auto cell = (NSTableCellView *)[tree viewAtColumn:0 row:tree.selectedRow makeIfNecessary:YES];
      CHECK(cell.textField.editable);
      auto editor = (NSTextView *)[window fieldEditor:NO forObject:cell.textField];
      CHECK(editor); editor.string = value; cell.textField.stringValue = value;
      CHECK([cell.textField.delegate control:cell.textField textView:editor doCommandBySelector:command]);
      settle();
    };
    rename(@"Renamed folder", @selector(insertNewline:));
    CHECK(session->title() == "Renamed folder");
    rename(@"Cancelled folder", @selector(cancelOperation:));
    CHECK(session->title() == "Renamed folder");
    auto beginTyping = [&](NSString *value) {
      auto renameButton = (NSButton *)find(window.contentView, @"editor-rename");
      [renameButton performClick:nil];
      auto field = ((NSTableCellView *)[tree viewAtColumn:0 row:tree.selectedRow makeIfNecessary:YES]).textField;
      auto editor = (NSTextView *)[window fieldEditor:NO forObject:field];
      CHECK(field.editable && editor);
      editor.string = value;
      field.stringValue = value;
    };
    beginTyping(@"Folder committed by Add");
    auto add = (NSButton *)find(window.contentView, @"editor-add-snippet");
    [add performClick:nil];
    settle();
    CHECK(session->selected()->kind == SnippetItemKind::Snippet);
    CHECK(session->folders().front().title == "Folder committed by Add");
    auto cell = (NSTableCellView *)[tree viewAtColumn:0 row:tree.selectedRow makeIfNecessary:YES];
    CHECK(cell.textField.editable);
    [window makeFirstResponder:tree]; settle();
    rename(@"Renamed snippet", @selector(insertNewline:));
    CHECK(session->title() == "Renamed snippet");
    CHECK(!find(window.contentView,@"editor-reload"));
    beginTyping(@"Snippet committed on focus change");
    CHECK([window makeFirstResponder:tree]);
    settle();
    CHECK(session->title() == "Snippet committed on focus change");
    // Ending an edit must release its field editor so another rename can start.
    rename(@"Renamed snippet", @selector(insertNewline:));
    CHECK(session->title() == "Renamed snippet");
    rename(@"", @selector(insertNewline:));
    CHECK(session->title() == "Renamed snippet");
    CHECK(session->reload());
    CHECK(ui.request_close());
    auto extraFolder=take(create_folder(store,"Added while hidden"));
    CHECK(session->reload());
    CHECK(ui.show(session));
    CHECK((__bridge NSWindow *)(void *)ui.native_handle()==window);
    CHECK(tree.numberOfRows==3); // two folders and the existing snippet
    CHECK(take(ui.request_close()));
    activate_locale(russian?"en":"ru");
    CHECK(ui.show(session));
    auto translatedWindow=(__bridge NSWindow *)(void *)ui.native_handle();
    CHECK([translatedWindow.title isEqualToString:russian?@"Cliptiq Snippets":@"Сниппеты Cliptiq"]);
    CHECK(take(ui.request_close()));
    std::cout << "macOS inline rename passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
} }

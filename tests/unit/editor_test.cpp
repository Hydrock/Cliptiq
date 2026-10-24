#include "cliptiq/editor.hpp"
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
int main() {
  int failed = 0;
  auto test = [&](const char *n, auto f) {
    try {
      f();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("autosave_undo_redo_resets_at_selection_boundary", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    auto f = take(e.add_folder());
    auto a = take(e.add_snippet());
    CHECK(e.edit_title("A"));
    CHECK(e.edit_content("first\nline"));
    CHECK(e.edit_content("second"));
    CHECK(e.can_undo());
    CHECK(e.undo());
    CHECK(take(get_snippet(db, a)).content == "first\nline");
    CHECK(e.redo());
    CHECK(e.content() == "second");
    auto b = take(e.add_snippet());
    CHECK(!e.can_undo() && !e.can_redo());
    CHECK(e.edit_content("B"));
    CHECK(e.select(SnippetItemId{SnippetItemKind::Snippet, a}));
    CHECK(!e.can_undo() && e.content() == "second");
    CHECK(!e.undo());
    CHECK(take(get_snippet(db, b)).content == "B");
    CHECK(e.select(SnippetItemId{SnippetItemKind::Folder, f}));
    CHECK(!e.edit_content("no folder body"));
  });
  test("cancelled_drag_and_stale_drag_do_not_mutate", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    auto a = take(e.add_folder());
    auto x = take(e.add_snippet());
    auto b = take(e.add_folder());
    auto rev = take(data_revision(db));
    auto token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(e.finish_drag(token, b, 0, false));
    CHECK(take(data_revision(db)) == rev &&
          take(get_snippet(db, x)).folder_id == a);
    token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(set_enabled(db, {SnippetItemKind::Snippet, x}, false));
    CHECK(!e.finish_drag(token, b, 0, true));
    CHECK(take(get_snippet(db, x)).folder_id == a);
    CHECK(e.reload());
    token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(e.finish_drag(token, b, 0, true));
    CHECK(take(get_snippet(db, x)).folder_id == b);
    CHECK(!e.finish_drag(token, a, 0, true));
  });
  test("delete_confirmation_empty_folder_and_binding_conflicts", [] {
    auto db = take(open_store(":memory:"));
    bool conflict = true;
    unsigned changes = 0;
    SnippetEditorCallbacks cb;
    cb.changed = [&] { ++changes; };
    cb.bind_folder = [&](const auto &, auto) -> Result<void> {
      if (conflict)
        return Error{ErrorCode::Busy, "conflict"};
      return {};
    };
    SnippetEditorSession e(db, cb);
    CHECK(e.reload());
    auto f = take(e.add_folder());
    CHECK(!e.bind_folder(take(parse_chord("Ctrl+Alt+Q"))));
    conflict = false;
    CHECK(e.bind_folder(take(parse_chord("Ctrl+Alt+Q"))));
    CHECK(e.erase_selected(false));
    CHECK(e.folders().size() == 1);
    CHECK(e.enable(false));
    CHECK(!take(list_folders(db))[0].enabled);
    CHECK(e.erase_selected(true));
    CHECK(e.folders().empty() && !e.selected() && changes >= 3);
    CHECK(!e.add_snippet());
  });
  test("failed_edit_preserves_document_and_undo", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    take(e.add_folder());
    auto id = take(e.add_snippet());
    CHECK(e.edit_content("safe"));
    auto before = e.content();
    CHECK(!e.edit_content(std::string(1024 * 1024 + 1, 'x')));
    CHECK(e.content() == before && take(get_snippet(db, id)).content == before);
    auto external = take(get_snippet(db, id));
    external.content = "external";
    CHECK(save_snippet(db, external));
    CHECK(!e.edit_content("stale"));
    CHECK(take(get_snippet(db, id)).content == "external");
    CHECK(e.reload());
    CHECK(!e.can_undo() && e.content() == "external");
  });
  return failed ? 1 : 0;
}

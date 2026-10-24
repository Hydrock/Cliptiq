#include "cliptiq/menu.hpp"
#include "cliptiq/settings.hpp"
#include "cliptiq/snippets.hpp"
#include "sqlite3.h"
#include <chrono>
#include <filesystem>
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
void sql(Store &s, const std::string &q) {
  CHECK(sqlite3_exec(s.connection(), q.c_str(), nullptr, nullptr, nullptr) ==
        SQLITE_OK);
}
Snippet make(const FolderId &f, std::string text = "text") {
  Snippet s;
  s.folder_id = f;
  s.title = "Title";
  s.content = std::move(text);
  return s;
}
int main() {
  int fails = 0;
  auto test = [&](const char *name, auto f) {
    try {
      f();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      ++fails;
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  };
  test("folder_snippet_crud_unicode_multiline_and_entity_cas", [] {
    auto db = take(open_store(":memory:"));
    auto f = take(create_folder(db, u8"Папка 😀"));
    auto id = take(save_snippet(db, make(f, u8"  Привет\nline2\t😀  ")));
    auto item = take(get_snippet(db, id));
    CHECK(item.content == u8"  Привет\nline2\t😀  " && item.enabled);
    auto stale = item;
    item.title = "Renamed";
    CHECK(save_snippet(db, item));
    CHECK(!save_snippet(db, stale));
    auto folders = take(list_folders(db));
    CHECK(folders.size() == 1 && folders[0].title == u8"Папка 😀");
    folders[0].title = "Folder2";
    CHECK(save_folder(db, folders[0]));
    CHECK(take(list_folders(db))[0].title == "Folder2");
    CHECK(erase_snippet_item(db, {SnippetItemKind::Snippet, id}));
    CHECK(!get_snippet(db, id));
    CHECK(erase_snippet_item(db, {SnippetItemKind::Folder, f}));
    CHECK(take(list_folders(db)).empty());
  });
  test("reorder_move_normalizes_duplicates_atomically", [] {
    auto db = take(open_store(":memory:"));
    auto a = take(create_folder(db, "A")), b = take(create_folder(db, "B"));
    auto x = take(save_snippet(db, make(a, "x"))),
         y = take(save_snippet(db, make(a, "y"))),
         z = take(save_snippet(db, make(a, "z")));
    sql(db, "UPDATE snippets SET position=7");
    CHECK(move_snippet(db, z, a, 0));
    auto v = take(list_snippets(db, a));
    CHECK(v.size() == 3 && v[0].id == z && v[0].index == 0 && v[1].index == 1 &&
          v[2].index == 2);
    CHECK(move_snippet(db, y, b, 0));
    CHECK(take(list_snippets(db, a)).size() == 2 &&
          take(list_snippets(db, b))[0].id == y);
    auto rev = take(data_revision(db));
    CHECK(!move_snippet(db, x, "missing", 0));
    CHECK(take(data_revision(db)) == rev);
    CHECK(!reorder_folders(db, {a, a}));
    CHECK(reorder_folders(db, {b, a}));
    CHECK(take(list_folders(db))[0].id == b);
  });
  test("disabled_menu_and_folder_hotkeys_cleanup", [] {
    auto db = take(open_store(":memory:"));
    auto f = take(create_folder(db, "F"));
    auto id = take(save_snippet(db, make(f)));
    CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, false));
    auto m = take(build_folder_menu(db, f));
    CHECK(m.items.size() == 1 && !m.items[0].enabled);
    CHECK(!take(get_snippet(db, id)).enabled);
    CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, true));
    auto prefs = take(load_settings(db));
    prefs.hotkeys["folder:" + f] = take(parse_chord("Ctrl+Alt+Q"));
    CHECK(save_settings(db, prefs));
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, false));
    CHECK(!take(settings_folders(db)).at(f));
    CHECK(!build_folder_menu(db, f));
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, true));
    CHECK(erase_snippet_item(db, {SnippetItemKind::Folder, f}));
    CHECK(!get_snippet(db, id));
    CHECK(!take(load_settings(db)).hotkeys.count("folder:" + f));
  });
  test("limits_invalid_utf8_and_no_partial_revision", [] {
    auto db = take(open_store(":memory:"));
    auto f = take(create_folder(db, "F"));
    auto rev = take(data_revision(db));
    CHECK(!create_folder(db, std::string(1025, 'x')));
    CHECK(!save_snippet(db, make(f, std::string(1024 * 1024 + 1, 'x'))));
    CHECK(!save_snippet(db, make(f, std::string("bad\0text", 8))));
    CHECK(!save_snippet(db, make(f, std::string("\xc0\x80", 2))));
    CHECK(take(data_revision(db)) == rev && take(list_snippets(db, f)).empty());
  });
  test("restart_and_v1_migration_preserve_old_data", [] {
    auto path = std::filesystem::temp_directory_path() /
                ("cliptiq-t10-" +
                 std::to_string(std::chrono::high_resolution_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".db");
    std::string id;
    {
      auto db = take(open_store(path.u8string()));
      auto f = take(create_folder(db, "F"));
      id = take(save_snippet(db, make(f, "saved\ntext")));
      CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, false));
    }
    {
      auto db = take(open_store(path.u8string()));
      CHECK(take(get_snippet(db, id)).content == "saved\ntext" &&
            !take(get_snippet(db, id)).enabled);
      sql(db, "DROP TABLE snippet_states;DROP TABLE "
              "snippet_folder_states;PRAGMA user_version=1");
    }
    {
      auto db = take(open_store(path.u8string()));
      CHECK(take(get_snippet(db, id)).content == "saved\ntext" &&
            take(get_snippet(db, id)).enabled);
    }
    std::filesystem::remove(path);
  });
  return fails ? 1 : 0;
}

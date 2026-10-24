#include "cliptiq/menu.hpp"
#include "sqlite3.h"
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
void ok(Result<void> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
}
void sql(Store &db, const char *s) {
  CHECK(sqlite3_exec(db.connection(), s, nullptr, nullptr, nullptr) ==
        SQLITE_OK);
}
void fill(Store &s, int n) {
  for (int i = 0; i < n; ++i) {
    auto text = std::string("record ") + std::to_string(i);
    ClipboardSnapshot snap;
    snap.representations = {{0, "x11", "UTF8_STRING", CanonicalKind::Text,
                             Bytes(text.begin(), text.end())}};
    take(ingest(s, snap, {1000}, i));
  }
}
std::vector<const MenuItem *> leaves(const MenuModel &m, MenuAction a) {
  std::vector<const MenuItem *> out;
  std::function<void(const std::vector<MenuItem> &)> visit =
      [&](const auto &items) {
        for (const auto &i : items) {
          if (i.action == a)
            out.push_back(&i);
          visit(i.children);
        }
      };
  visit(m.items);
  return out;
}
int main() {
  int failed = 0;
  auto test = [&](const char *n, auto fn) {
    try {
      fn();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("empty_clear_disabled", [] {
    auto db = take(open_store(":memory:"));
    auto m = take(build_menu(db, MenuKind::Main));
    auto a = leaves(m, MenuAction::ClearHistory);
    CHECK(a.size() == 1 && !a[0]->enabled);
  });
  for (unsigned in : {0u, 1u, 100u})
    for (unsigned group : {1u, 10u, 100u})
      test(("grouping_" + std::to_string(in) + "_" + std::to_string(group))
               .c_str(),
           [=] {
             auto db = take(open_store(":memory:"));
             fill(db, 101);
             MenuSettings s;
             s.inline_count = in;
             s.folder_count = group;
             auto m = take(build_menu(db, MenuKind::History, s));
             CHECK(leaves(m, MenuAction::SelectHistory).size() == 101);
             unsigned direct = 0, folders = 0;
             for (const auto &i : m.items) {
               if (i.action == MenuAction::SelectHistory)
                 ++direct;
               if (!i.children.empty()) {
                 ++folders;
                 CHECK(i.children.size() <= group);
               }
             }
             CHECK(direct == in);
             CHECK(folders == (101 - in + group - 1) / group);
           });
  test("zero_one_numbering_and_ten_shortcuts", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 12);
    for (bool zero : {false, true}) {
      MenuSettings s;
      s.zero_start = zero;
      s.numeric_selection = true;
      auto m = take(build_menu(db, MenuKind::History, s));
      auto a = leaves(m, MenuAction::SelectHistory);
      CHECK(a[0]->number == unsigned(zero ? 0 : 1));
      CHECK(a[9]->shortcut == (zero ? "9" : "0"));
      CHECK(a[10]->shortcut.empty());
    }
  });
  test("numbering_and_shortcuts_independent", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    MenuSettings s;
    s.numbered = false;
    s.numeric_selection = true;
    auto m = take(build_menu(db, MenuKind::History, s));
    auto a = leaves(m, MenuAction::SelectHistory);
    CHECK(!a[0]->number && a[0]->shortcut == "1");
  });
  test("graphemes_first_line_trim_and_ellipsis", [] {
    CHECK(take(menu_text(
              u8"  e\u0301👩‍👩‍👧‍👦🇺🇿abcd\nsecond", 5)) ==
          u8"e\u0301👩‍👩‍👧‍👦...");
    CHECK(take(menu_text(u8"\u3000abc\u00a0", 20)) == "abc");
  });
  test("invalid_utf8_controls_are_sanitized", [] {
    auto s = take(menu_text(std::string("a\xff\0b", 4), 20, false));
    CHECK(s.find('\0') == std::string::npos &&
          s.find('\xff') == std::string::npos);
  });
  test("utf16_history_title_and_tooltip", [] {
    auto db = take(open_store(":memory:"));
    ClipboardSnapshot s;
    s.representations = {{0,
                          "windows",
                          "CF_UNICODETEXT",
                          CanonicalKind::Text,
                          {'h', 0, 'i', 0, '\n', 0, 'b', 0, 0, 0},
                          TextEncoding::Utf16Le}};
    take(ingest(db, s, {}, 1));
    MenuSettings settings;
    settings.inline_count = 1;
    auto m = take(build_menu(db, MenuKind::History, settings));
    auto a = leaves(m, MenuAction::SelectHistory);
    CHECK(a[0]->label == "hi" && a[0]->tooltip == "hi\nb");
  });
  test("snippet_order_above_below_and_kinds", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    sql(db, "INSERT INTO snippet_folders VALUES('f','Folder',0);INSERT INTO "
            "snippets VALUES('s','f','Snippet','body',0);");
    for (bool above : {false, true}) {
      MenuSettings s;
      s.inline_count = 1;
      s.snippets_above = above;
      auto m = take(build_menu(db, MenuKind::Main, s));
      CHECK(leaves(m, MenuAction::SelectSnippet).size() == 1);
      CHECK((m.items[0].label == "Folder") == above);
    }
    CHECK(leaves(take(build_menu(db, MenuKind::History)),
                 MenuAction::SelectSnippet)
              .empty());
    CHECK(leaves(take(build_menu(db, MenuKind::Snippets)),
                 MenuAction::SelectHistory)
              .empty());
  });
  test("tooltip_and_title_limits", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    MenuSettings s;
    s.title_characters = 3;
    s.tooltip_characters = 4;
    auto a =
        leaves(take(build_menu(db, MenuKind::History, s)), MenuAction::None);
    CHECK(take(menu_text("abcdef", 3)) == "...");
    CHECK(take(menu_text("abcdef", 4, false)) == "a...");
    s.folder_count = 0;
    CHECK(!build_menu(db, MenuKind::Main, s));
  });
  test("hex_swatches_and_image_reference", [] {
    auto db = take(open_store(":memory:"));
    ClipboardSnapshot s;
    s.representations = {
        {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'#', 'f', '8', '0'}}};
    take(ingest(db, s, {}, 1));
    auto m = take(build_menu(db, MenuKind::History));
    auto a = leaves(m, MenuAction::SelectHistory);
    CHECK(a[0]->preview && a[0]->preview->rgba == 0xff8800ffu);
  });
  test("revision_and_deleted_ids_reject_stale_commands", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    auto m = take(build_menu(db, MenuKind::History));
    auto a = leaves(m, MenuAction::SelectHistory);
    MenuCommand c{a[0]->action, a[0]->id, m.revision};
    ok(validate_menu_command(db, c));
    ok(erase_history(db));
    CHECK(!validate_menu_command(db, c));
    CHECK(
        !validate_menu_command(db, {MenuAction::ClearHistory, "", m.revision}));
  });
  test("build_does_not_mutate_history", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 2);
    auto revision = take(data_revision(db));
    auto rows = take(list_history(db));
    take(build_menu(db, MenuKind::Main));
    CHECK(take(data_revision(db)) == revision &&
          take(list_history(db))[0].used_ms == rows[0].used_ms);
  });
  test("clear_command_checks_revision_before_deleting", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    auto model = take(build_menu(db, MenuKind::Main));
    MenuCommand c{MenuAction::ClearHistory, "", model.revision};
    fill(db, 1);
    CHECK(!execute_menu_clear(db, c));
    CHECK(!take(list_history(db)).empty());
    c.revision = take(data_revision(db));
    ok(execute_menu_clear(db, c));
    CHECK(take(list_history(db)).empty());
  });
  test("native_model_depth_and_text_budget", [] {
    MenuModel m;
    MenuItem i;
    i.label = "valid";
    m.items.push_back(i);
    ok(validate_menu_model(m));
    MenuItem *node = &m.items[0];
    for (int n = 0; n < 8; ++n) {
      node->children.push_back(i);
      node = &node->children[0];
    }
    CHECK(!validate_menu_model(m));
    m.items.clear();
    i.tooltip = std::string(1024 * 1024, 'a');
    for (int n = 0; n < 10; ++n)
      m.items.push_back(i);
    CHECK(!validate_menu_model(m));
  });
  test("global_commands_survive_history_refresh", [] {
    auto db = take(open_store(":memory:"));
    fill(db, 1);
    MenuCommand c{MenuAction::Quit, "", 0};
    ok(validate_menu_command(db, c));
  });
  test("native_model_rejects_invalid_utf8_and_nul", [] {
    MenuModel m;
    MenuItem i;
    i.label = std::string(1, char(0xff));
    m.items.push_back(i);
    CHECK(!validate_menu_model(m));
    m.items[0].label = "ok";
    m.items[0].tooltip = std::string("a\0b", 3);
    CHECK(!validate_menu_model(m));
  });
  test("settings_history_limit_applies_without_erasing_store", [] {
    auto store = take(open_store(":memory:"));
    fill(store, 20);
    MenuSettings settings;
    settings.history_limit = 3;
    auto menu = take(build_menu(store, MenuKind::History, settings));
    CHECK(leaves(menu, MenuAction::SelectHistory).size() == 3);
    CHECK(take(list_history(store, SortOrder::LastUsed, 1000)).size() == 20);
  });
  return failed ? 1 : 0;
}

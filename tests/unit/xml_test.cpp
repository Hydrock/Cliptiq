#include "cliptiq/xml.hpp"
#include "sqlite3.h"
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
Bytes bytes(const std::string &s) { return Bytes(s.begin(), s.end()); }
int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--roundtrip") {
    auto wire = read_snippet_xml(argv[2]);
    if (!wire) {
      std::cerr << wire.error().message;
      return 1;
    }
    auto decoded = decode_clipy_xml(wire.value());
    if (!decoded) {
      std::cerr << decoded.error().message;
      return 1;
    }
    auto db = open_store(":memory:");
    if (!db)
      return 1;
    auto imported = append_import(db.value(), decoded.value());
    if (!imported)
      return 1;
    auto encoded = encode_clipy_xml(db.value());
    if (!encoded)
      return 1;
    return write_snippet_xml_atomic(argv[3], encoded.value()) ? 0 : 1;
  }
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
  test("roundtrip_whitespace_unicode_disabled_entity_escape", [] {
    auto db = take(open_store(":memory:"));
    auto f = take(create_folder(db, u8"  Папка & < > \" ' 😀  "));
    Snippet s;
    s.folder_id = f;
    s.title = "A & B";
    s.content = u8"  \tПривет\n😀\rfinal  ";
    auto id = take(save_snippet(db, s));
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, false));
    CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, false));
    auto wire = take(encode_clipy_xml(db));
    std::string text(wire.begin(), wire.end());
    CHECK(text.find("&amp;") != std::string::npos);
    CHECK(text.find("enabled") == std::string::npos);
    CHECK(text.find(id) == std::string::npos);
    auto decoded = take(decode_clipy_xml(wire));
    CHECK(decoded.size() == 1 &&
          decoded[0].title == u8"  Папка & < > \" ' 😀  ");
    CHECK(decoded[0].snippets[0].content == s.content);
  });
  test("fallback_titles_cdata_and_empty_folders", [] {
    auto f = take(decode_clipy_xml(
        bytes("<?xml version=\"1.0\" "
              "encoding=\"UTF-8\"?><folders><folder><snippets><snippet><"
              "content>  <![CDATA[<&]]><!--join-->&#x1F600;&#13; "
              "</content></snippet><snippet><title/></snippet></snippets></"
              "folder><folder><title> </title></folder></folders>")));
    CHECK(f.size() == 2 && f[0].title == "untitled folder" &&
          f[1].title == " ");
    CHECK(f[0].snippets[0].title == "untitled snippet");
    CHECK(f[0].snippets[0].content == u8"  <&😀\r ");
    CHECK(f[0].snippets[1].content.empty());
  });
  test("append_not_replace_and_atomic_rollback", [] {
    auto db = take(open_store(":memory:"));
    auto old = take(create_folder(db, "old"));
    auto rev = take(data_revision(db));
    auto doc = take(decode_clipy_xml(bytes(
        "<folders><folder><title>new</title><snippets><snippet><title>a</"
        "title><content>b</content></snippet></snippets></folder></folders>")));
    auto sum = take(append_import(db, doc));
    CHECK(sum.folders == 1 && sum.snippets == 1);
    auto fs = take(list_folders(db));
    CHECK(fs.size() == 2 && fs[0].id == old && fs[1].enabled);
    CHECK(take(data_revision(db)) == rev + 1);
    rev = take(data_revision(db));
    CHECK(sqlite3_exec(db.connection(),
                       "CREATE TRIGGER fail_xml BEFORE INSERT ON snippets "
                       "BEGIN SELECT RAISE(ABORT,'fixture failure'); END",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(!append_import(db, doc));
    CHECK(take(list_folders(db)).size() == 2 && take(data_revision(db)) == rev);
  });
  test("malformed_is_atomic_external_entity_rejected", [] {
    for (auto xml :
         {"<!DOCTYPE folders SYSTEM 'file:///etc/passwd'><folders/>",
          "<!DOCTYPE folders [<!ENTITY x 'expanded'>]><folders/>",
          "<folders><folder></folders>", "<folders/><folders/>",
          "<folders><folder><title>&unknown;</title></folder></folders>",
          "<folders><folder><title>&#0;</title></folder></folders>",
          "<folders><folder><title>&#xD800;</title></folder></folders>",
          "<folders><folder><title>a</title><title>b</title></folder></"
          "folders>",
          "<folders><unexpected/></folders>", "<folders attr='x'/>",
          "<folders><folder><title><x/></title></folder></folders>",
          "<?xml version='1.0' encoding='UTF-16'?><folders/>"})
      CHECK(!decode_clipy_xml(bytes(xml)));
    CHECK(!decode_clipy_xml(
        Bytes{'<', 'f', 'o', 'l', 'd', 'e', 'r', 's', '/', '>', 0}));
  });
  test("size_count_utf8_and_store_limits", [] {
    ImportLimits l;
    l.file_bytes = 4;
    CHECK(!decode_clipy_xml(bytes("<folders/>"), l));
    l = {};
    l.folders = 1;
    CHECK(!decode_clipy_xml(bytes("<folders><folder/><folder/></folders>"), l));
    l = {};
    l.snippets = 0;
    CHECK(!decode_clipy_xml(bytes("<folders><folder><snippets><snippet/></"
                                  "snippets></folder></folders>"),
                            l));
    l = {};
    l.title_bytes = 1;
    CHECK(!decode_clipy_xml(
        bytes("<folders><folder><title>ab</title></folder></folders>"), l));
    CHECK(!decode_clipy_xml(
        bytes(std::string("<folders>") + char(0xff) + "</folders>")));
    auto db = take(open_store(":memory:"));
    auto rev = take(data_revision(db));
    ImportedFolder f;
    f.title = "valid";
    f.snippets.push_back({"bad", std::string(1024 * 1024 + 1, 'x')});
    CHECK(!append_import(db, {f}));
    CHECK(take(list_folders(db)).empty() && take(data_revision(db)) == rev);
  });
  test("atomic_destination_and_bounded_file_read", [] {
    auto p = std::filesystem::temp_directory_path() / "cliptiq-xml-unit.xml";
    auto path = p.u8string();
    CHECK(write_snippet_xml_atomic(path, bytes("old")));
    CHECK(write_snippet_xml_atomic(path, bytes("<folders/>")));
    CHECK(take(read_snippet_xml(path)) == bytes("<folders/>"));
    CHECK(!read_snippet_xml(path, 2));
    CHECK(!write_snippet_xml_atomic((p / "missing.xml").u8string(),
                                    bytes("new")));
    CHECK(take(read_snippet_xml(path)) == bytes("<folders/>"));
    std::filesystem::remove(p);
  });
  test("xml_line_ending_normalization_and_nested_transaction_preservation", [] {
    auto f = take(decode_clipy_xml(
        bytes("<folders><folder><snippets><snippet><content>A\n\rB\r\nC\rD</"
              "content></snippet></snippets></folder></folders>")));
    CHECK(f[0].snippets[0].content == "A\n\nB\nC\nD");
    auto db = take(open_store(":memory:"));
    CHECK(sqlite3_exec(db.connection(), "BEGIN", nullptr, nullptr, nullptr) ==
          SQLITE_OK);
    CHECK(!encode_clipy_xml(db));
    CHECK(sqlite3_get_autocommit(db.connection()) == 0);
    CHECK(sqlite3_exec(db.connection(), "ROLLBACK", nullptr, nullptr,
                       nullptr) == SQLITE_OK);
  });
  test("depth_node_budgets_and_no_partial_import_on_existing_count_limit", [] {
    std::string deep;
    for (unsigned i = 0; i < 1000; ++i)
      deep += "<a>";
    for (unsigned i = 0; i < 1000; ++i)
      deep += "</a>";
    auto depth = decode_clipy_xml(bytes(deep));
    CHECK(!depth && depth.error().code == ErrorCode::TooLarge);
    std::string nodes = "<folders>";
    for (unsigned i = 0; i < 75000; ++i)
      nodes += "<!---->";
    nodes += "</folders>";
    auto budget = decode_clipy_xml(bytes(nodes));
    CHECK(!budget && budget.error().code == ErrorCode::TooLarge);
    auto db = take(open_store(":memory:"));
    CHECK(sqlite3_exec(db.connection(),
                       "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 "
                       "FROM n WHERE x<1000) INSERT INTO snippet_folders "
                       "SELECT 'folder-'||x,'old',x-1 FROM n",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
    auto rev = take(data_revision(db));
    auto imported = append_import(db, {{"new", {}}});
    CHECK(!imported && imported.error().code == ErrorCode::TooLarge);
    CHECK(take(data_revision(db)) == rev &&
          take(list_folders(db)).size() == 1000);
  });
  return failed ? 1 : 0;
}

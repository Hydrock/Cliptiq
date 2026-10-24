#include "../../core/src/db.hpp"
#include "Clipboard.hpp"
#include "cliptiq/history.hpp"
#include "cliptiq/migration.hpp"
#include "cliptiq/paste.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#ifdef __APPLE__
#import <AppKit/AppKit.h>
#elif defined(_WIN32)
#include "../fixtures/windows/IsolatedClipboard.hpp"
#endif
using namespace cliptiq;
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
struct Paste : PasteBackend {
  Result<void> dispatch(const TargetToken &, Shortcut) override { return {}; }
};
int run() {
  try {
#ifdef _WIN32
    IsolatedClipboard isolation;
    windows::WindowsClipboardBackend backend;
#elif defined(__APPLE__)
    auto board = [NSPasteboard pasteboardWithUniqueName];
    macos::MacClipboardBackend backend(board);
#else
    auto owned = linux_backend::open_backend();
    auto &backend = *owned;
#endif
    auto root =
        std::filesystem::temp_directory_path() /
        ("cliptiq-replay-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    auto source = root / "source.sqlite3", neutral = root / "neutral.sqlite3";
    sqlite3 *db = nullptr;
    detail::check(nullptr, sqlite3_open(source.u8string().c_str(), &db));
    detail::exec(
        db,
        R"SQL(CREATE TABLE grdb_migrations(identifier TEXT);INSERT INTO grdb_migrations VALUES('Create initial tables');
 CREATE TABLE pasteboardHistories(id TEXT PRIMARY KEY,title TEXT,pasteboardTypes TEXT,deviceID TEXT,updateAt INTEGER);
 CREATE TABLE pasteboardHistoryAssets(id TEXT PRIMARY KEY,pasteboardHistoryID TEXT,"index" INTEGER,pasteboardType TEXT,data BLOB);
 CREATE TABLE pasteboardHistoryThumbnailAssets(pasteboardHistoryID TEXT PRIMARY KEY,kind TEXT,data BLOB);
 CREATE TABLE snippetFolders(id TEXT PRIMARY KEY,title TEXT,"index" INTEGER,isEnabled INTEGER);
 CREATE TABLE snippets(id TEXT PRIMARY KEY,folderID TEXT,title TEXT,content TEXT,"index" INTEGER,isEnabled INTEGER);
 INSERT INTO pasteboardHistories VALUES('unicode','Unicode','[]',NULL,1);INSERT INTO pasteboardHistories VALUES('files','Files','[]',NULL,1);
 CREATE TABLE migration_settings(key TEXT PRIMARY KEY,value BLOB,apply_value TEXT);
 INSERT INTO migration_settings VALUES('legacy.archive.unicode',X'736563726574',NULL);
 )SQL");
    auto part = [&](const char *id, const char *entry, int index,
                    const char *type, const std::string &data) {
      detail::Statement q(
          db, "INSERT INTO pasteboardHistoryAssets VALUES(?,?,?,?,?)");
      q.bind(1, std::string(id));
      q.bind(2, std::string(entry));
      q.bind(3, std::int64_t(index));
      q.bind(4, std::string(type));
      q.bind(5, Bytes(data.begin(), data.end()));
      q.done();
    };
    part("a", "unicode", 0, "public.utf8-plain-text", u8"Привет 🌍");
    part("rtf", "unicode", 1, "public.rtf", "{\\rtf1 Native import}");
    part("html", "unicode", 2, "public.html", u8"<b>Привет 🌍</b>");
    part("f1", "files", 0, "public.file-url", "file:///tmp/a");
    part("f2", "files", 1, "public.file-url", "file:///tmp/b");
    part("t1", "files", 2, "public.utf8-plain-text", "a");
    part("t2", "files", 3, "public.utf8-plain-text", "b");
    part("root", "files", 4, "NSFilenamesPboardType",
         "<?xml version=\"1.0\"?><plist "
         "version=\"1.0\"><array><string>/tmp/a</string><string>/tmp/b</"
         "string></array></plist>");
    sqlite3_close(db);
    auto exported = export_clipy_sqlite(source.u8string(), neutral.u8string());
    if (!exported)
      throw std::runtime_error(exported.error().message);
    auto store = take(open_store(":memory:"));
    take(import_clipy(store, neutral.u8string()));
    Paste paste;
    SelectionIntent intent;
    intent.auto_paste = false;
    intent.command = {
        MenuAction::SelectHistory, "unicode", take(data_revision(store)), {}};
    auto result = take(execute_selection(store, intent, backend, paste));
    if (result.status != SelectionStatus::Copied)
      throw std::runtime_error("Imported Unicode not copied");
    auto files = take(load_snapshot(store, "files"));
    if (files.representations.size() != 5 ||
        files.representations[0].item_index != 0 ||
        files.representations[1].item_index != 1 ||
        files.representations[2].item_index != 0 ||
        files.representations[3].item_index != 1 ||
        files.representations[4].item_index != RootPasteboardItem)
      throw std::runtime_error("Clipy type occurrence grouping lost");
    auto read = take(backend.read_snapshot());
    if (read.representations.empty())
      throw std::runtime_error(
          "Native clipboard empty after imported Unicode copy");
#ifdef __APPLE__
    intent.command = {
        MenuAction::SelectHistory, "files", take(data_revision(store)), {}};
    take(execute_selection(store, intent, backend, paste));
    if (board.pasteboardItems.count != 2)
      throw std::runtime_error("Multi-file item boundaries lost");
#endif
    // Exercise the actual paste-delete path and foreign-key provenance cleanup.
    intent.command = {MenuAction::SelectHistory,
                      "unicode",
                      take(data_revision(store)),
                      {"fixture-target"}};
    intent.auto_paste = true;
    intent.action = SelectionAction::PasteDelete;
    auto removed = take(execute_selection(store, intent, backend, paste));
    if (!removed.history_deleted)
      throw std::runtime_error("Paste-delete failed");
    detail::Statement count(store.connection(),
                            "SELECT count(*) FROM migration_originals WHERE "
                            "id='unicode' OR id='legacy.archive.unicode'");
    count.row();
    if (count.number(0))
      throw std::runtime_error("Paste-delete retained clipboard provenance");
    std::filesystem::remove_all(root);
    std::cout << "PASS imported Unicode through native clipboard, macOS "
                 "repeated types, paste-delete provenance cleanup\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
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

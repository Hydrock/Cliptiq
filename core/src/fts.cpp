#include "cliptiq/index.hpp"
#include "cliptiq/menu.hpp"
#include "cliptiq/ocr.hpp"
#include "index_gate.hpp"
#include <memory>
namespace cliptiq {
namespace {
struct ReadSnapshot {
  sqlite3 *db;
  bool active = true;
  explicit ReadSnapshot(sqlite3 *p) : db(p) { detail::exec(db, "BEGIN"); }
  void finish() {
    detail::exec(db, "COMMIT");
    active = false;
  }
  ~ReadSnapshot() {
    if (active)
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
  }
};
using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)>;
Connection open_index(const std::filesystem::path &p, bool create) {
  sqlite3 *raw = nullptr;
  auto rc = sqlite3_open_v2(
      p.u8string().c_str(), &raw,
      create
          ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOFOLLOW)
          : (SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW),
      nullptr);
  Connection result(raw, sqlite3_close_v2);
  detail::check(raw, rc);
  sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, 2 * 1024 * 1024);
  detail::exec(raw, "PRAGMA trusted_schema=OFF");
  return result;
}
std::string owner(Store &s) {
  return std::filesystem::u8path(sqlite3_db_filename(s.connection(), "main"))
      .filename()
      .u8string();
}
void location(Store &s, const std::string &path) {
  auto actual = std::filesystem::absolute(std::filesystem::u8path(path))
                    .lexically_normal();
  auto expected =
      std::filesystem::u8path(detail::companion_path(s.connection()));
  if (actual.filename() != "search.db" ||
      actual.parent_path().filename() != "index" ||
      !std::filesystem::equivalent(actual.parent_path().parent_path(),
                                   expected.parent_path().parent_path()))
    detail::invalid("Index does not belong to this profile");
}
} // namespace
Result<void> rebuild_index(Store &s, const std::string &path,
                           std::uint64_t revision) {
  return detail::guarded<void>([&]() -> Result<void> {
    location(s, path);
    detail::IndexGate gate(path, owner(s), true);
    gate.clear();
    ReadSnapshot snapshot(s.connection());
    auto current = data_revision(s);
    if (!current)
      return current.error();
    if (current.value() != revision)
      return Error{ErrorCode::Busy, "Main data changed before index snapshot"};
    auto temporary = gate.file().parent_path() / "build.db";
    {
      auto db = open_index(temporary, true);
      detail::exec(db.get(),
                   "PRAGMA journal_mode=DELETE; PRAGMA max_page_count=32768; "
                   "CREATE TABLE metadata(revision INTEGER NOT NULL); CREATE "
                   "VIRTUAL TABLE search USING fts5(id UNINDEXED,snippet "
                   "UNINDEXED,title,body,tokenize='unicode61');");
      detail::Transaction tx(db.get());
      detail::Statement meta(db.get(), "INSERT INTO metadata VALUES(?)");
      meta.bind(1, static_cast<std::int64_t>(revision));
      meta.done();
      std::size_t bytes = 0, rows = 0;
      auto add = [&](std::string id, bool snippet, std::string title,
                     std::string body) {
        bytes += id.size() + title.size() + body.size();
        if (bytes > 64 * 1024 * 1024 || ++rows > 110000)
          throw detail::Failure{
              {ErrorCode::TooLarge, "Companion index budget exceeded"}};
        detail::Statement insert(
            db.get(),
            "INSERT INTO search(id,snippet,title,body) VALUES(?,?,?,?)");
        insert.bind(1, id);
        insert.bind(2, std::int64_t(snippet));
        insert.bind(3, title);
        insert.bind(4, body);
        insert.done();
      };
      detail::Statement history(
          s.connection(),
          "SELECT id,coalesce(ocr_text,'') FROM history LIMIT 100001");
      std::size_t history_rows = 0;
      while (history.row()) {
        if (++history_rows > 100000)
          throw detail::Failure{
              {ErrorCode::TooLarge, "History index row limit exceeded"}};
        detail::Statement representation(
            s.connection(),
            "SELECT substr(data,1,65536),text_encoding FROM representations "
            "WHERE entry_id=? AND kind=0 ORDER BY ordinal LIMIT 1");
        representation.bind(1, history.text(0));
        std::string title;
        if (representation.row()) {
          auto decoded = index_text(representation.bytes(0),
                                    TextEncoding(representation.number(1)));
          if (!decoded)
            return decoded.error();
          title = std::move(decoded.value());
        }
        auto ocr = truncate_ocr_text(history.text(1));
        if (!ocr)
          return ocr.error();
        add(history.text(0), false, title, ocr.value());
      }
      detail::Statement snippets(
          s.connection(),
          "SELECT s.id,s.title,s.content FROM snippets s LEFT JOIN "
          "snippet_states st ON st.id=s.id LEFT JOIN snippet_folder_states fs "
          "ON fs.id=s.folder_id WHERE coalesce(st.enabled,1)=1 AND "
          "coalesce(fs.enabled,1)=1 LIMIT 10001");
      std::size_t snippet_rows = 0;
      while (snippets.row()) {
        if (++snippet_rows > 10000)
          throw detail::Failure{
              {ErrorCode::TooLarge, "Snippet index row limit exceeded"}};
        add(snippets.text(0), true, snippets.text(1), snippets.text(2));
      }
      detail::exec(db.get(),
                   "INSERT INTO search(search) VALUES('integrity-check')");
      tx.commit();
    }
    snapshot.finish();
    current = data_revision(s);
    if (!current)
      return current.error();
    if (current.value() != revision)
      return Error{ErrorCode::Busy, "Main data changed during index rebuild"};
    std::filesystem::rename(temporary, gate.file());
    return {};
  });
}
Result<std::vector<IndexHit>> search_index(Store &s, const std::string &path,
                                           const std::string &phrase,
                                           std::size_t limit) {
  return detail::guarded<std::vector<IndexHit>>(
      [&]() -> Result<std::vector<IndexHit>> {
        if (phrase.empty() || phrase.size() > 1024 ||
            phrase.find('\0') != std::string::npos || !limit || limit > 1000)
          detail::invalid("Invalid bounded search request");
        location(s, path);
        detail::IndexGate gate(path, owner(s));
        auto db = open_index(gate.file(), false);
        auto revision = data_revision(s);
        if (!revision)
          return revision.error();
        detail::Statement meta(db.get(), "SELECT revision FROM metadata");
        if (!meta.row() ||
            meta.number(0) != static_cast<std::int64_t>(revision.value()))
          return Error{ErrorCode::Busy, "Stale companion index"};
        std::string literal = "\"";
        for (char c : phrase) {
          literal += c;
          if (c == '\"')
            literal += '\"';
        }
        literal += '\"';
        detail::Statement query(
            db.get(),
            "SELECT id,snippet FROM search WHERE search MATCH ? LIMIT ?");
        query.bind(1, literal);
        query.bind(2, static_cast<std::int64_t>(limit));
        std::vector<IndexHit> result;
        while (query.row())
          result.push_back({query.text(0), query.number(1) != 0});
        return result;
      });
}
} // namespace cliptiq

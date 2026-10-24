#include "cliptiq/snippets.hpp"
#include "db.hpp"
#include <algorithm>
#include <random>
#include <set>
namespace cliptiq {
namespace {
using namespace detail;
constexpr std::uint64_t text_limit = 1024 * 1024,
                        total_limit = 16 * 1024 * 1024;
void utf8(const std::string &s, std::size_t limit) {
  if (s.size() > limit)
    throw Failure{{ErrorCode::TooLarge, "Snippet text/title exceeds limit"}};
  for (std::size_t i = 0; i < s.size();) {
    unsigned c = static_cast<unsigned char>(s[i++]), more = 0, min = 0;
    if (!c)
      invalid("NUL in snippet text");
    if (c < 128)
      continue;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      more = 1;
      min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      more = 2;
      min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      more = 3;
      min = 65536;
    } else
      invalid("Invalid snippet UTF-8");
    if (i + more > s.size())
      invalid("Incomplete snippet UTF-8");
    for (unsigned n = 0; n < more; ++n) {
      auto b = static_cast<unsigned char>(s[i++]);
      if ((b & 192) != 128)
        invalid("Invalid snippet UTF-8");
      c = (c << 6) | (b & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      invalid("Invalid snippet Unicode scalar");
  }
}
void id_valid(const std::string &id) {
  if (id.empty())
    invalid("Empty snippet item ID");
  utf8(id, 1024);
}
std::string uuid() {
  std::random_device random;
  unsigned char bytes[16];
  for (auto &b : bytes)
    b = static_cast<unsigned char>(random());
  bytes[6] = (bytes[6] & 15) | 64;
  bytes[8] = (bytes[8] & 63) | 128;
  std::string out;
  const char *h = "0123456789abcdef";
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      out += '-';
    out += h[bytes[i] >> 4];
    out += h[bytes[i] & 15];
  }
  return out;
}
void require_folder(sqlite3 *db, const FolderId &id) {
  id_valid(id);
  Statement q(db, "SELECT 1 FROM snippet_folders WHERE id=?");
  q.bind(1, id);
  if (!q.row())
    throw Failure{{ErrorCode::NotFound, "Folder disappeared"}};
}
std::vector<std::string> ids(sqlite3 *db, const FolderId *folder = nullptr) {
  Statement q(db, folder ? "SELECT id FROM snippets WHERE folder_id=? ORDER BY "
                           "position,rowid LIMIT 10001"
                         : "SELECT id FROM snippet_folders ORDER BY "
                           "position,rowid LIMIT 1001");
  if (folder)
    q.bind(1, *folder);
  std::vector<std::string> out;
  while (q.row()) {
    if (out.size() >= (folder ? 10000u : 1000u))
      throw Failure{{ErrorCode::TooLarge, "Too many snippet items"}};
    out.push_back(q.text(0));
  }
  return out;
}
void state(sqlite3 *db, bool folder, const std::string &id, bool enabled) {
  Statement q(
      db,
      folder
          ? "INSERT INTO snippet_folder_states(id,enabled,revision) "
            "VALUES(?,?,1) ON CONFLICT(id) DO UPDATE SET "
            "enabled=excluded.enabled,revision=snippet_folder_states.revision+1"
          : "INSERT INTO snippet_states(id,enabled,revision) VALUES(?,?,1) ON "
            "CONFLICT(id) DO UPDATE SET "
            "enabled=excluded.enabled,revision=snippet_states.revision+1");
  q.bind(1, id);
  q.bind(2, std::int64_t(enabled));
  q.done();
}
void reindex(sqlite3 *db, bool folder, const std::vector<std::string> &order) {
  for (std::size_t i = 0; i < order.size(); ++i) {
    Statement q(
        db,
        folder
            ? "UPDATE snippet_folders SET position=? WHERE id=? AND position<>?"
            : "UPDATE snippets SET position=? WHERE id=? AND position<>?");
    q.bind(1, std::int64_t(i));
    q.bind(2, order[i]);
    q.bind(3, std::int64_t(i));
    q.done();
    if (sqlite3_changes(db)) {
      Statement bump(
          db, folder ? "INSERT INTO snippet_folder_states(id,enabled,revision) "
                       "VALUES(?,1,1) ON CONFLICT(id) DO UPDATE SET "
                       "revision=snippet_folder_states.revision+1"
                     : "INSERT INTO snippet_states(id,enabled,revision) "
                       "VALUES(?,1,1) ON CONFLICT(id) DO UPDATE SET "
                       "revision=snippet_states.revision+1");
      bump.bind(1, order[i]);
      bump.done();
    }
  }
}
void place(std::vector<std::string> &order, const std::string &id,
           std::size_t index) {
  order.erase(std::remove(order.begin(), order.end(), id), order.end());
  if (index == snippet_append)
    index = order.size();
  if (index > order.size())
    invalid("Snippet insertion index out of range");
  order.insert(order.begin() + std::ptrdiff_t(index), id);
}
std::uint64_t version(sqlite3 *db, bool folder, const std::string &id) {
  Statement q(
      db, folder ? "SELECT coalesce(s.revision,0) FROM snippet_folders f LEFT "
                   "JOIN snippet_folder_states s ON s.id=f.id WHERE f.id=?"
                 : "SELECT coalesce(v.revision,0) FROM snippets s LEFT JOIN "
                   "snippet_states v ON v.id=s.id WHERE s.id=?");
  q.bind(1, id);
  if (!q.row())
    throw Failure{{ErrorCode::NotFound, "Snippet item disappeared"}};
  return std::uint64_t(q.number(0));
}
void check_version(sqlite3 *db, bool folder, const std::string &id,
                   std::uint64_t revision) {
  if (version(db, folder, id) != revision)
    throw Failure{{ErrorCode::Busy, "Item changed; reload before editing"}};
}
SnippetInfo header(Statement &q) {
  SnippetInfo s;
  s.id = q.text(0);
  s.folder_id = q.text(1);
  s.title = q.text(2);
  auto position = q.number(3);
  if (position < 0)
    throw Failure{{ErrorCode::Corrupt, "Negative snippet position"}};
  s.index = std::size_t(position);
  s.enabled = q.number(4) != 0;
  s.revision = std::uint64_t(q.number(5));
  utf8(s.title, 1024);
  id_valid(s.id);
  return s;
}
} // namespace
Result<std::vector<Folder>> list_folders(Store &store) {
  return detail::guarded<std::vector<Folder>>(
      [&]() -> Result<std::vector<Folder>> {
        Statement q(store.connection(),
                    "SELECT "
                    "f.id,f.title,f.position,coalesce(s.enabled,1),coalesce(s."
                    "revision,0),length(CAST(f.title AS BLOB)) FROM "
                    "snippet_folders f LEFT JOIN snippet_folder_states s ON "
                    "s.id=f.id ORDER BY f.position,f.rowid LIMIT 1001");
        std::vector<Folder> out;
        while (q.row()) {
          if (out.size() >= 1000 || q.number(5) > 1024)
            throw Failure{
                {ErrorCode::TooLarge, "Folder catalog/title exceeds limit"}};
          Folder f;
          f.id = q.text(0);
          f.title = q.text(1);
          if (q.number(2) < 0)
            throw Failure{{ErrorCode::Corrupt, "Negative folder position"}};
          f.index = std::size_t(q.number(2));
          f.enabled = q.number(3) != 0;
          f.revision = std::uint64_t(q.number(4));
          utf8(f.title, 1024);
          id_valid(f.id);
          out.push_back(std::move(f));
        }
        return out;
      });
}
Result<std::vector<SnippetInfo>> list_snippet_headers(Store &store) {
  return detail::guarded<std::vector<SnippetInfo>>(
      [&]() -> Result<std::vector<SnippetInfo>> {
        Statement q(store.connection(),
                    "SELECT "
                    "s.id,s.folder_id,s.title,s.position,coalesce(v.enabled,1),"
                    "coalesce(v.revision,0),length(CAST(s.title AS BLOB)) FROM "
                    "snippets s LEFT JOIN snippet_states v ON v.id=s.id ORDER "
                    "BY s.folder_id,s.position,s.rowid LIMIT 10001");
        std::vector<SnippetInfo> out;
        while (q.row()) {
          if (out.size() >= 10000 || q.number(6) > 1024)
            throw Failure{
                {ErrorCode::TooLarge, "Snippet catalog/title exceeds limit"}};
          out.push_back(header(q));
        }
        return out;
      });
}
Result<Snippet> get_snippet(Store &store, const SnippetId &id) {
  return detail::guarded<Snippet>([&]() -> Result<Snippet> {
    id_valid(id);
    Statement q(store.connection(),
                "SELECT "
                "s.id,s.folder_id,s.title,s.position,coalesce(v.enabled,1),"
                "coalesce(v.revision,0),length(CAST(s.content AS "
                "BLOB)),s.content,length(CAST(s.title AS BLOB)) FROM snippets "
                "s LEFT JOIN snippet_states v ON v.id=s.id WHERE s.id=?");
    q.bind(1, id);
    if (!q.row())
      return Error{ErrorCode::NotFound, "Snippet disappeared"};
    if (q.number(6) > std::int64_t(std::min(
                          text_limit, store.limits().representation_bytes)) ||
        q.number(8) > 1024)
      return Error{
          ErrorCode::TooLarge,
          "Snippet text/title exceeds editor limit; original preserved"};
    Snippet s;
    static_cast<SnippetInfo &>(s) = header(q);
    s.content = q.text(7);
    utf8(s.content, std::size_t(text_limit));
    return s;
  });
}
Result<std::vector<Snippet>> list_snippets(Store &store,
                                           const FolderId &folder) {
  return detail::guarded<std::vector<Snippet>>(
      [&]() -> Result<std::vector<Snippet>> {
        require_folder(store.connection(), folder);
        auto list = list_snippet_headers(store);
        if (!list)
          return list.error();
        std::vector<Snippet> out;
        std::size_t total = 0;
        for (auto &h : list.value())
          if (h.folder_id == folder) {
            auto s = get_snippet(store, h.id);
            if (!s)
              return s.error();
            total += s.value().content.size();
            if (total > total_limit)
              return Error{ErrorCode::TooLarge,
                           "Snippet collection exceeds editor budget"};
            out.push_back(std::move(s.value()));
          }
        return out;
      });
}
Result<FolderId> create_folder(Store &store, const std::string &title) {
  return detail::guarded<FolderId>([&]() -> Result<FolderId> {
    utf8(title, 1024);
    auto db = store.connection();
    Transaction tx(db);
    auto order = ids(db);
    if (order.size() >= 1000)
      return Error{ErrorCode::TooLarge, "Folder count limit"};
    auto id = uuid();
    Statement q(db,
                "INSERT INTO snippet_folders(id,title,position) VALUES(?,?,?)");
    q.bind(1, id);
    q.bind(2, title);
    q.bind(3, std::int64_t(order.size()));
    q.done();
    revision(db);
    tx.commit();
    return id;
  });
}
Result<void> save_folder(Store &store, const Folder &folder) {
  return detail::guarded<void>([&]() -> Result<void> {
    id_valid(folder.id);
    utf8(folder.title, 1024);
    auto db = store.connection();
    Transaction tx(db);
    check_version(db, true, folder.id, folder.revision);
    auto order = ids(db);
    place(order, folder.id, folder.index);
    Statement q(db, "UPDATE snippet_folders SET title=? WHERE id=?");
    q.bind(1, folder.title);
    q.bind(2, folder.id);
    q.done();
    state(db, true, folder.id, folder.enabled);
    reindex(db, true, order);
    revision(db);
    tx.commit();
    return {};
  });
}
Result<SnippetId> save_snippet(Store &store, const Snippet &s) {
  return detail::guarded<SnippetId>([&]() -> Result<SnippetId> {
    utf8(s.title, 1024);
    utf8(s.content, std::size_t(std::min(text_limit,
                                         store.limits().representation_bytes)));
    auto db = store.connection();
    Transaction tx(db);
    require_folder(db, s.folder_id);
    std::string id = s.id.empty() ? uuid() : s.id;
    id_valid(id);
    Statement budget(db, "SELECT count(*),coalesce(sum(length(CAST(content AS "
                         "BLOB))),0) FROM snippets WHERE id<>?");
    budget.bind(1, id);
    budget.row();
    if (budget.number(0) >= 10000 ||
        std::uint64_t(budget.number(1)) + s.content.size() > total_limit)
      return Error{ErrorCode::TooLarge,
                   "Snippet collection exceeds count/text budget"};
    if (!s.id.empty())
      check_version(db, false, id, s.revision);
    auto destination = ids(db, &s.folder_id);
    FolderId previous;
    if (!s.id.empty()) {
      Statement old(db, "SELECT folder_id FROM snippets WHERE id=?");
      old.bind(1, id);
      old.row();
      previous = old.text(0);
    }
    place(destination, id, s.index);
    Statement q(
        db, s.id.empty()
                ? "INSERT INTO snippets(id,folder_id,title,content,position) "
                  "VALUES(?,?,?,?,?)"
                : "UPDATE snippets SET "
                  "folder_id=?,title=?,content=?,position=? WHERE id=?");
    if (s.id.empty()) {
      q.bind(1, id);
      q.bind(2, s.folder_id);
      q.bind(3, s.title);
      q.bind(4, s.content);
      q.bind(5, std::int64_t(destination.size() - 1));
    } else {
      q.bind(1, s.folder_id);
      q.bind(2, s.title);
      q.bind(3, s.content);
      q.bind(4, std::int64_t(destination.size() - 1));
      q.bind(5, id);
    }
    q.done();
    state(db, false, id, s.enabled);
    reindex(db, false, destination);
    if (!previous.empty() && previous != s.folder_id)
      reindex(db, false, ids(db, &previous));
    revision(db);
    tx.commit();
    return id;
  });
}
Result<void> move_snippet(Store &store, const SnippetId &id,
                          const FolderId &folder, std::size_t index,
                          std::optional<std::uint64_t> expected_revision) {
  return detail::guarded<void>([&]() -> Result<void> {
    id_valid(id);
    auto db = store.connection();
    Transaction tx(db);
    require_folder(db, folder);
    if (expected_revision)
      check_version(db, false, id, *expected_revision);
    Statement old(db, "SELECT folder_id FROM snippets WHERE id=?");
    old.bind(1, id);
    if (!old.row())
      return Error{ErrorCode::NotFound, "Snippet disappeared"};
    auto previous = old.text(0);
    auto order = ids(db, &folder);
    place(order, id, index);
    Statement q(db, "UPDATE snippets SET folder_id=? WHERE id=?");
    q.bind(1, folder);
    q.bind(2, id);
    q.done();
    reindex(db, false, order);
    if (previous != folder && !previous.empty())
      reindex(db, false, ids(db, &previous));
    Statement bump(
        db, "INSERT INTO snippet_states(id,enabled,revision) VALUES(?,1,1) ON "
            "CONFLICT(id) DO UPDATE SET revision=snippet_states.revision+1");
    bump.bind(1, id);
    bump.done();
    revision(db);
    tx.commit();
    return {};
  });
}
Result<void> reorder_folders(Store &store, const std::vector<FolderId> &order,
                             std::optional<SnippetItemVersion> expected) {
  return detail::guarded<void>([&]() -> Result<void> {
    auto db = store.connection();
    Transaction tx(db);
    if (expected) {
      if (expected->data_revision) {
        auto current_revision = data_revision(store);
        if (!current_revision)
          return current_revision.error();
        if (current_revision.value() != *expected->data_revision)
          return Error{ErrorCode::Busy,
                       "Data changed during folder reorder; retry"};
      }
      if (expected->item.kind != SnippetItemKind::Folder)
        invalid("Folder reorder guard kind");
      check_version(db, true, expected->item.value, expected->revision);
    }
    auto current = ids(db);
    std::set<std::string> a(current.begin(), current.end()),
        b(order.begin(), order.end());
    if (order.size() != current.size() || b.size() != order.size() || a != b)
      invalid("Reorder must contain every folder exactly once");
    reindex(db, true, order);
    revision(db);
    tx.commit();
    return {};
  });
}
Result<void> set_enabled(Store &store, const SnippetItemId &item,
                         bool enabled) {
  return detail::guarded<void>([&]() -> Result<void> {
    id_valid(item.value);
    if (unsigned(item.kind) > 1)
      invalid("Unknown snippet item kind");
    auto db = store.connection();
    Transaction tx(db);
    version(db, item.kind == SnippetItemKind::Folder, item.value);
    state(db, item.kind == SnippetItemKind::Folder, item.value, enabled);
    revision(db);
    tx.commit();
    return {};
  });
}
Result<void> erase_snippet_item(Store &store, const SnippetItemId &item) {
  return detail::guarded<void>([&]() -> Result<void> {
    id_valid(item.value);
    if (unsigned(item.kind) > 1)
      invalid("Unknown snippet item kind");
    auto db = store.connection();
    Transaction tx(db);
    bool folder = item.kind == SnippetItemKind::Folder;
    version(db, folder, item.value);
    FolderId previous;
    if (folder) {
      Statement children(db, "DELETE FROM snippets WHERE folder_id=?");
      children.bind(1, item.value);
      children.done();
      Statement binding(db, "DELETE FROM settings WHERE key=?");
      binding.bind(1, "t09.hotkey:folder:" + item.value);
      binding.done();
      if (sqlite3_changes(db))
        exec(db, "UPDATE settings SET value=min(CAST(value AS "
                 "INTEGER)+1,2147483646) WHERE key='t09.revision'");
    } else {
      Statement q(db, "SELECT folder_id FROM snippets WHERE id=?");
      q.bind(1, item.value);
      q.row();
      previous = q.text(0);
    }
    Statement q(db, folder ? "DELETE FROM snippet_folders WHERE id=?"
                           : "DELETE FROM snippets WHERE id=?");
    q.bind(1, item.value);
    q.done();
    if (folder)
      reindex(db, true, ids(db));
    else if (!previous.empty())
      reindex(db, false, ids(db, &previous));
    revision(db);
    tx.commit();
    return {};
  });
}
} // namespace cliptiq

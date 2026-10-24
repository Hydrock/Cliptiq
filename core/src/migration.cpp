#include "cliptiq/migration.hpp"
#include "cliptiq/settings.hpp"
#include "db.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on
#else
#include <sys/stat.h>
#endif
namespace cliptiq {
namespace {
using namespace detail;
namespace fs = std::filesystem;
constexpr std::uint64_t cap = 256ull * 1024 * 1024;
const char *schema = R"SQL(
CREATE TABLE cliptiq_interchange(format_version INTEGER NOT NULL);INSERT INTO cliptiq_interchange VALUES(1);
CREATE TABLE pasteboardHistories(id TEXT PRIMARY KEY,title TEXT NOT NULL,pasteboardTypes TEXT NOT NULL,deviceID TEXT,updateAt INTEGER NOT NULL,createdAt INTEGER NOT NULL,ocrText TEXT);
CREATE TABLE pasteboardHistoryAssets(id TEXT PRIMARY KEY,pasteboardHistoryID TEXT NOT NULL,"index" INTEGER NOT NULL,pasteboardType TEXT NOT NULL,data BLOB NOT NULL);
CREATE TABLE pasteboardHistoryThumbnailAssets(pasteboardHistoryID TEXT PRIMARY KEY,kind TEXT NOT NULL,data BLOB NOT NULL);
CREATE TABLE snippetFolders(id TEXT PRIMARY KEY,title TEXT NOT NULL,"index" INTEGER NOT NULL,isEnabled INTEGER NOT NULL);
CREATE TABLE snippets(id TEXT PRIMARY KEY,folderID TEXT NOT NULL,title TEXT NOT NULL,content TEXT NOT NULL,"index" INTEGER NOT NULL,isEnabled INTEGER NOT NULL);
CREATE TABLE migration_settings(key TEXT PRIMARY KEY,value BLOB NOT NULL,apply_value TEXT);
)SQL";
void require(bool x, const char *msg) {
  if (!x)
    throw Failure{{ErrorCode::Corrupt, msg}};
}
std::string text(Statement &q, int i, std::size_t limit = 1024 * 1024) {
  auto s = q.text(i);
  require(s.size() <= limit && s.find('\0') == std::string::npos,
          "Invalid or oversized migration text");
  for (std::size_t pos = 0; pos < s.size();) {
    auto c = static_cast<unsigned char>(s[pos++]);
    unsigned count = 0, minimum = 0;
    unsigned code = c;
    if (c < 128)
      continue;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      code = c & 31;
      minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      code = c & 15;
      minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      code = c & 7;
      minimum = 65536;
    } else
      require(false, "Invalid migration UTF-8");
    require(pos + count <= s.size(), "Incomplete migration UTF-8");
    while (count--) {
      auto next = static_cast<unsigned char>(s[pos++]);
      require((next & 192) == 128, "Invalid migration UTF-8");
      code = (code << 6) | (next & 63);
    }
    require(code >= minimum && code <= 0x10ffff &&
                !(code >= 0xd800 && code <= 0xdfff),
            "Invalid migration Unicode scalar");
  }
  return s;
}
struct Scratch {
  fs::path dir;
  Scratch(fs::path parent = fs::temp_directory_path()) {
    for (unsigned n = 0; n < 32; ++n) {
      std::random_device r;
      dir = parent / (".cliptiq-import-" + std::to_string(r()) + "-" +
                      std::to_string(r()));
#ifdef _WIN32
      PSECURITY_DESCRIPTOR sd = nullptr;
      require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
                  L"D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1,
                  &sd, nullptr),
              "Private scratch descriptor failed");
      SECURITY_ATTRIBUTES a{sizeof(a), sd, FALSE};
      BOOL made = CreateDirectoryW(dir.c_str(), &a);
      LocalFree(sd);
      if (made)
        return;
#else
      if (mkdir(dir.c_str(), 0700) == 0)
        return;
#endif
    }
    throw Failure{{ErrorCode::Io, "Cannot create private migration scratch"}};
  }
  ~Scratch() {
    std::error_code e;
    fs::remove_all(dir, e);
  }
};
Bytes file(const fs::path &p, std::uint64_t limit = 1024ull * 1024 * 1024) {
  require(fs::is_regular_file(fs::symlink_status(p)),
          "Source must be a regular non-symlink file");
  auto size = fs::file_size(p);
  if (size > limit)
    throw Failure{{ErrorCode::TooLarge, "Migration file exceeds limit"}};
  std::ifstream in(p, std::ios::binary);
  Bytes b(static_cast<std::size_t>(size));
  if (!in.read(reinterpret_cast<char *>(b.data()), b.size()) ||
      in.peek() != EOF)
    throw Failure{{ErrorCode::Busy, "Source changed during read"}};
  return b;
}
std::string hash(const Bytes &b) {
  Sha256 h;
  h.field(b.data(), b.size());
  return h.finish();
}
void putfile(const fs::path &p, const Bytes &b) {
  std::ofstream o(p, std::ios::binary);
  o.write(reinterpret_cast<const char *>(b.data()), b.size());
  if (!o)
    throw Failure{{ErrorCode::Io, "Cannot write private source copy"}};
}
using Db = std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)>;
Db open(const fs::path &p, bool write = false) {
  sqlite3 *raw = nullptr;
  int rc = sqlite3_open_v2(p.u8string().c_str(), &raw,
                           (write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                  : SQLITE_OPEN_READONLY) |
                               SQLITE_OPEN_NOFOLLOW,
                           nullptr);
  Db db(raw, sqlite3_close_v2);
  check(raw, rc);
  sqlite3_busy_timeout(raw, 1000);
  sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, 65 * 1024 * 1024);
  sqlite3_limit(raw, SQLITE_LIMIT_SQL_LENGTH, 16384);
  sqlite3_db_config(raw, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
  exec(raw, "PRAGMA trusted_schema=OFF");
  return db;
}
struct Source {
  Scratch scratch;
  Db db{nullptr, sqlite3_close_v2};
  Source(const std::string &path) {
    auto p = fs::u8path(path);
    require(fs::is_regular_file(fs::symlink_status(p)), "Invalid source path");
    // Never open the original with SQLite: even a read-only WAL open may create
    // SHM. Verify the copied DB+WAL against a second full read; a changing
    // source is rejected.
    std::vector<std::pair<fs::path, std::string>> signatures;
    for (auto suffix : {"", "-wal", "-journal"}) {
      auto source = fs::u8path(path + suffix);
      if (!fs::exists(source)) {
        signatures.push_back({source, "absent"});
        continue;
      }
      auto b = file(source);
      signatures.push_back({source, hash(b)});
      putfile(scratch.dir / (std::string("source.sqlite3") + suffix), b);
    }
    for (auto &v : signatures)
      require(v.second == "absent"
                  ? !fs::exists(v.first)
                  : fs::exists(v.first) && hash(file(v.first)) == v.second,
              "Source changed; close Clipy and retry");
    db = open(fs::canonical(scratch.dir) / "source.sqlite3", true);
    exec(db.get(), "PRAGMA query_only=ON");
    exec(db.get(), "BEGIN");
    Statement q(db.get(), "PRAGMA quick_check");
    require(q.row() && q.text(0) == "ok", "Source integrity check failed");
  }
};
struct Part {
  std::string id, type;
  std::int64_t index;
  Bytes bytes;
};
struct H {
  std::string id, title, types, device, ocr, thumbkind;
  bool device_null, ocr_null;
  std::int64_t created, updated;
  Bytes thumb;
  std::vector<Part> parts;
};
struct F {
  std::string id, title;
  std::int64_t index, enabled;
};
struct N {
  std::string id, folder, title, content;
  std::int64_t index, enabled;
};
struct S {
  std::string key;
  Bytes raw;
  std::optional<std::string> apply;
};
struct Data {
  std::vector<H> history;
  std::vector<F> folders;
  std::vector<N> snippets;
  std::vector<S> settings;
  std::string digest;
};
bool table(sqlite3 *d, const char *n) {
  Statement q(d, "SELECT type FROM sqlite_schema WHERE name=?");
  q.bind(1, std::string(n));
  return q.row() && q.text(0) == "table";
}
std::set<std::string> columns(sqlite3 *d, const char *t) {
  std::string sql = "PRAGMA table_info(\"" + std::string(t) + "\")";
  Statement q(d, sql.c_str());
  std::set<std::string> c;
  while (q.row())
    c.insert(q.text(1));
  return c;
}
Data read(sqlite3 *d, bool neutral) {
  for (auto t :
       {"pasteboardHistories", "pasteboardHistoryAssets",
        "pasteboardHistoryThumbnailAssets", "snippetFolders", "snippets"})
    require(table(d, t), "Missing migration source table (views forbidden)");
  auto cols = columns(d, "pasteboardHistories");
  bool v3 = cols.count("createdAt"), v4 = cols.count("ocrText");
  require(!v4 || v3, "Invalid Clipy schema progression");
  std::string metadata_types = "SELECT 1 FROM pasteboardHistories WHERE "
                               "typeof(deviceID) NOT IN('text','null')";
  if (v3)
    metadata_types += " OR typeof(createdAt)!='integer'";
  if (v4)
    metadata_types += " OR typeof(ocrText) NOT IN('text','null')";
  metadata_types += " LIMIT 1";
  Statement metadata_type(d, metadata_types.c_str());
  require(!metadata_type.row(),
          "Invalid history timestamp/nullable metadata type");
  for (auto item :
       {std::pair<const char *, const char *>{
            "pasteboardHistories",
            "typeof(id)!='text' OR typeof(title)!='text' OR "
            "typeof(pasteboardTypes)!='text' OR typeof(updateAt)!='integer'"},
        {"pasteboardHistoryAssets",
         "typeof(id)!='text' OR typeof(pasteboardHistoryID)!='text' OR "
         "typeof(\"index\")!='integer' OR typeof(pasteboardType)!='text' OR "
         "typeof(data)!='blob'"},
        {"snippetFolders",
         "typeof(id)!='text' OR typeof(title)!='text' OR "
         "typeof(\"index\")!='integer' OR typeof(isEnabled)!='integer'"},
        {"snippets",
         "typeof(id)!='text' OR typeof(folderID)!='text' OR "
         "typeof(title)!='text' OR typeof(content)!='text' OR "
         "typeof(\"index\")!='integer' OR typeof(isEnabled)!='integer'"}}) {
    std::string sql = "SELECT 1 FROM " + std::string(item.first) + " WHERE " +
                      item.second + " LIMIT 1";
    Statement typed(d, sql.c_str());
    require(!typed.row(), "Invalid migration SQLite value type");
  }
  if (neutral) {
    require(table(d, "cliptiq_interchange") && table(d, "migration_settings"),
            "Not a neutral Cliptiq interchange");
    Statement v(d, "SELECT format_version FROM cliptiq_interchange");
    require(v.row() && v.number(0) == 1 && !v.row() && v3 && v4,
            "Unsupported neutral format version");
  } else {
    require(table(d, "grdb_migrations"),
            "Not a recognized Clipy SQLite database");
    Statement m(d, "SELECT identifier FROM grdb_migrations ORDER BY rowid");
    std::vector<std::string> ids;
    while (m.row()) {
      require(ids.size() < 4, "Unknown Clipy migration");
      ids.push_back(m.text(0));
    }
    std::vector<std::string> known = {"Create initial tables",
                                      "Create search indexes",
                                      "Add createdAt to pasteboardHistories",
                                      "Add OCR text to history search"};
    require(!ids.empty() && std::equal(ids.begin(), ids.end(), known.begin()) &&
                v3 == (ids.size() >= 3) && v4 == (ids.size() == 4),
            "Unsupported Clipy SQLite schema version");
  }
  Data out;
  std::uint64_t total = 0;
  auto budget = [&](std::uint64_t n) {
    if (n > cap - total)
      throw Failure{{ErrorCode::TooLarge, "Migration payload exceeds256MiB"}};
    total += n;
  };
  std::string sql = "SELECT id,title,pasteboardTypes,deviceID,updateAt," +
                    std::string(v3 ? "createdAt" : "updateAt") + "," +
                    (v4 ? "ocrText" : "NULL") +
                    " FROM pasteboardHistories ORDER BY " +
                    (v3 ? "createdAt" : "updateAt") + ",id LIMIT 100001";
  Statement h(d, sql.c_str());
  std::set<std::string> history_ids;
  while (h.row()) {
    require(out.history.size() < 100000, "Too many history entries");
    H x;
    x.id = text(h, 0, 1024);
    require(!x.id.empty() && history_ids.insert(x.id).second,
            "Duplicate/empty history ID");
    x.title = text(h, 1);
    x.types = text(h, 2);
    x.device = text(h, 3, 4096);
    x.device_null = h.is_null(3);
    x.updated = h.number(4);
    x.created = h.number(5);
    x.ocr = text(h, 6);
    x.ocr_null = h.is_null(6);
    require(x.created >= 0 && x.updated >= 0 && x.created <= INT64_MAX / 1000 &&
                x.updated <= INT64_MAX / 1000,
            "Invalid history time");
    budget(x.title.size() + x.types.size() + x.device.size() + x.ocr.size());
    Statement p(
        d,
        "SELECT id,\"index\",pasteboardType,data FROM pasteboardHistoryAssets "
        "WHERE pasteboardHistoryID=? ORDER BY \"index\",id LIMIT 4097");
    p.bind(1, x.id);
    std::uint64_t entry = 0;
    while (p.row()) {
      require(x.parts.size() < 4096, "Too many history representations");
      Part a{text(p, 0, 1024), text(p, 2, 4096), p.number(1), p.bytes(3)};
      require(!a.type.empty() && a.index >= 0 && a.index <= UINT32_MAX &&
                  a.bytes.size() <= 32 * 1024 * 1024,
              "Invalid history representation");
      entry += a.bytes.size();
      require(entry <= 64 * 1024 * 1024, "History entry too large");
      budget(a.bytes.size());
      x.parts.push_back(std::move(a));
    }
    require(!x.parts.empty(), "History payload/asset missing");
    Statement t(d, "SELECT kind,data FROM pasteboardHistoryThumbnailAssets "
                   "WHERE pasteboardHistoryID=?");
    t.bind(1, x.id);
    if (t.row()) {
      x.thumbkind = text(t, 0, 128);
      x.thumb = t.bytes(1);
      require(x.thumb.size() <= 32 * 1024 * 1024, "Thumbnail too large");
      budget(x.thumb.size());
    }
    out.history.push_back(std::move(x));
  }
  {
    Statement q(d, "SELECT count(*) FROM pasteboardHistoryAssets a LEFT JOIN "
                   "pasteboardHistories h ON h.id=a.pasteboardHistoryID WHERE "
                   "h.id IS NULL");
    q.row();
    require(q.number(0) == 0, "Orphan history assets");
  }
  std::set<std::string> folders, snippets;
  Statement f(d, "SELECT id,title,\"index\",isEnabled FROM snippetFolders "
                 "ORDER BY \"index\",id LIMIT 1001");
  while (f.row()) {
    F x{text(f, 0, 1024), text(f, 1), f.number(2), f.number(3)};
    require(out.folders.size() < 1000 && !x.id.empty() &&
                folders.insert(x.id).second && x.index >= 0 &&
                (x.enabled == 0 || x.enabled == 1),
            "Invalid folder");
    budget(x.title.size());
    out.folders.push_back(std::move(x));
  }
  Statement n(d, "SELECT id,folderID,title,content,\"index\",isEnabled FROM "
                 "snippets ORDER BY folderID,\"index\",id LIMIT 10001");
  while (n.row()) {
    N x{text(n, 0, 1024), text(n, 1, 1024), text(n, 2),
        text(n, 3),       n.number(4),      n.number(5)};
    require(out.snippets.size() < 10000 && !x.id.empty() &&
                snippets.insert(x.id).second && folders.count(x.folder) &&
                x.index >= 0 && (x.enabled == 0 || x.enabled == 1),
            "Invalid snippet/orphan folder");
    budget(x.title.size() + x.content.size());
    out.snippets.push_back(std::move(x));
  }
  if (table(d, "migration_settings")) {
    Statement s(d, "SELECT key,value,apply_value FROM migration_settings ORDER "
                   "BY key LIMIT 2049");
    while (s.row()) {
      require(out.settings.size() < 2048, "Too many migration settings");
      S x{text(s, 0, 1100), s.bytes(1), {}};
      require(x.raw.size() <=
                  (x.key.rfind("legacy.archive.", 0) == 0 ? 64u : 16u) * 1024 *
                      1024,
              "Settings/archive payload too large");
      if (!s.is_null(2))
        x.apply = text(s, 2, 65536);
      budget(x.raw.size());
      out.settings.push_back(std::move(x));
    }
  }
  // Stable receipt includes the complete neutral payload, not a path or source
  // mtime.
  Sha256 digest;
  auto field = [&](const auto &v) { digest.field(v.data(), v.size()); };
  digest.integer(1);
  digest.integer(out.history.size());
  digest.integer(out.folders.size());
  digest.integer(out.snippets.size());
  digest.integer(out.settings.size());
  for (auto &x : out.history) {
    field(x.id);
    digest.integer(x.parts.size());
    field(x.title);
    field(x.types);
    field(x.device);
    digest.integer(x.device_null);
    field(x.ocr);
    digest.integer(x.ocr_null);
    digest.integer(x.created);
    digest.integer(x.updated);
    for (auto &a : x.parts) {
      field(a.id);
      field(a.type);
      digest.integer(a.index);
      field(a.bytes);
    }
    field(x.thumbkind);
    field(x.thumb);
  }
  for (auto &x : out.folders) {
    field(x.id);
    field(x.title);
    digest.integer(x.index);
    digest.integer(x.enabled);
  }
  for (auto &x : out.snippets) {
    field(x.id);
    field(x.folder);
    field(x.title);
    field(x.content);
    digest.integer(x.index);
    digest.integer(x.enabled);
  }
  for (auto &x : out.settings) {
    field(x.key);
    field(x.raw);
    digest.integer(bool(x.apply));
    if (x.apply)
      field(*x.apply);
  }
  out.digest = digest.finish();
  return out;
}
std::string replay_name(const std::string &t) {
  static const std::map<std::string, std::string> aliases = {
      {"NSStringPboardType", "public.utf8-plain-text"},
      {"NSRTFPboardType", "public.rtf"},
      {"NSRTFDPboardType", "com.apple.flat-rtfd"},
      {"NSPDFPboardType", "com.adobe.pdf"},
      {"NSURLPboardType", "public.url"},
      {"NSTIFFPboardType", "public.tiff"}};
  auto p = aliases.find(t);
  return p == aliases.end() ? t : p->second;
}
CanonicalKind kind(const std::string &native) {
  const auto t = replay_name(native);
  if (t == "public.utf8-plain-text")
    return CanonicalKind::Text;
  if (t == "public.rtf")
    return CanonicalKind::Rtf;
  if (t == "com.apple.flat-rtfd")
    return CanonicalKind::Rtfd;
  if (t == "public.html")
    return CanonicalKind::Html;
  if (t == "com.adobe.pdf")
    return CanonicalKind::Pdf;
  if (t == "NSFilenamesPboardType" || t == "public.file-url")
    return CanonicalKind::Files;
  if (t == "public.url")
    return CanonicalKind::Url;
  if (t == "public.tiff" || t == "public.png")
    return CanonicalKind::Image;
  return CanonicalKind::Opaque;
}
void write(sqlite3 *d, const Data &x) {
  Transaction tx(d);
  exec(d, schema);
  for (auto &h : x.history) {
    Statement q(d, "INSERT INTO pasteboardHistories VALUES(?,?,?,?,?,?,?)");
    q.bind(1, h.id);
    q.bind(2, h.title);
    q.bind(3, h.types);
    if (h.device_null)
      q.null(4);
    else
      q.bind(4, h.device);
    q.bind(5, h.updated);
    q.bind(6, h.created);
    if (h.ocr_null)
      q.null(7);
    else
      q.bind(7, h.ocr);
    q.done();
    for (auto &p : h.parts) {
      Statement a(d, "INSERT INTO pasteboardHistoryAssets VALUES(?,?,?,?,?)");
      a.bind(1, p.id);
      a.bind(2, h.id);
      a.bind(3, p.index);
      a.bind(4, p.type);
      a.bind(5, p.bytes);
      a.done();
    }
    if (!h.thumbkind.empty()) {
      Statement t(d,
                  "INSERT INTO pasteboardHistoryThumbnailAssets VALUES(?,?,?)");
      t.bind(1, h.id);
      t.bind(2, h.thumbkind);
      t.bind(3, h.thumb);
      t.done();
    }
  }
  for (auto &f : x.folders) {
    Statement q(d, "INSERT INTO snippetFolders VALUES(?,?,?,?)");
    q.bind(1, f.id);
    q.bind(2, f.title);
    q.bind(3, f.index);
    q.bind(4, f.enabled);
    q.done();
  }
  for (auto &n : x.snippets) {
    Statement q(d, "INSERT INTO snippets VALUES(?,?,?,?,?,?)");
    q.bind(1, n.id);
    q.bind(2, n.folder);
    q.bind(3, n.title);
    q.bind(4, n.content);
    q.bind(5, n.index);
    q.bind(6, n.enabled);
    q.done();
  }
  for (auto &s : x.settings) {
    Statement q(d, "INSERT INTO migration_settings VALUES(?,?,?)");
    q.bind(1, s.key);
    q.bind(2, s.raw);
    if (s.apply)
      q.bind(3, *s.apply);
    else
      q.null(3);
    q.done();
  }
  tx.commit();
}
} // namespace
Result<void>
export_clipy_sqlite(const std::string &source, const std::string &target,
                    const std::vector<MigrationSetting> &settings) {
  return guarded<void>([&]() -> Result<void> {
    require(!target.empty() && !fs::exists(fs::u8path(target)),
            "Output already exists");
    Source input(source);
    auto data = read(input.db.get(), false);
    require(settings.size() <= 2048, "Too many exported settings");
    for (auto &s : settings) {
      require(s.key.size() <= 1100 &&
                  s.raw.size() <=
                      (s.key.rfind("legacy.archive.", 0) == 0 ? 64u : 16u) *
                          1024 * 1024 &&
                  (!s.apply_value || s.apply_value->size() <= 65536),
              "Export setting exceeds limit");
      data.settings.push_back({s.key, s.raw, s.apply_value});
    }
    auto output = fs::absolute(fs::u8path(target));
    Scratch scratch(output.parent_path());
    auto temp = fs::canonical(scratch.dir) / "export.sqlite3";
    {
      auto db = open(temp, true);
      write(db.get(), data);
      (void)read(db.get(), true); // Validate complete output before publishing.
    }
    fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write);
    fs::create_hard_link(temp, output);
    return {};
  });
}
Result<ImportSummary> import_clipy(Store &store, const std::string &path,
                                   ImportOptions options) {
  return guarded<ImportSummary>([&]() -> Result<ImportSummary> {
    Source input(path);
    auto data = read(input.db.get(), true);
    auto d = store.connection();
    auto settings = load_settings(store);
    if (!settings)
      return settings.error();
    if (options.apply_settings) {
      for (auto &s : data.settings)
        if (s.apply) {
          if (s.key.rfind("hotkey:", 0) == 0) {
            if (s.apply->empty())
              settings.value().hotkeys[s.key.substr(7)] = {};
            else {
              auto c = parse_chord(*s.apply);
              if (!c)
                return c.error();
              settings.value().hotkeys[s.key.substr(7)] = c.value();
            }
          } else if (settings.value().values.count(s.key) && s.key != "login" &&
                     s.key != "screenshots" && s.key != "diagnostics" &&
                     s.key != "updates_auto")
            settings.value().values[s.key] = *s.apply;
          else
            return Error{ErrorCode::Unsupported,
                         "Migration setting cannot be activated automatically"};
        }
      auto valid = validate_settings(settings.value());
      if (!valid)
        return valid.error();
    }
    Transaction tx(d);
    exec(d, "CREATE TABLE IF NOT EXISTS migration_receipts(digest TEXT PRIMARY "
            "KEY,settings_applied INTEGER NOT NULL); CREATE TABLE IF NOT "
            "EXISTS migration_originals(digest TEXT NOT NULL,category TEXT NOT "
            "NULL,id TEXT NOT NULL,field TEXT NOT NULL,data BLOB NOT "
            "NULL,owner_history TEXT REFERENCES history(id) ON DELETE "
            "CASCADE,owner_folder TEXT REFERENCES snippet_folders(id) ON "
            "DELETE CASCADE,owner_snippet TEXT REFERENCES snippets(id) ON "
            "DELETE CASCADE,PRIMARY KEY(digest,category,id,field))");
    {
      Statement q(
          d, "SELECT settings_applied FROM migration_receipts WHERE digest=?");
      q.bind(1, data.digest);
      if (q.row()) {
        require(!options.apply_settings || q.number(0) == 1,
                "Already imported without active settings; explicit settings "
                "reconciliation required");
        tx.commit();
        ImportSummary r;
        r.already_imported = true;
        return r;
      }
    }
    auto original = [&](const std::string &category, const std::string &id,
                        const std::string &field, const Bytes &value,
                        const std::string &history = {},
                        const std::string &folder = {},
                        const std::string &snippet = {}) {
      Statement q(d, "INSERT INTO migration_originals VALUES(?,?,?,?,?,?,?,?)");
      q.bind(1, data.digest);
      q.bind(2, category);
      q.bind(3, id);
      q.bind(4, field);
      q.bind(5, value);
      if (history.empty())
        q.null(6);
      else
        q.bind(6, history);
      if (folder.empty())
        q.null(7);
      else
        q.bind(7, folder);
      if (snippet.empty())
        q.null(8);
      else
        q.bind(8, snippet);
      q.done();
    };
    auto meta = [&](const std::string &c, const std::string &id,
                    const std::string &f, const std::string &v,
                    const std::string &history = {},
                    const std::string &folder = {},
                    const std::string &snippet = {}) {
      original(c, id, f, Bytes(v.begin(), v.end()), history, folder, snippet);
    };
    std::uint64_t total = 0;
    {
      Statement q(d, "SELECT coalesce(sum(payload_bytes),0) FROM history");
      q.row();
      total = q.number(0);
    }
    require(total <= store.limits().history_bytes,
            "Existing destination exceeds quota");
    for (auto &h : data.history) {
      Sha256 digest;
      digest.integer(1);
      digest.integer(h.parts.size());
      std::uint64_t size = 0;
      std::vector<std::uint32_t> items;
      std::map<std::string, std::uint32_t> occurrences;
      for (auto &p : h.parts)
        items.push_back(p.type == "NSFilenamesPboardType"
                            ? RootPasteboardItem
                            : occurrences[replay_name(p.type)]++);
      std::size_t item_ordinal = 0;
      for (auto &p : h.parts) {
        size += p.bytes.size();
        require(p.bytes.size() <= store.limits().representation_bytes,
                "Destination representation limit");
        std::string platform = "macos";
        digest.integer(items[item_ordinal++]);
        digest.field(platform.data(), platform.size());
        digest.field(p.type.data(), p.type.size());
        digest.integer(static_cast<unsigned>(kind(p.type)));
        digest.integer(0);
        digest.field(p.bytes.data(), p.bytes.size());
      }
      require(size <= store.limits().entry_bytes &&
                  size <= store.limits().history_bytes - total,
              "Destination history quota exceeded");
      total += size;
      Statement q(d, "INSERT INTO "
                     "history(id,digest,created_ms,used_ms,payload_bytes,"
                     "sequence_token,source_confidence,transient,concealed,"
                     "remote,ocr_text) VALUES(?,?,?,?,?,?,2,0,0,0,?)");
      q.bind(1, h.id);
      q.bind(2, digest.finish());
      q.bind(3, h.created * 1000);
      q.bind(4, h.updated * 1000);
      q.bind(5, std::int64_t(size));
      q.bind(6, "clipy-import:" + data.digest);
      if (h.ocr_null)
        q.null(7);
      else
        q.bind(7, h.ocr);
      q.done();
      std::int64_t ordinal = 0;
      for (auto &p : h.parts) {
        Statement a(d, "INSERT INTO representations VALUES(?,?,?,?,?,?,?,0)");
        a.bind(1, h.id);
        a.bind(2, ordinal);
        a.bind(3, std::int64_t(items[ordinal++]));
        a.bind(4, std::string("macos"));
        a.bind(5, p.type);
        a.bind(6, std::int64_t(kind(p.type)));
        a.bind(7, p.bytes);
        a.done();
        meta("asset", p.id, "ordinal", std::to_string(p.index), h.id);
      }
      meta("history", h.id, "title", h.title, h.id);
      meta("history", h.id, "pasteboardTypes", h.types, h.id);
      if (!h.device_null)
        meta("history", h.id, "deviceID", h.device, h.id);
      if (!h.thumbkind.empty())
        original("history", h.id, "thumbnail:" + h.thumbkind, h.thumb, h.id);
    }
    std::int64_t offset = 0;
    {
      Statement q(
          d,
          "SELECT count(*),coalesce(max(position),-1)+1 FROM snippet_folders");
      q.row();
      require(q.number(0) + data.folders.size() <= 1000,
              "Destination folder limit");
      offset = q.number(1);
    }
    for (auto &f : data.folders) {
      Statement q(d, "INSERT INTO snippet_folders VALUES(?,?,?)");
      q.bind(1, f.id);
      q.bind(2, f.title);
      q.bind(3, offset++);
      q.done();
      Statement s(d, "INSERT INTO snippet_folder_states VALUES(?,?,1)");
      s.bind(1, f.id);
      s.bind(2, f.enabled);
      s.done();
      meta("folder", f.id, "index", std::to_string(f.index), {}, f.id);
    }
    {
      Statement q(d, "SELECT count(*) FROM snippets");
      q.row();
      require(q.number(0) + data.snippets.size() <= 10000,
              "Destination snippet limit");
    }
    std::map<std::string, std::int64_t> positions;
    for (auto &n : data.snippets) {
      Statement q(d, "INSERT INTO snippets VALUES(?,?,?,?,?)");
      q.bind(1, n.id);
      q.bind(2, n.folder);
      q.bind(3, n.title);
      q.bind(4, n.content);
      q.bind(5, positions[n.folder]++);
      q.done();
      Statement s(d, "INSERT INTO snippet_states VALUES(?,?,1)");
      s.bind(1, n.id);
      s.bind(2, n.enabled);
      s.done();
      meta("snippet", n.id, "index", std::to_string(n.index), {}, {}, n.id);
    }
    for (auto &s : data.settings) {
      std::string owner;
      if (s.key.rfind("legacy.archive.", 0) == 0) {
        owner = s.key.substr(15);
        require(std::any_of(data.history.begin(), data.history.end(),
                            [&](const H &h) { return h.id == owner; }),
                "Orphan legacy archive");
      }
      original("settings", s.key, "raw", s.raw, owner);
    }
    if (options.apply_settings) {
      for (auto &p : settings.value().values) {
        Statement q(d, "INSERT OR REPLACE INTO settings VALUES(?,?)");
        q.bind(1, "t09." + p.first);
        q.bind(2, p.second);
        q.done();
      }
      for (auto &p : settings.value().hotkeys) {
        Statement q(d, "INSERT OR REPLACE INTO settings VALUES(?,?)");
        q.bind(1, "t09.hotkey:" + p.first);
        q.bind(2, p.second ? format_chord(*p.second) : "");
        q.done();
      }
      exec(d, "INSERT OR REPLACE INTO settings VALUES('t09.schema','1')");
      Statement rev(d,
                    "INSERT OR REPLACE INTO settings VALUES('t09.revision',?)");
      rev.bind(1, std::to_string(settings.value().revision + 1));
      rev.done();
    }
    Statement receipt(d, "INSERT INTO migration_receipts VALUES(?,?)");
    receipt.bind(1, data.digest);
    receipt.bind(2, std::int64_t(options.apply_settings));
    receipt.done();
    revision(d);
    tx.commit();
    return ImportSummary{data.history.size(), data.folders.size(),
                         data.snippets.size(), data.settings.size(), false};
  });
}
} // namespace cliptiq

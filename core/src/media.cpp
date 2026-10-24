#include "cliptiq/media.hpp"
#include "db.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <limits>
namespace cliptiq {
namespace {
bool valid(PreviewSize s) {
  return s.width && s.height && s.width <= 512 && s.height <= 512;
}
bool valid(const MenuImage &i) {
  return i.width && i.height &&
         std::uint64_t(i.width) * i.height <= media_pixel_limit &&
         i.rgba.size() == std::uint64_t(i.width) * i.height * 4;
}
void put(Bytes &b, std::uint64_t v) {
  for (unsigned n = 0; n < 8; ++n)
    b.push_back(std::uint8_t(v >> (n * 8)));
}
std::uint64_t get(const Bytes &b, size_t p) {
  if (p + 8 > b.size())
    return 0;
  std::uint64_t n = 0;
  for (unsigned i = 0; i < 8; ++i)
    n |= std::uint64_t(b[p + i]) << (i * 8);
  return n;
}
Bytes ticket_bytes(const PreviewTicket &t) {
  Bytes b;
  put(b, t.generation);
  put(b, t.size.width);
  put(b, t.size.height);
  b.insert(b.end(), t.digest.begin(), t.digest.end());
  return b;
}
Bytes asset(sqlite3 *d, const EntryId &id, const char *k, size_t limit) {
  detail::Statement q(d, "SELECT data FROM history_assets WHERE entry_id=? AND "
                         "kind=? AND length(data)<=?");
  q.bind(1, id);
  q.bind(2, std::string(k));
  q.bind(3, std::int64_t(limit));
  return q.row() ? q.bytes(0) : Bytes{};
}
void save(sqlite3 *d, const EntryId &id, const char *k, const Bytes &b) {
  detail::Statement q(d, "INSERT OR REPLACE INTO "
                         "history_assets(entry_id,kind,data) VALUES(?,?,?)");
  q.bind(1, id);
  q.bind(2, std::string(k));
  q.bind(3, b);
  q.done();
}
bool current(sqlite3 *d, const PreviewTicket &t) {
  detail::Statement q(d, "SELECT digest FROM history WHERE id=?");
  q.bind(1, t.id);
  return q.row() && q.text(0) == t.digest &&
         asset(d, t.id, "t12.request", 256) == ticket_bytes(t);
}
std::uint32_t be(const Bytes &b, size_t p) {
  return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p + 1]) << 16) |
         (std::uint32_t(b[p + 2]) << 8) | b[p + 3];
}
std::uint32_t crc(const std::uint8_t *p, size_t n) {
  std::uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; ++i)
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
  }
  return ~c;
}
} // namespace
Result<PreviewTicket> request_preview(Store &s, const EntryId &id,
                                      PreviewSize z, std::uint64_t g) {
  return detail::guarded<PreviewTicket>([&]() -> Result<PreviewTicket> {
    if (!valid(z))
      return Error{ErrorCode::InvalidArgument,
                   "Preview dimensions must be 1..512"};
    auto d = s.connection();
    detail::Transaction tx(d);
    detail::Statement q(d, "SELECT digest FROM history WHERE id=?");
    q.bind(1, id);
    if (!q.row())
      return Error{ErrorCode::NotFound, "History entry removed"};
    auto old = asset(d, id, "t12.request", 256);
    auto n = get(old, 0);
    if (n >= std::uint64_t(std::numeric_limits<std::int64_t>::max()) ||
        g > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
      return Error{ErrorCode::TooLarge, "Preview generation exhausted"};
    PreviewTicket t{id, q.text(0), z, std::max(g, n + 1)};
    save(d, id, "t12.request", ticket_bytes(t));
    tx.commit();
    return t;
  });
}
Result<bool> store_preview_if_present(Store &s, const PreviewTicket &t,
                                      const MenuImage &i) {
  return detail::guarded<bool>([&]() -> Result<bool> {
    if (!valid(t.size) || !valid(i) || i.width > t.size.width ||
        i.height > t.size.height)
      return Error{ErrorCode::InvalidArgument, "Invalid preview buffer"};
    auto d = s.connection();
    detail::Transaction tx(d);
    if (!current(d, t))
      return false;
    Bytes b = ticket_bytes(t);
    put(b, i.width);
    put(b, i.height);
    b.insert(b.end(), i.rgba.begin(), i.rgba.end());
    save(d, t.id, "t12.preview", b);
    std::int64_t size = 0;
    {
      detail::Statement sum(d, "SELECT coalesce(sum(length(data)),0) FROM "
                               "history_assets WHERE kind='t12.preview'");
      sum.row();
      size = sum.number(0);
    }
    while (size > std::int64_t(media_cache_limit)) {
      EntryId id;
      {
        detail::Statement first(
            d, "SELECT entry_id,length(data) FROM history_assets WHERE "
               "kind='t12.preview' ORDER BY rowid LIMIT 1");
        if (!first.row())
          break;
        id = first.text(0);
        size -= first.number(1);
      }
      detail::Statement del(
          d,
          "DELETE FROM history_assets WHERE entry_id=? AND kind='t12.preview'");
      del.bind(1, id);
      del.done();
    }
    detail::revision(d);
    tx.commit();
    return true;
  });
}
Result<std::optional<MenuImage>> load_preview(Store &s, const EntryId &id,
                                              PreviewSize z) {
  return detail::guarded<std::optional<MenuImage>>(
      [&]() -> Result<std::optional<MenuImage>> {
        if (!valid(z))
          return Error{ErrorCode::InvalidArgument, "Invalid dimensions"};
        auto d = s.connection();
        auto r = asset(d, id, "t12.request", 256);
        auto b = asset(d, id, "t12.preview", 512 * 512 * 4 + 272);
        if (r.size() != 88 || b.size() < 104 || get(r, 8) != z.width ||
            get(r, 16) != z.height ||
            !std::equal(r.begin(), r.end(), b.begin()))
          return std::optional<MenuImage>{};
        PreviewTicket t{id, std::string(r.begin() + 24, r.end()), z, get(r, 0)};
        if (!current(d, t))
          return std::optional<MenuImage>{};
        auto w = get(b, 88), h = get(b, 96);
        if (!w || !h || w > z.width || h > z.height ||
            b.size() != 104 + w * h * 4)
          return std::optional<MenuImage>{};
        return std::optional<MenuImage>{MenuImage{
            unsigned(w), unsigned(h), Bytes(b.begin() + 104, b.end())}};
      });
}
Result<MenuImage> fit_preview(const MenuImage &i, PreviewSize z) {
  if (!valid(z) || !valid(i))
    return Error{ErrorCode::TooLarge, "Invalid or oversized image"};
  double scale =
      std::min({1., double(z.width) / i.width, double(z.height) / i.height});
  unsigned w = std::max(1u, unsigned(i.width * scale)),
           h = std::max(1u, unsigned(i.height * scale));
  MenuImage out{w, h, Bytes(size_t(w) * h * 4)};
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x)
      std::copy_n(i.rgba.data() + (size_t(y) * i.height / h * i.width +
                                   size_t(x) * i.width / w) *
                                      4,
                  4, out.rgba.data() + (size_t(y) * w + x) * 4);
  return out;
}
Result<std::string> image_fingerprint(const MenuImage &i) {
  if (!valid(i))
    return Error{ErrorCode::TooLarge, "Invalid image"};
  detail::Sha256 h;
  h.integer(i.width);
  h.integer(i.height);
  h.field(i.rgba.data(), i.rgba.size());
  return h.finish();
}
Result<void> store_image_fingerprint(Store &s, const PreviewTicket &t,
                                     const std::string &fingerprint) {
  return detail::guarded<void>([&]() -> Result<void> {
    if (fingerprint.size() != 64)
      return Error{ErrorCode::InvalidArgument, "Invalid pixel fingerprint"};
    auto d = s.connection();
    detail::Transaction tx(d);
    if (!current(d, t))
      return {};
    Bytes b(fingerprint.begin(), fingerprint.end());
    save(d, t.id, "t12.pixel", b);
    std::vector<EntryId> files;
    bool clipboard = false;
    {
      detail::Statement q(
          d, "SELECT h.id,h.sequence_token FROM history h JOIN history_assets "
             "a ON a.entry_id=h.id WHERE a.kind='t12.pixel' AND a.data=? ORDER "
             "BY h.created_ms,h.rowid");
      q.bind(1, b);
      while (q.row()) {
        if (q.text(1).rfind("t12.file:", 0) == 0)
          files.push_back(q.text(0));
        else
          clipboard = true;
      }
    }
    bool changed = false;
    for (size_t n = clipboard ? 0 : 1; n < files.size(); ++n) {
      detail::Statement del(d, "DELETE FROM history WHERE id=?");
      del.bind(1, files[n]);
      del.done();
      changed = true;
    }
    if (changed)
      detail::revision(d);
    tx.commit();
    return {};
  });
}
Capabilities screenshot_capabilities(CapturePlatform p) {
  Capabilities c;
  c.platform = p;
  c.capture = {CapabilityStatus::Available, "Selected directory only"};
  c.source_app = {CapabilityStatus::Limited,
                  "File events do not establish source identity"};
  if (p == CapturePlatform::MacOS)
    c.native_types.push_back({"macos", "public.png", CanonicalKind::Image});
  else if (p == CapturePlatform::Windows)
    c.native_types.push_back({"windows", "PNG", CanonicalKind::Image});
  else if (p == CapturePlatform::X11 || p == CapturePlatform::Wayland)
    c.native_types.push_back({p == CapturePlatform::X11 ? "x11" : "wayland",
                              "image/png", CanonicalKind::Image});
  else
    c.capture = {CapabilityStatus::Unavailable, "Unknown platform"};
  return c;
}
ClipboardSnapshot screenshot_snapshot(CapturePlatform p, Bytes b,
                                      const std::string &v) {
  ClipboardSnapshot s;
  s.sequence_token = "t12.file:" + v;
  auto c = screenshot_capabilities(p);
  if (!c.native_types.empty()) {
    const auto &t = c.native_types[0];
    s.representations.push_back(
        {0, t.platform, t.native_type, CanonicalKind::Image, std::move(b)});
  }
  return s;
}
Result<void> validate_screenshot_png(const Bytes &b) {
  const std::uint8_t magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (b.size() > 32 * 1024 * 1024)
    return Error{ErrorCode::TooLarge, "Screenshot exceeds 32 MiB"};
  if (b.size() < 8 || !std::equal(magic, magic + 8, b.begin()))
    return Error{ErrorCode::Corrupt, "Not PNG"};
  bool header = false, data = false;
  size_t p = 8;
  while (p + 12 <= b.size()) {
    auto n = be(b, p);
    if (n > b.size() - p - 12)
      return Error{ErrorCode::Busy, "Incomplete PNG chunk"};
    auto type = be(b, p + 4);
    if (crc(b.data() + p + 4, n + 4) != be(b, p + 8 + n))
      return Error{ErrorCode::Corrupt, "PNG CRC mismatch"};
    if (!header) {
      if (type != 0x49484452 || n != 13)
        return Error{ErrorCode::Corrupt, "Missing IHDR"};
      auto w = be(b, p + 8), h = be(b, p + 12);
      if (!w || !h || std::uint64_t(w) * h > media_pixel_limit)
        return Error{ErrorCode::TooLarge, "Screenshot exceeds 40 MP"};
      header = true;
    } else if (type == 0x49484452)
      return Error{ErrorCode::Corrupt, "Repeated IHDR"};
    if (type == 0x49444154)
      data = true;
    if (type == 0x49454e44) {
      if (n || !data || p + 12 != b.size())
        return Error{ErrorCode::Corrupt, "Invalid IEND"};
      return {};
    }
    p += size_t(n) + 12;
  }
  return Error{ErrorCode::Busy, "Incomplete PNG"};
}
} // namespace cliptiq

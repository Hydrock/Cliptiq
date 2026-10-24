#include "cliptiq/ocr.hpp"
#include "db.hpp"
#include <algorithm>
#include <charconv>
extern "C" {
#include "grapheme.h"
}
namespace cliptiq {
namespace {
Result<void> privacy(Store &s, const EntryId &id, const CapturePolicy &p,
                     const Capabilities &c) {
  if (!p.request_ocr || c.ocr.status == CapabilityStatus::Unavailable)
    return Error{ErrorCode::Unsupported, "OCR unavailable or not requested"};
  auto source = load_snapshot(s, id);
  if (!source)
    return source.error();
  auto decision = evaluate_capture(source.value(), p, c);
  if (decision.disposition != CaptureDisposition::Accept)
    return Error{ErrorCode::Unsupported, "OCR rejected by privacy gate"};
  for (auto &r : decision.snapshot->representations)
    if (r.kind == CanonicalKind::Image)
      return {};
  return Error{ErrorCode::Unsupported,
               "OCR has no enabled image representation"};
}
} // namespace
Result<std::string> truncate_ocr_text(const std::string &s) {
  if (s.size() > 1048576)
    return Error{ErrorCode::TooLarge, "OCR result exceeds1MiB"};
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    unsigned n;
    std::uint32_t cp;
    if (c < 0x80) {
      if (!c)
        return Error{ErrorCode::Corrupt, "NUL in OCR text"};
      ++i;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      n = 2;
      cp = c & 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 3;
      cp = c & 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 4;
      cp = c & 7;
    } else
      return Error{ErrorCode::Corrupt, "Invalid OCR UTF-8"};
    if (s.size() - i < n)
      return Error{ErrorCode::Corrupt, "Truncated OCR UTF-8"};
    for (unsigned k = 1; k < n; ++k) {
      auto b = static_cast<unsigned char>(s[i + k]);
      if ((b & 0xc0) != 0x80)
        return Error{ErrorCode::Corrupt, "Invalid OCR UTF-8 continuation"};
      cp = (cp << 6) | (b & 63);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
      return Error{ErrorCode::Corrupt, "Noncanonical OCR UTF-8"};
    i += n;
  }
  size_t end = 0, count = 0;
  while (end < s.size() && count < 10000) {
    auto n = grapheme_next_character_break_utf8(s.data() + end, s.size() - end);
    if (!n)
      return Error{ErrorCode::Corrupt, "Invalid OCR boundary"};
    end += n;
    ++count;
  }
  return s.substr(0, end);
}
Result<OcrValue> load_ocr(Store &s, const EntryId &id) {
  return detail::guarded<OcrValue>([&]() -> Result<OcrValue> {
    detail::Statement q(
        s.connection(),
        "SELECT ocr_state,substr(ocr_text,1,1048576),length(CAST(ocr_text AS "
        "BLOB)),ocr_text IS NOT NULL FROM history WHERE id=?");
    q.bind(1, id);
    if (!q.row())
      return Error{ErrorCode::NotFound, "OCR entry removed"};
    if (q.number(2) > 1048576)
      return Error{ErrorCode::TooLarge, "Stored OCR exceeds bound"};
    auto state = q.is_null(0)
                     ? (q.number(3) ? OcrState::Done : OcrState::Pending)
                     : OcrState(q.number(0));
    if (static_cast<unsigned>(state) > 3)
      return Error{ErrorCode::Corrupt, "Invalid OCR state"};
    return OcrValue{state, q.text(1)};
  });
}
Result<OcrTicket> begin_ocr(Store &s, const EntryId &id,
                            std::uint64_t generation, const CapturePolicy &p,
                            const Capabilities &c) {
  return detail::guarded<OcrTicket>([&]() -> Result<OcrTicket> {
    detail::Transaction tx(s.connection());
    if (generation != detail::erase_generation(s.connection()))
      return Error{ErrorCode::Busy, "OCR generation changed"};
    auto permitted = privacy(s, id, p, c);
    if (!permitted)
      return permitted.error();
    detail::Statement q(s.connection(),
                        "SELECT digest FROM history WHERE id=?");
    q.bind(1, id);
    if (!q.row())
      return Error{ErrorCode::NotFound, "OCR entry removed"};
    std::uint64_t nonce = 0;
    detail::Statement old(s.connection(),
                          "SELECT data FROM history_assets WHERE entry_id=? "
                          "AND kind='t16.ocr_request' AND length(data)<=256");
    old.bind(1, id);
    if (old.row()) {
      auto raw = old.bytes(0);
      std::string v(raw.begin(), raw.end());
      auto end = v.find(':');
      if (end == std::string::npos)
        return Error{ErrorCode::Corrupt, "Invalid OCR ticket"};
      auto r = std::from_chars(v.data(), v.data() + end, nonce);
      if (r.ec != std::errc{} || r.ptr != v.data() + end)
        return Error{ErrorCode::Corrupt, "Invalid OCR ticket nonce"};
    }
    if (nonce >= INT64_MAX)
      return Error{ErrorCode::TooLarge, "OCR ticket exhausted"};
    OcrTicket t{id, q.text(0),
                std::to_string(nonce + 1) + ":" + std::to_string(generation) +
                    ":" + q.text(0),
                generation};
    detail::Statement save(
        s.connection(),
        "INSERT OR REPLACE INTO history_assets VALUES(?,'t16.ocr_request',?)");
    save.bind(1, id);
    save.bind(2, Bytes(t.token.begin(), t.token.end()));
    save.done();
    detail::Statement mark(s.connection(),
                           "UPDATE history SET ocr_state=0 WHERE id=?");
    mark.bind(1, id);
    mark.done();
    detail::revision(s.connection());
    tx.commit();
    return t;
  });
}
Result<bool> finish_ocr(Store &s, const OcrTicket &t, Result<std::string> text,
                        const CapturePolicy &p, const Capabilities &c,
                        bool cancelled) {
  return detail::guarded<bool>([&]() -> Result<bool> {
    if (cancelled)
      return false;
    detail::Transaction tx(s.connection());
    if (t.generation != detail::erase_generation(s.connection()))
      return false;
    detail::Statement q(
        s.connection(),
        "SELECT h.digest,a.data FROM history h JOIN history_assets a ON "
        "a.entry_id=h.id AND a.kind='t16.ocr_request' WHERE h.id=? AND "
        "length(a.data)<=256");
    q.bind(1, t.id);
    if (!q.row() || q.text(0) != t.digest ||
        q.bytes(1) != Bytes(t.token.begin(), t.token.end()))
      return false;
    auto permitted = privacy(s, t.id, p, c);
    if (!permitted)
      return false;
    OcrState state = OcrState::Done;
    if (text) {
      auto clean = truncate_ocr_text(text.value());
      if (!clean)
        text = clean.error();
      else
        text = std::move(clean.value());
    }
    if (!text)
      state = text.error().code == ErrorCode::Unsupported
                  ? OcrState::Unavailable
                  : OcrState::Failed;
    detail::Statement save(
        s.connection(),
        text ? "UPDATE history SET ocr_state=?,ocr_text=? WHERE id=?"
             : "UPDATE history SET ocr_state=?,ocr_text=coalesce(ocr_text,?) "
               "WHERE id=?");
    save.bind(1, std::int64_t(state));
    if (text)
      save.bind(2, text.value());
    else
      save.null(2);
    save.bind(3, t.id);
    save.done();
    detail::revision(s.connection());
    tx.commit();
    return true;
  });
}
Result<void> recognize(Store &s, const EntryId &id, std::uint64_t generation,
                       const ImageInput &i, OcrBackend &engine,
                       const CapturePolicy &p, const Capabilities &c,
                       const std::atomic_bool &cancel) {
  if (cancel)
    return Error{ErrorCode::Busy, "OCR cancelled"};
  if (!i.image.width || !i.image.height ||
      std::uint64_t(i.image.width) * i.image.height > media_pixel_limit ||
      i.image.rgba.size() != std::uint64_t(i.image.width) * i.image.height * 4)
    return Error{ErrorCode::TooLarge, "Invalid or oversized OCR image"};
  auto ticket = begin_ocr(s, id, generation, p, c);
  if (!ticket)
    return ticket.error();
  auto languages = engine.languages();
  Result<std::string> result =
      Error{ErrorCode::Unsupported, "OCR language unavailable"};
  bool available = languages && !i.languages.empty() && i.languages.size() <= 2;
  if (available)
    for (auto &lang : i.languages)
      if (std::find(languages.value().begin(), languages.value().end(), lang) ==
          languages.value().end())
        available = false;
  if (available)
    result = engine.recognize_text(i, cancel);
  if (result)
    result = truncate_ocr_text(result.value());
  auto published = finish_ocr(s, ticket.value(), result, p, c, cancel);
  if (!published)
    return published.error();
  if (!published.value())
    return Error{ErrorCode::Busy, "OCR superseded, cancelled or removed"};
  if (!result)
    return result.error();
  return {};
}
} // namespace cliptiq

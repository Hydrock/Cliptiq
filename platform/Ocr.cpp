#include "Ocr.hpp"
#include "db.hpp"
#include <algorithm>
#include <future>
#include <set>
namespace cliptiq {
struct OcrService::Impl {
  Store &store;
  std::unique_ptr<OcrBackend> engine;
  OcrDecoder decode;
  std::atomic_bool cancelled{false};
  std::future<Result<std::string>> worker;
  std::optional<OcrTicket> ticket;
  std::optional<Result<std::string>> ready;
  std::chrono::steady_clock::time_point retry =
      std::chrono::steady_clock::now();
  std::set<EntryId> attempted;
  std::int64_t cursor = 0;
  std::uint64_t revision = UINT64_MAX;
  Impl(Store &s, std::unique_ptr<OcrBackend> e, OcrDecoder d)
      : store(s), engine(std::move(e)), decode(std::move(d)) {}
};
OcrService::OcrService(Store &s, std::unique_ptr<OcrBackend> e, OcrDecoder d)
    : impl_(new Impl(s, std::move(e), std::move(d))) {}
OcrService::~OcrService() {
  cancel();
  if (impl_->worker.valid())
    impl_->worker.wait();
}
void OcrService::retry_unavailable() {
  impl_->attempted.clear();
  impl_->cursor = 0;
  impl_->retry = std::chrono::steady_clock::now();
}
void OcrService::cancel() { impl_->cancelled = true; }
Result<bool> OcrService::poll(const CapturePolicy &policy,
                              const Capabilities &caps) {
  return detail::guarded<bool>([&]() -> Result<bool> {
    auto &s = *impl_;
    bool changed = false;
    if (s.worker.valid()) {
      if (s.worker.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready)
        return false;
      s.ready = s.worker.get();
    }
    if (s.ready) {
      auto done =
          finish_ocr(s.store, *s.ticket, *s.ready, policy, caps, s.cancelled);
      if (!done && done.error().code == ErrorCode::Busy)
        return false;
      if (done && !done.value())
        s.attempted.erase(s.ticket->id);
      s.ticket.reset();
      s.ready.reset();
      if (!done)
        return done.error();
      changed = done.value();
    }
    if (std::chrono::steady_clock::now() - s.retry >= std::chrono::seconds(60))
      retry_unavailable();
    if (s.cancelled || !policy.request_ocr ||
        caps.ocr.status == CapabilityStatus::Unavailable || !s.engine)
      return changed;
    auto revision = data_revision(s.store);
    if (!revision)
      return revision.error();
    if (revision.value() != s.revision) {
      s.cursor = 0;
      s.revision = revision.value();
    }
    detail::Statement candidates(
        s.store.connection(),
        "SELECT rowid,id FROM history WHERE rowid>? AND (ocr_state IS NULL AND "
        "ocr_text IS NULL OR ocr_state IN(0,2)) ORDER BY rowid LIMIT 32");
    candidates.bind(1, s.cursor);
    while (candidates.row()) {
      s.cursor = candidates.number(0);
      auto id = candidates.text(1);
      if (s.attempted.count(id))
        continue;
      auto snapshot = load_snapshot(s.store, id);
      if (!snapshot)
        return snapshot.error();
      auto accepted = evaluate_capture(snapshot.value(), policy, caps);
      if (accepted.disposition != CaptureDisposition::Accept)
        continue;
      auto image = std::find_if(accepted.snapshot->representations.begin(),
                                accepted.snapshot->representations.end(),
                                [](const Representation &r) {
                                  return r.kind == CanonicalKind::Image;
                                });
      if (image == accepted.snapshot->representations.end())
        continue;
      auto generation = history_erase_generation(s.store);
      if (!generation)
        return generation.error();
      auto ticket = begin_ocr(s.store, id, generation.value(), policy, caps);
      if (!ticket) {
        if (ticket.error().code == ErrorCode::Busy) {
          s.cursor = 0;
          return changed;
        }
        return ticket.error();
      }
      s.ticket = ticket.value();
      if (s.attempted.size() >= 100000)
        return Error{ErrorCode::TooLarge,
                     "OCR session attempt budget exceeded"};
      s.attempted.insert(id);
      s.worker = std::async(
          std::launch::async,
          [&s, representation = *image]() -> Result<std::string> {
            try {
              if (s.cancelled)
                return Error{ErrorCode::Busy, "OCR cancelled"};
              auto pixels = s.decode(representation);
              if (!pixels)
                return pixels.error();
              if (!pixels.value().width || !pixels.value().height ||
                  std::uint64_t(pixels.value().width) * pixels.value().height >
                      media_pixel_limit ||
                  pixels.value().rgba.size() !=
                      std::uint64_t(pixels.value().width) *
                          pixels.value().height * 4)
                return Error{ErrorCode::TooLarge, "Invalid OCR decoded image"};
              auto supported = s.engine->languages();
              if (!supported)
                return supported.error();
              std::vector<std::string> languages;
              for (auto language : {"eng", "rus"})
                if (std::find(supported.value().begin(),
                              supported.value().end(),
                              language) != supported.value().end())
                  languages.push_back(language);
              if (languages.empty())
                return Error{ErrorCode::Unsupported,
                             "EN/RU OCR languages unavailable"};
              if (s.cancelled)
                return Error{ErrorCode::Busy, "OCR cancelled"};
              return s.engine->recognize_text(
                  {std::move(pixels.value()), std::move(languages)},
                  s.cancelled);
            } catch (const std::exception &) {
              return Error{ErrorCode::Internal, "OCR engine failure"};
            }
          });
      break;
    }
    return changed;
  });
}
} // namespace cliptiq

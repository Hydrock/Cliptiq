#pragma once
#include "media.hpp"
#include <atomic>
namespace cliptiq {
enum class OcrState { Pending = 0, Done = 1, Unavailable = 2, Failed = 3 };
struct OcrValue {
  OcrState state;
  std::string text;
};
struct ImageInput {
  MenuImage image;
  std::vector<std::string> languages;
};
class OcrBackend {
public:
  virtual ~OcrBackend() = default;
  virtual Result<std::vector<std::string>> languages() = 0;
  virtual Result<std::string>
  recognize_text(const ImageInput &, const std::atomic_bool &cancel) = 0;
};
struct OcrTicket {
  EntryId id;
  std::string digest, token;
  std::uint64_t generation;
};
Result<std::string> truncate_ocr_text(const std::string &);
Result<OcrValue> load_ocr(Store &, const EntryId &);
Result<OcrTicket> begin_ocr(Store &, const EntryId &, std::uint64_t,
                            const CapturePolicy &, const Capabilities &);
Result<bool> finish_ocr(Store &, const OcrTicket &, Result<std::string>,
                        const CapturePolicy &, const Capabilities &,
                        bool cancelled);
Result<void> recognize(Store &, const EntryId &, std::uint64_t,
                       const ImageInput &, OcrBackend &, const CapturePolicy &,
                       const Capabilities &, const std::atomic_bool &cancel);
} // namespace cliptiq

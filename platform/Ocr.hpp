#pragma once
#include "cliptiq/ocr.hpp"
#include <functional>
#include <memory>
namespace cliptiq {
using OcrDecoder = std::function<Result<MenuImage>(const Representation &)>;
std::unique_ptr<OcrBackend> make_native_ocr(const std::string &model_directory);
// All Store access and privacy checks occur on caller thread. One bounded image
// is decoded/recognized on a worker; publication rechecks current policy.
class OcrService {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  OcrService(Store &, std::unique_ptr<OcrBackend>, OcrDecoder);
  ~OcrService();
  Result<bool> poll(const CapturePolicy &, const Capabilities &);
  void cancel();
  void retry_unavailable();
};
} // namespace cliptiq

#include "Ocr.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
namespace cliptiq {
namespace {
class TesseractOcr final : public OcrBackend {
  std::string directory;

public:
  explicit TesseractOcr(std::string d) : directory(std::move(d)) {}
  Result<std::vector<std::string>> languages() override {
    std::vector<std::string> result;
    for (auto name : {"eng", "rus"}) {
      auto p = std::filesystem::u8path(directory) /
               (std::string(name) + ".traineddata");
      std::error_code ec;
      auto st = std::filesystem::symlink_status(p, ec);
      if (ec || std::filesystem::is_symlink(st) ||
          !std::filesystem::is_regular_file(st))
        continue;
      auto expected = std::string(name) == "eng" ? 4113088u : 3861738u;
      if (std::filesystem::file_size(p, ec) != expected || ec)
        continue;
      std::ifstream file(p, std::ios::binary);
      detail::Sha256 hash;
      char block[16384];
      while (file) {
        file.read(block, sizeof(block));
        hash.update(block, static_cast<std::size_t>(file.gcount()));
      }
      if (file.bad())
        continue;
      auto digest = hash.finish();
      if (digest == (std::string(name) == "eng"
                         ? "7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a521"
                           "32556427170b2"
                         : "e16e5e036cce1d9ec2b00063cf8b54472625b9e14d893a169e2"
                           "b0dedeb4df225"))
        result.push_back(name);
    }
    return result;
  }
  Result<std::string> recognize_text(const ImageInput &input,
                                     const std::atomic_bool &cancel) override {
    if (cancel)
      return Error{ErrorCode::Busy, "OCR cancelled"};
    auto &image = input.image;
    if (!image.width || !image.height ||
        std::uint64_t(image.width) * image.height > media_pixel_limit ||
        image.rgba.size() != std::uint64_t(image.width) * image.height * 4)
      return Error{ErrorCode::TooLarge, "Invalid Tesseract image"};
    auto verified = this->languages();
    if (!verified)
      return verified.error();
    std::string languages;
    for (auto &name : input.languages) {
      if (std::find(verified.value().begin(), verified.value().end(), name) ==
          verified.value().end())
        return Error{ErrorCode::Unsupported, "Unknown Tesseract model"};
      if (!languages.empty())
        languages += '+';
      languages += name;
    }
    if (languages.empty())
      return Error{ErrorCode::Unsupported, "No Tesseract language"};
    tesseract::TessBaseAPI engine;
    if (engine.Init(directory.c_str(), languages.c_str(),
                    tesseract::OEM_LSTM_ONLY))
      return Error{ErrorCode::Unsupported, "Tesseract model unavailable"};
    engine.SetVariable("debug_file",
#ifdef _WIN32
                       "NUL"
#else
                       "/dev/null"
#endif
    );
    engine.SetPageSegMode(tesseract::PSM_AUTO);
    engine.SetImage(image.rgba.data(), image.width, image.height, 4,
                    image.width * 4);
    engine.SetSourceResolution(150);
    tesseract::ETEXT_DESC monitor;
    monitor.cancel = [](void *context, int) {
      return static_cast<const std::atomic_bool *>(context)->load();
    };
    monitor.cancel_this = const_cast<std::atomic_bool *>(&cancel);
    monitor.set_deadline_msecs(30000);
    if (engine.Recognize(&monitor))
      return Error{cancel ? ErrorCode::Busy : ErrorCode::Internal,
                   "Tesseract recognition failed or timed out"};
    if (cancel)
      return Error{ErrorCode::Busy, "OCR cancelled"};
    std::unique_ptr<char[]> text(engine.GetUTF8Text());
    if (!text)
      return Error{ErrorCode::Internal, "Tesseract returned no text"};
    auto length = strnlen(text.get(), 1048577);
    if (length > 1048576)
      return Error{ErrorCode::TooLarge, "Tesseract text exceeds bound"};
    return truncate_ocr_text(std::string(text.get(), length));
  }
};
} // namespace
std::unique_ptr<OcrBackend> make_native_ocr(const std::string &directory) {
  return std::make_unique<TesseractOcr>(directory);
}
} // namespace cliptiq

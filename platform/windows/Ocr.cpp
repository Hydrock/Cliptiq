#define WIN32_LEAN_AND_MEAN
#include "../Ocr.hpp"
#include <windows.h>

#include <appmodel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
namespace cliptiq {
namespace {
bool packaged() {
  UINT32 length = 0;
  return GetCurrentPackageFullName(&length, nullptr) ==
         ERROR_INSUFFICIENT_BUFFER;
}
struct Apartment {
  Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
  ~Apartment() { winrt::uninit_apartment(); }
};
class WindowsOcr final : public OcrBackend {
public:
  Result<std::vector<std::string>> languages() override {
    if (!packaged())
      return Error{ErrorCode::Unsupported,
                   "Windows OCR requires verified package identity"};
    try {
      Apartment apartment;
      std::vector<std::string> result;
      for (auto language : winrt::Windows::Media::Ocr::OcrEngine::
               AvailableRecognizerLanguages()) {
        auto tag = winrt::to_string(language.LanguageTag());
        if (tag.rfind("en", 0) == 0)
          result.push_back("eng");
        if (tag.rfind("ru", 0) == 0)
          result.push_back("rus");
      }
      return result;
    } catch (const winrt::hresult_error &) {
      return Error{ErrorCode::Unsupported, "Windows OCR language unavailable"};
    }
  }
  Result<std::string> recognize_text(const ImageInput &input,
                                     const std::atomic_bool &cancel) override {
    if (!packaged())
      return Error{ErrorCode::Unsupported,
                   "Windows OCR requires package identity"};
    if (cancel)
      return Error{ErrorCode::Busy, "OCR cancelled"};
    try {
      Apartment apartment;
      auto &image = input.image;
      auto maximum = winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
      if (!image.width || !image.height || image.width > maximum ||
          image.height > maximum ||
          std::uint64_t(image.width) * image.height > media_pixel_limit ||
          image.rgba.size() != std::uint64_t(image.width) * image.height * 4)
        return Error{ErrorCode::TooLarge,
                     "Windows OCR image exceeds engine limit"};
      // WinRT recognizes one language per engine; EN/RU passes are bounded and
      // concatenated, rather than claiming automatic bilingual detection
      // parity.
      Bytes bgra = image.rgba;
      for (size_t i = 0; i < bgra.size(); i += 4)
        std::swap(bgra[i], bgra[i + 2]);
      winrt::Windows::Storage::Streams::DataWriter writer;
      writer.WriteBytes(bgra);
      winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap(
          winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
          image.width, image.height,
          winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
      bitmap.CopyFromBuffer(writer.DetachBuffer());
      std::string result;
      for (auto &language : input.languages) {
        if (cancel)
          return Error{ErrorCode::Busy, "OCR cancelled"};
        if (language != "eng" && language != "rus")
          return Error{ErrorCode::Unsupported, "Unknown Windows OCR language"};
        auto engine =
            winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                winrt::Windows::Globalization::Language(
                    language == "eng" ? L"en-US" : L"ru-RU"));
        if (!engine)
          return Error{ErrorCode::Unsupported, "Windows OCR language missing"};
        auto operation = engine.RecognizeAsync(bitmap);
        while (operation.Status() ==
               winrt::Windows::Foundation::AsyncStatus::Started) {
          if (cancel) {
            operation.Cancel();
            return Error{ErrorCode::Busy, "OCR cancelled"};
          }
          Sleep(5);
        }
        auto text = winrt::to_string(operation.GetResults().Text());
        if (result.size() + text.size() + 1 > 1048576)
          return Error{ErrorCode::TooLarge, "Windows OCR text exceeds bound"};
        if (!result.empty())
          result += '\n';
        result += text;
      }
      return truncate_ocr_text(result);
    } catch (const winrt::hresult_error &) {
      return Error{ErrorCode::Internal, "Windows OCR failed"};
    }
  }
};
} // namespace
std::unique_ptr<OcrBackend> make_native_ocr(const std::string &) {
  return std::make_unique<WindowsOcr>();
}
} // namespace cliptiq

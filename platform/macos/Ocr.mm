#include "../Ocr.hpp"
#import <AppKit/AppKit.h>
#import <Vision/Vision.h>
#include <algorithm>
namespace cliptiq {
namespace {
class VisionOcr final : public OcrBackend {
public:
  Result<std::vector<std::string>> languages() override {
    @autoreleasepool {
      VNRecognizeTextRequest *request = [VNRecognizeTextRequest new];
      request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
      NSError *error = nil;
      NSArray<NSString *> *supported =
          [request supportedRecognitionLanguagesAndReturnError:&error];
      if (error)
        return Error{ErrorCode::Unsupported, "Vision languages unavailable"};
      std::vector<std::string> result;
      for (NSString *language in supported) {
        if ([language hasPrefix:@"en-"] &&
            std::find(result.begin(), result.end(), "eng") == result.end())
          result.push_back("eng");
        if ([language hasPrefix:@"ru-"] &&
            std::find(result.begin(), result.end(), "rus") == result.end())
          result.push_back("rus");
      }
      return result;
    }
  }
  Result<std::string> recognize_text(const ImageInput &input,
                                     const std::atomic_bool &cancel) override {
    @autoreleasepool {
      if (cancel)
        return Error{ErrorCode::Busy, "OCR cancelled"};
      auto &image = input.image;
      if (!image.width || !image.height ||
          std::uint64_t(image.width) * image.height > media_pixel_limit ||
          image.rgba.size() != std::uint64_t(image.width) * image.height * 4)
        return Error{ErrorCode::TooLarge, "Invalid Vision image"};
      CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
      CGDataProviderRef provider = CGDataProviderCreateWithData(
          nullptr, image.rgba.data(), image.rgba.size(), nullptr);
      CGImageRef cg =
          CGImageCreate(image.width, image.height, 8, 32, image.width * 4,
                        colors, kCGImageAlphaLast | kCGBitmapByteOrderDefault,
                        provider, nullptr, false, kCGRenderingIntentDefault);
      CGDataProviderRelease(provider);
      CGColorSpaceRelease(colors);
      if (!cg)
        return Error{ErrorCode::Internal, "Cannot create Vision image"};
      VNRecognizeTextRequest *request = [VNRecognizeTextRequest new];
      request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
      request.usesLanguageCorrection = YES;
      request.automaticallyDetectsLanguage = YES;
      NSMutableArray<NSString *> *languages = [NSMutableArray new];
      for (auto &language : input.languages) {
        if (language == "eng")
          [languages addObject:@"en-US"];
        else if (language == "rus")
          [languages addObject:@"ru-RU"];
        else {
          CGImageRelease(cg);
          return Error{ErrorCode::Unsupported, "Unknown Vision language"};
        }
      }
      request.recognitionLanguages = languages;
      VNImageRequestHandler *handler =
          [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
      CGImageRelease(cg);
      NSError *error = nil;
      BOOL ok = [handler performRequests:@[ request ] error:&error];
      if (cancel)
        return Error{ErrorCode::Busy, "OCR cancelled"};
      if (!ok)
        return Error{ErrorCode::Internal, "Vision recognition failed"};
      std::string result;
      for (VNRecognizedTextObservation *line in request.results) {
        NSString *text = [line topCandidates:1].firstObject.string;
        if (text) {
          auto length = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
          if (result.size() + length + 1 > 1048576)
            return Error{ErrorCode::TooLarge, "Vision text exceeds bound"};
          if (!result.empty())
            result += '\n';
          result += text.UTF8String;
        }
      }
      return truncate_ocr_text(result);
    }
  }
};
} // namespace
std::unique_ptr<OcrBackend> make_native_ocr(const std::string &) {
  return std::make_unique<VisionOcr>();
}
} // namespace cliptiq

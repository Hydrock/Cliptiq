#include "../Media.hpp"
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
namespace cliptiq {
Result<MenuImage> decode_native_image(const Representation &r) {
  @autoreleasepool {
    if (r.kind != CanonicalKind::Image || r.data.empty() ||
        r.data.size() > 32 * 1024 * 1024)
      return Error{ErrorCode::TooLarge, "Invalid encoded image"};
    NSData *data = [NSData dataWithBytes:r.data.data() length:r.data.size()];
    auto source =
        CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
    if (!source)
      return Error{ErrorCode::Corrupt, "ImageIO rejected image"};
    NSDictionary *props = CFBridgingRelease(
        CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr));
    auto w = [props[(__bridge NSString *)kCGImagePropertyPixelWidth]
             unsignedLongLongValue],
         h = [props[(__bridge NSString *)kCGImagePropertyPixelHeight]
             unsignedLongLongValue];
    if (!w || !h || w > media_pixel_limit || h > media_pixel_limit ||
        w * h > media_pixel_limit) {
      CFRelease(source);
      return Error{ErrorCode::TooLarge, "Image exceeds 40 MP"};
    }
    auto image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image)
      return Error{ErrorCode::Corrupt, "ImageIO decode failed"};
    MenuImage out{unsigned(w), unsigned(h), Bytes(size_t(w * h * 4))};
    auto space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto ctx = CGBitmapContextCreate(out.rgba.data(), w, h, 8, w * 4, space,
                                     kCGImageAlphaPremultipliedLast |
                                         kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!ctx) {
      CGImageRelease(image);
      return Error{ErrorCode::Io, "Cannot allocate image context"};
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), image);
    CGContextRelease(ctx);
    CGImageRelease(image);
    for (size_t p = 0; p < out.rgba.size(); p += 4) {
      auto a = out.rgba[p + 3];
      for (unsigned c = 0; c < 3; ++c)
        out.rgba[p + c] =
            a ? std::min(255u, unsigned(out.rgba[p + c]) * 255 / a) : 0;
    }
    return out;
  }
}
struct ScreenshotWatcher::Impl {
  NSMetadataQuery *query = nil;
  id observer = nil;
  std::function<void(std::string)> callback;
};
ScreenshotWatcher::ScreenshotWatcher() : impl_(new Impl) {}
ScreenshotWatcher::~ScreenshotWatcher() { stop(); }
void ScreenshotWatcher::stop() {
  if (impl_->observer)
    [[NSNotificationCenter defaultCenter] removeObserver:impl_->observer];
  impl_->observer = nil;
  [impl_->query stopQuery];
  impl_->query = nil;
  impl_->callback = {};
}
Result<void> ScreenshotWatcher::start(const std::string &root,
                                      std::function<void(std::string)> cb) {
  stop();
  auto state = impl_.get();
  state->callback = std::move(cb);
  state->query = [NSMetadataQuery new];
  state->query.predicate =
      [NSPredicate predicateWithFormat:@"kMDItemIsScreenCapture = 1"];
  state->query.searchScopes = @[ [NSString stringWithUTF8String:root.c_str()] ];
  NSString *selectedRoot = [NSString stringWithUTF8String:root.c_str()];
  state->observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSMetadataQueryDidUpdateNotification
                  object:state->query
                   queue:[NSOperationQueue mainQueue]
              usingBlock:^(NSNotification *n) {
                for (NSString *key in @[
                       NSMetadataQueryUpdateAddedItemsKey,
                       NSMetadataQueryUpdateChangedItemsKey
                     ])
                  for (NSMetadataItem *item in n.userInfo[key]) {
                    NSString *path =
                        [item valueForAttribute:NSMetadataItemPathKey];
                    if (path &&
                        [path.stringByDeletingLastPathComponent
                            isEqualToString:selectedRoot] &&
                        state->callback)
                      state->callback(path.lastPathComponent.UTF8String);
                  }
              }];
  if (![state->query startQuery]) {
    stop();
    return Error{ErrorCode::Io, "Screenshot metadata query unavailable"};
  }
  return {};
}
} // namespace cliptiq

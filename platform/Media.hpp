#pragma once
#include "cliptiq/media.hpp"
#include "cliptiq/settings.hpp"
#include <functional>
#include <memory>
namespace cliptiq {
Result<MenuImage> decode_native_image(const Representation &);
// Native callbacks carry a basename, never trusted arbitrary paths.
class ScreenshotWatcher {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  ScreenshotWatcher();
  ~ScreenshotWatcher();
  Result<void> start(const std::string &directory,
                     std::function<void(std::string)>);
  void stop();
};
class MediaService {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  MediaService(Store &, CapturePlatform);
  ~MediaService();
  Result<void> configure(const Settings &, bool isolated = false);
  void stop();
  // GUI thread: bounded SQL work and publication; codecs/file reads stay on
  // worker.
  Result<bool> poll();
  std::optional<MenuImage> preview(const PreviewRef &) const;
  // GUI thread. Only the selected image is decoded; null releases/cancels it.
  std::optional<MenuImage> workspace_preview(std::optional<PreviewRef>);
  Capability observation_status() const;
  Result<void> on_screenshot_file(const std::string &basename);
};
} // namespace cliptiq

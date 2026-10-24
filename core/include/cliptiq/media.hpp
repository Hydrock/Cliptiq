#pragma once
#include "history.hpp"
#include "native_menu.hpp"
#include "policy.hpp"
namespace cliptiq {
struct PreviewSize {
  unsigned width = 100, height = 32;
};
struct PreviewTicket {
  EntryId id;
  std::string digest;
  PreviewSize size;
  std::uint64_t generation = 0;
};
constexpr std::uint64_t media_pixel_limit = 40000000;
constexpr std::size_t media_cache_limit = 16 * 1024 * 1024;
Result<PreviewTicket> request_preview(Store &, const EntryId &, PreviewSize,
                                      std::uint64_t generation);
Result<bool> store_preview_if_present(Store &, const PreviewTicket &,
                                      const MenuImage &);
Result<std::optional<MenuImage>> load_preview(Store &, const EntryId &,
                                              PreviewSize);
Result<MenuImage> fit_preview(const MenuImage &, PreviewSize);
Result<std::string> image_fingerprint(const MenuImage &);
// Only screenshot-origin entries may be removed by pixel deduplication.
Result<void> store_image_fingerprint(Store &, const PreviewTicket &,
                                     const std::string &);
Capabilities screenshot_capabilities(CapturePlatform);
ClipboardSnapshot screenshot_snapshot(CapturePlatform, Bytes,
                                      const std::string &version);
// Complete bounded PNG framing/CRC check, before invoking a system codec.
Result<void> validate_screenshot_png(const Bytes &);
} // namespace cliptiq

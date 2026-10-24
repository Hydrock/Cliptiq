#include "cliptiq/native_menu.hpp"
#include <algorithm>
namespace cliptiq {
std::optional<MenuImage> menu_preview(const MenuItem &item,
                                      const NativeMenuOptions &o,
                                      const MenuPreviewProvider &provider) {
  if (!item.preview || !o.preview_width || !o.preview_height ||
      o.preview_width > 512 || o.preview_height > 512)
    return {};
  MenuImage source;
  if (item.preview->rgba) {
    auto c = *item.preview->rgba;
    source = {16, 16, Bytes(16 * 16 * 4)};
    for (size_t i = 0; i < source.rgba.size(); i += 4) {
      source.rgba[i] = c >> 24;
      source.rgba[i + 1] = c >> 16;
      source.rgba[i + 2] = c >> 8;
      source.rgba[i + 3] = c;
    }
  } else {
    if (!provider)
      return {};
    try {
      auto image = provider(*item.preview);
      if (!image)
        return {};
      source = std::move(*image);
    } catch (...) {
      return {};
    }
  }
  if (!source.width || !source.height || source.width > 512 ||
      source.height > 512 ||
      source.rgba.size() != size_t(source.width) * source.height * 4)
    return {};
  double scale = std::min({1., double(o.preview_width) / source.width,
                           double(o.preview_height) / source.height});
  unsigned width = std::max(1u, unsigned(source.width * scale)),
           height = std::max(1u, unsigned(source.height * scale));
  MenuImage out{width, height, Bytes(size_t(width) * height * 4)};
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      auto a = (size_t(y) * width + x) * 4;
      auto b = (size_t(y) * source.height / height * source.width +
                size_t(x) * source.width / width) *
               4;
      std::copy_n(source.rgba.data() + b, 4, out.rgba.data() + a);
    }
  return out;
}
MenuImage scale_menu_image(const MenuImage &source, unsigned dpi) {
  if (!source.width || !source.height || source.width > 512 ||
      source.height > 512 ||
      source.rgba.size() != size_t(source.width) * source.height * 4 ||
      dpi < 48 || dpi > 384)
    return {};
  unsigned width = std::min(512u, std::max(1u, source.width * dpi / 96)),
           height = std::min(512u, std::max(1u, source.height * dpi / 96));
  MenuImage out{width, height, Bytes(size_t(width) * height * 4)};
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      size_t a = (size_t(y) * width + x) * 4,
             b = (size_t(y) * source.height / height * source.width +
                  size_t(x) * source.width / width) *
                 4;
      std::copy_n(source.rgba.data() + b, 4, out.rgba.data() + a);
    }
  return out;
}
} // namespace cliptiq

#include "cliptiq/native_menu.hpp"
#include <algorithm>
namespace cliptiq {
MenuImage tray_icon(unsigned size){
  size=std::clamp(size,16u,64u);
  MenuImage icon{size,size,Bytes(size_t(size)*size*4)};
  // One clipboard silhouette with light paper and a dark outline. Both tones
  // are always present, so no theme detection or alternate asset is needed.
  for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){
    unsigned coverage=0,color=0;
    for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx){
      double u=(x+(sx+.5)/4)*16/size,v=(y+(sy+.5)/4)*16/size;
      auto rect=[&](double l,double t,double r,double b){return u>=l&&u<r&&v>=t&&v<b;};
      bool body=rect(3,3,13,15)&&!(rect(3,3,4,4)||rect(12,3,13,4)||rect(3,14,4,15)||rect(12,14,13,15));
      bool clip=rect(5,1,11,5);
      if(!body&&!clip)continue;
      bool paper=rect(4,4,12,14);
      bool line=rect(6,7,10,8)||rect(6,9,10,10)||rect(6,11,9,12);
      bool light=clip?rect(6,2,10,4):(paper&&!line);
      color+=light?248:38;++coverage;
    }
    if(coverage){auto p=(size_t(y)*size+x)*4;icon.rgba[p]=icon.rgba[p+1]=icon.rgba[p+2]=static_cast<unsigned char>(color/coverage);icon.rgba[p+3]=static_cast<unsigned char>((coverage*255+8)/16);}
  }
  return icon;
}
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

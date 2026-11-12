#include "cliptiq/native_menu.hpp"
#include <iostream>
#include <fstream>
using namespace cliptiq;
int main(int argc,char** argv) {
  if(argc==3&&std::string(argv[1])=="--tray-rgba"){
    auto icon=tray_icon(64);std::ofstream out(argv[2],std::ios::binary);
    out.write(reinterpret_cast<const char*>(icon.rgba.data()),icon.rgba.size());
    return out?0:1;
  }
  if(argc==3&&std::string(argv[1])=="--tray-sheet"){
    std::ofstream out(argv[2],std::ios::binary);out<<"P6\n320 240\n255\n";
    for(unsigned y=0;y<240;++y){
      auto size=y<80?16u:y<160?24u:32u;auto icon=tray_icon(size);
      for(unsigned x=0;x<320;++x){
        unsigned bg=x<160?248:27;int ix=(int(x%160)-int(160-size*2)/2)/2,iy=(int(y%80)-int(80-size*2)/2)/2;
        unsigned a=0,c=0;
        if(ix>=0&&iy>=0&&ix<int(size)&&iy<int(size)){auto p=(size_t(iy)*size+ix)*4;a=icon.rgba[p+3];c=icon.rgba[p];}
        char value=static_cast<char>((c*a+bg*(255-a))/255);out.write(&value,1);out.write(&value,1);out.write(&value,1);
      }
    }
    return out?0:1;
  }
  int fails = 0;
  auto test = [&](const char *n, bool value) {
    std::cout << (value ? "PASS " : "FAIL ") << n << "\n";
    if (!value)
      ++fails;
  };
  MenuItem item;
  for(auto size:{16u,20u,24u,32u}){
    auto icon=tray_icon(size);unsigned dark=0,light=0,transparent=0;
    for(size_t i=0;i<icon.rgba.size();i+=4){
      if(icon.rgba[i+3]==0)++transparent;
      if(icon.rgba[i+3]>240&&icon.rgba[i]<70)++dark;
      if(icon.rgba[i+3]>240&&icon.rgba[i]>220)++light;
    }
    test("tray_icon_size",icon.width==size&&icon.height==size&&icon.rgba.size()==size_t(size)*size*4);
    test("tray_icon_contrast_on_both_themes",dark>=10&&light>=15&&transparent>size);
  }
  item.preview = PreviewRef{"id", 0xff8800ff};
  auto color = menu_preview(item, {}, {});
  test("swatch_rgba",
       color && color->width && color->rgba[0] == 255 && color->rgba[1] == 136);
  item.preview->rgba.reset();
  auto fit =
      menu_preview(item, {}, [](const auto &) -> std::optional<MenuImage> {
        return MenuImage{200, 200, Bytes(200 * 200 * 4, 255)};
      });
  test("aspect_fit_32", fit && fit->width == 32 && fit->height == 32);
  test("invalid_provider_dimensions_rejected",
       !menu_preview(item, {}, [](const auto &) -> std::optional<MenuImage> {
         return MenuImage{100000, 100000, {}};
       }));
  test("short_provider_buffer_rejected",
       !menu_preview(item, {}, [](const auto &) -> std::optional<MenuImage> {
         return MenuImage{2, 2, {1}};
       }));
  test(
      "empty_reference_no_provider",
      !menu_preview(MenuItem{}, {},
                    [](const auto &) -> std::optional<MenuImage> { throw 1; }));
  for (auto dpi : {96u, 144u, 192u}) {
    MenuImage source{32, 16, Bytes(32 * 16 * 4, 255)};
    auto scaled = scale_menu_image(source, dpi);
    test((std::string("preview_dpi_") + std::to_string(dpi)).c_str(),
         scaled.width == 32 * dpi / 96 && scaled.height == 16 * dpi / 96 &&
             scaled.rgba.size() == size_t(scaled.width) * scaled.height * 4);
  }
  return fails ? 1 : 0;
}

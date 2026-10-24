#include "cliptiq/native_menu.hpp"
#include <iostream>
using namespace cliptiq;
int main() {
  int fails = 0;
  auto test = [&](const char *n, bool value) {
    std::cout << (value ? "PASS " : "FAIL ") << n << "\n";
    if (!value)
      ++fails;
  };
  MenuItem item;
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

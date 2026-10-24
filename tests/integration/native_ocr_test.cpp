#include "Media.hpp"
#include "Ocr.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
int main(int argc, char **argv) {
  try {
    CHECK(argc >= 3);
    auto engine = make_native_ocr(argv[1]);
    auto languages = engine->languages();
#ifdef _WIN32
#ifdef CLIPTIQ_OCR_PACKAGED_TEST
    // Hosted portable CI has no package identity. Verify the negative boundary.
    CHECK(!languages);
    CHECK(languages.error().code == ErrorCode::Unsupported);
    std::cout << "portable identity correctly rejected; positive packaged "
                 "acceptance OPEN\n";
    return 0;
#endif
#endif
    CHECK(languages);
    std::atomic_bool cancel{false};
    int recognized = 0;
    for (auto language : {"eng", "rus"}) {
      if (std::find(languages.value().begin(), languages.value().end(),
                    language) == languages.value().end()) {
        std::cout << language << ": unavailable on this OS\n";
        continue;
      }
      std::ifstream file(std::string(argv[2]) + "/" + language + ".png",
                         std::ios::binary);
      Bytes bytes((std::istreambuf_iterator<char>(file)), {});
      CHECK(!bytes.empty());
#ifdef _WIN32
      const char *platform = "windows";
      const char *type = "PNG";
#elif defined(__APPLE__)
      const char *platform = "macos";
      const char *type = "public.png";
#else
      const char *platform = "x11";
      const char *type = "image/png";
#endif
      auto pixels = take(decode_native_image(
          {0, platform, type, CanonicalKind::Image, std::move(bytes)}));
      auto text = take(engine->recognize_text({pixels, {language}}, cancel));
      auto expected = std::string(language) == "eng" ? "clipboard" : "Привет";
      std::cerr<<language<<": fixture result: "<<text<<"\n";
    CHECK(text.find(expected) != std::string::npos);
      ++recognized;
      std::cout << language << ": recognized\n";
    }
    CHECK(recognized > 0);
    cancel = true;
    CHECK(!engine->recognize_text({}, cancel));
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

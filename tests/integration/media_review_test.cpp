#include "../../platform/Media.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
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
#ifdef _WIN32
    auto platform = CapturePlatform::Windows;
#elif defined(__APPLE__)
    auto platform = CapturePlatform::MacOS;
#else
    auto platform = CapturePlatform::X11;
#endif
    auto root =
        std::filesystem::temp_directory_path() /
        ("cliptiq-review-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    auto dir = root / "selected";
    std::filesystem::create_directory(dir);
    auto db = take(open_store(":memory:"));
    MediaService media(db, platform);
    auto prefs = default_settings(platform);
    prefs.values["screenshots"] = "1";
    prefs.values["screenshot_directory"] = dir.u8string();
    CHECK(media.configure(prefs));
    auto pump = [&](int ms) {
      for (int n = 0; n < ms / 10; ++n) {
        auto result = media.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    };
    std::string mode = argc > 1 ? argv[1] : "clear";
    if (mode == "clear") {
      std::filesystem::copy_file(std::filesystem::u8path(MEDIA_FIXTURES) /
                                     "red.png",
                                 dir / "pending.png");
      CHECK(media.on_screenshot_file("pending.png"));
      CHECK(erase_history(db));
      pump(900);
      CHECK(take(list_history(db)).empty());
    } else if (mode == "root") {
      CHECK(media.on_screenshot_file("pending.png"));
      std::filesystem::rename(dir, root / "original");
      std::filesystem::create_directory(dir);
      std::filesystem::copy_file(std::filesystem::u8path(MEDIA_FIXTURES) /
                                     "red.png",
                                 dir / "pending.png");
      pump(900);
      CHECK(take(list_history(db)).empty());
    } else if (mode == "error") {
      std::thread fail([&] { CHECK(media.on_screenshot_file("")); });
      pump(100);
      fail.join();
      auto status = media.observation_status();
      CHECK(status.status == CapabilityStatus::Unavailable);
      CHECK(status.reason.find("restart") != std::string::npos);
      pump(100);
      CHECK(media.observation_status().status == CapabilityStatus::Unavailable);
    }
    media.stop();
    std::filesystem::remove_all(root);
    std::cout << "PASS " << mode << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

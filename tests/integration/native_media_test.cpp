#include "../../platform/Media.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <glib.h>
#endif
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
Bytes read(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), {});
}
void write(const std::filesystem::path &p, const Bytes &b) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(b.data()), b.size());
}
int main() {
  std::filesystem::path root;
  try {
#ifdef _WIN32
    auto platform = CapturePlatform::Windows;
#elif defined(__APPLE__)
    auto platform = CapturePlatform::MacOS;
#else
    auto platform = CapturePlatform::X11;
#endif
    auto bytes = read(std::filesystem::u8path(MEDIA_FIXTURES) / "red.png");
    CHECK(validate_screenshot_png(bytes));
    auto snapshot = screenshot_snapshot(platform, bytes, "fixture");
    auto image = take(decode_native_image(snapshot.representations[0]));
    CHECK(image.width == 2 && image.height == 4 && image.rgba[0] == 255 &&
          image.rgba[3] == 255);
    auto malformed = snapshot.representations[0];
    malformed.data = {1, 2, 3};
    CHECK(!decode_native_image(malformed));
    malformed.data =
        read(std::filesystem::u8path(MEDIA_FIXTURES) / "oversized.png");
    CHECK(!validate_screenshot_png(malformed.data));
    CHECK(!decode_native_image(malformed));
    auto partial = bytes;
    partial.resize(partial.size() / 2);
    CHECK(!validate_screenshot_png(partial));
    auto badcrc = bytes;
    badcrc[20] ^= 1;
    CHECK(!validate_screenshot_png(badcrc));
    root = std::filesystem::temp_directory_path() /
           ("cliptiq-media-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));
    {
      ScreenshotWatcher watcher;
      std::atomic<unsigned> events{0};
      CHECK(watcher.start(root.u8string(), [&](std::string name) {
        if (name == "watch.png" || name == "renamed.png")
          ++events;
      }));
#ifndef __APPLE__
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      write(root / "watch.png", bytes);
      for (unsigned n = 0; n < 100 && !events; ++n) {
#ifndef _WIN32
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      CHECK(events > 0);
      auto before = events.load();
      std::filesystem::rename(root / "watch.png", root / "renamed.png");
      for (unsigned n = 0; n < 100 && events == before; ++n) {
#ifndef _WIN32
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      CHECK(events > before);
#endif
      watcher.stop();
    }
    auto db = take(open_store(":memory:"));
    MediaService service(db, platform);
    auto settings = default_settings(platform);
    CHECK(settings.values.at("screenshots") == "0");
    CHECK(service.configure(settings));
    CHECK(service.on_screenshot_file("a.png"));
    settings.values["screenshots"] = "1";
    settings.values["screenshot_directory"] = root.u8string();
    CHECK(service.configure(settings));
    CHECK(!service.on_screenshot_file("../outside.png"));
    CHECK(!service.on_screenshot_file("file.txt"));
    auto pump = [&](int ms) {
      for (int n = 0; n < ms / 10; ++n) {
#if !defined(_WIN32) && !defined(__APPLE__)
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
#endif
        CHECK(service.poll());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    };
    write(root / "growing.png", partial);
    CHECK(service.on_screenshot_file("growing.png"));
    pump(150);
    CHECK(take(list_history(db)).empty());
    write(root / "growing.png", bytes);
    pump(900);
    CHECK(take(list_history(db)).size() == 1);
    auto id = take(list_history(db))[0].id;
    pump(200);
    CHECK(service.preview({id, {}}));
    CHECK(service.on_screenshot_file("growing.png"));
    pump(700);
    CHECK(take(list_history(db)).size() == 1);
    // Different encoded origin; pixel equality removes only screenshot records.
    auto clipboard = snapshot;
    clipboard.sequence_token = "native:2";
    clipboard.representations[0].data =
        read(std::filesystem::u8path(MEDIA_FIXTURES) / "red-metadata.png");
    auto cid = *take(ingest(db, clipboard, {}, 2));
    pump(400);
    auto records = take(list_history(db));
    CHECK(records.size() == 1 && records[0].id == cid);
    CHECK(service.on_screenshot_file("second.png"));
    write(root / "second.png", bytes);
    pump(800);
    records = take(list_history(db));
    CHECK(records.size() == 1 && records[0].id == cid);
    // Delete/clear while worker results are pending must never resurrect
    // history.
    CHECK(erase_history(db));
    auto pending = *take(ingest(db, clipboard, {}, 3));
    CHECK(service.poll());
    CHECK(erase_history(db, pending));
    pump(400);
    CHECK(take(list_history(db)).empty());
    CHECK(!service.preview({pending, {}}));
    settings.values["excluded"] = "private.app";
    CHECK(service.configure(settings));
    write(root / "private.png", bytes);
    CHECK(service.on_screenshot_file("private.png"));
    pump(600);
    CHECK(take(list_history(db)).empty());
    settings.values["excluded"] = "";
    settings.values["type_image"] = "0";
    CHECK(service.configure(settings));
    CHECK(service.on_screenshot_file("private.png"));
    pump(400);
    CHECK(take(list_history(db)).empty());
    settings.values["type_image"] = "1";
    CHECK(service.configure(settings));
    CHECK(service.on_screenshot_file("private.png"));
    service.stop();
    pump(400);
    CHECK(take(list_history(db)).empty());
    // Queue is bounded even if a missing/growing file occupies the worker.
    CHECK(service.configure(settings));
    unsigned busy = 0;
    for (unsigned n = 0; n < 100; ++n)
      if (!service.on_screenshot_file("missing" + std::to_string(n) + ".png"))
        ++busy;
    CHECK(busy >= 35);
    service.stop();
    // Symlinks must never escape the selected directory (Windows may deny
    // creating them).
    std::error_code ec;
    std::filesystem::create_symlink(std::filesystem::u8path(MEDIA_FIXTURES) /
                                        "red.png",
                                    root / "link.png", ec);
    if (!ec) {
      CHECK(service.configure(settings));
      CHECK(service.on_screenshot_file("link.png"));
      pump(500);
      CHECK(take(list_history(db)).empty());
      service.stop();
    }
    settings.values["screenshot_directory"] = (root / "missing").u8string();
    CHECK(!service.configure(settings));
    service.stop();
    std::filesystem::remove_all(root);
    std::cout << "PASS native codec, malformed/40MP, partial write, event "
                 "coalescing, pixel dedup both orders, privacy "
                 "exclusions/types, stop, deletion race\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

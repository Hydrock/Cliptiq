#include "Ocr.hpp"
#include <iostream>
#include <stdexcept>
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
struct Engine : OcrBackend {
  Result<std::vector<std::string>> languages() override {
    return std::vector<std::string>{"eng"};
  }
  Result<std::string> recognize_text(const ImageInput &,
                                     const std::atomic_bool &) override {
    return std::string("queued text");
  }
};
int main() {
  try {
    auto store = take(open_store(":memory:"));
    auto caps = screenshot_capabilities(CapturePlatform::X11);
    caps.ocr.status = CapabilityStatus::Available;
    CapturePolicy p;
    p.request_ocr = true;
    auto id = *take(ingest(
        store, screenshot_snapshot(CapturePlatform::X11, {1}, "queue"), {}, 1));
    std::atomic_int decoded{0};
    OcrService queue(store, std::make_unique<Engine>(),
                     [&](const Representation &) -> Result<MenuImage> {
                       ++decoded;
                       return MenuImage{1, 1, {255, 255, 255, 255}};
                     });
    CHECK(queue.poll(p, caps));
    p.request_ocr = false;
    for (int n = 0; n < 100; ++n) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      CHECK(queue.poll(p, caps));
    }
    CHECK(decoded == 1);
    CHECK(take(load_ocr(store, id)).state == OcrState::Pending);
    CHECK(take(load_ocr(store, id)).text.empty());
    CHECK(erase_history(store));
    CHECK(queue.poll(p, caps));
    CHECK(take(list_history(store)).empty());
    std::cout << "async privacy revocation prevents publication\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

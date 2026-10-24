#include "cliptiq/history.hpp"
#include "cliptiq/policy.hpp"
#ifdef CLIPTIQ_WITH_LINUX_BACKEND
#include "Application.hpp"
#include <atomic>
#include <thread>
#endif
#include <cstdlib>
using namespace cliptiq;
struct Sink : CaptureSink {
  Store &db;
  explicit Sink(Store &s) : db(s) {}
  Result<std::optional<EntryId>> store(const ClipboardSnapshot &s) override {
    return ingest(db, s, {}, 1);
  }
  void thumbnail(const EntryId &, const ClipboardSnapshot &) override {}
  void ocr(const EntryId &, const ClipboardSnapshot &) override {}
};
int main(int argc, char **argv) {
  if (argc != 2 || !getenv("CLIPTIQ_TEST_ISOLATED") ||
      std::string(getenv("CLIPTIQ_TEST_ISOLATED")) != argv[1])
    return 2;
  auto opened = open_store(":memory:");
  if (!opened)
    return 3;
  auto &db = opened.value();
  Sink sink(db);
  ClipboardSnapshot snapshot;
  bool x = std::string(argv[1]) == "x11";
  snapshot.representations = {{0,
                               x ? "x11" : "wayland",
                               x ? "UTF8_STRING" : "text/plain;charset=utf-8",
                               CanonicalKind::Text,
                               {'h', 'i'}}};
  Capabilities caps;
#ifdef CLIPTIQ_WITH_LINUX_BACKEND
  using namespace cliptiq::linux_backend;
  auto make = [&]() -> std::unique_ptr<LinuxClipboardBackend> {
    if (x)
      return std::make_unique<ClipboardX11>();
    return std::make_unique<ClipboardWayland>();
  };
  std::atomic<bool> ready{false}, done{false}, failed{false};
  std::thread source([&] {
    auto b = make();
    failed = !b->write_snapshot(snapshot);
    ready = true;
    while (!done) {
      if (!b->dispatch(2))
        failed = true;
    }
  });
  while (!ready)
    std::this_thread::yield();
  bool captured = false;
  int result = 0;
  {
    LinuxCaptureSession session(make(), ":memory:");
    caps = session.clipboard().capabilities();
    if (!session.start({}, {}, [&](Result<CaptureEvent> r) {
          captured = bool(r) && r.value().entry_id.has_value();
        }))
      result = 4;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!result && !captured && std::chrono::steady_clock::now() < end)
      if (!session.dispatch(5))
        result = 5;
    session.stop();
  }
  done = true;
  source.join();
  if (result || failed || !captured)
    return 6;
#else
  caps.platform = x ? CapturePlatform::X11 : CapturePlatform::Wayland;
  caps.capture = {CapabilityStatus::Available, "fixture"};
  caps.native_types.push_back({snapshot.representations[0].platform,
                               snapshot.representations[0].native_type,
                               CanonicalKind::Text, TextEncoding::Utf8});
#endif
  auto decision = dispatch_capture(snapshot, {}, caps, sink);
  if (!decision || decision.value().disposition != CaptureDisposition::Accept)
    return 7;
  auto rows = list_history(db);
  if (!rows || rows.value().empty())
    return 8;
  auto id = rows.value()[0].id;
  if (!load_snapshot(db, id) || !touch_history(db, id, 2) ||
      !data_revision(db) || !tick_cleanup(db, {300000}, 0) ||
      !erase_history(db) || !compact_store(db))
    return 9;
  return 0;
}

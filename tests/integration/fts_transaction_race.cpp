#include "cliptiq/index.hpp"
#include "cliptiq/history.hpp"
#include "sqlite3.h"
#include "index_gate.hpp"
#include <condition_variable>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
using namespace cliptiq;
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
struct Barrier {
  std::mutex mutex;
  std::condition_variable wake;
  bool entered = false, release = false;
};
int main(int argc, char **) {
  try {
    auto root =
        std::filesystem::temp_directory_path() /
        ("cliptiq-fts-race-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    auto db = (root / "history.sqlite3").u8string();
    auto writer = take(open_store(db));
    auto reader = take(open_store(db));
    ClipboardSnapshot source;
    source.sequence_token = "race";
    source.representations.push_back({0,
                                      "x11",
                                      "UTF8_STRING",
                                      CanonicalKind::Text,
                                      {'s', 'e', 'c', 'r', 'e', 't'}});
    take(ingest(writer, source, {}, 1));
    auto path = take(prepare_index_namespace(writer));
    auto revision = take(data_revision(writer));
    if (!rebuild_index(reader, path, revision))
      throw std::runtime_error("seed rebuild failed");
    if (argc == 1) {
      Barrier barrier;
      sqlite3_set_authorizer(
          writer.connection(),
          [](void *context, int action, const char *table, const char *,
             const char *, const char *) {
            auto &b = *static_cast<Barrier *>(context);
            if (action == SQLITE_UPDATE && table &&
                std::string(table) == "store_state") {
              std::unique_lock<std::mutex> lock(b.mutex);
              b.entered = true;
              b.wake.notify_all();
              b.wake.wait(lock, [&] { return b.release; });
            }
            return SQLITE_OK;
          },
          &barrier);
      auto deletion =
          std::async(std::launch::async, [&] { return erase_history(writer); });
      {
        std::unique_lock<std::mutex> lock(barrier.mutex);
        if (!barrier.wake.wait_for(lock, std::chrono::seconds(10),
                                   [&] { return barrier.entered; }))
          throw std::runtime_error("writer did not reach barrier");
      }
      auto rebuild = std::async(std::launch::async, [&] {
        return rebuild_index(reader, path, revision);
      });
      auto published_before_commit =
          rebuild.wait_for(std::chrono::seconds(1)) ==
          std::future_status::ready;
      {
        std::lock_guard<std::mutex> lock(barrier.mutex);
        barrier.release = true;
      }
      barrier.wake.notify_all();
      auto erased = deletion.get();
      auto rebuilt = rebuild.get();
      sqlite3_set_authorizer(writer.connection(), nullptr, nullptr);
      if (!erased)
        throw std::runtime_error(erased.error().message);
      if (published_before_commit || rebuilt ||
          std::filesystem::exists(std::filesystem::u8path(path)))
        throw std::runtime_error(
            "rebuild published deleted payload before writer commit");
    }
    // GUI-side mutations must fail promptly rather than wait for publication.
    auto held = std::make_unique<detail::IndexGate>(path);
    auto mutation =
        std::async(std::launch::async, [&] { return erase_history(writer); });
    auto prompt = mutation.wait_for(std::chrono::milliseconds(150)) ==
                  std::future_status::ready;
    held.reset();
    auto blocked = mutation.get();
    if (!prompt || blocked || blocked.error().code != ErrorCode::Busy)
      throw std::runtime_error("main mutation blocked on index publication");
    std::cout << "index gate spans complete main deletion transaction\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

#include "../../platform/LifecycleNative.hpp"
#include "cliptiq/lifecycle.hpp"
#include "cliptiq/locale.hpp"
#include "cliptiq/menu.hpp"
#include "cliptiq/storage.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
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
    if (argc >= 4 && (std::string(argv[1]) == "--serve" ||
                      std::string(argv[1]) == "--forward")) {
      SessionInstance instance;
      auto role = take(instance.start({argv[2], "process"},
                                      std::string(argv[1]) == "--forward"
                                          ? take(parse_launch_action(argv[3]))
                                          : LaunchAction::Main));
      if (std::string(argv[1]) == "--forward") {
        CHECK(role == InstanceRole::Forwarded);
        return 0;
      }
      CHECK(role == InstanceRole::Primary);
      std::ofstream(argv[3]) << "ready";
      unsigned count = 0;
      for (unsigned n = 0; n < 200 && count < 4; ++n) {
        auto events = take(instance.receive());
        for (auto action : events) {
          std::ofstream(std::filesystem::u8path(argv[2]) / "events",
                        std::ios::app)
              << launch_action_name(action) << '\n';
          ++count;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      CHECK(count == 4);
      return 0;
    }
    CHECK(take(parse_launch_action("settings")) == LaunchAction::Settings);
    CHECK(!parse_launch_action("clear"));
    CHECK(!parse_launch_action("/bin/sh"));
    CHECK(!parse_launch_action("settings;quit"));
    CHECK(activate_locale("zz-ZZ") == "en");
    CHECK(tr("Settings") == "Settings");
    CHECK(activate_locale("ru_RU.UTF-8") == "ru");
    CHECK(tr("Settings") == u8"Настройки");
    CHECK(tr("unknown.key") == "unknown.key");
    CHECK(activate_locale("de") == "de");
    CHECK(tr("main") == "Main menu");
    activate_locale("ru");
    {
      auto store = take(open_store(":memory:"));
      ClipboardSnapshot snapshot;
      snapshot.representations = {
          {0, "x11", "UTF8_STRING", CanonicalKind::Text,
           Bytes{'S', 'e', 't', 't', 'i', 'n', 'g', 's'}}};
      auto id = *take(ingest(store, snapshot, {}, 1));
      MenuSettings menu;
      menu.inline_count = 1;
      auto model = take(build_menu(store, MenuKind::Main, menu));
      bool retained = false;
      for (const auto &item : model.items)
        if (item.action == MenuAction::SelectHistory) {
          CHECK(item.label.find("Settings") != std::string::npos);
          retained = true;
        }
      CHECK(retained);
      CHECK(take(load_snapshot(store, id)).representations[0].data ==
            snapshot.representations[0].data);
    }
    auto root =
        std::filesystem::temp_directory_path() /
        ("cliptiq-lifecycle-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
#ifndef _WIN32
    std::filesystem::permissions(root, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
#endif
    InstanceOptions options{(root / "private").u8string(), "fixture"};
    {
      SessionInstance owner;
      CHECK(take(owner.start(options, LaunchAction::Main)) ==
            InstanceRole::Primary);
      for (auto action :
           {LaunchAction::Main, LaunchAction::History, LaunchAction::Snippets,
            LaunchAction::Settings, LaunchAction::Editor}) {
        SessionInstance second;
        CHECK(take(second.start(options, action)) == InstanceRole::Forwarded);
        auto events = take(owner.receive());
        CHECK(events.size() == 1 && events[0] == action);
      }
      SessionInstance invalid;
      CHECK(!invalid.start(options, static_cast<LaunchAction>(99)));
      SessionInstance local_only;
      CHECK(!local_only.start(options, std::nullopt));
      CHECK(take(owner.receive()).empty());
      for (unsigned n = 0; n < 16; ++n) {
        SessionInstance second;
        CHECK(take(second.start(options, LaunchAction::Settings)) ==
              InstanceRole::Forwarded);
      }
      SessionInstance full;
      CHECK(!full.start(options, LaunchAction::Main));
      CHECK(take(owner.receive()).size() == 16);
    }
    {
      SessionInstance restarted;
      CHECK(take(restarted.start(options, LaunchAction::Settings)) ==
            InstanceRole::Primary);
      CHECK(take(restarted.receive()).empty());
    }
    {
      // A restarting owner holds the initialization gate before its owner lock.
      // A retained application_id must not let a follower bypass that gate.
      auto gate =
          take(lock_instance((root / "private/fixture.init.lock").u8string()));
      CHECK(gate);
      auto owner =
          take(lock_instance((root / "private/fixture.lock").u8string()));
      CHECK(owner);
      auto forwarding = std::async(std::launch::async, [&]() {
        SessionInstance follower;
        return follower.start(options, LaunchAction::Settings);
      });
      auto waiting = forwarding.wait_for(std::chrono::milliseconds(150));
      gate.reset();
      auto result = forwarding.get();
      CHECK(waiting == std::future_status::timeout);
      CHECK(take(std::move(result)) == InstanceRole::Forwarded);
    }
#ifndef _WIN32
    {
      std::filesystem::create_directory_symlink(root, root / "alias");
      SessionInstance aliased;
      CHECK(take(aliased.start({(root / "alias/private").u8string(), "alias"},
                               LaunchAction::Settings)) ==
            InstanceRole::Primary);
    }
#endif
    {
      auto foreign =
          take(open_store((root / "private/foreign.sqlite3").u8string()));
    }
    {
      SessionInstance foreign;
      CHECK(!foreign.start({(root / "private").u8string(), "foreign"},
                           LaunchAction::Settings));
    }
    {
      std::ofstream(root / "private/empty.sqlite3", std::ios::binary);
      SessionInstance recovering;
      CHECK(take(recovering.start({(root / "private").u8string(), "empty"},
                                  LaunchAction::Settings)) ==
            InstanceRole::Primary);
    }
    Autostart startup({"relative.exe", root.u8string(), true});
    CHECK(!startup.set_autostart(true));
    CHECK(!startup.status().enabled && !startup.status().reason.empty());
    Autostart native({take(executable_path()), root.u8string(), true});
#ifdef __APPLE__
    CHECK(!native.set_autostart(true));
    CHECK(!native.inspect());
#else
    CHECK(take(native.set_autostart(true)).enabled);
    CHECK(take(native.inspect()).enabled);
    CHECK(!take(native.set_autostart(false)).enabled);
    unsigned committed = 0;
    CHECK(native.apply(true, false, [&]() -> Result<void> {
      ++committed;
      return {};
    }));
    CHECK(committed == 1 && take(native.inspect()).enabled);
    CHECK(native.set_autostart(false));
    CHECK(!native.apply(true, false, []() -> Result<void> {
      return Error{ErrorCode::Full, "fixture full"};
    }));
    CHECK(!take(native.inspect()).enabled);
    CHECK(!native.apply(false, true, []() -> Result<void> {
      return Error{ErrorCode::Full,
                   "fixture full with stale enabled preference"};
    }));
    CHECK(!take(native.inspect()).enabled);
    CHECK(native.set_autostart(true));
    CHECK(!native.apply(false, false, []() -> Result<void> {
      return Error{ErrorCode::Full,
                   "fixture full with stale disabled preference"};
    }));
    CHECK(take(native.inspect()).enabled);
    CHECK(native.set_autostart(false));
#endif
    std::filesystem::remove_all(root);
    std::cout << "PASS fixed routes, second-instance forwarding, settings "
                 "recovery, bounded IPC, restart, locale fallback and visible "
                 "startup failure\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

#include "cliptiq/settings.hpp"
#include "sqlite3.h"
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
int main() {
  int failed = 0;
  auto test = [&](const char *n, auto f) {
    try {
      f();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("defaults_audit_and_platform_shortcuts", [] {
    auto s = default_settings(CapturePlatform::Windows);
    CHECK(settings_history(s).max_count == 30 &&
          settings_menu(s).folder_count == 10);
    CHECK(s.values.at("auto_paste") == "1" &&
          s.values.at("confirm_clear") == "1");
    CHECK(format_chord(*s.hotkeys.at("main")) == "Ctrl+Alt+V");
    CHECK(format_chord(
              *default_settings(CapturePlatform::MacOS).hotkeys.at("main")) ==
          "Shift+Super+V");
  });
  test("atomic_roundtrip_and_privacy_projection", [] {
    auto store = take(open_store(":memory:"));
    auto s = default_settings(CapturePlatform::X11);
    s.values["history_count"] = "3";
    s.values["concealed"] = "1";
    s.values["excluded"] = "app.one\napp.two";
    CHECK(save_settings(store, s));
    auto loaded = take(load_settings(store, CapturePlatform::X11));
    CHECK(settings_history(loaded).max_count == 3 &&
          settings_capture(loaded).ignore_concealed &&
          settings_capture(loaded).excluded_apps.size() == 2);
    CHECK(loaded.revision == 1);
    s.values["history_count"] = "4";
    auto stale = save_settings(store, s);
    CHECK(!stale && stale.error().code == ErrorCode::Busy);
    CHECK(settings_history(take(load_settings(store))).max_count == 3);
  });
  test("ranges_rejected_without_partial_write", [] {
    auto store = take(open_store(":memory:"));
    for (auto field :
         {"history_count", "folder_count", "title_characters",
          "tooltip_characters", "preview_width", "preview_height"}) {
      auto s = default_settings(CapturePlatform::X11);
      s.values[field] = "0";
      auto r = save_settings(store, s);
      CHECK(!r && r.error().code == ErrorCode::InvalidArgument);
      CHECK(take(load_settings(store)).revision == 0);
    }
    auto s = default_settings(CapturePlatform::X11);
    s.values["inline_count"] = "101";
    CHECK(!save_settings(store, s));
    s.values["inline_count"] = "0";
    s.values["history_count"] = "1000";
    CHECK(save_settings(store, s));
  });
  test("settings_cannot_unbind_or_duplicate_and_invalid_values", [] {
    auto s = default_settings(CapturePlatform::X11);
    s.hotkeys["settings"].reset();
    CHECK(!validate_settings(s));
    s = default_settings(CapturePlatform::X11);
    s.hotkeys["history"] = s.hotkeys["main"];
    CHECK(!validate_settings(s));
    s = default_settings(CapturePlatform::X11);
    s.values["concealed"] = "yes";
    CHECK(!validate_settings(s));
  });
  test("eight_groups_and_unknown_capability_disabled_reason", [] {
    auto fields = settings_fields(default_settings(CapturePlatform::X11), {});
    bool groups[8]{};
    for (auto &f : fields) {
      groups[unsigned(f.group)] = true;
      if (!f.enabled)
        CHECK(!f.reason.empty());
      if (f.key == "remote_macos" || f.key == "auto_paste")
        CHECK(!f.enabled && !f.reason.empty());
    }
    for (bool g : groups)
      CHECK(g);
  });
  test("corrupt_and_future_settings_not_silently_defaulted", [] {
    auto store = take(open_store(":memory:"));
    CHECK(sqlite3_exec(store.connection(),
                       "INSERT INTO settings VALUES('t09.schema','9')", nullptr,
                       nullptr, nullptr) == SQLITE_OK);
    auto r = load_settings(store);
    CHECK(!r && r.error().code == ErrorCode::Unsupported);
  });
  test("future_schema_save_does_not_overwrite_and_physical_meaning_roundtrip",
       [] {
         auto store = take(open_store(":memory:"));
         auto s = default_settings(CapturePlatform::X11);
         CHECK(sqlite3_exec(store.connection(),
                            "INSERT INTO settings VALUES('t09.schema','9')",
                            nullptr, nullptr, nullptr) == SQLITE_OK);
         auto result = save_settings(store, s);
         CHECK(!result && result.error().code == ErrorCode::Unsupported);
         CHECK(!load_settings(store));
         auto clean = take(open_store(":memory:"));
         s.hotkeys["main"] = take(parse_chord("@55:Ctrl+Alt+V"));
         CHECK(save_settings(clean, s));
         auto loaded = take(load_settings(clean));
         CHECK(loaded.hotkeys.at("main")->meaning == KeyMeaning::Physical &&
               loaded.hotkeys.at("main")->physical_code == 55);
       });
  test("count_prunes_now_types_do_not_erase_old_and_folder_route", [] {
    auto store = take(open_store(":memory:"));
    for (int i = 0; i < 4; ++i) {
      ClipboardSnapshot snap;
      snap.representations = {{0, "x11", "UTF8_STRING", CanonicalKind::Text,
                               Bytes{std::uint8_t('a' + i)}}};
      take(ingest(store, snap, {}, i));
    }
    auto s = default_settings(CapturePlatform::X11);
    s.values["history_count"] = "3";
    s.values["type_text"] = "0";
    CHECK(save_settings(store, s));
    CHECK(take(list_history(store)).size() == 3);
    CHECK(!settings_capture(take(load_settings(store))).enabled_kinds[0]);
    CHECK(sqlite3_exec(
              store.connection(),
              "INSERT INTO snippet_folders VALUES('f','Folder',0);INSERT INTO "
              "snippets VALUES('s','f','Snippet','body',0)",
              nullptr, nullptr, nullptr) == SQLITE_OK);
    auto menu = take(build_folder_menu(store, "f"));
    CHECK(menu.items.size() == 1 && menu.items[0].id == "s");
    CHECK(!build_folder_menu(store, "missing"));
  });
  return failed ? 1 : 0;
}

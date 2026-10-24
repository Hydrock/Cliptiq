#include "cliptiq/settings.hpp"
#include "cliptiq/locale.hpp"
#include "db.hpp"
#include <charconv>
#include <sstream>
namespace cliptiq {
namespace {
struct Spec {
  SettingsGroup group;
  const char *key, *label, *value;
  int low, high;
  const char *future = "";
};
const std::vector<Spec> &specs() {
  static const std::vector<Spec> s = {
      {SettingsGroup::General, "history_count", "History count", "30", 1, 1000},
      {SettingsGroup::General, "sort", "Sort: 0 created / 1 last used", "1", 0,
       1},
      {SettingsGroup::General, "auto_paste", "Paste automatically", "1", 0, 1},
      {SettingsGroup::General, "duplicates", "Allow duplicates", "1", 0, 1},
      {SettingsGroup::General, "replace_duplicates", "Replace duplicates", "1",
       0, 1},
      {SettingsGroup::General, "confirm_clear", "Confirm clear history", "1", 0,
       1},
      {SettingsGroup::General, "clear_quit", "Clear history on quit", "0", 0,
       1},
      {SettingsGroup::General, "periodic", "Periodic whole-history cleanup",
       "0", 0, 1},
      {SettingsGroup::General, "cleanup_seconds", "Cleanup interval seconds",
       "3600", 300, 604800},
      {SettingsGroup::General, "login", "Launch at login", "0", 0, 1, ""},
      {SettingsGroup::General, "locale", "Language (auto/en/ru)", "auto", -1,
       -1},
      {SettingsGroup::Menu, "tray", "Tray: 0 standard / 1 reversed / 2 hidden",
       "0", 0, 2},
      {SettingsGroup::Menu, "inline_count", "Inline items", "0", 0, 100},
      {SettingsGroup::Menu, "folder_count", "Items per folder", "10", 1, 100},
      {SettingsGroup::Menu, "title_characters", "Title characters", "20", 1,
       1000},
      {SettingsGroup::Menu, "snippets_above", "Snippets above history", "0", 0,
       1},
      {SettingsGroup::Menu, "numbered", "Number items", "1", 0, 1},
      {SettingsGroup::Menu, "zero_start", "Start numbering at zero", "0", 0, 1},
      {SettingsGroup::Menu, "numeric_selection", "Numeric selection", "0", 0,
       1},
      {SettingsGroup::Menu, "icons", "Type icons", "1", 0, 1},
      {SettingsGroup::Menu, "tooltips", "Tooltips", "1", 0, 1},
      {SettingsGroup::Menu, "tooltip_characters", "Tooltip characters", "200",
       1, 10000},
      {SettingsGroup::Menu, "image_previews", "Ready image previews", "1", 0,
       1},
      {SettingsGroup::Menu, "preview_width", "Preview width", "100", 1, 512},
      {SettingsGroup::Menu, "preview_height", "Preview height", "32", 1, 512},
      {SettingsGroup::Menu, "hex_swatches", "Hex swatches", "1", 0, 1},
      {SettingsGroup::Menu, "show_clear", "Show Clear History", "1", 0, 1},
      {SettingsGroup::Types, "type_text", "Text", "1", 0, 1},
      {SettingsGroup::Types, "type_rtf", "RTF", "1", 0, 1},
      {SettingsGroup::Types, "type_rtfd", "RTFD", "1", 0, 1},
      {SettingsGroup::Types, "type_html", "HTML", "0", 0, 1},
      {SettingsGroup::Types, "type_pdf", "PDF", "1", 0, 1},
      {SettingsGroup::Types, "type_files", "File references", "1", 0, 1},
      {SettingsGroup::Types, "type_url", "URL", "1", 0, 1},
      {SettingsGroup::Types, "type_image", "Images", "1", 0, 1},
      {SettingsGroup::Excluded, "concealed", "Ignore concealed content", "0", 0,
       1},
      {SettingsGroup::Excluded, "remote_macos",
       "Ignore Universal Clipboard (macOS)", "0", 0, 1},
      {SettingsGroup::Excluded, "excluded", "Excluded app IDs (one per line)",
       "", -1, -1},
      {SettingsGroup::Excluded, "unknown_source",
       "Unknown source: 0 reject with exclusions / 1 allow / 2 reject", "0", 0,
       2},
      {SettingsGroup::Updates, "updates_auto",
       "Check for updates automatically", "0", 0, 1,
       ""},
      {SettingsGroup::Updates, "updates_interval", "Update interval seconds", "604800", 86400, 2592000},
      {SettingsGroup::Updates, "diagnostics", "Send diagnostics", "0", 0, 1,
       "Local export only; network diagnostics disabled"},
      {SettingsGroup::Beta, "plain_modifier",
       "Plain modifier: 0 off / 1 Ctrl / 2 Shift / 4 Alt / 8 Super", "1", 0, 8},
      {SettingsGroup::Beta, "delete_modifier", "Delete modifier", "0", 0, 8},
      {SettingsGroup::Beta, "paste_delete_modifier", "Paste-delete modifier",
       "0", 0, 8},
      {SettingsGroup::Beta, "screenshots", "Observe screenshot files", "0", 0,
       1},
      {SettingsGroup::Beta, "screenshot_directory",
       "Screenshot directory (absolute path)", "", -1, -1},
      {SettingsGroup::Donate, "support", "Project support",
       "https://github.com/Hydrock/Cliptiq", -1, -1}};
  return s;
}
int number(const std::string &s) {
  int n = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (s.empty() || r.ec != std::errc{} || r.ptr != s.data() + s.size())
    detail::invalid("Expected integer setting");
  return n;
}
int n(const Settings &s, const char *key) {
  auto p = s.values.find(key);
  if (p == s.values.end())
    detail::invalid("Missing setting");
  return number(p->second);
}
std::vector<std::string> exclusions(const Settings &s) {
  std::vector<std::string> out;
  std::istringstream in(s.values.at("excluded"));
  std::string id;
  while (std::getline(in, id)) {
    if (id.empty())
      continue;
    if (id.size() > 4096 || id.find('\0') != std::string::npos ||
        out.size() == 1000)
      detail::invalid("Invalid application exclusions");
    out.push_back(id);
  }
  return out;
}
CapturePlatform native_platform() {
#ifdef __APPLE__
  return CapturePlatform::MacOS;
#elif defined(_WIN32)
  return CapturePlatform::Windows;
#else
  return CapturePlatform::X11;
#endif
}
} // namespace
Settings default_settings(CapturePlatform platform) {
  if (platform == CapturePlatform::Unknown)
    platform = native_platform();
  Settings s;
  for (auto &spec : specs())
    s.values[spec.key] = spec.value;
  if (platform == CapturePlatform::MacOS)
    s.values["plain_modifier"] = "8";
  auto chord = [](const char *text) { return parse_chord(text).value(); };
  if (platform == CapturePlatform::MacOS)
    s.hotkeys = {{"main", chord("Shift+Super+V")},
                 {"history", chord("Ctrl+Super+V")},
                 {"snippets", chord("Shift+Super+B")},
                 {"settings", chord("Ctrl+Shift+Alt+Super+V")},
                 {"editor", {}},
                 {"clear", {}}};
  else
    s.hotkeys = {{"main", chord("Ctrl+Alt+V")},
                 {"history", chord("Ctrl+Alt+H")},
                 {"snippets", chord("Ctrl+Alt+B")},
                 {"settings", chord("Ctrl+Shift+Alt+V")},
                 {"editor", {}},
                 {"clear", {}}};
  return s;
}
HistoryPolicy settings_history(const Settings &s) {
  return {std::size_t(n(s, "history_count")), bool(n(s, "duplicates")),
          bool(n(s, "replace_duplicates")), SortOrder(n(s, "sort"))};
}
MenuSettings settings_menu(const Settings &s) {
  MenuSettings m;
  m.history_limit = n(s, "history_count");
  m.inline_count = n(s, "inline_count");
  m.folder_count = n(s, "folder_count");
  m.title_characters = n(s, "title_characters");
  m.tooltip_characters = n(s, "tooltip_characters");
  m.snippets_above = n(s, "snippets_above");
  m.numbered = n(s, "numbered");
  m.zero_start = n(s, "zero_start");
  m.numeric_selection = n(s, "numeric_selection");
  m.icons = n(s, "icons");
  m.tooltips = n(s, "tooltips");
  m.image_previews = n(s, "image_previews");
  m.preview_width = n(s, "preview_width");
  m.preview_height = n(s, "preview_height");
  m.hex_swatches = n(s, "hex_swatches");
  m.sort_order = SortOrder(n(s, "sort"));
  return m;
}
CapturePolicy settings_capture(const Settings &s) {
  CapturePolicy p;
  const char *keys[] = {"type_text", "type_rtf",   "type_rtfd", "type_html",
                        "type_pdf",  "type_files", "type_url",  "type_image"};
  for (unsigned i = 0; i < 8; ++i)
    p.enabled_kinds[i] = n(s, keys[i]);
  p.ignore_concealed = n(s, "concealed");
  p.ignore_remote_macos = n(s, "remote_macos");
  p.unknown_source_mode = UnknownSourceMode(n(s, "unknown_source"));
  p.excluded_apps = exclusions(s);
  return p;
}
ModifierBindings settings_modifiers(const Settings &s) {
  return {unsigned(n(s, "plain_modifier")), unsigned(n(s, "delete_modifier")),
          unsigned(n(s, "paste_delete_modifier"))};
}
Result<void> validate_settings(const Settings &s) {
  return detail::guarded<void>([&]() -> Result<void> {
    if (s.values.size() != specs().size())
      detail::invalid("Unknown or missing settings keys");
    for (auto &spec : specs()) {
      auto it = s.values.find(spec.key);
      if (it == s.values.end() || it->second.size() > 65536 ||
          it->second.find('\0') != std::string::npos)
        detail::invalid("Invalid setting data");
      if (spec.low >= 0) {
        auto value = number(it->second);
        if (value < spec.low || value > spec.high ||
            it->second != std::to_string(value))
          detail::invalid("Setting out of range");
      }
      if (*spec.future && it->second != spec.value)
        return Error{ErrorCode::Unsupported, spec.future};
    }
    auto interval = n(s, "updates_interval");
    if(interval!=86400 && interval!=604800 && interval!=2592000) detail::invalid("Unsupported update interval");
    auto seconds = n(s, "cleanup_seconds");
    const auto &language = s.values.at("locale");
    if (language.empty() || language.size() > 64 ||
        language.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"
                                   "RSTUVWXYZ0123456789-_.@") !=
            std::string::npos)
      detail::invalid("Invalid language identifier");
    if (s.values.at("support") != "https://github.com/Hydrock/Cliptiq")
      detail::invalid("Project support URL is fixed");
    if (seconds != 300 && seconds != 1800 && seconds != 3600 &&
        seconds != 14400 && seconds != 28800 && seconds != 86400 &&
        seconds != 259200 && seconds != 604800)
      detail::invalid("Unsupported cleanup interval");
    auto bindings = selection_action(0, settings_modifiers(s));
    if (!bindings)
      return bindings.error();
    exclusions(s);
    return validate_hotkeys(s.hotkeys);
  });
}
Result<std::map<FolderId, bool>> settings_folders(Store &store) {
  return detail::guarded<std::map<FolderId, bool>>(
      [&]() -> Result<std::map<FolderId, bool>> {
        std::map<FolderId, bool> result;
        detail::Statement q(
            store.connection(),
            "SELECT id,length(CAST(id AS BLOB)),coalesce((SELECT enabled FROM "
            "snippet_folder_states WHERE id=snippet_folders.id),1) FROM "
            "snippet_folders LIMIT 1001");
        while (q.row()) {
          if (q.number(1) > 1024 || result.size() >= 1000)
            return Error{ErrorCode::TooLarge,
                         "Folder shortcut catalog too large"};
          result[q.text(0)] = q.number(2) != 0;
        }
        return result;
      });
}
Result<Settings> load_settings(Store &store, CapturePlatform platform) {
  return detail::guarded<Settings>([&]() -> Result<Settings> {
    struct Read {
      sqlite3 *db;
      bool committed = false;
      Read(sqlite3 *d) : db(d) { detail::exec(db, "BEGIN"); }
      ~Read() {
        if (!committed)
          sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      }
    } tx(store.connection());
    {
      detail::Statement bounds(
          store.connection(),
          "SELECT count(*),coalesce(sum(length(CAST(key AS "
          "BLOB))+length(CAST(value AS "
          "BLOB))),0),coalesce(max(length(CAST(value AS "
          "BLOB))),0),coalesce(max(length(CAST(key AS BLOB))),0) FROM settings "
          "WHERE key GLOB 't09.*'");
      bounds.row();
      if (bounds.number(0) > 2048 || bounds.number(1) > 1024 * 1024 ||
          bounds.number(2) > 65536 || bounds.number(3) > 1100)
        return Error{ErrorCode::TooLarge, "Persisted settings exceed bounds"};
    }
    auto result = default_settings(platform);
    detail::Statement q(
        store.connection(),
        "SELECT key,value FROM settings WHERE key GLOB 't09.*' ORDER BY key");
    std::size_t total = 0;
    while (q.row()) {
      auto key = q.text(0);
      auto value = q.text(1);
      total += key.size() + value.size();
      if (key.size() > 1100 || value.size() > 65536 || total > 1024 * 1024)
        return Error{ErrorCode::TooLarge, "Stored settings too large"};
      key = key.substr(4);
      if (key == "schema") {
        if (value != "1")
          return Error{ErrorCode::Unsupported, "Unsupported settings version"};
      } else if (key == "revision") {
        auto v = number(value);
        if (v < 0)
          detail::invalid("Invalid settings revision");
        result.revision = std::uint64_t(v);
      } else if (key.rfind("hotkey:", 0) == 0) {
        if (value.empty())
          result.hotkeys[key.substr(7)] = {};
        else {
          auto c = parse_chord(value);
          if (!c)
            return c.error();
          result.hotkeys[key.substr(7)] = c.value();
        }
      } else if (result.values.count(key))
        result.values[key] = value;
      else
        return Error{ErrorCode::Unsupported, "Unknown stored setting"};
    }
    auto folders = settings_folders(store);
    if (!folders)
      return folders.error();
    for (auto it = result.hotkeys.begin(); it != result.hotkeys.end();) {
      if (it->first.rfind("folder:", 0) == 0 &&
          !folders.value().count(it->first.substr(7)))
        it = result.hotkeys.erase(it);
      else
        ++it;
    }
    for (auto &p : folders.value())
      if (!result.hotkeys.count("folder:" + p.first))
        result.hotkeys["folder:" + p.first] = {};
    auto valid = validate_settings(result);
    if (!valid)
      return valid.error();
    detail::exec(store.connection(), "COMMIT");
    tx.committed = true;
    return result;
  });
}
Result<void> save_settings(Store &store, const Settings &s) {
  auto valid = validate_settings(s);
  if (!valid)
    return valid;
  return detail::guarded<void>([&]() -> Result<void> {
    detail::Transaction tx(store.connection());
    {
      detail::Statement schema(
          store.connection(),
          "SELECT value FROM settings WHERE key='t09.schema'");
      if (schema.row() && schema.text(0) != "1")
        return Error{ErrorCode::Unsupported, "Unsupported settings version"};
    }
    std::uint64_t revision = 0;
    {
      detail::Statement q(
          store.connection(),
          "SELECT value FROM settings WHERE key='t09.revision'");
      if (q.row())
        revision = std::uint64_t(number(q.text(0)));
    }
    if (revision != s.revision)
      return Error{ErrorCode::Busy, "Settings changed; reload before saving"};
    if (revision >= 2147483646)
      return Error{ErrorCode::TooLarge, "Settings revision exhausted"};
    detail::exec(store.connection(),
                 "DELETE FROM settings WHERE key GLOB 't09.*'");
    auto insert = [&](const std::string &key, const std::string &value) {
      detail::Statement q(store.connection(),
                          "INSERT INTO settings VALUES(?,?)");
      q.bind(1, "t09." + key);
      q.bind(2, value);
      q.done();
    };
    insert("schema", "1");
    insert("revision", std::to_string(revision + 1));
    for (auto &p : s.values)
      insert(p.first, p.second);
    for (auto &p : s.hotkeys) {
      insert("hotkey:" + p.first, p.second ? format_chord(*p.second) : "");
    }
    {
      detail::Statement prune(
          store.connection(),
          n(s, "sort")
              ? "DELETE FROM history WHERE id NOT IN (SELECT id FROM history "
                "ORDER BY used_ms DESC,rowid DESC LIMIT ?)"
              : "DELETE FROM history WHERE id NOT IN (SELECT id FROM history "
                "ORDER BY created_ms DESC,rowid DESC LIMIT ?)");
      prune.bind(1, std::int64_t(n(s, "history_count")));
      prune.done();
    }
    detail::revision(store.connection());
    tx.commit();
    return {};
  });
}
Result<HotkeyMap> available_hotkeys(Store &store, const HotkeyMap &requested) {
  auto catalog = settings_folders(store);
  if (!catalog)
    return catalog.error();
  auto keys = requested;
  for (auto &p : keys)
    if (p.first.rfind("folder:", 0) == 0) {
      auto found = catalog.value().find(p.first.substr(7));
      if (found == catalog.value().end() || !found->second)
        p.second.reset();
    }
  return keys;
}
Result<void> synchronize_folder_hotkeys(Store &store,
                                        HotkeyRegistry &registry) {
  auto catalog = settings_folders(store);
  if (!catalog)
    return catalog.error();
  registry.reconcile_folders(catalog.value());
  auto settings = load_settings(store);
  if (!settings)
    return settings.error();
  auto keys = available_hotkeys(store, settings.value().hotkeys);
  if (!keys)
    return keys.error();
  std::optional<Error> failure;
  for (const auto &p : keys.value())
    if (p.first.rfind("folder:", 0) == 0 && p.second) {
      auto r = registry.bind_hotkey(p.first, p.second);
      if (!r && r.error().code != ErrorCode::Unsupported && !failure)
        failure = r.error();
    }
  if (failure)
    return *failure;
  return {};
}
Result<void> apply_settings(Store &store, HotkeyRegistry &registry,
                            const Settings &s) {
  auto valid = validate_settings(s);
  if (!valid)
    return valid;
  auto current = load_settings(store);
  if (!current)
    return current.error();
  bool same = current.value().hotkeys.size() == s.hotkeys.size();
  if (same)
    for (auto &p : s.hotkeys) {
      auto it = current.value().hotkeys.find(p.first);
      if (it == current.value().hotkeys.end() ||
          bool(p.second) != bool(it->second) ||
          (p.second && !(*p.second == *it->second))) {
        same = false;
        break;
      }
    }
  if (same)
    return save_settings(store, s);
  auto available = available_hotkeys(store, s.hotkeys);
  if (!available)
    return available.error();
  auto applied = registry.replace(available.value(),
                                  [&] { return save_settings(store, s); });
  if (applied) {
    auto folders = settings_folders(store);
    if (folders)
      registry.reconcile_folders(folders.value());
  }
  return applied;
}
Result<Settings>
edit_settings(Settings draft,
              const std::map<std::string, std::string> &values) {
  for (auto &p : values) {
    if (p.first.rfind("hotkey:", 0) == 0) {
      if (p.second.empty())
        draft.hotkeys[p.first.substr(7)] = {};
      else {
        auto chord = parse_chord(p.second);
        if (!chord)
          return chord.error();
        draft.hotkeys[p.first.substr(7)] = chord.value();
      }
    } else if (draft.values.count(p.first)) {
      auto value = p.second;
      if (p.first == "excluded") {
        std::size_t pos = 0;
        while ((pos = value.find("\r\n", pos)) != std::string::npos)
          value.replace(pos, 2, "\n");
      }
      draft.values[p.first] = std::move(value);
    } else
      return Error{ErrorCode::InvalidArgument, "Unknown settings field"};
  }
  auto valid = validate_settings(draft);
  if (!valid)
    return valid.error();
  return draft;
}
std::vector<SettingField> settings_fields(const Settings &s,
                                          const Capabilities &caps) {
  std::vector<SettingField> out;
  for (auto &spec : specs()) {
    SettingField f{spec.group,
                   spec.key,
                   tr(spec.label),
                   s.values.at(spec.key),
                   spec.future,
                   true,
                   spec.low == 0 && spec.high == 1,
                   false};
    if (!f.reason.empty())
      f.enabled = false;
    if (f.key == "support") {
      f.enabled = false;
      f.reason = "Cliptiq 0.1.0 — https://github.com/Hydrock/Cliptiq";
    }
    if ((f.key == "auto_paste" &&
         caps.auto_paste.status == CapabilityStatus::Unavailable) ||
        (f.group == SettingsGroup::Types &&
         caps.capture.status == CapabilityStatus::Unavailable)) {
      f.enabled = false;
      f.reason =
          f.key == "auto_paste" ? caps.auto_paste.reason : caps.capture.reason;
      f.reason += "; use supported backend/manual paste";
    }
    if (f.key == "remote_macos" && caps.platform != CapturePlatform::MacOS) {
      f.enabled = false;
      f.reason = "Apple-only policy; not applicable on this platform";
    }
    if (f.key == "icons" && caps.platform == CapturePlatform::Windows) {
      f.enabled = false;
      f.reason =
          "Generic Windows type icons deferred since T07; RGBA previews work";
    }
    out.push_back(std::move(f));
  }
  for (auto &p : s.hotkeys)
    out.push_back(
        {SettingsGroup::Shortcuts, "hotkey:" + p.first, tr(p.first),
         p.second ? format_chord(*p.second) : "",
         caps.hotkeys.status == CapabilityStatus::Unavailable
             ? caps.hotkeys.reason + "; preference only, not registered"
             : "",
         true, false, true});
  return out;
}
} // namespace cliptiq

#pragma once
#include "hotkeys.hpp"
namespace cliptiq {
enum class SettingsGroup {
  General,
  Menu,
  Types,
  Excluded,
  Shortcuts,
  Updates,
  Beta,
  Donate
};
struct Settings {
  std::map<std::string, std::string> values;
  HotkeyMap hotkeys;
  std::uint64_t revision = 0;
};
struct SettingField {
  SettingsGroup group;
  std::string key, label, value, reason;
  bool enabled = true, boolean = false, shortcut = false;
};
Settings default_settings(CapturePlatform);
Result<Settings> load_settings(Store &,
                               CapturePlatform = CapturePlatform::Unknown);
Result<void> save_settings(Store &, const Settings &);
Result<void> validate_settings(const Settings &);
Result<void> apply_settings(Store &, HotkeyRegistry &, const Settings &);
Result<Settings> edit_settings(Settings,
                               const std::map<std::string, std::string> &);
std::vector<SettingField> settings_fields(const Settings &,
                                          const Capabilities &);
HistoryPolicy settings_history(const Settings &);
MenuSettings settings_menu(const Settings &);
CapturePolicy settings_capture(const Settings &);
ModifierBindings settings_modifiers(const Settings &);
Result<std::map<FolderId, bool>> settings_folders(Store &);
class NativeSettings {
  struct State;
  std::shared_ptr<State> state_;

public:
  NativeSettings();
  ~NativeSettings();
  Result<void>
  show(Settings, const Capabilities &, std::function<Result<void>(Settings)>,
       std::function<Result<void>(const HotkeyMap &)> authorize = {});
  Result<void> process_events();
  std::uintptr_t native_handle() const;
  bool confirm_clear() const;
  void status(const std::string &);
  bool record_chord(const KeyChord &);
};
} // namespace cliptiq

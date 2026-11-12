#pragma once
#include "menu.hpp"
#include <memory>
namespace cliptiq {
struct MenuImage {
  unsigned width = 0, height = 0;
  Bytes rgba;
};
using MenuPreviewProvider =
    std::function<std::optional<MenuImage>(const PreviewRef &)>;
struct NativeMenuOptions {
  TrayStyle tray_style = TrayStyle::Standard;
  unsigned preview_width = 100, preview_height = 32;
  bool icons = true;
  // Windows background startup: retain the launcher as an IPC/tray owner,
  // but show it as a recovery route if the tray is unavailable/disabled.
  bool start_hidden = false;
  bool workspace_enabled = false;
  std::function<Result<MenuModel>()> workspace_model;
  std::function<Result<std::string>(const MenuCommand &)> workspace_preview;
  std::string workspace_status;
  // Called on the GUI thread before a native popup takes focus. Empty means
  // copy/manual paste only. External DBusMenu events cannot invent a target.
  std::function<InvocationContext()> invocation_provider;
};
struct NativeMenuStatus {
  bool launcher_visible = false, tray_available = false;
  std::string tray_reason;
};
struct NativeMenuHandles {
  std::uintptr_t window = 0, menu = 0;
}; // Borrowed owner-thread handles, invalidated by update/destruction.
class NativeMenu {
  struct State;
  std::shared_ptr<State> state_;

public:
  NativeMenu(NativeMenuOptions, std::function<void(MenuCommand)>,
             MenuPreviewProvider = {});
  ~NativeMenu();
  NativeMenu(const NativeMenu &) = delete;
  Result<void> initialize();
  Result<void> update(MenuModel);
  Result<void> show(const InvocationContext & = {});
  Result<void> set_tray_style(TrayStyle);
  Result<void> set_options(NativeMenuOptions);
  Result<void> process_events(unsigned wait_ms = 0);
  NativeMenuStatus status() const;
  NativeMenuHandles native_handles() const;
#ifdef _WIN32
  Result<void> show_workspace();
  void workspace_status(const std::string &);
#endif
};
MenuImage scale_menu_image(const MenuImage &, unsigned dpi);
std::optional<MenuImage> menu_preview(const MenuItem &,
                                      const NativeMenuOptions &,
                                      const MenuPreviewProvider &);
} // namespace cliptiq

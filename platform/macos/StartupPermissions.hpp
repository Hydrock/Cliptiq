#pragma once
#include <functional>
#include <memory>
namespace cliptiq::macos {
struct StartupPermissionCalls {
  std::function<bool()> clipboard_allowed, paste_allowed;
  std::function<void()> request_clipboard, request_paste;
};
class StartupPermissions {
  struct State;
  std::unique_ptr<State> state_;
public:
  StartupPermissions();
  explicit StartupPermissions(StartupPermissionCalls);
  ~StartupPermissions();
  // Owner GUI thread only. False means quit without starting the application.
  bool run();
};
}

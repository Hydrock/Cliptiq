#pragma once
#include "types.hpp"
#include <functional>
#include <memory>
namespace cliptiq {
enum class LaunchAction { Main = 1, History, Snippets, Settings, Editor };
Result<LaunchAction> parse_launch_action(const std::string &);
std::string launch_action_name(LaunchAction);
enum class InstanceRole { Primary, Forwarded };
// Options are an embedding/test API; they are never accepted as IPC messages.
struct InstanceOptions {
  std::string directory, scope;
};
class SessionInstance {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  SessionInstance();
  ~SessionInstance();
  Result<InstanceRole> start(InstanceOptions, std::optional<LaunchAction>);
  Result<std::vector<LaunchAction>> receive();
};
struct AutostartStatus {
  bool enabled = false, requires_approval = false;
  std::string reason;
};
struct AutostartOptions {
  std::string executable, directory;
  bool isolated = false;
};
class Autostart {
  AutostartOptions options_;
  AutostartStatus status_;

public:
  explicit Autostart(AutostartOptions);
  Result<AutostartStatus> set_autostart(bool);
  Result<AutostartStatus> inspect();
  Result<void> apply(bool requested, bool previous,
                     const std::function<Result<void>()> &commit);
  const AutostartStatus &status() const { return status_; }
};
Result<std::string> lifecycle_directory();
std::string lifecycle_scope();
Result<std::string> executable_path();
std::string system_locale();
} // namespace cliptiq

#pragma once
#include "cliptiq/hotkeys.hpp"
namespace cliptiq {
class PortalHotkeys {
  struct State;
  std::shared_ptr<State> state_;

public:
  explicit PortalHotkeys(std::function<void(ActionId, InvocationContext)>);
  ~PortalHotkeys();
  void reconcile_folders(const std::map<FolderId, bool> &);
  Result<void> configure(const HotkeyMap &);
  std::map<ActionId, BindingStatus> statuses() const;
};
} // namespace cliptiq

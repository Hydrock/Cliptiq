#pragma once
#include "cliptiq/hotkeys.hpp"
namespace cliptiq {
class PortalHotkeys {
  struct State;
  std::shared_ptr<State> state_;

public:
  explicit PortalHotkeys(std::function<void(ActionId, InvocationContext)>);
  ~PortalHotkeys();
  Result<void> configure(const HotkeyMap &);
  std::map<ActionId, BindingStatus> statuses() const;
};
} // namespace cliptiq

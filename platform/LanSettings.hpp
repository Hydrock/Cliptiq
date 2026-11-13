#pragma once
#include "cliptiq/lan.hpp"
#include "DiscoveryUi.hpp"
#include <map>
namespace cliptiq {
class NativeLanSettings {
  struct State;
  std::shared_ptr<State> state_;
public:
  using Save = std::function<Result<void>(lan::Config&, std::map<std::string,std::string>)>;
  NativeLanSettings();
  ~NativeLanSettings();
  Result<void> show(lan::Config, Save, std::function<Result<std::string>()>, std::function<Result<void>(std::string)>, void* parent = nullptr);
  Result<void> process_events();
#ifdef __APPLE__
  Result<void> apply();
#endif
  void status(std::string);
  void discovery(DiscoveryUiCallbacks);
};
} // namespace cliptiq

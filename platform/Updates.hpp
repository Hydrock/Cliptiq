#pragma once
#include "cliptiq/update.hpp"
namespace cliptiq {
// UI thread only; no calls are made in smoke/isolation mode. OS providers own
// their network/signature/install lifecycle; the base never replaces itself.
class NativeUpdates {
  struct State;
  std::shared_ptr<State> state_;
  Result<void> check_metadata();
public:
  NativeUpdates();
  bool poll();
  std::string metadata_status() const;
  Result<void> initialize();
  bool manages_schedule() const;
  std::int64_t last_check() const;
  bool automatic_available() const;
  Result<void> check_now(bool automatic=false);
  void preferences(bool automatic,std::int64_t interval);
  std::string status() const;
  Result<void> export_diagnostics();
  Result<void> open_download();
};
}

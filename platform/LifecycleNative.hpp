#pragma once
#include "cliptiq/lifecycle.hpp"
namespace cliptiq {
Result<void> secure_instance_directory(const std::string &);
Result<std::shared_ptr<void>> lock_instance(const std::string &);
Result<AutostartStatus> native_autostart(bool, const AutostartOptions &);
Result<AutostartStatus> native_autostart_status(const AutostartOptions &);
std::string native_session_hint();
} // namespace cliptiq

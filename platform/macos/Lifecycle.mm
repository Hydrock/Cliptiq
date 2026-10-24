#include "../LifecycleNative.hpp"
#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include <filesystem>
#include <unistd.h>
namespace cliptiq {
Result<std::string> lifecycle_directory() {
  return (std::filesystem::temp_directory_path() /
          ("cliptiq-" + std::to_string(getuid())))
      .u8string();
}
std::string native_session_hint() { return std::to_string(getuid()); }
Result<std::string> executable_path() {
  auto path = NSBundle.mainBundle.executablePath;
  return path ? Result<std::string>(path.UTF8String)
              : Result<std::string>(
                    Error{ErrorCode::Io, "Executable path unavailable"});
}
std::string system_locale() {
  auto language = NSLocale.preferredLanguages.firstObject;
  return language ? language.UTF8String : "en";
}
Result<AutostartStatus> native_autostart(bool enabled,
                                         const AutostartOptions &o) {
  if (o.isolated ||
      ![NSBundle.mainBundle.bundleIdentifier
          isEqualToString:@"io.github.hydrock.Cliptiq"] ||
      ![NSBundle.mainBundle.bundlePath.pathExtension isEqualToString:@"app"])
    return Error{ErrorCode::Unsupported,
                 "Login requires the Cliptiq app bundle; isolated tests never "
                 "register login items"};
  auto service = SMAppService.mainAppService;
  NSError *error = nil;
  if (enabled && service.status != SMAppServiceStatusEnabled &&
      service.status != SMAppServiceStatusRequiresApproval) {
    if (![service registerAndReturnError:&error])
      return Error{ErrorCode::Io, error.localizedDescription.UTF8String
                                      ?: "Login registration failed"};
  }
  if (!enabled && service.status != SMAppServiceStatusNotRegistered) {
    if (![service unregisterAndReturnError:&error])
      return Error{ErrorCode::Io, error.localizedDescription.UTF8String
                                      ?: "Login removal failed"};
  }
  auto status = service.status;
  return AutostartStatus{status == SMAppServiceStatusEnabled,
                         status == SMAppServiceStatusRequiresApproval,
                         status == SMAppServiceStatusRequiresApproval
                             ? "Autostart requires approval in System Settings "
                               "> General > Login Items"
                         : status == SMAppServiceStatusEnabled
                             ? "Autostart enabled"
                             : "Autostart disabled"};
}
Result<AutostartStatus> native_autostart_status(const AutostartOptions &o) {
  if (o.isolated || ![NSBundle.mainBundle.bundleIdentifier
                        isEqualToString:@"io.github.hydrock.Cliptiq"])
    return Error{ErrorCode::Unsupported,
                 "Login requires the Cliptiq app bundle; isolated tests never "
                 "register login items"};
  auto status = SMAppService.mainAppService.status;
  return AutostartStatus{status == SMAppServiceStatusEnabled,
                         status == SMAppServiceStatusRequiresApproval,
                         status == SMAppServiceStatusRequiresApproval
                             ? "Autostart requires approval in System Settings "
                               "> General > Login Items"
                         : status == SMAppServiceStatusEnabled
                             ? "Autostart enabled"
                             : "Autostart disabled"};
}
} // namespace cliptiq

#include "../LifecycleNative.hpp"
#include "../LocaleNative.hpp"
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
Result<std::optional<bool>> show_first_run_autostart() {
  @autoreleasepool {
    if(!NSThread.isMainThread)return Error{ErrorCode::InvalidArgument,"Autostart setup requires the main thread"};
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSAlert *alert=[NSAlert new];
    alert.messageText=loc(@"Keep Cliptiq ready when you sign in");
    alert.informativeText=loc(@"Start Cliptiq when you sign in so clipboard history is ready. You can change this later in Settings.");
    [alert addButtonWithTitle:loc(@"Continue")];[alert addButtonWithTitle:loc(@"Quit")];
    NSButton *choice=[NSButton checkboxWithTitle:loc(@"Launch at login") target:nil action:nil];
    choice.identifier=@"first-run-autostart";choice.state=NSControlStateValueOn;
    [choice sizeToFit];alert.accessoryView=choice;
    [NSApp activateIgnoringOtherApps:YES];
    if([alert runModal]!=NSAlertFirstButtonReturn)return std::optional<bool>{};
    return std::optional<bool>{choice.state==NSControlStateValueOn};
  }
}
void show_first_run_autostart_approval() {
  @autoreleasepool {
    NSAlert *alert=[NSAlert new];alert.messageText=loc(@"Set up Cliptiq");
    alert.informativeText=loc(@"Autostart requires approval in System Settings > General > Login Items");
    [alert addButtonWithTitle:loc(@"Open Login Items")];[alert addButtonWithTitle:loc(@"Continue")];
    if([alert runModal]==NSAlertFirstButtonReturn)[SMAppService openSystemSettingsLoginItems];
  }
}
} // namespace cliptiq

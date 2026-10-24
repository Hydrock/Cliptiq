#import <AppKit/AppKit.h>
#include "../Updates.hpp"
#ifdef CLIPTIQ_SPARKLE
#import <Sparkle/Sparkle.h>
@interface CliptiqUpdateDelegate:NSObject<SPUUpdaterDelegate>
@end
@implementation CliptiqUpdateDelegate
-(BOOL)updater:(SPUUpdater*)u shouldProceedWithUpdate:(SUAppcastItem*)item updateCheck:(SPUUpdateCheck)check error:(NSError**)error {
  (void)u;(void)check;
  BOOL valid=item.contentLength>0&&item.contentLength<=5ull*1024*1024&&[item.fileURL.absoluteString hasPrefix:@"https://github.com/Hydrock/Cliptiq/releases/download/"];
  if(!valid&&error)*error=[NSError errorWithDomain:@"CliptiqUpdate" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Update exceeds size budget or project origin policy"}];
  return valid;
}
-(BOOL)updater:(SPUUpdater*)u shouldDownloadReleaseNotesForUpdate:(SUAppcastItem*)item{(void)u;(void)item;return NO;}
@end
static SPUStandardUpdaterController* controller;
static CliptiqUpdateDelegate* updateDelegate;
static bool started=false;
static bool configured(){
  auto bundle=NSBundle.mainBundle;
  auto feed=[bundle objectForInfoDictionaryKey:@"SUFeedURL"];
  auto key=[bundle objectForInfoDictionaryKey:@"SUPublicEDKey"];
  return [feed isKindOfClass:NSString.class]&&[feed hasPrefix:@"https://github.com/Hydrock/Cliptiq/"]&&[key isKindOfClass:NSString.class]&&[(NSString*)key length]==44;
}
static SPUStandardUpdaterController* updater(){
  if(!configured())return nil;
  if(!controller){updateDelegate=[CliptiqUpdateDelegate new];controller=[[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:updateDelegate userDriverDelegate:nil];controller.updater.sendsSystemProfile=NO;}
  return controller;
}
#endif
namespace cliptiq {
Result<std::string> native_update_feed(){return Error{ErrorCode::Unsupported,"Use the signed Sparkle route"};}
Result<void> NativeUpdates::initialize(){
#ifdef CLIPTIQ_SPARKLE
  auto u=updater();
  if(u&&!started){NSError*error=nil;if(![u.updater startUpdater:&error])return Error{ErrorCode::Io,"Cannot start signed updater"};started=true;}
#endif
  return {};
}
bool NativeUpdates::manages_schedule()const{return automatic_available();}
std::int64_t NativeUpdates::last_check()const{
#ifdef CLIPTIQ_SPARKLE
  return controller?static_cast<std::int64_t>(controller.updater.lastUpdateCheckDate.timeIntervalSince1970):0;
#else
  return 0;
#endif
}
bool NativeUpdates::automatic_available()const{
#ifdef CLIPTIQ_SPARKLE
  return configured();
#else
  return false;
#endif
}
void NativeUpdates::preferences(bool automatic,std::int64_t interval){
#ifdef CLIPTIQ_SPARKLE
  if(interval!=86400&&interval!=604800&&interval!=2592000)return;
  auto u=updater();if(!u)return;
  // Set policy before start, never silently enable automatic installation.
  u.updater.automaticallyChecksForUpdates=automatic;
  u.updater.updateCheckInterval=interval;
  u.updater.automaticallyDownloadsUpdates=NO;
#else
  (void)automatic;(void)interval;
#endif
}
std::string NativeUpdates::status()const{
  return automatic_available()?"Updates: Sparkle, signed project appcast; installation requires confirmation":"Updates: explicit project release download; Sparkle feed/public key is not configured";
}
Result<void> NativeUpdates::check_now(bool automatic){
#ifdef CLIPTIQ_SPARKLE
  auto u=updater();
  if(u){
    auto result=initialize();if(!result)return result;
    if(automatic)[u.updater checkForUpdatesInBackground];else[u checkForUpdates:nil];return {};
  }
#endif
  if(automatic)return Error{ErrorCode::Unsupported,status()};
  return open_download();
}
Result<void> NativeUpdates::open_download(){
  if(![NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/Hydrock/Cliptiq/releases"]])return Error{ErrorCode::Io,"Cannot open project releases"};return {};
}
Result<void> NativeUpdates::export_diagnostics(){
  auto panel=NSSavePanel.savePanel;panel.nameFieldStringValue=@"Cliptiq-diagnostics.txt";
  if([panel runModal]!=NSModalResponseOK)return {};
  auto text=local_diagnostics();auto data=[NSData dataWithBytes:text.data() length:text.size()];
  if(![data writeToURL:panel.URL options:NSDataWritingAtomic error:nil])return Error{ErrorCode::Io,"Cannot export local diagnostics"};return {};
}
}

#include "platform/LanSettings.hpp"
#include "cliptiq/settings.hpp"
#include "cliptiq/locale.hpp"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
static NSButton *button(NSView *root, NSString *title) {
 if([root isKindOfClass:NSButton.class] && [((NSButton *)root).title isEqualToString:title])return (NSButton *)root;
 for(NSView *child in root.subviews)if(auto result=button(child,title))return result;
 return nil;
}
static NSControl *field(NSView *root, NSString *title) {
 if([root isKindOfClass:NSStackView.class]){
  auto children=((NSStackView *)root).arrangedSubviews;
  if(children.count==2 && [children[0] isKindOfClass:NSTextField.class] &&
     [((NSTextField *)children[0]).stringValue isEqualToString:title] && [children[1] isKindOfClass:NSControl.class])return (NSControl *)children[1];
 }
 for(NSView *child in root.subviews)if(auto result=field(child,title))return result;
 return nil;
}
static NSTabView *tabs(NSView *root) {
 if([root isKindOfClass:NSTabView.class])return (NSTabView *)root;
 for(NSView *child in root.subviews)if(auto result=tabs(child))return result;
 return nil;
}
int main(int argc,char **argv) { @autoreleasepool { try {
 bool russian=argc>1 && std::string(argv[1])=="--ru";activate_locale(russian?"ru":"en");
 auto text=[](const char *key){return [NSString stringWithUTF8String:tr(key).c_str()];};
 NativeLanSettings network;NativeSettings settings;
 lan::Config config;config.device_id=lan::new_id();config.device_name="Original";
 bool rejectSave=false;
 unsigned saves=0,copies=0;std::map<std::string,std::string> savedKeys;NSStackView *panel=nil;
 settings.advanced_apply([&]{return network.apply();});
 settings.advanced([&](void *parent){
  CHECK(parent);panel=(__bridge NSStackView *)parent;
  CHECK(network.show(config,[&](lan::Config &draft,std::map<std::string,std::string> keys)->Result<void>{
   if(rejectSave)return Error{ErrorCode::Busy,"Test persistence failure"};
   CHECK(draft.revision==config.revision);++draft.revision;config=draft;savedKeys=std::move(keys);++saves;return {};
  },[]()->Result<std::string>{return std::string(64,'a');},[&](std::string key)->Result<void>{CHECK(key==std::string(64,'a'));++copies;return {};},parent));
 });
 auto show=[&]{
  Capabilities caps;caps.platform=CapturePlatform::MacOS;
  CHECK(settings.show(default_settings(CapturePlatform::MacOS),caps,[](Settings)->Result<void>{return {};}));
  auto window=(__bridge NSWindow *)(void *)settings.native_handle();
  auto tab=tabs(window.contentView);CHECK(tab && tab.numberOfTabViewItems==10);
  CHECK([[tab tabViewItemAtIndex:9].label isEqualToString:russian?@"Сеть":@"Network"]);
  [tab selectTabViewItemAtIndex:9];CHECK(panel.window==window);
  [window.contentView layoutSubtreeIfNeeded];[NSApp updateWindows];return window;
 };
 auto click=[&](const char *key){auto control=button(panel,text(key));CHECK(control);[control performClick:nil];};
 auto edit=[&](const char *key,NSString *value){auto control=(NSTextField *)field(panel,text(key));CHECK(control);control.stringValue=value;};
 auto window=show();auto initialWindows=NSApp.windows.count;
 edit("Local device name",@"Unsaved");click("Cancel");CHECK(saves==0);
 CHECK([((NSTextField *)field(panel,text("Local device name"))).stringValue isEqualToString:@"Original"]);
 click("Add peer");CHECK(saves==0);click("Generate key");
 auto secret=field(panel,text("Pair key (blank keeps saved key)"));CHECK([secret isKindOfClass:NSSecureTextField.class]);
 click("Copy key");CHECK(copies==1 && saves==0);
 auto apply=button(window.contentView,text("Apply"));CHECK(apply);
 edit("Peer address",@"127.0.0.1");edit("Listen port",@"70000");[apply performClick:nil];CHECK(saves==0);
 edit("Listen port",@"49731");
 rejectSave=true;[apply performClick:nil];CHECK(saves==0);rejectSave=false;
 [apply performClick:nil];CHECK(saves==1 && savedKeys.size()==1 && config.peers.size()==1);
 CHECK(window.visible && panel.window==window && NSApp.windows.count==initialWindows);
 click("Save");CHECK(saves==2 && savedKeys.empty());
 edit("Local device name",@"Discarded");click("Cancel");
 CHECK([((NSTextField *)field(panel,text("Local device name"))).stringValue isEqualToString:@"Original"]);
 click("Remove peer");click("Save");CHECK(saves==3 && config.peers.empty());
 for(unsigned n=0;n<3;++n)click("Add peer");
 [window.contentView layoutSubtreeIfNeeded];[NSApp updateWindows];CHECK(panel.frame.size.height>window.contentView.frame.size.height);
 click("Cancel");CHECK(!field(panel,text("Peer address")) && saves==3);
 [window performClose:nil];CHECK(!window.visible);window=show();CHECK(window.visible && panel.window==window);
 [window performClose:nil];std::cout<<"PASS embedded macOS Network settings: draft, keys, repeated save, cancel, layout and reopen\n";return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;} } }

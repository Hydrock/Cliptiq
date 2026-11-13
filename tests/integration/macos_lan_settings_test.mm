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
static NSTextField* codeField(NSView* root){if([root isKindOfClass:NSTextField.class]&&[((NSTextField*)root).placeholderString isEqualToString:[NSString stringWithUTF8String:tr("6-digit code").c_str()]])return (NSTextField*)root;for(NSView* child in root.subviews)if(auto result=codeField(child))return result;return nil;}
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
 [window performClose:nil];
 NativeLanSettings discoveredNetwork;lan::DiscoverySnapshot snapshot;snapshot.available=true;unsigned searches=0,closes=0,approvals=0,submitted=0;DiscoveryUiCallbacks callbacks;callbacks.snapshot=[&]{return snapshot;};callbacks.available=[&](bool value)->Result<void>{snapshot.enabled=value;return {};};callbacks.search=[&]()->Result<void>{++searches;snapshot.searching=true;return {};};callbacks.stop_search=[&]{snapshot.searching=false;};callbacks.close_view=[&]{++closes;};callbacks.connect=[](std::string)->Result<void>{return {};};callbacks.accept=[&](std::string id)->Result<void>{CHECK(id=="ui-session");++approvals;return {};};callbacks.enter_code=[&](std::string id,std::string code)->Result<void>{CHECK(id=="ui-session"&&code=="123456");++submitted;return {};};callbacks.cancel=[](std::string){};
 snapshot.interfaces={{"en0","Wi-Fi","192.168.1.1",2}};callbacks.select_interface=[&](std::string id)->Result<void>{snapshot.interface_id=std::move(id);return {};};
 discoveredNetwork.discovery(callbacks);CHECK(discoveredNetwork.show(config,[](lan::Config&,std::map<std::string,std::string>)->Result<void>{return {};},[]()->Result<std::string>{return std::string(64,'a');},[](std::string)->Result<void>{return {};}));
 NSWindow* discoveryWindow=nil;for(NSWindow* candidate in NSApp.windows)if([candidate.title isEqualToString:text("Advanced LAN settings")]&&candidate.visible)discoveryWindow=candidate;CHECK(discoveryWindow);
 auto searchButton=button(discoveryWindow.contentView,text("Find devices on network"));CHECK(searchButton);[searchButton performClick:nil];CHECK(searches==1);CHECK(discoveredNetwork.process_events());
 auto automatic=button(discoveryWindow.contentView,text("Automatic (recommended)"));CHECK(automatic&&automatic.isHiddenOrHasHiddenAncestor&&automatic.state==NSControlStateValueOn);
 auto manual=button(discoveryWindow.contentView,text("Advanced settings"));CHECK(manual);[manual performClick:nil];auto nameField=(NSTextField*)field(discoveryWindow.contentView,text("Local device name"));CHECK(nameField&&!nameField.hidden);nameField.stringValue=@"Preserve draft";
 CHECK(!automatic.isHiddenOrHasHiddenAncestor);auto wifi=button(discoveryWindow.contentView,@"Wi-Fi (192.168.1.1)");CHECK(wifi);[wifi performClick:nil];CHECK(snapshot.interface_id=="en0");[button(discoveryWindow.contentView,text("Automatic (recommended)")) performClick:nil];CHECK(snapshot.interface_id.empty());
 lan::DiscoveryDevice found;found.device_id="found";found.name="Same name";found.platform="windows";auto sameName=found;sameName.device_id="another-found";snapshot.devices={found,sameName};CHECK(discoveredNetwork.process_events());CHECK([nameField.stringValue isEqualToString:@"Preserve draft"]);CHECK(button(discoveryWindow.contentView,text("Connect")));
 snapshot.pairing=lan::PairingEvent::Kind::Incoming;snapshot.session_id="ui-session";snapshot.peer_name="Test PC";CHECK(discoveredNetwork.process_events());auto allow=button(discoveryWindow.contentView,text("Allow pairing"));CHECK(allow);[allow performClick:nil];CHECK(approvals==1);
 snapshot.pairing=lan::PairingEvent::Kind::Code;snapshot.code="012345";CHECK(discoveredNetwork.process_events());CHECK(button(discoveryWindow.contentView,text("Cancel pairing")));
 snapshot.pairing=lan::PairingEvent::Kind::EnterCode;snapshot.code.clear();CHECK(discoveredNetwork.process_events());auto input=codeField(discoveryWindow.contentView);CHECK(input);input.stringValue=@"123456";snapshot.message="Search complete.";CHECK(discoveredNetwork.process_events());CHECK([codeField(discoveryWindow.contentView).stringValue isEqualToString:@"123456"]);[button(discoveryWindow.contentView,text("Pair device")) performClick:nil];CHECK(submitted==1);
 snapshot.pairing=lan::PairingEvent::Kind::Failed;snapshot.message="Pairing failed. Start again with a new code.";CHECK(discoveredNetwork.process_events());CHECK(button(discoveryWindow.contentView,text("Connect")).enabled);
 [discoveryWindow performClose:nil];CHECK(closes==1);snapshot.message="Late result";CHECK(discoveredNetwork.process_events());
 // Short collapsed discovery content must start at the top of the embedded page.
 snapshot.pairing.reset();snapshot.session_id.clear();snapshot.devices.clear();snapshot.message.clear();
 NativeSettings compactSettings;compactSettings.advanced([&](void* parent){CHECK(discoveredNetwork.show(config,[](lan::Config&,std::map<std::string,std::string>)->Result<void>{return {};},[]()->Result<std::string>{return std::string(64,'a');},[](std::string)->Result<void>{return {};},parent));});
 Capabilities compactCaps;compactCaps.platform=CapturePlatform::MacOS;CHECK(compactSettings.show(default_settings(CapturePlatform::MacOS),compactCaps,[](Settings)->Result<void>{return {};}));
 auto compactWindow=(__bridge NSWindow*)(void*)compactSettings.native_handle();auto compactTabs=tabs(compactWindow.contentView);[compactTabs selectTabViewItemAtIndex:9];
 [compactWindow.contentView layoutSubtreeIfNeeded];[NSApp updateWindows];
 auto available=button(compactWindow.contentView,text("Available for discovery"));CHECK(available);
 NSView* clip=available.superview;while(clip&&![clip isKindOfClass:NSClipView.class])clip=clip.superview;CHECK(clip);
 CHECK([available.superview isKindOfClass:NSStackView.class]);auto first=((NSStackView*)available.superview).arrangedSubviews.firstObject;CHECK(first);
 auto location=[first convertRect:first.bounds toView:clip];CGFloat gap=clip.isFlipped?NSMinY(location)-NSMinY(clip.bounds):NSMaxY(clip.bounds)-NSMaxY(location);
 if(gap>24)std::cerr<<"Discovery top gap: "<<gap<<"\n";CHECK(gap>=0&&gap<24);
 [compactWindow performClose:nil];
 std::cout<<"PASS embedded macOS Network settings: draft, keys, repeated save, cancel, layout and reopen\n";return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;} } }

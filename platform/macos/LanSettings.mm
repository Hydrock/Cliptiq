#include "../LanSettingsDraft.hpp"
#include "../LocaleNative.hpp"
#import <AppKit/AppKit.h>
#include <thread>
#include <algorithm>
@interface CliptiqLanAction : NSObject <NSWindowDelegate> { @public std::function<void(NSInteger)> callback; std::function<void()> closed; }
- (void)clicked:(id)sender;
@end
@implementation CliptiqLanAction
- (void)clicked:(id)sender {if(callback)callback([sender tag]);}
- (void)windowWillClose:(NSNotification*)note {(void)note;if(closed)closed();}
@end
namespace cliptiq {
namespace {NSString* ls(const std::string&s){return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]?:@"";}}
struct NativeLanSettings::State : std::enable_shared_from_this<NativeLanSettings::State> {
 std::thread::id owner=std::this_thread::get_id();NSWindow* window=nil;NSTextField* message=nil;NSStackView* stack=nil;CliptiqLanAction* target=nil;
 bool embedded=false;lan::Config saved;
 LanSettingsDraft draft;Save save;std::function<Result<std::string>()> generate;std::function<Result<void>(std::string)> copy;
 struct Input{LanSettingsDraft::Field field;NSControl* widget;};std::vector<Input> inputs;std::map<NSInteger,std::pair<int,std::string>> actions;
 ~State(){if(pairingTarget)pairingTarget->closed={};[pairingPanel close];if(target){target->callback={};target->closed={};}[window close];}
 void status(std::string s){message.stringValue=ls(s);}
 Result<void> collect(){[stack.window makeFirstResponder:nil];std::map<std::string,std::string> values;for(auto& i:inputs)values[i.field.key]=i.field.check?([(NSButton*)i.widget state]==NSControlStateValueOn?"1":"0"):std::string([(NSTextField*)i.widget stringValue].UTF8String);return draft.collect(values);}
 NSButton* button(NSString* title,int kind,std::string peer={}){auto b=[NSButton buttonWithTitle:loc(title) target:target action:@selector(clicked:)];b.tag=actions.size()+1;actions[b.tag]={kind,std::move(peer)};return b;}
 NSStackView* root=nil;NSStackView* discoveryStack=nil;NSButton* manualToggle=nil;NSTextField* codeField=nil;NSPanel* pairingPanel=nil;NSStackView* pairingStack=nil;CliptiqLanAction* pairingTarget=nil;
 DiscoveryUiCallbacks discovery;lan::DiscoverySnapshot snapshot;std::string signature;bool manualExpanded=false,viewVisible=false;std::map<NSInteger,std::pair<int,std::string>> discoveryActions;NSInteger nextDiscoveryTag=10000;
 NSButton* discoveryButton(const char* title,int kind,std::string value={}){auto b=[NSButton buttonWithTitle:ls(tr(title)) target:target action:@selector(clicked:)];b.tag=nextDiscoveryTag++;discoveryActions[b.tag]={kind,std::move(value)};return b;}
 void pairingView(NSStackView* container){
  using Kind=lan::PairingEvent::Kind;if(!snapshot.pairing)return;
  auto name=[NSTextField wrappingLabelWithString:ls(snapshot.peer_name)];name.font=[NSFont boldSystemFontOfSize:14];[container addArrangedSubview:name];
  if(snapshot.pairing==Kind::Incoming){[container addArrangedSubview:[NSTextField wrappingLabelWithString:loc(@"Allow this device to pair?")]];[container addArrangedSubview:discoveryButton("Allow pairing",13,snapshot.session_id)];}
  if(snapshot.pairing==Kind::Code){[container addArrangedSubview:[NSTextField wrappingLabelWithString:loc(@"Enter this code on the other computer. It expires in 2 minutes.")]];auto code=[NSTextField labelWithString:ls(snapshot.code)];code.font=[NSFont monospacedDigitSystemFontOfSize:32 weight:NSFontWeightSemibold];[container addArrangedSubview:code];}
  if(snapshot.pairing==Kind::EnterCode){[container addArrangedSubview:[NSTextField wrappingLabelWithString:loc(@"Enter the 6-digit code shown on the other computer.")]];codeField=[NSTextField new];codeField.placeholderString=loc(@"6-digit code");[codeField.widthAnchor constraintEqualToConstant:180].active=YES;[container addArrangedSubview:codeField];[container addArrangedSubview:discoveryButton("Pair device",14,snapshot.session_id)];}
  if(snapshot.pairing!=Kind::Complete&&snapshot.pairing!=Kind::Failed)[container addArrangedSubview:discoveryButton("Cancel pairing",15,snapshot.session_id)];
 }
 void renderDiscovery(){
  if(!discoveryStack||!discovery.snapshot)return;bool editing=codeField.currentEditor!=nil;std::string entered=codeField?std::string(codeField.stringValue.UTF8String):"";codeField=nil;
  for(NSView* v in [discoveryStack.arrangedSubviews copy]){[discoveryStack removeArrangedSubview:v];[v removeFromSuperview];}discoveryActions.clear();nextDiscoveryTag=10000;
  auto availability=discoveryButton("Available for discovery",10);availability.buttonType=NSButtonTypeSwitch;availability.state=snapshot.enabled?NSControlStateValueOn:NSControlStateValueOff;availability.enabled=snapshot.available;[discoveryStack addArrangedSubview:availability];
  [discoveryStack addArrangedSubview:[NSTextField wrappingLabelWithString:loc(@"Enable discovery once on each computer you want to connect.")]];
  for(const auto& iface:snapshot.interfaces){auto b=discoveryButton(iface.name.c_str(),16,iface.id);b.title=ls(iface.name);b.buttonType=NSButtonTypeRadio;b.state=iface.id==snapshot.interface_id?NSControlStateValueOn:NSControlStateValueOff;[discoveryStack addArrangedSubview:b];}
  auto search=discoveryButton(snapshot.searching?"Stop search":"Find devices on network",snapshot.searching?12:11);search.enabled=snapshot.available;[discoveryStack addArrangedSubview:search];
  for(const auto& d:snapshot.devices){auto row=[NSStackView new];row.orientation=NSUserInterfaceLayoutOrientationHorizontal;row.spacing=12;auto title=[NSTextField wrappingLabelWithString:ls(d.name+" · "+d.platform)];[title.widthAnchor constraintEqualToConstant:400].active=YES;[row addArrangedSubview:title];bool paired=std::any_of(snapshot.peers.begin(),snapshot.peers.end(),[&](const auto& p){return p.remote_device_id==d.device_id;});auto connect=discoveryButton(paired?"Paired":"Connect",17,d.device_id);connect.enabled=!paired&&snapshot.session_id.empty();[row addArrangedSubview:connect];[discoveryStack addArrangedSubview:row];}
  if(!snapshot.peers.empty()){[discoveryStack addArrangedSubview:[NSTextField labelWithString:loc(@"Paired devices")]];for(const auto& peer:snapshot.peers)[discoveryStack addArrangedSubview:[NSTextField wrappingLabelWithString:ls(peer.name)]];}
  bool pending=snapshot.pairing&&snapshot.pairing!=lan::PairingEvent::Kind::Complete&&snapshot.pairing!=lan::PairingEvent::Kind::Failed;
  bool hidden=!root.window.visible||root.isHiddenOrHasHiddenAncestor;
  if((pending&&(pairingPanel||hidden))||(pairingPanel.visible&&snapshot.pairing)){
   if(!pairingPanel){pairingPanel=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,480,340) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];pairingPanel.releasedWhenClosed=NO;pairingPanel.title=loc(@"Pair device");pairingPanel.level=NSFloatingWindowLevel;pairingPanel.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary;pairingStack=[NSStackView new];pairingStack.orientation=NSUserInterfaceLayoutOrientationVertical;pairingStack.alignment=NSLayoutAttributeLeading;pairingStack.spacing=12;pairingStack.edgeInsets=NSEdgeInsetsMake(20,20,20,20);pairingStack.frame=pairingPanel.contentView.bounds;pairingStack.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[pairingPanel.contentView addSubview:pairingStack];pairingTarget=[CliptiqLanAction new];std::weak_ptr<State> weak=shared_from_this();pairingTarget->closed=[weak]{if(auto s=weak.lock()){if(s->discovery.cancel&&!s->snapshot.session_id.empty())s->discovery.cancel(s->snapshot.session_id);}};pairingPanel.delegate=pairingTarget;[pairingPanel center];}
   for(NSView* v in [pairingStack.arrangedSubviews copy]){[pairingStack removeArrangedSubview:v];[v removeFromSuperview];}pairingView(pairingStack);if(!snapshot.message.empty())[pairingStack addArrangedSubview:[NSTextField wrappingLabelWithString:ls(tr(snapshot.message))]];if(!pending)[pairingStack addArrangedSubview:discoveryButton("Close",18)];if(!pairingPanel.visible){[pairingPanel makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
  }else{if(pairingPanel)[pairingPanel orderOut:nil];pairingView(discoveryStack);}
  if(codeField){codeField.stringValue=ls(entered);if(editing)[codeField.window makeFirstResponder:codeField];}
  if(!snapshot.message.empty())[discoveryStack addArrangedSubview:[NSTextField wrappingLabelWithString:ls(tr(snapshot.message))]];
 }
 void discoveryAction(NSInteger tag){auto it=discoveryActions.find(tag);if(it==discoveryActions.end())return;auto command=it->second;Result<void> result;
  if(command.first==10&&discovery.available)result=discovery.available(!snapshot.enabled);
  if(command.first==11&&discovery.search)result=discovery.search();if(command.first==12&&discovery.stop_search)discovery.stop_search();
  if(command.first==13&&discovery.accept)result=discovery.accept(command.second);
  if(command.first==14&&discovery.enter_code)result=discovery.enter_code(command.second,codeField?std::string(codeField.stringValue.UTF8String):"");
  if(command.first==15&&discovery.cancel)discovery.cancel(command.second);
  if(command.first==16&&discovery.select_interface)result=discovery.select_interface(command.second);
  if(command.first==17&&discovery.connect)result=discovery.connect(command.second);if(command.first==18){[pairingPanel orderOut:nil];return;}
  snapshot=discovery.snapshot();if(!result)snapshot.message=result.error().message;signature=discovery_signature(snapshot);renderDiscovery();
 }
 void setupDiscovery(){
  root=stack;if(!discovery.snapshot){manualExpanded=true;return;}
  discoveryStack=[NSStackView new];discoveryStack.orientation=NSUserInterfaceLayoutOrientationVertical;discoveryStack.alignment=NSLayoutAttributeLeading;discoveryStack.spacing=12;[root addArrangedSubview:discoveryStack];
  manualToggle=[NSButton buttonWithTitle:loc(@"Manual connection") target:target action:@selector(clicked:)];manualToggle.buttonType=NSButtonTypePushOnPushOff;manualToggle.tag=9000;[root addArrangedSubview:manualToggle];
  stack=[NSStackView new];stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=12;[root addArrangedSubview:stack];manualExpanded=false;stack.hidden=YES;snapshot=discovery.snapshot();signature=discovery_signature(snapshot);renderDiscovery();
 }
 void build(){for(NSView* v in [stack.arrangedSubviews copy]){[stack removeArrangedSubview:v];[v removeFromSuperview];}inputs.clear();actions.clear();
 auto info=[NSTextField wrappingLabelWithString:loc(@"Changes take effect only after Save. No peers means LAN is off.")];[stack addArrangedSubview:info];
 for(auto& f:draft.fields()){auto section=draft.section(f);if(!section.empty()){auto divider=[NSBox new];divider.boxType=NSBoxSeparator;[divider.widthAnchor constraintEqualToConstant:676].active=YES;[stack addArrangedSubview:divider];auto heading=[NSTextField labelWithString:ls(section)];heading.font=[NSFont boldSystemFontOfSize:14];[stack addArrangedSubview:heading];}auto row=[NSStackView new];row.orientation=NSUserInterfaceLayoutOrientationHorizontal;row.spacing=16;auto label=[NSTextField wrappingLabelWithString:ls(f.label)];[label.widthAnchor constraintEqualToConstant:330].active=YES;[row addArrangedSubview:label];NSControl* w=nil;if(f.check){auto b=[NSButton checkboxWithTitle:@"" target:nil action:nil];b.state=f.value=="1"?NSControlStateValueOn:NSControlStateValueOff;w=b;}else{NSTextField* e=f.secret?[NSSecureTextField new]:[NSTextField new];e.stringValue=ls(f.value);[e.widthAnchor constraintEqualToConstant:330].active=YES;w=e;}[row addArrangedSubview:w];[stack addArrangedSubview:row];inputs.push_back({f,w});auto help=[NSTextField wrappingLabelWithString:ls(LanSettingsDraft::explanation(f))];help.font=[NSFont systemFontOfSize:[NSFont smallSystemFontSize]];help.textColor=NSColor.secondaryLabelColor;[help.widthAnchor constraintEqualToConstant:676].active=YES;[stack addArrangedSubview:help];if(f.secret){auto peer=f.key.substr(0,f.key.size()-4);auto buttons=[NSStackView new];buttons.orientation=NSUserInterfaceLayoutOrientationHorizontal;buttons.spacing=12;[buttons addArrangedSubview:button(@"Generate key",1,peer)];[buttons addArrangedSubview:button(@"Copy key",2,peer)];[buttons addArrangedSubview:button(@"Remove peer",3,peer)];[stack addArrangedSubview:buttons];}}
 auto buttons=[NSStackView new];buttons.orientation=NSUserInterfaceLayoutOrientationHorizontal;buttons.spacing=16;auto add=button(@"Add peer",4);add.enabled=draft.config.peers.size()<16;[buttons addArrangedSubview:add];[buttons addArrangedSubview:button(@"Save",5)];[buttons addArrangedSubview:button(@"Cancel",6)];[stack addArrangedSubview:buttons];message=[NSTextField wrappingLabelWithString:loc(@"Ready. Pair keys are stored securely after Save.")];[stack addArrangedSubview:message];}
 Result<void> apply(){
  auto collected=collect();if(!collected){status(collected.error().message);return collected;}
  auto result=save(draft.config,draft.keys);if(!result){status(result.error().message);return result;}
  draft.keys.clear();saved=draft.config;
  if(embedded){build();status(tr("Network settings saved."));}else [window orderOut:nil];
  return {};
 }
 void action(NSInteger tag){if(tag==9000){manualExpanded=!manualExpanded;stack.hidden=!manualExpanded;return;}if(tag>=10000){discoveryAction(tag);return;}auto it=actions.find(tag);if(it==actions.end())return;int kind=it->second.first;auto peer=it->second.second;if(kind==5){(void)apply();return;}if(kind==6){draft.keys.clear();if(embedded){draft.config=saved;build();}else [window orderOut:nil];return;}auto r=collect();if(!r){status(r.error().message);return;}if(kind==1){auto k=generate();if(!k){status(k.error().message);return;}draft.keys[peer]=k.value();build();}if(kind==2){auto k=draft.keys.find(peer);if(k==draft.keys.end()){status(tr("Enter or generate a key to copy. Saved keys are not displayed."));return;}auto c=copy(k->second);status(c?tr("Key copied. Keep it private."):c.error().message);}if(kind==3){draft.remove(peer);build();}if(kind==4){draft.add();build();}}
};
NativeLanSettings::NativeLanSettings():state_(std::make_shared<State>()){}NativeLanSettings::~NativeLanSettings()=default;
Result<void> NativeLanSettings::show(lan::Config config,Save save,std::function<Result<std::string>()> generate,std::function<Result<void>(std::string)> copy,void* parent){auto s=state_;if(s->owner!=std::this_thread::get_id()||!NSThread.isMainThread)return Error{ErrorCode::InvalidArgument,"LAN settings require main thread"};if(!save||!generate||!copy)return Error{ErrorCode::InvalidArgument,"LAN settings callbacks required"};if(s->target){s->target->callback={};s->target->closed={};}[s->window close];s->window=nil;s->embedded=parent!=nullptr;s->saved=config;s->draft={std::move(config),{}};s->save=std::move(save);s->generate=std::move(generate);s->copy=std::move(copy);if(!parent){s->window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,760,680) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];s->window.releasedWhenClosed=NO;s->window.title=loc(@"Advanced LAN settings");s->window.minSize=NSMakeSize(740,400);auto scroll=[[NSScrollView alloc] initWithFrame:s->window.contentView.bounds];scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;scroll.hasVerticalScroller=YES;s->stack=[NSStackView new];s->stack.orientation=NSUserInterfaceLayoutOrientationVertical;s->stack.alignment=NSLayoutAttributeLeading;s->stack.spacing=12;s->stack.edgeInsets=NSEdgeInsetsMake(16,16,16,16);scroll.documentView=s->stack;s->stack.translatesAutoresizingMaskIntoConstraints=NO;[s->stack.leadingAnchor constraintEqualToAnchor:scroll.contentView.leadingAnchor].active=YES;[s->stack.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor].active=YES;[s->stack.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor].active=YES;[s->window.contentView addSubview:scroll];}else{s->stack=(__bridge NSStackView*)parent;}s->target=[CliptiqLanAction new];std::weak_ptr<State> weak=s;s->target->callback=[weak](NSInteger tag){if(auto s=weak.lock())s->action(tag);};s->target->closed=[weak]{if(auto s=weak.lock()){s->draft.keys.clear();s->viewVisible=false;if(s->discovery.close_view)s->discovery.close_view();}};s->window.delegate=s->target;s->setupDiscovery();s->build();s->viewVisible=true;if(!parent){[s->window center];[s->window makeKeyAndOrderFront:nil];}return {};}
Result<void> NativeLanSettings::process_events(){auto s=state_;if(s->owner!=std::this_thread::get_id()||!NSThread.isMainThread)return Error{ErrorCode::InvalidArgument,"LAN settings require main thread"};if(s->discovery.snapshot){bool visible=s->root&&s->root.window.visible&&!s->root.isHiddenOrHasHiddenAncestor;if(s->viewVisible&&!visible&&s->discovery.close_view)s->discovery.close_view();s->viewVisible=visible;auto snapshot=s->discovery.snapshot();auto signature=discovery_signature(snapshot);if(signature!=s->signature){s->snapshot=std::move(snapshot);s->signature=std::move(signature);s->renderDiscovery();}}return {};}
void NativeLanSettings::discovery(DiscoveryUiCallbacks callbacks){auto s=state_;s->discovery=std::move(callbacks);if(!s->target){s->target=[CliptiqLanAction new];std::weak_ptr<State> weak=s;s->target->callback=[weak](NSInteger tag){if(auto s=weak.lock())s->action(tag);};}if(!s->discoveryStack)s->discoveryStack=[NSStackView new];}
Result<void> NativeLanSettings::apply(){
 if(state_->owner!=std::this_thread::get_id()||!NSThread.isMainThread||!state_->stack||!state_->save)
  return Error{ErrorCode::InvalidArgument,"LAN settings are not open on the main thread"};
 return state_->apply();
}
void NativeLanSettings::status(std::string s){state_->status(std::move(s));}
}

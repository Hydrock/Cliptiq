#include "StartupPermissions.hpp"
#include "../LocaleNative.hpp"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <thread>
@interface CliptiqStartupActions : NSObject <NSWindowDelegate> {
@public std::function<void(NSInteger)> action;
}
- (void)clicked:(NSButton *)sender;
@end
@implementation CliptiqStartupActions
- (void)clicked:(NSButton *)sender { if(action)action(sender.tag); }
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender;if(action)action(0);return NO; }
@end
namespace cliptiq::macos {
namespace {
StartupPermissionCalls native_calls() {
 return {
  []{
   if(@available(macOS 15.4,*))return NSPasteboard.generalPasteboard.accessBehavior==NSPasteboardAccessBehaviorAlwaysAllow;
   return true;
  },
  []{return bool(AXIsProcessTrusted() && CGPreflightPostEventAccess());},
  []{
   // Only this explicit request may trigger an access alert. Discard the value;
   // no history store or capture monitor exists yet.
   @autoreleasepool {
    auto board=NSPasteboard.generalPasteboard;
    if(@available(macOS 15.4,*)){
     if(board.accessBehavior==NSPasteboardAccessBehaviorDefault || board.accessBehavior==NSPasteboardAccessBehaviorAsk)
      (void)[board dataForType:NSPasteboardTypeString];
    }
   }
   [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Pasteboard"]];
  },
  []{
   if(!AXIsProcessTrusted()){
    const void *keys[]={kAXTrustedCheckOptionPrompt};const void *values[]={kCFBooleanTrue};
    auto options=CFDictionaryCreate(nullptr,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    AXIsProcessTrustedWithOptions(options);CFRelease(options);
   }
   if(!CGPreflightPostEventAccess())CGRequestPostEventAccess();
   [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
  }
 };
}
NSTextField *label(NSString *text,CGFloat size){auto v=[NSTextField wrappingLabelWithString:text];v.font=[NSFont systemFontOfSize:size];return v;}
}
struct StartupPermissions::State {
 StartupPermissionCalls calls;
 std::thread::id owner=std::this_thread::get_id();
 NSPanel *window=nil;CliptiqStartupActions *target=nil;
 NSTextField *clipboard_status=nil,*paste_status=nil;
 NSButton *clipboard_button=nil,*paste_button=nil,*next=nil;
 bool accepted=false;
 bool ready(){return calls.clipboard_allowed() && calls.paste_allowed();}
 void refresh(){
  bool clipboard=calls.clipboard_allowed(),paste=calls.paste_allowed();
  clipboard_status.stringValue=[NSString stringWithFormat:@"%@: %@",loc(@"Clipboard access"),loc(clipboard?@"Allowed":@"Required")];
  paste_status.stringValue=[NSString stringWithFormat:@"%@: %@",loc(@"Automatic paste"),loc(paste?@"Allowed":@"Required")];
  clipboard_button.enabled=!clipboard;paste_button.enabled=!paste;next.enabled=clipboard&&paste;
 }
 void clicked(NSInteger tag){
  if(tag==0){[NSApp stopModalWithCode:NSModalResponseCancel];return;}
  if(tag==1)calls.request_clipboard();
  if(tag==2)calls.request_paste();
  refresh();
  if(tag==4 && ready()){accepted=true;[NSApp stopModalWithCode:NSModalResponseOK];}
 }
 void close(){if(target)target->action={};window.delegate=nil;[window orderOut:nil];[window close];window=nil;target=nil;clipboard_status=nil;paste_status=nil;clipboard_button=nil;paste_button=nil;next=nil;}
 ~State(){close();}
};
StartupPermissions::StartupPermissions():StartupPermissions(native_calls()){}
StartupPermissions::StartupPermissions(StartupPermissionCalls calls):state_(new State{std::move(calls)}){}
StartupPermissions::~StartupPermissions()=default;
bool StartupPermissions::run(){ @autoreleasepool {
 auto s=state_.get();
 if(s->owner!=std::this_thread::get_id() || !NSThread.isMainThread)return false;
 if(!s->calls.clipboard_allowed || !s->calls.paste_allowed || !s->calls.request_clipboard || !s->calls.request_paste)return false;
 if(s->ready())return true;
 [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
 s->accepted=false;s->target=[CliptiqStartupActions new];s->target->action=[s](NSInteger tag){s->clicked(tag);};
 s->window=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,660,440) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
 s->window.releasedWhenClosed=NO;s->window.identifier=@"startup-permissions";s->window.title=loc(@"Set up Cliptiq");s->window.delegate=s->target;
 s->window.hidesOnDeactivate=YES;s->window.level=NSModalPanelWindowLevel;
 s->window.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorCanJoinAllApplications|NSWindowCollectionBehaviorFullScreenAuxiliary;
 auto add=[s](NSView *view,CGFloat x,CGFloat y,CGFloat w,CGFloat h){view.frame=NSMakeRect(x,y,w,h);[s->window.contentView addSubview:view];};
 add(label(loc(@"Set up Cliptiq"),24),24,378,612,36);
 add(label(loc(@"Allow clipboard access and automatic paste before using Cliptiq. History capture and the quick menu start only after both permissions are granted."),14),24,314,612,58);
 s->clipboard_status=label(@"",15);add(s->clipboard_status,24,273,612,25);
 add(label(loc(@"In System Settings, give Cliptiq permanent clipboard access (Always Allow)."),12),24,238,612,32);
 auto button=[&](NSString *title,NSInteger tag,NSString *id,CGFloat x,CGFloat y,CGFloat w){auto b=[NSButton buttonWithTitle:title target:s->target action:@selector(clicked:)];b.tag=tag;b.identifier=id;add(b,x,y,w,34);return b;};
 s->clipboard_button=button(loc(@"Allow clipboard access"),1,@"permission-clipboard",24,198,612);
 s->paste_status=label(@"",15);add(s->paste_status,24,153,612,25);
 add(label(loc(@"Allow Cliptiq to control other apps so it can send Command-V to your active field."),12),24,117,612,32);
 s->paste_button=button(loc(@"Allow automatic paste"),2,@"permission-paste",24,77,612);
 button(loc(@"Quit"),0,@"permission-quit",24,20,160);
 button(loc(@"Check Again"),3,@"permission-refresh",202,20,210);
 s->next=button(loc(@"Continue"),4,@"permission-continue",430,20,206);
 s->refresh();[s->window center];[NSApp activateIgnoringOtherApps:YES];[s->window makeKeyAndOrderFront:nil];
 auto timer=[NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *){s->refresh();}];
 [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
 auto response=[NSApp runModalForWindow:s->window];
 [timer invalidate];bool allowed=response==NSModalResponseOK && s->accepted && s->ready();s->close();return allowed;
} }
}

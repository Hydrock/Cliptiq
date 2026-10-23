#import <AppKit/AppKit.h>
#import <mach/mach.h>
#include "native.h"
#include "core.h"
static NSWindow* window;
static NSStatusItem* status;
static NSAutoreleasePool* pool;
@interface ProbeActions : NSObject
- (void)copyFixture:(id)sender;
- (void)showWindow:(id)sender;
- (void)quit:(id)sender;
- (void)closeSmoke:(id)sender;
@end
@implementation ProbeActions
- (void)copyFixture:(id)sender {
    int idValue=(int)[sender tag]; unsigned char data[4096];
    NSPasteboard* board=[NSPasteboard generalPasteboard]; [board clearContents];
    const char* formats[]={"text/plain","image/png","text/html","text/rtf"};
    NSArray* native=@[NSPasteboardTypeString,NSPasteboardTypePNG,NSPasteboardTypeHTML,NSPasteboardTypeRTF];
    for (int i=0;i<4;i++) { int len=probe_get(idValue,formats[i],data,sizeof data); if (len>=0) [board setData:[NSData dataWithBytes:data length:len] forType:native[i]]; }
}
- (void)showWindow:(id)sender { [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES]; }
- (void)quit:(id)sender { [NSApp stop:nil]; }
- (void)closeSmoke:(id)sender { [window close]; [window setTitle:@"Retained after close"]; [NSApp stop:nil]; }
@end
static ProbeActions* actions;
bool native_setup(bool visible) {
    pool=[NSAutoreleasePool new];
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,460,180) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setTitle:@"Cliptiq — footprint prototype"];
    NSTextField* label=[NSTextField labelWithString:@"33 fixture entries / 35 representations\nSame AppKit shell for C++ and Rust cores.\nNo real clipboard history is monitored."];
    [label setFrame:NSMakeRect(20,40,420,100)]; [[window contentView] addSubview:label];
    actions=[ProbeActions new];
    if (visible) {
        status=[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength]; [[status button] setTitle:@"Cq"];
        NSMenu* menu=[NSMenu new]; NSArray* titles=@[@"Copy text",@"Copy PNG",@"Copy HTML",@"Copy RTF"];
        int ids[]={0,30,31,32};
        for(int i=0;i<4;i++){ NSMenuItem* item=[[NSMenuItem alloc] initWithTitle:titles[i] action:@selector(copyFixture:) keyEquivalent:@""]; [item setTag:ids[i]]; [item setTarget:actions]; [menu addItem:item]; }
        NSMenuItem* settings=[[NSMenuItem alloc] initWithTitle:@"Settings" action:@selector(showWindow:) keyEquivalent:@","]; [settings setTarget:actions]; [menu addItem:settings];
        NSMenuItem* quit=[[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(quit:) keyEquivalent:@"q"]; [quit setTarget:actions]; [menu addItem:quit];
        [status setMenu:menu]; [window makeKeyAndOrderFront:nil];
    }
    return window!=nil;
}
void native_loop(){ [NSApp run]; }
void native_schedule_close(){ [NSTimer scheduledTimerWithTimeInterval:0.03 target:actions selector:@selector(closeSmoke:) userInfo:nil repeats:NO]; }
void native_cleanup(){ if(status) [[NSStatusBar systemStatusBar] removeStatusItem:status]; status=nil; [window close]; [window release]; window=nil; [actions release]; actions=nil; [pool drain]; pool=nil; }
size_t native_memory(){ mach_task_basic_info info{}; mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT; return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count)==KERN_SUCCESS ? info.resident_size : 0; }

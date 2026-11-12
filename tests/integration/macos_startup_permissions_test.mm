#include "StartupPermissions.hpp"
#include "cliptiq/locale.hpp"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq::macos;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
static NSView *find(NSView *root,NSString *id){if([root.identifier isEqualToString:id])return root;for(NSView *child in root.subviews)if(auto found=find(child,id))return found;return nil;}
static NSWindow *setup(){for(NSWindow *window in NSApp.windows)if([window.identifier isEqualToString:@"startup-permissions"])return window;return nil;}
int main(int argc,char **argv){@autoreleasepool{try{
 [NSApplication sharedApplication];bool russian=argc>1&&std::string(argv[1])=="--ru";cliptiq::activate_locale(russian?"ru":"en");
 struct Probe {bool clipboard=true,paste=true,observed=false,finished=false;unsigned requests=0,stage=0;std::string error;};
 auto probe=std::make_shared<Probe>();
 StartupPermissionCalls calls{[probe]{return probe->clipboard;},[probe]{return probe->paste;},[probe]{++probe->requests;probe->clipboard=true;},[probe]{++probe->requests;probe->paste=true;}};
 {StartupPermissions gate(calls);CHECK(gate.run());CHECK(!setup() && probe->requests==0);}
 probe->clipboard=probe->paste=false;
 auto timer=[NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *){try{
  auto window=setup();CHECK(window && window.visible);probe->observed=true;
  CHECK(window.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces);
  CHECK(window.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllApplications);
  CHECK([window.title isEqualToString:russian?@"Подготовка Cliptiq":@"Set up Cliptiq"]);
  auto next=(NSButton *)find(window.contentView,@"permission-continue");CHECK(next);
  if(probe->stage==0){CHECK(!next.enabled && probe->requests==0);[next performClick:nil];CHECK(!probe->finished);[(NSButton *)find(window.contentView,@"permission-clipboard") performClick:nil];CHECK(probe->clipboard && !probe->paste && probe->requests==1);probe->stage=1;}
  else if(probe->stage==1){CHECK(!next.enabled);[(NSButton *)find(window.contentView,@"permission-paste") performClick:nil];CHECK(probe->paste && probe->requests==2);probe->stage=2;}
  else if(probe->stage==2 && next.enabled){probe->clipboard=false;[next performClick:nil];CHECK(!next.enabled && !probe->finished);probe->clipboard=true;[(NSButton *)find(window.contentView,@"permission-refresh") performClick:nil];probe->stage=3;}
  else if(probe->stage==3 && next.enabled){[next performClick:nil];probe->stage=4;}
 }catch(const std::exception&e){probe->error=e.what();[NSApp abortModal];}}];
 [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];
 {StartupPermissions gate(calls);CHECK(gate.run());probe->finished=true;}
 [timer invalidate];CHECK(probe->error.empty() && probe->observed && probe->stage==4 && probe->requests==2);CHECK(!setup());
 probe->clipboard=false;probe->observed=false;
 auto cancel=[NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *){auto window=setup();if(window){probe->observed=true;[window performClose:nil];}}];
 [[NSRunLoop mainRunLoop] addTimer:cancel forMode:NSModalPanelRunLoopMode];
 {StartupPermissions gate(calls);CHECK(!gate.run());}[cancel invalidate];CHECK(probe->observed && !setup());
 std::cout<<"PASS startup permissions: ready, blocked, independent requests, continue and close\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}}

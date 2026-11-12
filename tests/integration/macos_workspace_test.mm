#include "cliptiq/native_menu.hpp"
#include "cliptiq/locale.hpp"
#include "cliptiq/settings.hpp"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
static NSView *find(NSView *root, NSString *identifier) {
 if([root.identifier isEqualToString:identifier])return root;
 for(NSView *child in root.subviews)if(auto found=find(child,identifier))return found;
 return nil;
}
static NSButton *button(NSView *root, NSInteger tag) {
 if([root isKindOfClass:NSButton.class]&&((NSButton *)root).tag==tag)return (NSButton *)root;
 for(NSView *child in root.subviews)if(auto found=button(child,tag))return found;
 return nil;
}
int main(int argc, char **argv) { @autoreleasepool { try {
 const bool russian=argc>1 && std::string(argv[1])=="--ru";
 activate_locale(russian?"ru":"en");
 MenuModel model;model.revision=7;
 MenuItem h;h.label="History entry";h.action=MenuAction::SelectHistory;h.id="history";h.tooltip="history searchable";h.source_device="PC";
 MenuItem snippet;snippet.label="Snippet entry";snippet.action=MenuAction::SelectSnippet;snippet.id="snippet";
 model.items={h,snippet};
 std::vector<MenuCommand> commands;unsigned previews=0,releases=0;
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;options.transient_popup=true;
 options.workspace_model=[&]()->Result<MenuModel>{return model;};
 options.workspace_preview=[&](const MenuCommand &c)->Result<std::string>{++previews;return c.id+" full text\nSecond line";};
 options.workspace_image=[&](std::optional<PreviewRef> ref)->std::optional<MenuImage>{if(!ref)++releases;return std::nullopt;};
 NativeSettings settingsWindow;
 unsigned settingsOpened=0;
 NativeMenu ui(options,[&](MenuCommand c){
  if(c.action==MenuAction::OpenSettings){
   Capabilities caps;caps.platform=CapturePlatform::MacOS;
   CHECK(settingsWindow.show(default_settings(CapturePlatform::MacOS),caps,[](Settings)->Result<void>{return {};}));
   ++settingsOpened;
  }
  commands.push_back(std::move(c));
 });CHECK(ui.initialize());CHECK(ui.update(model));CHECK(ui.native_handles().menu==0);
 auto window=(__bridge NSWindow *)(void *)ui.native_handles().window;
 if(ui.status().tray_available){CHECK(!window.visible);CHECK(!find(window.contentView,@"workspace-list"));CHECK(previews==0);}
 CHECK(ui.show_workspace());CHECK(window.visible);
 auto list=(NSTableView *)find(window.contentView,@"workspace-list");CHECK(list);CHECK(list.numberOfRows==1);
 CHECK([button(window.contentView,1).title isEqualToString:russian?@"История":@"History"]);
 CHECK([button(window.contentView,4).title isEqualToString:russian?@"Редактор сниппетов":@"Edit Snippets"]);
 CHECK([button(window.contentView,5).title isEqualToString:russian?@"Копировать":@"Copy"]);
 CHECK([list.tableColumns[0].title isEqualToString:russian?@"Содержимое":@"Content"]);
 CHECK([list.tableColumns[1].title isEqualToString:russian?@"Тип / папка":@"Type / folder"]);
 CHECK(list.tableColumns.count==3);
 CHECK([list.tableColumns[2].title isEqualToString:russian?@"Источник":@"Source"]);
 auto cell=[&](NSUInteger column){return (NSTextField *)[list.delegate tableView:list viewForTableColumn:list.tableColumns[column] row:0];};
 CHECK([cell(0).stringValue isEqualToString:@"History entry"]);
 CHECK([cell(2).stringValue isEqualToString:@"PC"]);

 auto preview=(NSTextView *)find(window.contentView,@"workspace-preview");CHECK(preview&&!preview.editable&&preview.selectable);CHECK([preview.string containsString:@"history full text"]);
 [button(window.contentView,5) performClick:nil];CHECK(commands.size()==1);CHECK(commands.back().id=="history"&&commands.back().invocation.copy_only&&!commands.back().invocation.delete_only);
 auto search=(NSSearchField *)find(window.contentView,@"workspace-filter");search.stringValue=@"absent";
 [search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];CHECK(list.numberOfRows==0);CHECK(!button(window.contentView,5).enabled);
 CHECK([preview.string isEqualToString:russian?@"По этому фильтру ничего не найдено.":@"No entries match this filter."]);
 search.stringValue=@"";[search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];
 [button(window.contentView,2) performClick:nil];CHECK(list.numberOfRows==1);
 CHECK([cell(2).stringValue isEqualToString:russian?@"Это устройство":@"This device"]);CHECK(!button(window.contentView,6).enabled);CHECK(!button(window.contentView,7).enabled);
 [button(window.contentView,5) performClick:nil];CHECK(commands.back().id=="snippet");
 // Exercise the actual workspace button with a real settings window, including
 // AppKit layout and close/reopen; a command-only assertion misses show failures.
 for(unsigned n=1;n<=2;++n){
  auto settingsButton=button(window.contentView,3);
  [settingsButton performClick:nil];
  CHECK(settingsOpened==n && commands.back().action==MenuAction::OpenSettings);
  auto settings=(__bridge NSWindow *)(void *)settingsWindow.native_handle();
  CHECK(settings.visible);
  [settings.contentView layoutSubtreeIfNeeded];
  CHECK(ui.process_events(10));
  CHECK([settings.title isEqualToString:russian?@"Настройки Cliptiq":@"Cliptiq Settings"]);
  CHECK(!settingsButton.highlighted);
  [settings performClose:nil];
  CHECK(!settings.visible);
 }
 ui.workspace_status("copied");CHECK([((NSTextField *)find(window.contentView,@"workspace-status")).stringValue isEqualToString:@"copied"]);
 [window performClose:nil];CHECK(!window.visible);CHECK(!ui.status().launcher_visible);CHECK(ui.process_events(10));CHECK(!find(window.contentView,@"workspace-list"));CHECK(releases>0);
 CHECK(ui.update(model));CHECK(ui.native_handles().menu==0);CHECK(!window.visible);
 CHECK([NSApp.delegate respondsToSelector:@selector(applicationShouldHandleReopen:hasVisibleWindows:)]);
 [NSApp.delegate applicationShouldHandleReopen:NSApp hasVisibleWindows:NO];CHECK(window.visible);CHECK(ui.native_handles().menu==0);
 CHECK(ui.show_workspace());CHECK(find(window.contentView,@"workspace-list"));
 // Exercise real AppKit tracking repeatedly, rather than the popup seam used
 // by the owner-policy test. Each cancelled popup must return and release UI.
 [window performClose:nil];CHECK(ui.process_events(10));
 for(unsigned attempt=0;attempt<3;++attempt){
  __block bool observed=false;
  auto popupUi=&ui;
  auto timer=[NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *){
   auto menu=(__bridge NSMenu *)(void *)popupUi->native_handles().menu;
   if(menu){observed=true;[menu cancelTracking];}
  }];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSEventTrackingRunLoopMode];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
  InvocationContext context;context.keyboard=true;context.target="repeat-test";
  CHECK(ui.show(context));[timer invalidate];
  CHECK(observed);CHECK(ui.native_handles().menu==0);CHECK(!window.visible);
 }
 std::cout<<"PASS repeated real AppKit popup cancellation\n";
 std::cout<<"macOS workspace passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;} } }

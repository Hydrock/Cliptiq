#include "cliptiq/native_menu.hpp"
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
int main() { @autoreleasepool { try {
 MenuModel model;model.revision=7;
 MenuItem h;h.label="History entry";h.action=MenuAction::SelectHistory;h.id="history";h.tooltip="history searchable";
 MenuItem snippet;snippet.label="Snippet entry";snippet.action=MenuAction::SelectSnippet;snippet.id="snippet";
 model.items={h,snippet};
 std::vector<MenuCommand> commands;unsigned previews=0,releases=0;
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;options.transient_popup=true;
 options.workspace_model=[&]()->Result<MenuModel>{return model;};
 options.workspace_preview=[&](const MenuCommand &c)->Result<std::string>{++previews;return c.id+" full text\nSecond line";};
 options.workspace_image=[&](std::optional<PreviewRef> ref)->std::optional<MenuImage>{if(!ref)++releases;return std::nullopt;};
 NativeMenu ui(options,[&](MenuCommand c){commands.push_back(std::move(c));});CHECK(ui.initialize());CHECK(ui.update(model));CHECK(ui.native_handles().menu==0);
 auto window=(__bridge NSWindow *)(void *)ui.native_handles().window;
 if(ui.status().tray_available){CHECK(!window.visible);CHECK(!find(window.contentView,@"workspace-list"));CHECK(previews==0);}
 CHECK(ui.show_workspace());CHECK(window.visible);
 auto list=(NSTableView *)find(window.contentView,@"workspace-list");CHECK(list);CHECK(list.numberOfRows==1);
 auto preview=(NSTextView *)find(window.contentView,@"workspace-preview");CHECK(preview&&!preview.editable&&preview.selectable);CHECK([preview.string containsString:@"history full text"]);
 [button(window.contentView,5) performClick:nil];CHECK(commands.size()==1);CHECK(commands.back().id=="history"&&commands.back().invocation.copy_only&&!commands.back().invocation.delete_only);
 auto search=(NSSearchField *)find(window.contentView,@"workspace-filter");search.stringValue=@"absent";
 [search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];CHECK(list.numberOfRows==0);CHECK(!button(window.contentView,5).enabled);
 search.stringValue=@"";[search.delegate controlTextDidChange:[NSNotification notificationWithName:NSControlTextDidChangeNotification object:search]];
 [button(window.contentView,2) performClick:nil];CHECK(list.numberOfRows==1);CHECK(!button(window.contentView,6).enabled);CHECK(!button(window.contentView,7).enabled);
 [button(window.contentView,5) performClick:nil];CHECK(commands.back().id=="snippet");
 ui.workspace_status("copied");CHECK([((NSTextField *)find(window.contentView,@"workspace-status")).stringValue isEqualToString:@"copied"]);
 [window performClose:nil];CHECK(!window.visible);CHECK(!ui.status().launcher_visible);CHECK(ui.process_events(10));CHECK(!find(window.contentView,@"workspace-list"));CHECK(releases>0);
 CHECK(ui.update(model));CHECK(ui.native_handles().menu==0);CHECK(!window.visible);
 CHECK([NSApp.delegate respondsToSelector:@selector(applicationShouldHandleReopen:hasVisibleWindows:)]);
 [NSApp.delegate applicationShouldHandleReopen:NSApp hasVisibleWindows:NO];CHECK(window.visible);CHECK(ui.native_handles().menu==0);
 CHECK(ui.show_workspace());CHECK(find(window.contentView,@"workspace-list"));
 std::cout<<"macOS workspace passed\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;} } }

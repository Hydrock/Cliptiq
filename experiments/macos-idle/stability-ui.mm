#include "cliptiq/native_menu.hpp"
#include "cliptiq/settings.hpp"
#include "cliptiq/editor.hpp"
#include "platform/LanSettings.hpp"
#import <AppKit/AppKit.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/resource.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
NSView* find(NSView* root,NSString* id){if([root.identifier isEqualToString:id])return root;for(NSView* v in root.subviews)if(auto result=find(v,id))return result;return nil;}
NSButton* named(NSView* root,NSString* name){if([root isKindOfClass:NSButton.class]&&[((NSButton*)root).title isEqualToString:name])return (NSButton*)root;for(NSView* v in root.subviews)if(auto result=named(v,name))return result;return nil;}
struct WeakView{__weak NSView* view;};
unsigned alive(const std::vector<WeakView>& views){unsigned count=0;for(const auto& v:views)if(v.view)++count;return count;}
void metric(unsigned cycle,unsigned workspaceAlive=0,unsigned settingsAlive=0){mach_timebase_info_data_t tb{};mach_timebase_info(&tb);rusage_info_v2 r{};CHECK(proc_pid_rusage(getpid(),RUSAGE_INFO_V2,(rusage_info_t*)&r)==0);std::cout<<"{\"cycle\":"<<cycle<<",\"pid\":"<<getpid()<<",\"rss_bytes\":"<<r.ri_resident_size<<",\"footprint_bytes\":"<<r.ri_phys_footprint<<",\"cpu_ticks\":"<<r.ri_user_time+r.ri_system_time<<",\"timebase_numer\":"<<tb.numer<<",\"timebase_denom\":"<<tb.denom<<",\"workspace_roots_alive\":"<<workspaceAlive<<",\"settings_roots_alive\":"<<settingsAlive<<"}"<<std::endl;}
int main(int argc,char** argv){@autoreleasepool{try{
 unsigned cycles=argc>1?unsigned(std::stoul(argv[1])):200;unsigned hold=argc>2?unsigned(std::stoul(argv[2])):0;
 auto path=std::filesystem::temp_directory_path()/("cliptiq-ui-stability-"+std::to_string(getpid())+".db");
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);std::filesystem::remove(p.string()+"-wal",e);std::filesystem::remove(p.string()+"-shm",e);}}cleanup{path};
 auto store=take(open_store(path.string()));auto folder=take(create_folder(store,"Cliptiq stress fixture"));
 for(unsigned n=0;n<10;++n){Snippet snippet;snippet.folder_id=folder;snippet.title="Fixture "+std::to_string(n);snippet.content=std::string(4096,'x');take(save_snippet(store,snippet));}
 auto session=std::make_shared<SnippetEditorSession>(store,SnippetEditorCallbacks{});CHECK(session->reload());
 NativeSettings settings;NativeSnippetEditor editor;NativeLanSettings lanUi;
 lan::Config config;config.device_id="fixture";config.device_name="Cliptiq stress";
 lan::DiscoverySnapshot snapshot;snapshot.available=true;snapshot.enabled=true;snapshot.interfaces={{"fixture","Fixture LAN","192.0.2.1",1}};
 DiscoveryUiCallbacks callbacks;callbacks.snapshot=[&]{return snapshot;};callbacks.close_view=[]{};lanUi.discovery(callbacks);
 settings.advanced([&](void* parent){CHECK(lanUi.show(config,[](lan::Config&,std::map<std::string,std::string>)->Result<void>{return {};},[]()->Result<std::string>{return std::string(64,'a');},[](std::string)->Result<void>{return {};},parent));});
 MenuModel model;model.revision=1;MenuItem item;item.action=MenuAction::SelectHistory;item.id="fixture-image";item.label="Cliptiq stress image";item.tooltip="Synthetic data only";item.kind=CanonicalKind::Image;item.preview=PreviewRef{item.id,{}};model.items={item};
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;options.tray_style=TrayStyle::Hidden;
 options.workspace_model=[&]()->Result<MenuModel>{return model;};options.workspace_preview=[](const MenuCommand&)->Result<std::string>{return std::string(16384,'T');};
 options.workspace_image=[](std::optional<PreviewRef> ref)->std::optional<MenuImage>{if(!ref)return {};return MenuImage{512,512,Bytes(512*512*4,160)};};
 NativeMenu ui(options,[](MenuCommand){});CHECK(ui.initialize());CHECK(ui.update(model));
 Capabilities caps;caps.platform=CapturePlatform::MacOS;
 std::vector<WeakView> workspaceRoots,settingsRoots;metric(0);
 for(unsigned n=1;n<=cycles;++n){
  __weak NSView* workspaceRoot=nil;__weak NSView* settingsRoot=nil;
  @autoreleasepool{
   CHECK(ui.show_workspace());auto window=(__bridge NSWindow*)(void*)ui.native_handles().window;workspaceRoot=window.contentView;
   CHECK(find(window.contentView,@"workspace-image"));[window.contentView layoutSubtreeIfNeeded];
   // Alternate decoded image and text, then close while preview state is live.
   model.items[0].preview=n%2?std::optional<PreviewRef>{PreviewRef{item.id,{}}}:std::nullopt;++model.revision;CHECK(ui.update(model));
   [window performClose:nil];CHECK(ui.process_events(1));
   CHECK(settings.show(default_settings(CapturePlatform::MacOS),caps,[](Settings)->Result<void>{return {};}));
   auto sw=(__bridge NSWindow*)(void*)settings.native_handle();settingsRoot=sw.contentView;auto tabs=(NSTabView*)find(sw.contentView,@"settings-tabs");CHECK(tabs);
   for(unsigned page=0;page<10;++page){[tabs selectTabViewItemAtIndex:page];CHECK(lanUi.process_events());}
   auto advanced=named(sw.contentView,@"Advanced settings");CHECK(advanced);[advanced performClick:nil];[sw.contentView layoutSubtreeIfNeeded];
   [sw performClose:nil];CHECK(settings.process_events());CHECK(lanUi.process_events());
   if(!std::getenv("CLIPTIQ_STRESS_SKIP_EDITOR")){if(std::getenv("CLIPTIQ_STRESS_NO_ANIMATION")){[NSAnimationContext beginGrouping];NSAnimationContext.currentContext.duration=0;}CHECK(editor.show(session));if(std::getenv("CLIPTIQ_STRESS_NO_ANIMATION"))[NSAnimationContext endGrouping];CHECK(editor.process_events());CHECK(take(editor.request_close()));}
   CHECK(ui.process_events(2));
  }
  @autoreleasepool{CHECK(ui.process_events(2));CHECK(lanUi.process_events());}
  workspaceRoots.push_back({workspaceRoot});settingsRoots.push_back({settingsRoot});
  if(n%20==0||n==cycles){@autoreleasepool{auto until=[NSDate dateWithTimeIntervalSinceNow:(n%100==0?10:1)];while(until.timeIntervalSinceNow>0){CHECK(ui.process_events(5));CHECK(editor.process_events());CHECK(lanUi.process_events());}}metric(n,alive(workspaceRoots),alive(settingsRoots));}
 }
 std::cout<<"READY cycles completed; isolated database and no system clipboard writes"<<std::endl;
 auto until=std::chrono::steady_clock::now()+std::chrono::seconds(hold);while(std::chrono::steady_clock::now()<until){@autoreleasepool{CHECK(ui.process_events(20));CHECK(editor.process_events());CHECK(lanUi.process_events());}}
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}}

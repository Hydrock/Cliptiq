#include "cliptiq/settings.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include <commctrl.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
GtkWidget* find(GtkWidget*w,const char*name){if(std::string(gtk_widget_get_name(w))==name)return w;if(GTK_IS_CONTAINER(w)){auto children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget*out=nullptr;for(auto p=children;p&&!out;p=p->next)out=find(GTK_WIDGET(p->data),name);g_list_free(children);return out;}return nullptr;}
#endif
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T>r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
#ifdef __APPLE__
NSView*find(NSView*view,NSString*key){if([view.identifier isEqualToString:key])return view;for(NSView*child in view.subviews){auto out=find(child,key);if(out)return out;}return nil;}
#endif
int run(){try{
#ifdef _WIN32
IsolatedClipboard station;
#endif
auto store=take(open_store(":memory:"));auto settings=default_settings(CapturePlatform::X11);Capabilities caps;caps.platform=CapturePlatform::X11;caps.capture={CapabilityStatus::Available,"fixture"};caps.auto_paste={CapabilityStatus::Limited,"fixture"};unsigned applied=0;
NativeSettings ui;CHECK(ui.show(settings,caps,[&](Settings s){++applied;return save_settings(store,s);}));CHECK(ui.native_handle());
#ifdef _WIN32
auto window=reinterpret_cast<HWND>(ui.native_handle());CHECK(TabCtrl_GetItemCount(GetDlgItem(window,200))==8);CHECK(SetWindowTextW(GetDlgItem(window,1000),L"5"));SendMessageW(window,WM_COMMAND,100,0);
#elif defined(__APPLE__)
auto window=(__bridge NSWindow *)(void*)ui.native_handle();auto tabs=find(window.contentView,@"settings-tabs");CHECK([tabs isKindOfClass:NSTabView.class]&&[(NSTabView*)tabs numberOfTabViewItems]==8);auto field=(NSTextField*)find(window.contentView,@"history_count");CHECK(field);field.stringValue=@"5";[(NSButton*)find(window.contentView,@"apply") performClick:nil];
#else
auto window=reinterpret_cast<GtkWidget*>(ui.native_handle());auto tabs=find(window,"settings-tabs");CHECK(tabs&&gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs))==8);auto field=find(window,"history_count");CHECK(GTK_IS_ENTRY(field));gtk_entry_set_text(GTK_ENTRY(field),"5");auto apply=find(window,"apply");CHECK(GTK_IS_BUTTON(apply));gtk_button_clicked(GTK_BUTTON(apply));auto future=find(window,"login");CHECK(future&&!gtk_widget_get_sensitive(future));
#endif
CHECK(ui.process_events());CHECK(applied==1&&settings_history(take(load_settings(store))).max_count==5);std::cout<<"PASS eight_native_panels_apply_to_real_store_and_disabled_future_controls\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL native_settings: "<<e.what()<<'\n';return 1;}}
int main(){
#ifdef __APPLE__
@autoreleasepool{return run();}
#else
return run();
#endif
}

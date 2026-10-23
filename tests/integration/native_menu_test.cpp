#include "cliptiq/native_menu.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
#endif
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
void ok(Result<void>r){if(!r)throw std::runtime_error(r.error().message);}
int main(){try{
#ifdef _WIN32
 IsolatedClipboard isolation;
#elif !defined(__APPLE__)
 CHECK(getenv("CLIPTIQ_TEST_ISOLATED"));
#endif
 unsigned selected=0;MenuCommand command;NativeMenuOptions options;options.tray_style=TrayStyle::Hidden;NativeMenu ui(options,[&](MenuCommand c){++selected;command=std::move(c);});ok(ui.initialize());CHECK(ui.status().launcher_visible&&!ui.status().tray_available);std::cout<<"PASS hidden_tray_keeps_launcher\n";
 MenuModel m;m.revision=7;MenuItem row;row.label="A&B_😀";row.id="entry-id";row.action=MenuAction::SelectHistory;row.tooltip="private tooltip";row.number=1;row.shortcut="1";row.preview=PreviewRef{row.id,0xff8800ff};m.items.push_back(row);MenuItem disabled;disabled.label="Clear History";disabled.enabled=false;disabled.action=MenuAction::ClearHistory;m.items.push_back(disabled);ok(ui.update(m));auto handles=ui.native_handles();CHECK(handles.window&&handles.menu);
#ifdef _WIN32
 auto menu=reinterpret_cast<HMENU>(handles.menu);CHECK(GetMenuItemCount(menu)==2);auto id=GetMenuItemID(menu,0);CHECK(GetMenuState(menu,1,MF_BYPOSITION)&MF_DISABLED);SendMessageW(reinterpret_cast<HWND>(handles.window),WM_COMMAND,id,0);
#elif defined(__APPLE__)
 NSMenu*menu=(__bridge NSMenu*)(void*)handles.menu;CHECK(menu.numberOfItems==2);CHECK(![menu itemAtIndex:1].enabled);[menu performActionForItemAtIndex:0];
#else
 auto children=gtk_container_get_children(GTK_CONTAINER((void*)handles.menu));CHECK(g_list_length(children)==2);auto first=GTK_WIDGET(children->data);g_object_ref(first);CHECK(!gtk_widget_get_sensitive(GTK_WIDGET(children->next->data)));gtk_menu_item_activate(GTK_MENU_ITEM(first));g_list_free(children);
#endif
 CHECK(selected==1&&command.id=="entry-id"&&command.revision==7);std::cout<<"PASS native_structure_enabled_labels_preview_and_selection\n";
 m.revision=8;m.items[0].id="replacement";ok(ui.update(m));
#ifdef _WIN32
 SendMessageW(reinterpret_cast<HWND>(handles.window),WM_COMMAND,id,0);
#elif defined(__APPLE__)
 [menu performActionForItemAtIndex:0];
#else
 gtk_menu_item_activate(GTK_MENU_ITEM(first));g_object_unref(first);
#endif
 CHECK(selected==1);std::cout<<"PASS old_native_item_cannot_select_replacement\n";
 ok(ui.set_tray_style(TrayStyle::Standard));ok(ui.process_events(5));ok(ui.set_tray_style(TrayStyle::Reversed));ok(ui.process_events(5));ok(ui.set_tray_style(TrayStyle::Hidden));CHECK(ui.status().launcher_visible);std::cout<<"PASS tray_style_changes_preserve_fallback\n";
 return 0;}catch(const std::exception&e){std::cerr<<"FAIL native_menu: "<<e.what()<<'\n';return 1;}}

#include "cliptiq/editor.hpp"
#include "cliptiq/settings.hpp"
#include "cliptiq/menu.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include <commctrl.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#else
#include <gtk/gtk.h>
#endif
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T>r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
#ifndef _WIN32
#ifdef __APPLE__
NSView* find(NSView*w,NSString*name){if([w.identifier isEqualToString:name])return w;for(NSView*c in w.subviews){auto r=find(c,name);if(r)return r;}return nil;}
#else
GtkWidget* find(GtkWidget*w,const char*name){if(std::string(gtk_widget_get_name(w))==name)return w;if(GTK_IS_CONTAINER(w)){auto children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget*r=nullptr;for(auto c=children;c&&!r;c=c->next)r=find(GTK_WIDGET(c->data),name);g_list_free(children);return r;}return nullptr;}
#endif
#endif
struct Backend:HotkeyBackend {unsigned serial=0;std::map<unsigned,std::string> held;Result<unsigned> acquire(const ActionId&,const KeyChord&c)override{auto v=format_chord(c);if(v=="Ctrl+Alt+W")return Error{ErrorCode::Busy,"fixture external conflict"};for(auto&p:held)if(p.second==v)return Error{ErrorCode::Busy,"duplicate"};held[++serial]=v;return serial;}void release(unsigned i)override{held.erase(i);}Result<void>dispatch()override{return {};}Capability capability()const override{return {CapabilityStatus::Available,"fixture"};}};
int run(){try{
#ifdef _WIN32
 IsolatedClipboard station;
#endif
 auto store=take(open_store(":memory:"));Backend backend;HotkeyRegistry registry(backend);auto f=take(create_folder(store,"Folder A"));auto other=take(create_folder(store,"Folder B"));
 SnippetEditorCallbacks cb;unsigned changes=0;cb.changed=[&]{++changes;};cb.bind_folder=[&](const FolderId&id,auto chord){auto s=take(load_settings(store));s.hotkeys["folder:"+id]=chord;return apply_settings(store,registry,s);};cb.folder_chord=[&](const FolderId&id){return take(load_settings(store)).hotkeys.at("folder:"+id);};
 auto session=std::make_shared<SnippetEditorSession>(store,cb);CHECK(session->reload());CHECK(session->select(SnippetItemId{SnippetItemKind::Folder,f}));NativeSnippetEditor ui;CHECK(ui.show(session));CHECK(ui.native_handle());
#ifdef _WIN32
 auto window=reinterpret_cast<HWND>(ui.native_handle());auto click=[&](int id){SendMessageW(window,WM_COMMAND,id,0);CHECK(ui.process_events());};click(508);auto selected=session->selected();CHECK(selected&&selected->kind==SnippetItemKind::Snippet);auto id=selected->value;
 auto body=GetDlgItem(window,504);CHECK(body);CHECK(SetWindowTextW(body,L"  Привет\r\nline2 😀  "));SendMessageW(window,WM_COMMAND,MAKEWPARAM(504,EN_CHANGE),reinterpret_cast<LPARAM>(body));
 CHECK(session->content()==u8"  Привет\nline2 😀  ");click(510);CHECK(session->content().empty());click(511);CHECK(session->content()==u8"  Привет\nline2 😀  ");SendMessageW(GetDlgItem(window,503),BM_SETCHECK,BST_UNCHECKED,0);click(503);
#elif defined(__APPLE__)
 auto window=(__bridge NSWindow*)(void*)ui.native_handle();auto click=[&](NSString*name){auto b=(NSButton*)find(window.contentView,name);CHECK(b);[b performClick:nil];};click(@"editor-add-snippet");auto selected=session->selected();CHECK(selected&&selected->kind==SnippetItemKind::Snippet);auto id=selected->value;
 auto body=(NSTextView*)find(window.contentView,@"editor-content");CHECK(body);body.string=@"  Привет\nline2 😀  ";[body.delegate textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:body]];CHECK(session->content()==u8"  Привет\nline2 😀  ");click(@"editor-undo");CHECK(session->content().empty());click(@"editor-redo");auto enabled=(NSButton*)find(window.contentView,@"editor-enabled");enabled.state=NSControlStateValueOff;[NSApp sendAction:enabled.action to:enabled.target from:enabled];
#else
 auto window=reinterpret_cast<GtkWidget*>(ui.native_handle());auto click=[&](const char*name){auto b=find(window,name);CHECK(GTK_IS_BUTTON(b));gtk_button_clicked(GTK_BUTTON(b));};click("editor-add-snippet");auto selected=session->selected();CHECK(selected&&selected->kind==SnippetItemKind::Snippet);auto id=selected->value;
 auto body=find(window,"editor-content");CHECK(GTK_IS_TEXT_VIEW(body));gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(body)),u8"  Привет\nline2 😀  ",-1);CHECK(session->content()==u8"  Привет\nline2 😀  ");click("editor-undo");CHECK(session->content().empty());click("editor-redo");auto enabled=find(window,"editor-enabled");CHECK(GTK_IS_TOGGLE_BUTTON(enabled));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled),FALSE);
#endif
 CHECK(!take(get_snippet(store,id)).enabled);auto menu=take(build_folder_menu(store,f));CHECK(menu.items.size()==1&&!menu.items[0].enabled);CHECK(session->select(SnippetItemId{SnippetItemKind::Folder,other}));CHECK(!session->can_undo());CHECK(ui.show(session));
#ifdef _WIN32
 window=reinterpret_cast<HWND>(ui.native_handle());CHECK(SetWindowTextW(GetDlgItem(window,505),L"Ctrl+Alt+Q"));SendMessageW(window,WM_COMMAND,506,0);
#elif defined(__APPLE__)
 window=(__bridge NSWindow*)(void*)ui.native_handle();auto shortcut=(NSTextField*)find(window.contentView,@"editor-shortcut");CHECK(shortcut);shortcut.stringValue=@"Ctrl+Alt+Q";auto bind=(NSButton*)find(window.contentView,@"editor-bind");CHECK(bind);[bind performClick:nil];
#else
 window=reinterpret_cast<GtkWidget*>(ui.native_handle());auto shortcut=find(window,"editor-shortcut");CHECK(GTK_IS_ENTRY(shortcut));gtk_entry_set_text(GTK_ENTRY(shortcut),"Ctrl+Alt+Q");gtk_button_clicked(GTK_BUTTON(find(window,"editor-bind")));
#endif
 CHECK(format_chord(*take(load_settings(store)).hotkeys.at("folder:"+other))=="Ctrl+Alt+Q");CHECK(registry.registered("folder:"+other));auto failed=session->bind_folder(take(parse_chord("Ctrl+Alt+W")));CHECK(!failed&&format_chord(*take(load_settings(store)).hotkeys.at("folder:"+other))=="Ctrl+Alt+Q");
 auto drag=take(session->begin_drag({SnippetItemKind::Snippet,id}));auto rev=take(data_revision(store));CHECK(session->finish_drag(drag,other,0,false));CHECK(take(data_revision(store))==rev);drag=take(session->begin_drag({SnippetItemKind::Snippet,id}));CHECK(session->finish_drag(drag,other,0,true));CHECK(take(get_snippet(store,id)).folder_id==other);CHECK(changes>=5);
 std::cout<<"PASS native_editor_autosave_unicode_undo_enabled_menu_folder_binding_conflict_and_drag_contract\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL native_editor: "<<e.what()<<'\n';return 1;}}
int main(){
#ifdef __APPLE__
 @autoreleasepool{return run();}
#else
 return run();
#endif
}

#include "cliptiq/editor.hpp"
#include "cliptiq/native_menu.hpp"
#include "../../platform/windows/Workspace.hpp"
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
void check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
int main(){try{
 auto path=std::filesystem::temp_directory_path()/("cliptiq-unified-"+std::to_string(GetCurrentProcessId())+".db");
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove(p,e);}} cleanup{path};
 auto store=take(open_store(path.u8string()));auto folder=take(create_folder(store,"Folder"));
 Snippet doc;doc.folder_id=folder;doc.title="Snippet";doc.content="Before";doc.enabled=false;auto id=take(save_snippet(store,doc));
 auto session=std::make_shared<SnippetEditorSession>(store);check(bool(session->reload()),"reload");check(bool(session->select(SnippetItemId{SnippetItemKind::Snippet,id})),"select");
 NativeSnippetEditor editor;unsigned copies=0,mounts=0;std::string copied;
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;
 options.workspace_snippets=[&](std::uintptr_t parent,std::function<void(const SnippetId&)>){if(!parent){editor.unmount();return Result<void>{};}++mounts;return editor.show_embedded(session,parent,[&](const SnippetId& value){check(value==id,"wrong copied id");copied=session->content();++copies;});};
 options.workspace_can_close=[&]{return editor.can_close();};
 options.windows_message_filter=[&](std::uintptr_t m){return editor.pre_translate(m);};
 NativeMenu ui(options,[](MenuCommand){});check(bool(ui.initialize()),"initialize");check(bool(ui.show_workspace(MenuKind::Snippets)),"open snippets");
 auto root=reinterpret_cast<HWND>(ui.native_handles().window);auto panel=reinterpret_cast<HWND>(editor.native_handle());
 check(!GetDlgItem(panel,512),"reload button must be absent");
 check(panel&&IsChild(root,panel)&&GetAncestor(panel,GA_ROOT)==root,"editor must be a child of main window");check(!GetDlgItem(root,7104),"duplicate sidebar editor entry");
 auto body=GetDlgItem(panel,504);check(body&&IsWindowEnabled(body),"editable snippet including disabled items");
 wchar_t initialText[32]{};GetWindowTextW(body,initialText,32);check(std::wstring(initialText)==L"Before","initial snippets loaded from database");
 SetWindowTextW(body,L"Newest unsaved text");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(515,BN_CLICKED),0);
 for(int i=0;i<150&&!copies;++i){ui.process_events(0);editor.process_events();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 check(copies==1&&copied=="Newest unsaved text","copy must wait for current draft to persist");
 SendMessageW(root,WM_COMMAND,MAKEWPARAM(7101,BN_CLICKED),0);check(!IsWindowVisible(panel),"history hides editor");
 check(IsWindowVisible(GetDlgItem(root,7110)),"history list visible");
 check(bool(ui.show_workspace(MenuKind::Snippets)),"reopen snippets");check(mounts==1&&reinterpret_cast<HWND>(editor.native_handle())==panel,"page switches reuse editor");
 check(session->content()=="Newest unsaved text","switch retains content");
 check(bool(ui.update({})),"refresh menu");
 auto hiddenCopy=GetDlgItem(root,7105);SendMessageW(root,WM_COMMAND,MAKEWPARAM(7105,BN_CLICKED),reinterpret_cast<LPARAM>(hiddenCopy));check(copies==1,"hidden history Copy must be inert");check(reinterpret_cast<HWND>(editor.native_handle())==panel,"model refresh must not rebuild editor");
 {
 auto dpiHost=CreateWindowExW(0,L"STATIC",L"DPI fixture",WS_OVERLAPPEDWINDOW,0,0,1040,680,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 check(dpiHost!=nullptr,"DPI host");
 {windows::Workspace page(dpiHost,{},[](MenuCommand){},{});auto heading=GetDlgItem(dpiHost,7192);LOGFONTW before{},after{};
 GetObjectW(reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)),sizeof(before),&before);
 unsigned nextDpi=before.lfHeight==-MulDiv(11,144,72)?192:144;page.layout(nextDpi);check(GetDlgItem(dpiHost,7192)==heading,"DPI must retain controls");
 GetObjectW(reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)),sizeof(after),&after);
 check(after.lfHeight==-MulDiv(11,nextDpi,72),"DPI must update font in place");}
 DestroyWindow(dpiHost);
 }
 std::cout<<"PASS unified child editor, autosave copy, disabled snippet and page reuse\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

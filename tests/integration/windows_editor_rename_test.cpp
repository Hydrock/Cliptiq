#include "cliptiq/editor.hpp"
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
int main(){try{
 struct Temp { std::filesystem::path path=std::filesystem::temp_directory_path()/("cliptiq-rename-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db"); ~Temp(){std::error_code error;std::filesystem::remove(path,error);} } temp;
 auto path=temp.path;
 auto store=take(open_store(path.u8string()));auto folder=take(create_folder(store,"Original"));
 auto session=std::make_shared<SnippetEditorSession>(store);CHECK(session->reload());
 CHECK(session->select(SnippetItemId{SnippetItemKind::Folder,folder}));
 NativeSnippetEditor editor;CHECK(editor.show(session));
 auto window=reinterpret_cast<HWND>(editor.native_handle());auto tree=GetDlgItem(window,501);
 SendMessageW(tree,WM_KEYDOWN,VK_F2,0);
 auto edit=TreeView_GetEditControl(tree);CHECK(edit!=nullptr);
 SetWindowTextW(edit,L"Renamed folder");PostMessageW(edit,WM_KEYDOWN,VK_RETURN,0);CHECK(editor.process_events());
 for(int n=0;n<200 && (session->saving()||session->has_unsaved());++n){CHECK(editor.process_events());std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 CHECK(take(list_folders(store)).at(0).title=="Renamed folder");
 CHECK(GetDlgItem(window,502)==nullptr);
 SendMessageW(tree,WM_KEYDOWN,VK_F2,0);edit=TreeView_GetEditControl(tree);CHECK(edit);
 SetWindowTextW(edit,L"Cancelled");PostMessageW(edit,WM_KEYDOWN,VK_ESCAPE,0);CHECK(editor.process_events());
 CHECK(session->title()=="Renamed folder");
 SendMessageW(tree,WM_KEYDOWN,VK_F2,0);edit=TreeView_GetEditControl(tree);CHECK(edit);
 SetWindowTextW(edit,L"");TreeView_EndEditLabelNow(tree,FALSE);
 CHECK(session->title()=="Renamed folder");
 CHECK(take(list_folders(store)).at(0).title=="Renamed folder");
 std::cout<<"PASS inline rename persists; cancel and empty name preserve original; separate title removed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

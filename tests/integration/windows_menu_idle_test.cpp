#include "cliptiq/native_menu.hpp"
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
using Clock=std::chrono::steady_clock;
static unsigned delivered=0;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
LRESULT CALLBACK observer(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){if(m==WM_APP+49)return 0;if(m==WM_APP+47){++delivered;return 0;}return DefSubclassProc(w,m,wp,lp);}
int main(){try{
 unsigned filtered=0;
 NativeMenuOptions options;options.windows_message_filter=[&](std::uintptr_t message){auto& m=*reinterpret_cast<MSG*>(message);if(m.message==WM_APP+48){++filtered;return true;}return false;};options.workspace_enabled=true;options.start_hidden=true;options.transient_popup=true;
 unsigned copied=0;NativeMenu ui(options,[&](MenuCommand c){if(c.action==MenuAction::SelectHistory)++copied;});check(bool(ui.initialize()),"initialize");
 MenuModel model;model.revision=1;MenuItem item;item.id="fixture";item.label="Synthetic row";item.tooltip="Synthetic preview";item.action=MenuAction::SelectHistory;model.items.push_back(item);check(bool(ui.update(model))&&bool(ui.show_workspace()),"workspace");
 auto root=reinterpret_cast<HWND>(ui.native_handles().window);check(SetWindowSubclass(root,observer,1,0)!=FALSE,"subclass");
 for(int i=0;i<20;++i)check(bool(ui.process_events(0)),"drain");
 // Windows UI Automation can wake a wait without a removable message.
 // The public bounded idle pump must not turn those wakes into a busy loop.
 std::atomic<bool> stop=false;timeBeginPeriod(1);
 std::thread requests([&]{while(!stop){DWORD_PTR result=0;SendMessageTimeoutW(root,WM_APP+49,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,50,&result);Sleep(1);}});
 auto start=Clock::now();unsigned calls=0;bool pumpOk=true;
 while(Clock::now()-start<std::chrono::milliseconds(300)&&calls<=100){pumpOk=bool(ui.process_events(25));if(!pumpOk)break;++calls;}
 stop=true;requests.join();timeEndPeriod(1);
 check(pumpOk,"idle pump");check(calls<=100,"Idle menu spins on nonqueued cross-thread requests");
 for(int i=0;i<130;++i)PostMessageW(root,WM_APP+47,0,0);
 start=Clock::now();for(int i=0;i<3;++i)check(bool(ui.process_events(100)),"bounded backlog");
 check(delivered==130,"bounded pass stranded old queued input");check(Clock::now()-start<std::chrono::milliseconds(400),"backlog waited unnecessarily");
 auto previous=delivered;
 std::thread sender([&]{Sleep(30);PostMessageW(root,WM_APP+47,0,0);});
 start=Clock::now();while(delivered==previous&&Clock::now()-start<std::chrono::milliseconds(500))check(bool(ui.process_events(100)),"posted wake");sender.join();check(delivered==previous+1,"new posted input did not wake pump");
 std::thread routed([&]{Sleep(30);PostMessageW(root,WM_APP+48,0,0);});
 start=Clock::now();while(!filtered&&Clock::now()-start<std::chrono::milliseconds(500))ui.process_events(100);routed.join();check(filtered==1,"blocking path skipped owner pretranslation");
 auto list=GetDlgItem(root,7110),preview=GetDlgItem(root,7195);
 SetFocus(list);PostMessageW(list,WM_KEYDOWN,VK_RETURN,0);for(int i=0;i<3;++i)ui.process_events(0);check(copied==1,"list Enter lost");
 PostMessageW(root,WM_COMMAND,MAKEWPARAM(7105,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(root,7105)));ui.process_events(0);check(copied==2,"copy command lost");
 auto filter=GetDlgItem(root,7109);SetFocus(filter);PostMessageW(filter,WM_KEYDOWN,VK_TAB,0);for(int i=0;i<3;++i)ui.process_events(0);check(GetFocus()==list,"filter Tab navigation lost");
 SendMessageW(preview,EM_SETSEL,0,4);DWORD first=0,last=0;SendMessageW(preview,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));check(first==0&&last==4,"read-only selection lost");
 auto dialog=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Synthetic settings",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,300,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 auto input=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,5,5,100,20,dialog,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
 auto next=CreateWindowExW(0,L"BUTTON",L"Next",WS_CHILD|WS_VISIBLE|WS_TABSTOP,5,35,100,20,dialog,reinterpret_cast<HMENU>(2),GetModuleHandleW(nullptr),nullptr);
 check(dialog&&input&&next,"foreign dialog fixture");SetFocus(input);PostMessageW(input,WM_KEYDOWN,VK_TAB,0);for(int i=0;i<3;++i)ui.process_events(0);check(GetFocus()==next,"input routed through wrong dialog root");DestroyWindow(dialog);
 RemoveWindowSubclass(root,observer,1);std::cout<<"PASS bounded idle, spurious wake, backlog, new input, Enter, Tab and selection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

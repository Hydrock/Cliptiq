#include "cliptiq/native_menu.hpp"
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
using Clock=std::chrono::steady_clock;
extern "C" UINT cliptiq_test_track_popup(HMENU,UINT,int,int,HWND,LPTPMPARAMS){return 0;}
#define check(x) do { if(!(x)) throw std::runtime_error("workspace check: " #x); } while(false)
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double percentile(std::vector<double> values,unsigned p){std::sort(values.begin(),values.end());return values[std::min(values.size()-1,values.size()*p/100)];}
int main(int argc,char**argv){try{
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 bool verify=argc>1&&std::string(argv[1])=="--verify";
 unsigned previews=0,copies=0;bool quit=false;NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;options.transient_popup=true;
 options.workspace_preview=[&](const MenuCommand& c)->Result<std::string>{++previews;return "Synthetic preview "+c.id;};
 NativeMenu ui(options,[&](MenuCommand c){if(c.action==MenuAction::SelectHistory&&c.invocation.copy_only)++copies;if(c.action==MenuAction::Quit)quit=true;});check(bool(ui.initialize()));
 MenuModel model;model.revision=1;
 for(unsigned i=0;i<1000;++i){MenuItem row;row.id=std::to_string(i);row.label="Synthetic clipboard entry "+std::to_string(i);row.tooltip="Long synthetic text for filtering";row.source_device=i%2?"Test Mac":"";row.action=MenuAction::SelectHistory;model.items.push_back(std::move(row));}
 check(bool(ui.update(model)));auto start=Clock::now();check(bool(ui.show_workspace()));ui.process_events(0);
 auto root=reinterpret_cast<HWND>(ui.native_handles().window);UpdateWindow(root);double open=elapsed(start);
 auto list=GetDlgItem(root,7110),filter=GetDlgItem(root,7109);check(list&&filter);check(ListView_GetItemCount(list)==1000);
#ifdef CLIPTIQ_WORKSPACE_PREVIEW
 SetWindowTextW(root,L"Cliptiq — preview");
 while(!quit&&IsWindowVisible(root)&&ui.process_events(25)){}
 return 0;
#endif
 auto paint=[&]{RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);};
 if(verify){
  RECT row{},field{},client{},copy{},erase{};ListView_GetItemRect(list,0,&row,LVIR_BOUNDS);GetWindowRect(filter,&field);MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&field),2);GetClientRect(root,&client);
  check(MulDiv(field.top,96,GetDpiForWindow(root))<100);check(MulDiv(row.bottom-row.top,96,GetDpiForWindow(root))>=48);check((GetWindowLongPtrW(list,GWL_STYLE)&LVS_NOCOLUMNHEADER)!=0);
  GetWindowRect(GetDlgItem(root,7105),&copy);GetWindowRect(GetDlgItem(root,7106),&erase);check(copy.bottom-copy.top>erase.bottom-erase.top);
  auto before=previews;paint();paint();check(previews==before);
  ListView_SetItemState(list,7,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SendMessageW(root,WM_COMMAND,MAKEWPARAM(7105,BN_CLICKED),0);check(copies==1);
  PostMessageW(list,WM_KEYDOWN,VK_RETURN,0);ui.process_events(0);check(copies==2);
  wchar_t accessible[256]{};LVITEMW item{};item.pszText=accessible;item.cchTextMax=256;SendMessageW(list,LVM_GETITEMTEXTW,1,reinterpret_cast<LPARAM>(&item));check(std::wstring(accessible).find(L"Test Mac")!=std::wstring::npos);
  LVFINDINFOW find{};find.flags=LVFI_STRING|LVFI_PARTIAL|LVFI_WRAP;find.psz=L"Synthetic clipboard entry 9";
  check(SendMessageW(list,LVM_FINDITEMW,WPARAM(-1),reinterpret_cast<LPARAM>(&find))==9);
  find.psz=L"No matching entry";check(SendMessageW(list,LVM_FINDITEMW,WPARAM(-1),reinterpret_cast<LPARAM>(&find))==-1);
  ListView_SetItemState(list,-1,0,LVIS_SELECTED);check(!IsWindowEnabled(GetDlgItem(root,7105)));
  SetWindowTextW(filter,L"not-present");check(ListView_GetItemCount(list)==0);check(!IsWindowEnabled(GetDlgItem(root,7105)));
  SetWindowTextW(filter,L"Test Mac");check(ListView_GetItemCount(list)==500);
  SetWindowTextW(filter,L"");check(ListView_GetItemCount(list)==1000);
  SetWindowPos(root,nullptr,0,0,880,580,SWP_NOMOVE|SWP_NOZORDER);ui.process_events(0);GetClientRect(list,&client);check(GetScrollPos(list,SB_HORZ)==0);
  check(ListView_GetColumnWidth(list,0)<=client.right);
  std::cout<<"PASS compact header, two-line native rows, copy, filter, paint and minimum width\n";return 0;
 }
 std::vector<double> filtering,selection,scroll;
 for(int n=0;n<35;++n){start=Clock::now();SetWindowTextW(filter,n%2?L"":L"entry 9");paint();if(n>=5)filtering.push_back(elapsed(start));}
 SetWindowTextW(filter,L"");
 for(int n=0;n<65;++n){start=Clock::now();int i=(n*17)%1000;ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,i,FALSE);paint();if(n>=5)selection.push_back(elapsed(start));}
 for(int n=0;n<65;++n){start=Clock::now();SendMessageW(list,WM_VSCROLL,n%2?SB_PAGEUP:SB_PAGEDOWN,0);paint();if(n>=5)scroll.push_back(elapsed(start));}
 PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
 std::cout<<"{\"open_dispatch_paint_ms\":"<<open<<",\"working_set_bytes\":"<<mem.WorkingSetSize<<",\"private_bytes\":"<<mem.PrivateUsage<<",\"gdi_objects\":"<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(auto entry:{std::make_pair("filter",filtering),std::make_pair("selection",selection),std::make_pair("scroll",scroll)})std::cout<<",\""<<entry.first<<"_median_ms\":"<<percentile(entry.second,50)<<",\""<<entry.first<<"_p95_ms\":"<<percentile(entry.second,95);
 std::cout<<"}\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

#include "cliptiq/lifecycle.hpp"
#include "cliptiq/locale.hpp"
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Probe {int action;bool clicked=false;};
BOOL CALLBACK choose(HWND window,LPARAM data){
 wchar_t name[64]{};GetClassNameW(window,name,64);
 if(std::wstring(name)!=L"#32770")return TRUE;
 auto& probe=*reinterpret_cast<Probe*>(data);probe.clicked=true;
 if(probe.action==1)SendMessageW(window,TDM_CLICK_VERIFICATION,FALSE,FALSE);
 SendMessageW(window,TDM_CLICK_BUTTON,probe.action==2?101:100,0);return FALSE;
}
std::optional<bool> run(int action){
 const auto owner=GetCurrentThreadId();std::atomic<bool> finished=false;Probe probe{action};
 std::thread driver([&]{auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(!finished&&std::chrono::steady_clock::now()<deadline){
   EnumThreadWindows(owner,choose,reinterpret_cast<LPARAM>(&probe));if(probe.clicked)return;
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
 });
 auto result=show_first_run_autostart();finished=true;driver.join();
 check(bool(result),"native setup could not open");check(probe.clicked,"native setup was not exercised");return result.value();
}
int main(){try{
 for(auto locale:{"en","ru"}){
  activate_locale(locale);
  auto checked=run(0);check(checked&&*checked,"autostart must be checked by default");
  auto unchecked=run(1);check(unchecked&&!*unchecked,"unchecked choice was lost");
  check(!run(2),"Quit must not return a saved choice");
 }
 std::cout<<"PASS EN/RU native first-run checkbox defaults, opt-out and cancellation; no OS registration\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

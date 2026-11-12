#include "cliptiq/native_menu.hpp"
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
// Exercise real menu/bitmap allocation without displaying or choosing a menu.
extern "C" UINT cliptiq_test_track_popup(HMENU,UINT,int,int,HWND,LPTPMPARAMS){return 0;}
int main(int argc,char** argv){try{
 using namespace cliptiq;
 bool transient=argc==2&&std::string(argv[1])=="transient";
 NativeMenuOptions options;options.start_hidden=true;options.workspace_enabled=true;options.transient_popup=transient;
 MenuImage image{100,32,Bytes(100*32*4,255)};
 NativeMenu ui(options,[](auto){},[&](const PreviewRef&){return std::optional<MenuImage>{image};});
 if(!ui.initialize())throw std::runtime_error("initialize");
 MenuModel model;model.revision=1;
 for(int n=0;n<1000;++n){MenuItem row;row.label="Synthetic clipboard entry "+std::to_string(n);row.id=std::to_string(n);row.action=MenuAction::SelectHistory;if(n%10==0)row.preview=PreviewRef{row.id,{}};model.items.push_back(std::move(row));}
 if(!ui.update(model))throw std::runtime_error("update");
 PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
 GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
 auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 auto users=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
 std::vector<double> samples;
 InvocationContext context;context.keyboard=true;context.target="benchmark";context.x=100;context.y=100;
 for(int n=0;n<25;++n){
  auto started=std::chrono::steady_clock::now();
  if(!ui.update(model)||!ui.show(context))throw std::runtime_error("cycle");
  samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
 }
 auto cold=samples.front();samples.erase(samples.begin());std::sort(samples.begin(),samples.end());
 std::cout<<"{\"mode\":\""<<(transient?"transient":"eager")<<"\",\"entries\":1000,\"images\":100,\"private_idle_bytes\":"<<memory.PrivateUsage
  <<",\"gdi_idle\":"<<gdi<<",\"user_idle\":"<<users<<",\"first_update_show_ms\":"<<cold<<",\"median_update_show_ms\":"<<samples[samples.size()/2]<<",\"p95_update_show_ms\":"<<samples[samples.size()*95/100]<<"}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

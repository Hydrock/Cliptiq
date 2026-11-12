#include "cliptiq/native_menu.hpp"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
static HWND popup_owner=nullptr;
static POINT anchor{};
static bool choose=false;
extern "C" UINT cliptiq_test_track_popup(HMENU menu,UINT,int x,int y,HWND owner,LPTPMPARAMS){
 popup_owner=owner;anchor={x,y};return choose?GetMenuItemID(menu,0):0;
}
int main(){try{
 using namespace cliptiq;
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;
 MenuCommand selected;
 NativeMenu ui(options,[&](auto command){selected=std::move(command);});
 auto initialized=ui.initialize();if(!initialized)throw std::runtime_error(initialized.error().message);
 MenuModel model;model.revision=1;MenuItem item;item.label="Test";item.id="entry";item.action=MenuAction::SelectHistory;model.items.push_back(item);
 if(!ui.update(model))throw std::runtime_error("model");
 auto main=reinterpret_cast<HWND>(ui.native_handles().window);
 ShowWindow(main,SW_HIDE);
 InvocationContext ctx;ctx.keyboard=true;ctx.target="external-target";ctx.x=-200;ctx.y=160;
 if(!ui.show(ctx))throw std::runtime_error("show");
 if(!popup_owner||popup_owner==main)throw std::runtime_error("Popup raised workspace owner");
 if(IsWindowVisible(main)||IsWindowVisible(popup_owner))throw std::runtime_error("Popup left a visible application window");
 if(anchor.x!=-200||anchor.y!=160)throw std::runtime_error("Popup anchor changed");
 choose=true;
 if(!ui.show(ctx)||selected.invocation.target!=ctx.target||!selected.invocation.keyboard)
   throw std::runtime_error("Popup lost original paste target");
 std::cout<<"PASS independent popup owner, hidden workspace and screen anchor\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

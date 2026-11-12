#include "../../platform/LanSettings.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
std::wstring caption(HWND w){wchar_t b[512]{};GetWindowTextW(w,b,512);return b;}
HWND named(HWND root,const wchar_t* value){for(auto c=GetWindow(root,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))if(caption(c)==value)return c;throw std::runtime_error("Control missing");}
HWND edit(HWND root,const wchar_t* label){RECT r{};auto l=named(root,label);GetWindowRect(l,&r);for(auto c=GetWindow(root,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)){wchar_t cls[32]{};GetClassNameW(c,cls,32);RECT q{};GetWindowRect(c,&q);if((std::wstring(cls)==L"Edit"||(std::wstring(cls)==L"Button"&&caption(c).empty()))&&q.top<=r.top&&q.bottom>r.top)return c;}throw std::runtime_error("Field missing");}
void click(HWND root,const wchar_t* label){SendMessageW(named(root,label),BM_CLICK,0,0);}
int main(){try{
 NativeLanSettings ui;check(!FindWindowW(L"CliptiqLanSettings",nullptr),"constructor allocated window");
 lan::Config config;config.device_name="Original";config.device_id="local";int saves=0,copies=0;lan::Config saved;std::map<std::string,std::string> keys;
 auto show=[&]{auto r=ui.show(config,[&](lan::Config c,std::map<std::string,std::string> k)->Result<void>{++saves;saved=std::move(c);keys=std::move(k);return {};},[]()->Result<std::string>{return std::string(64,'a');},[&](std::string k)->Result<void>{check(k==std::string(64,'a'),"wrong copied key");++copies;return {};});check(bool(r),"show failed");return FindWindowW(L"CliptiqLanSettings",nullptr);};
 auto w=show();check(w!=nullptr,"window missing");SetWindowTextW(edit(w,L"Local device name"),L"Unsaved");click(w,L"Cancel");check(saves==0,"cancel applied draft");
 w=show();check(caption(edit(w,L"Local device name"))==L"Original","cancel retained draft");
 click(w,L"Add peer");check(saves==0,"add activated sharing");click(w,L"Generate key");auto key=edit(w,L"Pair key (blank keeps saved key)");check((GetWindowLongPtrW(key,GWL_STYLE)&ES_PASSWORD)!=0,"key not masked");click(w,L"Copy key");check(copies==1&&saves==0,"copy saved settings");
 SetWindowTextW(edit(w,L"Peer address"),L"127.0.0.1");SendMessageW(edit(w,L"Receive into history only"),BM_SETCHECK,BST_CHECKED,0);
 SetWindowTextW(edit(w,L"Listen port"),L"70000");click(w,L"Save");check(saves==0,"invalid port accepted");SetWindowTextW(edit(w,L"Listen port"),L"49731");click(w,L"Save");check(saves==1&&saved.peers.size()==1&&keys.size()==1,"draft not saved");check(saved.receive_mode==lan::ReceiveMode::HistoryOnly,"receive mode missing");check(saved.peers[0].secret_ref.empty(),"UI wrote credential reference");
 config=saved;config.peers[0].secret_ref="existing-secure-reference";w=show();check(caption(edit(w,L"Pair key (blank keeps saved key)")).empty(),"saved key exposed");click(w,L"Copy key");check(copies==1,"copied hidden saved key");click(w,L"Save");check(saves==2&&keys.empty()&&saved.peers[0].secret_ref=="existing-secure-reference","unchanged saved key was replaced");
 config.peers.clear();w=show();click(w,L"Add peer");click(w,L"Remove peer");click(w,L"Save");check(saves==3&&saved.peers.empty()&&keys.empty(),"peer removal failed");
 std::cout<<"native LAN settings: passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

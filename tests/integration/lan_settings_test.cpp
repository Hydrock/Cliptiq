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
HWND own_window(){HWND w=nullptr;while((w=FindWindowExW(nullptr,w,L"CliptiqLanSettings",nullptr))){DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId())return w;}return nullptr;}
int main(){try{
 NativeLanSettings ui;check(!own_window(),"constructor allocated window");
 lan::Config config;config.device_name="Original";config.device_id="local";int saves=0,copies=0;lan::Config saved;std::map<std::string,std::string> keys;
 auto show=[&]{auto r=ui.show(config,[&](lan::Config c,std::map<std::string,std::string> k)->Result<void>{++saves;saved=std::move(c);keys=std::move(k);return {};},[]()->Result<std::string>{return std::string(64,'a');},[&](std::string k)->Result<void>{check(k==std::string(64,'a'),"wrong copied key");++copies;return {};});check(bool(r),"show failed");return own_window();};
 auto w=show();check(w!=nullptr,"window missing");SetWindowTextW(edit(w,L"Local device name"),L"Unsaved");click(w,L"Cancel");check(saves==0,"cancel applied draft");
 w=show();check(caption(edit(w,L"Local device name"))==L"Original","cancel retained draft");
 click(w,L"Add peer");check(saves==0,"add activated sharing");click(w,L"Generate key");auto key=edit(w,L"Pair key (blank keeps saved key)");check((GetWindowLongPtrW(key,GWL_STYLE)&ES_PASSWORD)!=0,"key not masked");click(w,L"Copy key");check(copies==1&&saves==0,"copy saved settings");
 SetWindowTextW(edit(w,L"Peer address"),L"127.0.0.1");SendMessageW(edit(w,L"Receive into history only"),BM_SETCHECK,BST_CHECKED,0);
 SetWindowTextW(edit(w,L"Listen port"),L"70000");click(w,L"Save");check(saves==0,"invalid port accepted");SetWindowTextW(edit(w,L"Listen port"),L"49731");click(w,L"Save");check(saves==1&&saved.peers.size()==1&&keys.size()==1,"draft not saved");check(saved.receive_mode==lan::ReceiveMode::HistoryOnly,"receive mode missing");check(saved.peers[0].secret_ref.empty(),"UI wrote credential reference");
 config=saved;config.peers[0].secret_ref="existing-secure-reference";w=show();check(caption(edit(w,L"Pair key (blank keeps saved key)")).empty(),"saved key exposed");click(w,L"Copy key");check(copies==1,"copied hidden saved key");click(w,L"Save");check(saves==2&&keys.empty()&&saved.peers[0].secret_ref=="existing-secure-reference","unchanged saved key was replaced");
 config.peers.clear();w=show();click(w,L"Add peer");click(w,L"Remove peer");click(w,L"Save");check(saves==3&&saved.peers.empty()&&keys.empty(),"peer removal failed");
 // Embedded lifecycle: no top-level LAN window; repeated saves retain refreshed revision.
 NativeLanSettings embedded;auto parent=CreateWindowExW(0,L"STATIC",L"Test settings page",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 int embedded_saves=0;config.revision=0;
 auto attached=embedded.show(config,[&](lan::Config& c,std::map<std::string,std::string>)->Result<void>{check(c.revision==unsigned(embedded_saves),"stale embedded revision");++embedded_saves;++c.revision;return {};},[]()->Result<std::string>{return std::string(64,'a');},[](std::string)->Result<void>{return {};},parent);
 check(bool(attached),"embed failed");auto child=GetDlgItem(parent,303);check(child&&GetParent(child)==parent,"not embedded in page");check((GetWindowLongPtrW(child,GWL_STYLE)&WS_CHILD)!=0,"embedded form is top-level");
 click(child,L"Save");click(child,L"Save");check(embedded_saves==2,"embedded save hidden or stale");
 SetWindowTextW(edit(child,L"Local device name"),L"Discard me");click(child,L"Cancel");check(caption(edit(child,L"Local device name"))==L"Original","embedded cancel failed");check((GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0,"cancel hid embedded form");
 DestroyWindow(parent);embedded.status("after parent destroyed");check(bool(embedded.process_events()),"destroyed embedded form retained window");
 NativeLanSettings discovery;lan::DiscoverySnapshot snapshot;snapshot.available=true;unsigned searches=0,closes=0;DiscoveryUiCallbacks callbacks;callbacks.snapshot=[&]{return snapshot;};callbacks.search=[&]()->Result<void>{++searches;snapshot.searching=true;return {};};callbacks.stop_search=[&]{snapshot.searching=false;};callbacks.close_view=[&]{++closes;};callbacks.available=[&](bool on)->Result<void>{snapshot.enabled=on;return {};};callbacks.connect=[](std::string)->Result<void>{return {};};callbacks.accept=[](std::string)->Result<void>{return {};};callbacks.enter_code=[](std::string,std::string)->Result<void>{return {};};callbacks.cancel=[](std::string){};discovery.discovery(callbacks);
 check(bool(discovery.show(config,[](lan::Config&,std::map<std::string,std::string>)->Result<void>{return {};},[]()->Result<std::string>{return std::string(64,'a');},[](std::string)->Result<void>{return {};})),"discovery show failed");w=own_window();check(w!=nullptr,"discovery window missing");click(w,L"Find devices on network");check(searches==1,"search callback missing");click(w,L"Manual connection");auto original=edit(w,L"Local device name");SetWindowTextW(original,L"Retain draft");snapshot.message="Search complete.";check(bool(discovery.process_events()),"snapshot update failed");check(IsWindow(original)&&caption(original)==L"Retain draft","snapshot rebuilt manual widgets");click(w,L"Manual connection");click(w,L"Manual connection");check(caption(edit(w,L"Local device name"))==L"Retain draft","disclosure lost draft");
 lan::Peer removeFixture;removeFixture.id="remove-me";removeFixture.name="Paired fixture";snapshot.peers={removeFixture};unsigned removals=0;
 callbacks.remove_peer=[&](std::string id)->Result<lan::Config>{check(id==removeFixture.id,"removed wrong peer");++removals;snapshot.peers.clear();auto next=config;++next.revision;return next;};discovery.discovery(callbacks);
 check(bool(discovery.process_events()),"paired row update failed");click(w,L"Delete");check(removals==1&&snapshot.peers.empty(),"paired Delete callback missing");check(caption(edit(w,L"Local device name"))==L"Retain draft","Delete lost unrelated draft");
 snapshot.pairing=lan::PairingEvent::Kind::EnterCode;snapshot.session_id="fixture";check(bool(discovery.process_events()),"code view failed");auto code=GetDlgItem(w,8999);check(code!=nullptr,"code entry missing");SetWindowTextW(code,L"123456");SetFocus(code);snapshot.message="Searching for devices…";check(bool(discovery.process_events()),"code update failed");check(caption(GetDlgItem(w,8999))==L"123456","code entry was discarded");SendMessageW(w,WM_CLOSE,0,0);check(closes==1,"close callback missing");
 std::cout<<"native LAN settings: passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

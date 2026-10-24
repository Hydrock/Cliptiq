#include "cliptiq/settings.hpp"
#include <iostream>
#include <stdexcept>
#include <set>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T>r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
struct Backend:HotkeyBackend {
 unsigned next=0;std::map<unsigned,KeyChord> held;std::string blocked;
 Result<unsigned>acquire(const ActionId &,const KeyChord &c)override{if(format_chord(c)==blocked)return Error{ErrorCode::Busy,"external conflict"};for(auto &p:held)if(p.second==c)return Error{ErrorCode::Busy,"occupied"};held[++next]=c;return next;}
 void release(unsigned id)override{held.erase(id);}Result<void>dispatch()override{return {};}
 Capability capability()const override{return {CapabilityStatus::Available,"fixture"};}
};
int main(){int failed=0;auto test=[&](const char*n,auto f){try{f();std::cout<<"PASS "<<n<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<n<<": "<<e.what()<<'\n';}};
test("chord_roundtrip_meaning_and_reserved",[]{auto c=take(parse_chord("Ctrl+Alt+V"));CHECK(c.modifiers==(Control|Alt)&&c.key=="V"&&c.meaning==KeyMeaning::Logical);CHECK(format_chord(c)=="Ctrl+Alt+V");CHECK(!parse_chord("V"));CHECK(!parse_chord("Ctrl+Ctrl+V"));CHECK(!parse_chord("Super+V"));CHECK(!parse_chord("Ctrl+F12"));});
test("conflict_keeps_previous_and_settings_protected",[]{Backend b;HotkeyRegistry r(b);CHECK(take(r.bind_hotkey("settings",take(parse_chord("Ctrl+Alt+Shift+V")))).registered);CHECK(!r.bind_hotkey("settings",{}));CHECK(take(r.bind_hotkey("main",take(parse_chord("Ctrl+Alt+V")))).registered);b.blocked="Ctrl+Alt+H";CHECK(!r.bind_hotkey("main",take(parse_chord(b.blocked))));CHECK(r.registered("main")&&b.held.size()==2);CHECK(r.bind_hotkey("main",{}));CHECK(!r.registered("main"));});
test("batch_os_or_persistence_failure_rolls_back",[]{Backend b;HotkeyRegistry r(b);auto s=default_settings(CapturePlatform::X11);CHECK(r.replace(s.hotkeys,[]{return Result<void>{};}));auto before=b.held.size();auto changed=s.hotkeys;changed["main"]=take(parse_chord("Ctrl+Alt+Q"));changed["history"]=take(parse_chord("Ctrl+Alt+W"));b.blocked="Ctrl+Alt+W";CHECK(!r.replace(changed,[]{return Result<void>{};}));CHECK(b.held.size()==before&&r.registered("main"));b.blocked.clear();CHECK(!r.replace(changed,[]{return Result<void>{Error{ErrorCode::ReadOnly,"disk readonly"}};}));CHECK(b.held.size()==before);});
test("disabled_deleted_folders_release_routes",[]{Backend b;HotkeyRegistry r(b);CHECK(r.bind_hotkey("folder:a",take(parse_chord("Ctrl+Alt+A"))));CHECK(r.bind_hotkey("folder:b",take(parse_chord("Ctrl+Alt+B"))));r.reconcile_folders({{"a",false}});CHECK(!r.registered("folder:a")&&!r.registered("folder:b")&&b.held.empty());});
test("registry_lifetime_releases_and_unknown_actions_rejected",[]{Backend b;{HotkeyRegistry r(b);CHECK(!r.bind_hotkey("arbitrary",take(parse_chord("Ctrl+Alt+A"))));CHECK(r.bind_hotkey("main",take(parse_chord("Ctrl+Alt+A"))));}CHECK(b.held.empty());});
return failed?1:0;}

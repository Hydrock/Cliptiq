#include "PasteNative.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Fixture {
 std::shared_ptr<NativePasteCalls> calls=std::make_shared<NativePasteCalls>();
 HWND window=reinterpret_cast<HWND>(100),foreground=window;DWORD pid=424242,thread=123;std::uint64_t born=5,generation=1;
 bool alive=true,restore=true;DWORD level=0x2000;int held=0;UINT accepted=99,scan=0x2f;
 std::vector<std::vector<INPUT>> batches;
 Fixture(){
 calls->foreground=[&]{return foreground;};calls->alive=[&](HWND){return alive;};
 calls->window_thread=[&](HWND w,DWORD*p){*p=w==reinterpret_cast<HWND>(200)?GetCurrentProcessId():pid;return thread;};
 calls->process_birth=[&](DWORD){return born;};calls->integrity=[&](DWORD p){return p==GetCurrentProcessId()?DWORD(0x2000):level;};
 calls->key_state=[&](int key){return SHORT(key==held?0x8000:0);};calls->restore=[&](HWND w){if(restore)foreground=w;return restore;};
 calls->scan=[&](DWORD){return scan;};calls->lifetime=[&](HWND){return generation;};
 calls->send=[&](UINT n,INPUT*p){batches.emplace_back(p,p+n);return std::min(n,accepted);};
 }
};
int main(){int failed=0;auto test=[&](const char*n,auto f){try{f();std::cout<<"PASS "<<n<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<n<<": "<<e.what()<<'\n';}};
test("issued_target_success_and_chords",[]{for(auto shortcut:{Shortcut::Paste,Shortcut::TerminalPaste}){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());CHECK(p.dispatch(t,shortcut));CHECK(f.batches.size()==1);auto&v=f.batches[0];CHECK(v.size()==(shortcut==Shortcut::Paste?4:6));CHECK(v[0].ki.wVk==VK_CONTROL&&!v[0].ki.dwFlags);CHECK(v.back().ki.wVk==VK_CONTROL&&(v.back().ki.dwFlags&KEYEVENTF_KEYUP));auto n=shortcut==Shortcut::Paste?1:2;CHECK(v[n].ki.wScan==0x2f&&v[n].ki.dwFlags==KEYEVENTF_SCANCODE);CHECK(!p.dispatch(t,shortcut));}});
test("closed_replaced_process_and_reused_hwnd_rejected",[]{for(int n=0;n<5;++n){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());if(n==0)f.alive=false;if(n==1)++f.pid;if(n==2)++f.thread;if(n==3)++f.born;if(n==4)++f.generation;CHECK(!p.dispatch(t,Shortcut::Paste)&&f.batches.empty());}});
test("uipi_unknown_and_held_modifiers_no_input",[]{for(int n=0;n<8;++n){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());if(n==0)f.level=0x3000;else if(n==1)f.level=MAXDWORD;else f.held=std::vector<int>{VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN,'V'}[n-2];CHECK(!p.dispatch(t,Shortcut::Paste)&&f.batches.empty());}});
test("unrelated_focus_and_denied_restore_no_input",[]{for(bool denied:{false,true}){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());f.foreground=reinterpret_cast<HWND>(denied?200:300);f.restore=false;CHECK(!p.dispatch(t,Shortcut::Paste)&&f.batches.empty());}});
test("normal_self_restore_then_dispatch",[]{Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());f.foreground=reinterpret_cast<HWND>(200);CHECK(p.dispatch(t,Shortcut::Paste)&&f.foreground==f.window&&f.batches.size()==1);});
test("every_partial_prefix_releases_only_injected_keys",[]{for(auto shortcut:{Shortcut::Paste,Shortcut::TerminalPaste}){unsigned count=shortcut==Shortcut::Paste?4:6;for(unsigned prefix=0;prefix<count;++prefix){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());f.accepted=prefix;CHECK(!p.dispatch(t,shortcut));CHECK(f.batches.size()==(prefix?2:1));if(prefix){bool down[256]{};auto&batch=f.batches[0];for(unsigned k=0;k<prefix;++k){auto code=batch[k].ki.wVk?batch[k].ki.wVk:batch[k].ki.wScan;down[code]=!(batch[k].ki.dwFlags&KEYEVENTF_KEYUP);}auto cleanup=f.batches[1];for(auto &event:cleanup){auto code=event.ki.wVk?event.ki.wVk:event.ki.wScan;CHECK(down[code]&&(event.ki.dwFlags&KEYEVENTF_KEYUP));down[code]=false;}for(bool value:down)CHECK(!value);}}}});
return failed?1:0;}

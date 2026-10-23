#include "PasteNative.hpp"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Fixture {
 std::shared_ptr<NativePasteCalls> calls=std::make_shared<NativePasteCalls>();
 bool trusted=true,alive=true,same=true,restore=true,keydown=false,allocate=true;int key=9;CGEventFlags held=0;
 PasteForeground foreground=PasteForeground::Target;unsigned posts=0;std::vector<CGEventFlags> flags;std::vector<CGKeyCode> keys;
 Fixture(){calls->trusted=[&]{return trusted;};calls->capture=[] {return true;};calls->alive=[&]{return alive;};calls->same_window=[&]{return same;};calls->activate=[&]{if(restore)foreground=PasteForeground::Target;return restore;};calls->foreground=[&]{return foreground;};calls->held_flags=[&]{return held;};calls->paste_key=[&]{return key;};calls->key_down=[&](int){return keydown;};calls->create=[&](CGKeyCode k,bool down){return allocate?CGEventCreateKeyboardEvent(nullptr,k,down):nullptr;};calls->post=[&](CGEventRef event){++posts;flags.push_back(CGEventGetFlags(event));keys.push_back(static_cast<CGKeyCode>(CGEventGetIntegerValueField(event,kCGKeyboardEventKeycode)));};}
};
int run(){int failed=0;auto test=[&](const char*n,auto f){try{f();std::cout<<"PASS "<<n<<'\n';}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<n<<": "<<e.what()<<'\n';}};
test("issued_target_and_terminal_both_clipboard_command_v",[]{for(auto s:{Shortcut::Paste,Shortcut::TerminalPaste}){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());CHECK(p.dispatch(t,s));CHECK(f.posts==2&&f.keys[0]==9&&f.keys[1]==9);CHECK(f.flags[0]==kCGEventFlagMaskCommand&&f.flags[1]==kCGEventFlagMaskCommand);CHECK(!p.dispatch(t,s));}});
test("permission_denial_and_revocation",[]{Fixture f;f.trusted=false;NativePasteBackend p(f.calls);CHECK(p.capture_target().empty());f.trusted=true;auto t=p.capture_target();CHECK(!t.empty());f.trusted=false;CHECK(!p.dispatch(t,Shortcut::Paste)&&!f.posts);});
test("closed_changed_window_and_unrelated_focus",[]{for(int n=0;n<3;++n){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());if(n==0)f.alive=false;if(n==1)f.same=false;if(n==2)f.foreground=PasteForeground::Other;CHECK(!p.dispatch(t,Shortcut::Paste)&&!f.posts);}});
test("held_modifiers_key_and_missing_keymap_or_allocation",[]{for(int n=0;n<7;++n){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());if(n<4)f.held=std::vector<CGEventFlags>{kCGEventFlagMaskControl,kCGEventFlagMaskCommand,kCGEventFlagMaskShift,kCGEventFlagMaskAlternate}[n];if(n==4)f.keydown=true;if(n==5)f.key=-1;if(n==6)f.allocate=false;CHECK(!p.dispatch(t,Shortcut::Paste)&&!f.posts);}});
test("normal_restore_denial_and_window_recheck",[]{for(int n=0;n<3;++n){Fixture f;NativePasteBackend p(f.calls);auto t=p.capture_target();CHECK(!t.empty());f.foreground=PasteForeground::Self;if(n==0)f.restore=false;if(n==1)f.calls->activate=[&]{f.foreground=PasteForeground::Target;f.same=false;return true;};auto r=p.dispatch(t,Shortcut::Paste);if(n<2)CHECK(!r&&!f.posts);else CHECK(r&&f.posts==2);}});
return failed?1:0;}
int main(){@autoreleasepool{return run();}}

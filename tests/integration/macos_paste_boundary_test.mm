#include "../fixtures/PasteHistory.hpp"
#include "PasteNative.hpp"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
struct Fixture {
  std::shared_ptr<NativePasteCalls> calls =
      std::make_shared<NativePasteCalls>();
  bool trusted = true, alive = true, same = true, restore = true,
       keydown = false, allocate = true;
  int key = 9;
  CGEventFlags held = 0;
  PasteForeground foreground = PasteForeground::Target;
  unsigned posts = 0;
  std::vector<CGEventFlags> flags;
  std::vector<CGKeyCode> keys;
  Fixture() {
    calls->trusted = [&] { return trusted; };
    calls->capture = [] { return true; };
    calls->alive = [&] { return alive; };
    calls->same_window = [&] { return same; };
    calls->activate = [&] {
      if (restore)
        foreground = PasteForeground::Target;
      return restore;
    };
    calls->foreground = [&] { return foreground; };
    calls->held_flags = [&] { return held; };
    calls->paste_key = [&] { return key; };
    calls->key_down = [&](int) { return keydown; };
    calls->create = [&](CGKeyCode k, bool down) {
      return allocate ? CGEventCreateKeyboardEvent(nullptr, k, down) : nullptr;
    };
    calls->post = [&](CGEventRef event) {
      ++posts;
      flags.push_back(CGEventGetFlags(event));
      keys.push_back(static_cast<CGKeyCode>(
          CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode)));
    };
  }
};
int run() {
  int failed = 0;
  auto test = [&](const char *n, auto f) {
    try {
      f();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("issued_target_and_terminal_both_clipboard_command_v", [] {
    for (auto s : {Shortcut::Paste, Shortcut::TerminalPaste}) {
      Fixture f;
      NativePasteBackend p(f.calls);
      auto t = p.capture_target();
      CHECK(!t.empty());
      CHECK(p.dispatch(t, s));
      CHECK(f.posts == 2 && f.keys[0] == 9 && f.keys[1] == 9);
      CHECK(f.flags[0] == kCGEventFlagMaskCommand &&
            f.flags[1] == kCGEventFlagMaskCommand);
      CHECK(!p.dispatch(t, s));
    }
  });
  test("permission_denial_and_revocation", [] {
    Fixture f;
    f.trusted = false;
    NativePasteBackend p(f.calls);
    CHECK(p.capture_target().empty());
    f.trusted = true;
    auto t = p.capture_target();
    CHECK(!t.empty());
    f.trusted = false;
    CHECK(!p.dispatch(t, Shortcut::Paste) && !f.posts);
  });
  test("closed_changed_window_and_unrelated_focus", [] {
    for (int n = 0; n < 3; ++n) {
      Fixture f;
      NativePasteBackend p(f.calls);
      auto t = p.capture_target();
      CHECK(!t.empty());
      if (n == 0)
        f.alive = false;
      if (n == 1)
        f.same = false;
      if (n == 2)
        f.foreground = PasteForeground::Other;
      CHECK(!p.dispatch(t, Shortcut::Paste) && !f.posts);
    }
  });
  test("held_modifiers_key_and_missing_keymap_or_allocation", [] {
    for (int n = 0; n < 7; ++n) {
      Fixture f;
      NativePasteBackend p(f.calls);
      auto t = p.capture_target();
      CHECK(!t.empty());
      if (n < 4)
        f.held = std::vector<CGEventFlags>{
            kCGEventFlagMaskControl, kCGEventFlagMaskCommand,
            kCGEventFlagMaskShift, kCGEventFlagMaskAlternate}[n];
      if (n == 4)
        f.keydown = true;
      if (n == 5)
        f.key = -1;
      if (n == 6)
        f.allocate = false;
      CHECK(!p.dispatch(t, Shortcut::Paste) && !f.posts);
    }
  });
  test("normal_restore_denial_and_window_recheck", [] {
    for (int n = 0; n < 3; ++n) {
      Fixture f;
      NativePasteBackend p(f.calls);
      auto t = p.capture_target();
      CHECK(!t.empty());
      f.foreground = PasteForeground::Self;
      if (n == 0)
        f.restore = false;
      if (n == 1)
        f.calls->activate = [&] {
          f.foreground = PasteForeground::Target;
          f.same = false;
          return true;
        };
      auto r = p.dispatch(t, Shortcut::Paste);
      if (n < 2)
        CHECK(!r && !f.posts);
      else
        CHECK(r && f.posts == 2);
    }
  });
  test("native_denial_preserves_paste_delete_history", [] {
    for (int n = 0; n < 5; ++n) {
      Fixture f;
      NativePasteBackend p(f.calls);
      auto target = p.capture_target();
      CHECK(!target.empty());
      if (n == 0)
        f.trusted = false;
      if (n == 1)
        f.foreground = PasteForeground::Other;
      if (n == 2)
        f.held = kCGEventFlagMaskCommand;
      if (n == 3)
        f.same = false;
      if (n == 4)
        f.allocate = false;
      paste_fixture::retained_after_native_denial(p, target);
    }
  });
  test("permission_help_after_copy_is_once_until_access_changes", [] {
    Fixture f;f.trusted=false;
    unsigned dialogs=0;f.calls->permission_help=[&]{++dialogs;};
    NativePasteBackend p(f.calls);
    auto store=paste_fixture::take(open_store(":memory:"));
    ClipboardSnapshot snapshot;
    snapshot.representations={{0,"macos","public.utf8-plain-text",CanonicalKind::Text,{'s','a','f','e'}}};
    auto id=*paste_fixture::take(ingest(store,snapshot,{},1));
    SelectionIntent intent;intent.command={MenuAction::SelectHistory,id,paste_fixture::take(data_revision(store)),{p.capture_target()}};
    paste_fixture::MemoryWriter clipboard;
    auto outcome=paste_fixture::take(execute_selection(store,intent,clipboard,p));
    CHECK(outcome.status==SelectionStatus::NeedsManualPaste && clipboard.writes==1);
    CHECK(dialogs==0 && f.posts==0);
    CHECK(p.explain_paste_permission(intent,outcome));
    CHECK(dialogs==1 && !p.explain_paste_permission(intent,outcome));
    f.trusted=true;CHECK(!p.explain_paste_permission(intent,outcome));
    f.trusted=false;CHECK(p.explain_paste_permission(intent,outcome));
    CHECK(dialogs==2 && f.posts==0 && paste_fixture::take(list_history(store)).size()==1);
  });
  test("permission_help_only_for_failed_auto_paste_not_copy_or_other_denials", [] {
    for(int n=0;n<9;++n){
      Fixture f;f.trusted=false;unsigned dialogs=0;f.calls->permission_help=[&]{++dialogs;};
      NativePasteBackend p(f.calls);SelectionIntent intent;intent.command.action=MenuAction::SelectSnippet;
      SelectionOutcome outcome;outcome.status=SelectionStatus::NeedsManualPaste;outcome.sequence_token="copied";
      if(n==0)intent.auto_paste=false;
      if(n==1)intent.command.invocation.copy_only=true;
      if(n==2)intent.action=SelectionAction::DeleteOnly;
      if(n==3)outcome.status=SelectionStatus::Copied;
      if(n==4)outcome.status=SelectionStatus::Cancelled;
      if(n==5)outcome.status=SelectionStatus::Dispatched;
      if(n==6)outcome.sequence_token.clear();
      if(n==7)f.trusted=true;
      if(n==8)intent.command.action=MenuAction::OpenSettings;
      CHECK(!p.explain_paste_permission(intent,outcome) && dialogs==0);
    }
    Fixture f;f.trusted=false;unsigned dialogs=0;f.calls->permission_help=[&]{++dialogs;};
    NativePasteBackend p(f.calls);SelectionIntent intent;intent.command.action=MenuAction::SelectSnippet;
    SelectionOutcome outcome;outcome.status=SelectionStatus::NeedsManualPaste;outcome.sequence_token="copied";
    CHECK(p.explain_paste_permission(intent,outcome) && dialogs==1);
  });
  return failed ? 1 : 0;
}
int main() {
  @autoreleasepool {
    return run();
  }
}

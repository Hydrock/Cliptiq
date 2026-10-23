#include "cliptiq/paste.hpp"
#include "sqlite3.h"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
struct Clipboard : ClipboardBackend {
  ClipboardSnapshot written;
  CapturePlatform platform=CapturePlatform::X11;
  unsigned writes = 0;
  bool fail = false;
  std::function<void()> after_write;
  Capabilities capabilities() const override {
    Capabilities c;
    c.platform = platform;
    return c;
  }
  Result<ClipboardSnapshot> read_snapshot() override { return written; }
  Result<std::string> write_snapshot(const ClipboardSnapshot &s) override {
    if (fail)
      return Error{ErrorCode::Io, "clipboard locked"};
    written = s;
    ++writes;
    if (after_write)
      after_write();
    return std::string("own:42");
  }
  Result<void> start(CaptureCallback) override { return {}; }
  void stop() override {}
};
struct Paste : PasteBackend {
  bool denied = false;
  unsigned calls = 0;
  TargetToken target;
  Shortcut shortcut{};
  Result<void> dispatch(const TargetToken &t, Shortcut s) override {
    ++calls;
    target = t;
    shortcut = s;
    if (denied)
      return Error{ErrorCode::Unsupported,
                   "permission/UIPI/focus/modifier denied"};
    return {};
  }
};
struct Fixture {
  Store store = take(open_store(":memory:"));
  Clipboard clipboard;
  Paste paste;
  SelectionIntent intent;
  Fixture(bool image = false) {
    ClipboardSnapshot s;
    s.representations = {
        {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'h', 'i'}},
        {0, "x11", "text/html", CanonicalKind::Html, {'<', 'b', '>'}}};
    if (image)
      s.representations = {
          {0, "x11", "image/png", CanonicalKind::Image, {1, 2, 3}}};
    auto id = *take(ingest(store, s, {}, 1));
    auto model = take(build_menu(store, MenuKind::History));
    intent.command = {
        MenuAction::SelectHistory, id, model.revision, {"target:7"}};
    intent.now_ms = 10;
  }
  SelectionOutcome run() {
    return take(execute_selection(store, intent, clipboard, paste));
  }
};
int main() {
  int failures = 0;
  auto test = [&](const char *name, auto fn) {
    try {
      fn();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  };
  test("auto_off_copies_original_all_formats_and_touches", [] {
    Fixture f;
    f.intent.auto_paste = false;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Copied && f.clipboard.writes == 1 &&
          f.paste.calls == 0);
    CHECK(f.clipboard.written.representations.size() == 2 &&
          o.sequence_token == "own:42");
    CHECK(take(list_history(f.store))[0].used_ms == 10);
  });
  test("plain_keeps_only_text_without_rich_formats", [] {
    Fixture f;
    f.intent.action = SelectionAction::Plain;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Dispatched);
    CHECK(f.clipboard.written.representations.size() == 1 &&
          f.clipboard.written.representations[0].data == Bytes({'h', 'i'}));
  });
  test("image_only_plain_never_overwrites_clipboard", [] {
    Fixture f(true);
    f.intent.action = SelectionAction::Plain;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Cancelled && f.clipboard.writes == 0 &&
          f.paste.calls == 0);
    CHECK(take(list_history(f.store)).size() == 1);
  });
  test("delete_only_removes_history_without_write_or_input", [] {
    Fixture f;
    f.intent.action = SelectionAction::DeleteOnly;
    auto o = f.run();
    CHECK(o.history_deleted && f.clipboard.writes == 0 && f.paste.calls == 0);
    CHECK(take(list_history(f.store)).empty());
  });
  test("paste_delete_removes_only_after_backend_acceptance", [] {
    Fixture f;
    f.intent.action = SelectionAction::PasteDelete;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Dispatched && o.history_deleted &&
          f.paste.target == "target:7");
    CHECK(take(list_history(f.store)).empty());
  });
  test("denied_paste_delete_keeps_sole_record_and_manual_copy", [] {
    Fixture f;
    f.intent.action = SelectionAction::PasteDelete;
    f.paste.denied = true;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::NeedsManualPaste && !o.history_deleted &&
          !o.reason.empty() && f.clipboard.writes == 1);
    CHECK(take(list_history(f.store)).size() == 1);
  });
  test("auto_off_paste_delete_does_not_delete", [] {
    Fixture f;
    f.intent.action = SelectionAction::PasteDelete;
    f.intent.auto_paste = false;
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Copied && !o.history_deleted &&
          take(list_history(f.store)).size() == 1);
  });
  test("failed_write_never_dispatches_or_deletes", [] {
    Fixture f;
    f.clipboard.fail = true;
    f.intent.action = SelectionAction::PasteDelete;
    auto result = execute_selection(f.store, f.intent, f.clipboard, f.paste);
    CHECK(!result && result.error().code == ErrorCode::Io);
    CHECK(f.paste.calls == 0 && take(list_history(f.store)).size() == 1);
  });
  test("stale_menu_revision_has_no_side_effect", [] {
    Fixture f;
    take(ingest(f.store,
                {{{0, "x11", "UTF8_STRING", CanonicalKind::Text, {'n'}}}}, {},
                2));
    auto result = execute_selection(f.store, f.intent, f.clipboard, f.paste);
    CHECK(!result && result.error().code == ErrorCode::Busy);
    CHECK(f.clipboard.writes == 0);
  });
  test("concurrent_revision_after_dispatch_does_not_delete", [] {
    Fixture f;
    f.intent.action = SelectionAction::PasteDelete;
    f.clipboard.after_write = [&] {
      take(ingest(f.store,
                  {{{0, "x11", "UTF8_STRING", CanonicalKind::Text, {'n'}}}}, {},
                  2));
    };
    auto o = f.run();
    CHECK(o.status == SelectionStatus::Dispatched && !o.history_deleted &&
          o.history_error.has_value());
    CHECK(take(list_history(f.store)).size() == 2);
  });
  test("terminal_shortcut_and_missing_target_manual", [] {
    Fixture f;
    f.intent.shortcut = Shortcut::TerminalPaste;
    f.run();
    CHECK(f.paste.shortcut == Shortcut::TerminalPaste);
    Fixture g;
    g.intent.command.invocation.target.clear();
    CHECK(g.run().status == SelectionStatus::NeedsManualPaste &&
          g.paste.calls == 0);
  });
  test("snippets_copied_without_history_deletion", [] {
    Fixture f;
    CHECK(sqlite3_exec(
              f.store.connection(),
              "INSERT INTO snippet_folders VALUES('f','Folder',0);INSERT INTO "
              "snippets VALUES('s','f','Snippet','line1\nline2',0)",
              nullptr, nullptr, nullptr) == SQLITE_OK);
    f.intent.command = {MenuAction::SelectSnippet,
                        "s",
                        take(data_revision(f.store)),
                        {"target:7"}};
    f.intent.action = SelectionAction::Original;
    CHECK(f.run().status == SelectionStatus::Dispatched);
    CHECK(f.clipboard.written.representations[0].data ==
          Bytes({'l', 'i', 'n', 'e', '1', '\n', 'l', 'i', 'n', 'e', '2'}));
  });
  test("destructive_modifier_never_deletes_snippet", [] {
    Fixture f;
    CHECK(
        sqlite3_exec(f.store.connection(),
                     "INSERT INTO snippets VALUES('s',NULL,'Snippet','safe',0)",
                     nullptr, nullptr, nullptr) == SQLITE_OK);
    f.intent.command = {MenuAction::SelectSnippet,
                        "s",
                        take(data_revision(f.store)),
                        {"target:7"}};
    f.intent.action = SelectionAction::DeleteOnly;
    CHECK(f.run().status == SelectionStatus::Cancelled &&
          f.clipboard.writes == 0);
  });
  test("modifier_binding_unique_single_and_conflicting_held_cancel", [] {
    CHECK(take(selection_action(Control, {})) == SelectionAction::Plain);
    CHECK(take(selection_action(0, {})) == SelectionAction::Original);
    CHECK(take(selection_action(Alt, {Control, Alt, Shift})) ==
          SelectionAction::DeleteOnly);
    CHECK(take(selection_action(Shift, {Control, Alt, Shift})) ==
          SelectionAction::PasteDelete);
    CHECK(!selection_action(Control, {Control, Control, 0}));
    CHECK(!selection_action(3, {3, 0, 0}));
    CHECK(!selection_action(Shift | Control, {Control, 0, Shift}));
    CHECK(!selection_action(16, {}));
  });
  test("invalid_action_shortcut_or_clock_rejected_before_write", [] {
    for (int n = 0; n < 3; ++n) {
      Fixture f;
      if (n == 0)
        f.intent.action = SelectionAction(99);
      if (n == 1)
        f.intent.shortcut = Shortcut(99);
      if (n == 2)
        f.intent.now_ms = -1;
      auto result = execute_selection(f.store, f.intent, f.clipboard, f.paste);
      CHECK(!result && result.error().code == ErrorCode::InvalidArgument);
      CHECK(f.clipboard.writes == 0);
    }
  });
  test("snippet_encoded_and_entry_limits_before_writer", [] {
    struct Case {std::uint64_t rep,entry;const char *content;bool allowed;std::size_t size;};
    for(auto c:{Case{8,8,"abcde",false,0},Case{16,8,"abcde",false,0},Case{8,8,"abc",true,8},Case{6,6,"😀",true,6},Case{5,5,"😀",false,0}}){
      Limits limits;limits.representation_bytes=c.rep;limits.entry_bytes=c.entry;
      auto store=take(open_store(":memory:",limits));std::string sql="INSERT INTO snippets VALUES('s',NULL,'S','"+std::string(c.content)+"',0)";
      CHECK(sqlite3_exec(store.connection(),sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);
      Clipboard clipboard;clipboard.platform=CapturePlatform::Windows;Paste paste;SelectionIntent intent;intent.command={MenuAction::SelectSnippet,"s",take(data_revision(store)),{"target:7"}};
      auto result=execute_selection(store,intent,clipboard,paste);
      if(c.allowed){CHECK(result&&clipboard.writes==1&&clipboard.written.representations[0].data.size()==c.size);}
      else{CHECK(!result&&result.error().code==ErrorCode::TooLarge&&clipboard.writes==0&&paste.calls==0);}
    }
  });
  return failures ? 1 : 0;
}

#include "cliptiq/editor.hpp"
#include "cliptiq/paste.hpp"
#include "cliptiq/settings.hpp"
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
struct Backend : HotkeyBackend {
  unsigned serial = 0;
  std::map<unsigned, std::string> held;
  Result<unsigned> acquire(const ActionId &, const KeyChord &c) override {
    auto value = format_chord(c);
    for (auto &p : held)
      if (p.second == value)
        return Error{ErrorCode::Busy, "fixture conflict"};
    held[++serial] = value;
    return serial;
  }
  void release(unsigned id) override { held.erase(id); }
  Result<void> dispatch() override { return {}; }
  Capability capability() const override {
    return {CapabilityStatus::Available, "fixture"};
  }
};
struct Clipboard : ClipboardBackend {
  unsigned writes = 0;
  Capabilities capabilities() const override {
    Capabilities c;
    c.platform = CapturePlatform::X11;
    return c;
  }
  Result<ClipboardSnapshot> read_snapshot() override {
    return ClipboardSnapshot{};
  }
  Result<std::string> write_snapshot(const ClipboardSnapshot &) override {
    ++writes;
    return std::string("fixture");
  }
  Result<void> start(CaptureCallback) override { return {}; }
  void stop() override {}
};
struct Paste : PasteBackend {
  unsigned calls = 0;
  Result<void> dispatch(const TargetToken &, Shortcut) override {
    ++calls;
    return {};
  }
};
int main() {
  int failures = 0;
  auto test = [&](const char *n, auto f) {
    try {
      f();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failures;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("disabled_folder_preferences_never_register_and_reenable_restores", [] {
    auto db = take(open_store(":memory:"));
    auto f = take(create_folder(db, "F"));
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, false));
    Backend b;
    HotkeyRegistry registry(b);
    auto prefs = take(load_settings(db));
    prefs.hotkeys["folder:" + f] = take(parse_chord("Ctrl+Alt+Q"));
    CHECK(apply_settings(db, registry, prefs));
    CHECK(!registry.registered("folder:" + f));
    CHECK(format_chord(*take(load_settings(db)).hotkeys.at("folder:" + f)) ==
          "Ctrl+Alt+Q");
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, true));
    CHECK(synchronize_folder_hotkeys(db, registry));
    CHECK(registry.registered("folder:" + f));
    CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, false));
    CHECK(synchronize_folder_hotkeys(db, registry));
    CHECK(!registry.registered("folder:" + f));
    CHECK(erase_snippet_item(db, {SnippetItemKind::Folder, f}));
    CHECK(synchronize_folder_hotkeys(db, registry));
    CHECK(!take(load_settings(db)).hotkeys.count("folder:" + f));
  });
  test("menu_selection_and_paste_cannot_replay_disabled_or_deleted_snippets",
       [] {
         auto db = take(open_store(":memory:"));
         auto f = take(create_folder(db, "F"));
         Snippet s;
         s.folder_id = f;
         s.title = "S";
         s.content = "body";
         auto id = take(save_snippet(db, s));
         auto menu = take(build_folder_menu(db, f));
         CHECK(menu.items[0].id == id);
         auto command = MenuCommand{
             MenuAction::SelectSnippet, id, menu.revision, {"owned-target"}};
         CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, false));
         CHECK(!validate_menu_command(db, command));
         command.revision = take(data_revision(db));
         CHECK(!validate_menu_command(db, command));
         Clipboard writer;
         Paste input;
         SelectionIntent intent;
         intent.command = command;
         CHECK(!execute_selection(db, intent, writer, input));
         CHECK(writer.writes == 0 && input.calls == 0);
         CHECK(set_enabled(db, {SnippetItemKind::Snippet, id}, true));
         intent.command.revision = take(data_revision(db));
         intent.auto_paste = false;
         CHECK(execute_selection(db, intent, writer, input));
         CHECK(writer.writes == 1);
         CHECK(set_enabled(db, {SnippetItemKind::Folder, f}, false));
         intent.command.revision = take(data_revision(db));
         CHECK(!execute_selection(db, intent, writer, input) &&
               writer.writes == 1);
         CHECK(erase_snippet_item(db, {SnippetItemKind::Folder, f}));
         CHECK(!get_snippet(db, id));
       });
  return failures ? 1 : 0;
}

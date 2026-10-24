#include "cliptiq/editor.hpp"
#include "sqlite3.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
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
int main() {
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
  test("autosave_undo_redo_resets_at_selection_boundary", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    auto f = take(e.add_folder());
    auto a = take(e.add_snippet());
    CHECK(e.edit_title("A"));
    CHECK(e.edit_content("first\nline"));
    CHECK(e.edit_content("second"));
    CHECK(e.can_undo());
    CHECK(e.undo());
    CHECK(take(get_snippet(db, a)).content == "first\nline");
    CHECK(e.redo());
    CHECK(e.content() == "second");
    auto b = take(e.add_snippet());
    CHECK(!e.can_undo() && !e.can_redo());
    CHECK(e.edit_content("B"));
    CHECK(e.select(SnippetItemId{SnippetItemKind::Snippet, a}));
    CHECK(!e.can_undo() && e.content() == "second");
    CHECK(!e.undo());
    CHECK(take(get_snippet(db, b)).content == "B");
    CHECK(e.select(SnippetItemId{SnippetItemKind::Folder, f}));
    CHECK(!e.edit_content("no folder body"));
  });
  test("cancelled_drag_and_stale_drag_do_not_mutate", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    auto a = take(e.add_folder());
    auto x = take(e.add_snippet());
    auto b = take(e.add_folder());
    auto rev = take(data_revision(db));
    auto token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(e.finish_drag(token, b, 0, false));
    CHECK(take(data_revision(db)) == rev &&
          take(get_snippet(db, x)).folder_id == a);
    token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(set_enabled(db, {SnippetItemKind::Snippet, x}, false));
    CHECK(!e.finish_drag(token, b, 0, true));
    CHECK(take(get_snippet(db, x)).folder_id == a);
    CHECK(e.reload());
    token = take(e.begin_drag({SnippetItemKind::Snippet, x}));
    CHECK(e.finish_drag(token, b, 0, true));
    CHECK(take(get_snippet(db, x)).folder_id == b);
    CHECK(!e.finish_drag(token, a, 0, true));
  });
  test("delete_confirmation_empty_folder_and_binding_conflicts", [] {
    auto db = take(open_store(":memory:"));
    bool conflict = true;
    unsigned changes = 0;
    SnippetEditorCallbacks cb;
    cb.changed = [&] { ++changes; };
    cb.bind_folder = [&](const auto &, auto) -> Result<void> {
      if (conflict)
        return Error{ErrorCode::Busy, "conflict"};
      return {};
    };
    SnippetEditorSession e(db, cb);
    CHECK(e.reload());
    auto f = take(e.add_folder());
    CHECK(!e.bind_folder(take(parse_chord("Ctrl+Alt+Q"))));
    conflict = false;
    CHECK(e.bind_folder(take(parse_chord("Ctrl+Alt+Q"))));
    CHECK(e.erase_selected(false));
    CHECK(e.folders().size() == 1);
    CHECK(e.enable(false));
    CHECK(!take(list_folders(db))[0].enabled);
    CHECK(e.erase_selected(true));
    CHECK(e.folders().empty() && !e.selected() && changes >= 3);
    CHECK(!e.add_snippet());
  });
  test("failed_edit_preserves_document_and_undo", [] {
    auto db = take(open_store(":memory:"));
    SnippetEditorSession e(db);
    CHECK(e.reload());
    take(e.add_folder());
    auto id = take(e.add_snippet());
    CHECK(e.edit_content("safe"));
    auto before = e.content();
    CHECK(!e.edit_content(std::string(1024 * 1024 + 1, 'x')));
    CHECK(e.content() == before && take(get_snippet(db, id)).content == before);
    auto external = take(get_snippet(db, id));
    external.content = "external";
    CHECK(save_snippet(db, external));
    CHECK(!e.edit_content("stale"));
    CHECK(take(get_snippet(db, id)).content == "external");
    CHECK(e.reload());
    CHECK(!e.can_undo() && e.content() == "external");
  });
  test("failed_autosave_blocks_navigation_until_corrected_or_explicit_reload",
       [] {
         auto db = take(open_store(":memory:"));
         SnippetEditorSession e(db);
         CHECK(e.reload());
         auto folder = take(e.add_folder());
         auto id = take(e.add_snippet());
         CHECK(e.edit_content("safe"));
         auto external = take(get_snippet(db, id));
         external.content = "external";
         CHECK(save_snippet(db, external));
         CHECK(!e.edit_content("unsaved draft"));
         CHECK(e.has_unsaved());
         CHECK(!e.select(SnippetItemId{SnippetItemKind::Folder, folder}));
         CHECK(!e.add_folder());
         CHECK(!e.undo());
         CHECK(e.selected()->value == id &&
               take(get_snippet(db, id)).content == "external");
         CHECK(e.reload());
         CHECK(!e.has_unsaved() && e.content() == "external");
       });
  test("drag_revision_is_checked_inside_write_transaction", [] {
    auto path = std::filesystem::temp_directory_path() /
                ("cliptiq-drag-" +
                 std::to_string(std::chrono::high_resolution_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".db");
    {
      auto db = take(open_store(path.u8string()));
      auto peer = take(open_store(path.u8string()));
      SnippetEditorSession e(db);
      CHECK(e.reload());
      auto a = take(e.add_folder());
      auto id = take(e.add_snippet());
      auto b = take(e.add_folder());
      auto token = take(e.begin_drag({SnippetItemKind::Snippet, id}));
      struct Hook {
        Store *peer;
        SnippetItemId item;
        bool fired = false, ok = false;
      } hook{&peer, {SnippetItemKind::Snippet, id}};
      sqlite3_trace_v2(
          db.connection(), SQLITE_TRACE_STMT,
          +[](unsigned, void *data, void *, void *sql) -> int {
            auto h = static_cast<Hook *>(data);
            if (!h->fired && std::string(static_cast<const char *>(sql)) ==
                                 "BEGIN IMMEDIATE") {
              h->fired = true;
              h->ok = bool(set_enabled(*h->peer, h->item, false));
            }
            return 0;
          },
          &hook);
      auto dropped = e.finish_drag(token, b, 0, true);
      sqlite3_trace_v2(db.connection(), 0, nullptr, nullptr);
      CHECK(hook.fired && hook.ok);
      CHECK(!dropped && dropped.error().code == ErrorCode::Busy);
      CHECK(take(get_snippet(db, id)).folder_id == a);
    }
    std::filesystem::remove(path);
  });
  test("folder_drop_does_not_overwrite_concurrent_other_folder_reorder", [] {
    auto path = std::filesystem::temp_directory_path() /
                ("cliptiq-folder-drag-" +
                 std::to_string(std::chrono::high_resolution_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                 ".db");
    {
      auto db = take(open_store(path.u8string()));
      auto peer = take(open_store(path.u8string()));
      SnippetEditorSession e(db);
      CHECK(e.reload());
      auto a = take(e.add_folder()), b = take(e.add_folder()),
           c = take(e.add_folder());
      auto token = take(e.begin_drag({SnippetItemKind::Folder, c}));
      struct Hook {
        Store *peer;
        std::vector<FolderId> order;
        bool fired = false, ok = false;
      } hook{&peer, {b, a, c}};
      sqlite3_trace_v2(
          db.connection(), SQLITE_TRACE_STMT,
          +[](unsigned, void *data, void *, void *sql) -> int {
            auto h = static_cast<Hook *>(data);
            if (!h->fired && std::string(static_cast<const char *>(sql)) ==
                                 "BEGIN IMMEDIATE") {
              h->fired = true;
              h->ok = bool(reorder_folders(*h->peer, h->order));
            }
            return 0;
          },
          &hook);
      auto result = e.finish_drag(token, "", 0, true);
      sqlite3_trace_v2(db.connection(), 0, nullptr, nullptr);
      CHECK(hook.fired && hook.ok);
      CHECK(!result && result.error().code == ErrorCode::Busy);
      CHECK(take(list_folders(db))[0].id == b);
    }
    std::filesystem::remove(path);
  });
  test("committed_snapshot_does_not_adopt_peer_revision", [] {
    auto path = (std::filesystem::temp_directory_path() / "cliptiq-editor-commit-race.db").string();
    std::filesystem::remove(path);
    {
      auto db=take(open_store(path)); auto peer=take(open_store(path));
      SnippetEditorSession e(db);CHECK(e.reload());take(e.add_folder());auto id=take(e.add_snippet());
      struct Hook { Store* peer; std::string id; bool armed=false,fired=false,ok=false; } hook{&peer,id};
      sqlite3_trace_v2(db.connection(),SQLITE_TRACE_STMT,+[](unsigned,void* data,void* stmt,void*)->int {
        auto h=static_cast<Hook*>(data);
        auto sql=std::string(sqlite3_sql(static_cast<sqlite3_stmt*>(stmt)));
        if(sql=="COMMIT") h->armed=true;
        if(h->armed && !h->fired && sql.rfind("SELECT",0)==0) {
          h->fired=true; auto item=get_snippet(*h->peer,h->id);
          if(item) {item.value().content="peer"; h->ok=bool(save_snippet(*h->peer,item.value()));}
        } return 0;
      },&hook);
      CHECK(e.edit_content("mine"));take(data_revision(db));sqlite3_trace_v2(db.connection(),0,nullptr,nullptr);
      CHECK(hook.fired && hook.ok);CHECK(e.content()=="mine");
      auto next=e.edit_content("mine next");CHECK(!next && next.error().code==ErrorCode::Busy);
      CHECK(take(get_snippet(peer,id)).content=="peer");
    }
    std::filesystem::remove(path);
  });
  test("background_autosave_keeps_event_loop_responsive_under_write_contention", [] {
    auto path=(std::filesystem::temp_directory_path()/"cliptiq-editor-worker.db").string();std::filesystem::remove(path);
    {
      auto db=take(open_store(path));auto peer=take(open_store(path));SnippetEditorSession e(db);
      CHECK(e.reload());take(e.add_folder());auto id=take(e.add_snippet());CHECK(e.background_saves());
      CHECK(sqlite3_exec(peer.connection(),"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK);
      auto start=std::chrono::steady_clock::now();CHECK(e.queue_content("first"));CHECK(e.queue_content("latest"));
      CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(150));
      CHECK(e.has_unsaved() && e.saving());CHECK(!e.select(std::nullopt));CHECK(!e.reload());
      CHECK(sqlite3_exec(peer.connection(),"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK);
      for(unsigned i=0;i<300 && e.saving();++i) {CHECK(e.poll_saves());std::this_thread::sleep_for(std::chrono::milliseconds(5));}
      CHECK(!e.saving() && !e.has_unsaved());CHECK(e.content()=="latest");CHECK(take(get_snippet(db,id)).content=="latest");
      CHECK(e.can_undo());
      auto external=take(get_snippet(peer,id));external.content="peer";CHECK(save_snippet(peer,external));
      CHECK(e.queue_content("stale visible draft"));bool conflict=false;
      for(unsigned i=0;i<300 && e.saving();++i) {auto r=e.poll_saves();if(!r) {CHECK(r.error().code==ErrorCode::Busy);conflict=true;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
      CHECK(conflict && e.has_unsaved());CHECK(take(get_snippet(db,id)).content=="peer");CHECK(e.reload());
    }
    std::filesystem::remove(path);
  });
  return failed ? 1 : 0;
}

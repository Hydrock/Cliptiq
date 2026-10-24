#include "cliptiq/editor.hpp"
#include "db.hpp"
#include <algorithm>
#include <deque>
#include <future>
#include <chrono>
namespace cliptiq {
namespace {
template <class T> T need(Result<T> r) {
  if (!r)
    throw detail::Failure{r.error()};
  return std::move(r.value());
}
void need(Result<void> r) {
  if (!r)
    throw detail::Failure{r.error()};
}
bool same(const SnippetItemId &a, const SnippetItemId &b) {
  return a.kind == b.kind && a.value == b.value;
}
// Discrete UI commands fail fast on contention; bulk text persistence uses
// the independent worker connection. Store callers serialize this scope.
class UiBusyScope {
  sqlite3 *db=nullptr;
public:
  UiBusyScope(Store &s,bool ui) {if(ui) {db=s.connection();sqlite3_busy_timeout(db,0);}}
  ~UiBusyScope() {if(db) sqlite3_busy_timeout(db,1000);}
};
using History = std::deque<std::string>;
void trim(History &h) {
  std::size_t bytes = 0;
  for (auto &s : h)
    bytes += s.size();
  while (h.size() > 64 || bytes > 4 * 1024 * 1024) {
    bytes -= h.front().size();
    h.pop_front();
  }
}
} // namespace
struct SnippetEditorSession::State {
  Store &store;
  SnippetEditorCallbacks callbacks;
  std::vector<Folder> folders;
  std::vector<SnippetInfo> snippets;
  std::optional<SnippetItemId> selected;
  std::optional<Folder> folder;
  std::optional<Snippet> doc;
  History undo, redo;
  struct Worker {
    std::string path;
    Limits limits;
    std::optional<Store> db;
  };
  struct Saved { std::optional<Folder> folder; std::optional<Snippet> doc; };
  std::shared_ptr<Worker> worker;
  std::future<Result<Saved>> pending;
  std::optional<std::string> desired_title,desired_content;
  bool dirty_title = false, dirty_content = false;
  bool dirty() const { return dirty_title || dirty_content || pending.valid() || desired_title || desired_content; }
  void start_save() {
    if(pending.valid() || (!desired_title && !desired_content)) return;
    auto next_doc=doc;auto next_folder=folder;
    if(next_doc) {
      if(desired_title) next_doc->title=*desired_title;
      if(desired_content) next_doc->content=*desired_content;
    } else if(next_folder && desired_title) next_folder->title=*desired_title;
    auto w=worker;
    pending=std::async(std::launch::async,[w,next_doc=std::move(next_doc),next_folder=std::move(next_folder)]() mutable {
      return detail::guarded<Saved>([&]() -> Result<Saved> {
        if(w->path.empty()) return Error{ErrorCode::Unsupported,"Background editing requires a file-backed store"};
        if(!w->db) w->db.emplace(need(open_store(w->path,w->limits)));
        Saved result;
        if(next_doc) result.doc=need(save_snippet_committed(*w->db,*next_doc));
        else if(next_folder) result.folder=need(save_folder_committed(*w->db,*next_folder));
        else return Error{ErrorCode::InvalidArgument,"Select an item first"};
        return result;
      });
    });
  }
  void require_saved() const {
    if (dirty())
      throw detail::Failure{{ErrorCode::Busy,
                             "Draft not saved: correct it or Reload to discard "
                             "before navigating"}};
  }
  struct Drag {
    std::uint64_t token;
    SnippetItemId item;
    std::uint64_t revision;
  };
  std::optional<Drag> drag;
  std::uint64_t token = 0;
  State(Store &s, SnippetEditorCallbacks c)
      : store(s), callbacks(std::move(c)) {}
  void changed() {
    if (callbacks.changed)
      callbacks.changed();
  }
  void catalog() {
    auto f = need(list_folders(store));
    auto s = need(list_snippet_headers(store));
    folders = std::move(f);
    snippets = std::move(s);
  }
  void clear() {
    dirty_title = dirty_content = false;
    desired_title.reset();desired_content.reset();
    selected.reset();
    folder.reset();
    doc.reset();
    undo.clear();
    redo.clear();
  }
  void select(std::optional<SnippetItemId> item, bool reset) {
    if (!item) {
      clear();
      return;
    }
    if (unsigned(item->kind) > 1)
      detail::invalid("Unknown editor item kind");
    if (item->kind == SnippetItemKind::Folder) {
      auto f = std::find_if(folders.begin(), folders.end(),
                            [&](auto &f) { return f.id == item->value; });
      if (f == folders.end())
        throw detail::Failure{
            {ErrorCode::NotFound, "Folder disappeared; reload editor"}};
      folder = *f;
      doc.reset();
    } else {
      auto next = need(get_snippet(store, item->value));
      if (doc && doc->revision != next.revision)
        reset = true;
      doc = std::move(next);
      folder.reset();
    }
    selected = std::move(item);
    if (reset) {
      undo.clear();
      redo.clear();
    }
  }
  void persist(Snippet next) {
    if(pending.valid()) throw detail::Failure{{ErrorCode::Busy,"Save pending"}};
    doc = need(save_snippet_committed(store, next));
    auto id = doc->id;
    for (auto &h : snippets)
      if (h.id == id) {
        h = static_cast<const SnippetInfo &>(*doc);
        break;
      }
    changed();
  }
  void persist(Folder next) {
    if(pending.valid()) throw detail::Failure{{ErrorCode::Busy,"Save pending"}};
    folder = need(save_folder_committed(store,next));
    for(auto &f:folders) if(f.id==folder->id) {f=*folder;break;}
    changed();
  }
  std::uint64_t item_version(const SnippetItemId &item) {
    if (item.kind == SnippetItemKind::Folder) {
      auto fs = need(list_folders(store));
      for (auto &f : fs)
        if (f.id == item.value)
          return f.revision;
    } else
      return need(get_snippet(store, item.value)).revision;
    throw detail::Failure{{ErrorCode::NotFound, "Dragged item disappeared"}};
  }
};
SnippetEditorSession::SnippetEditorSession(Store &s, SnippetEditorCallbacks cb)
    : state_(new State(s, std::move(cb))) {}
SnippetEditorSession::~SnippetEditorSession() = default;
std::string SnippetEditorSession::database_path() const {
  auto path = sqlite3_db_filename(state_->store.connection(), "main");
  return path ? path : "";
}
Limits SnippetEditorSession::store_limits() const {
  return state_->store.limits();
}
Result<void> SnippetEditorSession::refresh_external() {
  if (has_unsaved())
    return Error{ErrorCode::Busy,
                 "Draft must be saved before refreshing imported data"};
  auto result = reload();
  if (result)
    state_->changed();
  return result;
}
Result<void> SnippetEditorSession::reload() {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    if(s.pending.valid()) return Error{ErrorCode::Busy,"Save pending; retry Reload after completion"};
    auto selected = s.selected;
    s.catalog();
    if (selected) {
      auto loaded = detail::guarded<void>([&]() -> Result<void> {
        s.select(selected, true);
        return {};
      });
      if (!loaded && loaded.error().code == ErrorCode::NotFound)
        s.clear();
      else if (!loaded)
        return loaded;
    }
    s.dirty_title = s.dirty_content = false;
    s.desired_title.reset();s.desired_content.reset();
    return {};
  });
}
const std::vector<Folder> &SnippetEditorSession::folders() const {
  return state_->folders;
}
const std::vector<SnippetInfo> &SnippetEditorSession::snippets() const {
  return state_->snippets;
}
std::optional<SnippetItemId> SnippetEditorSession::selected() const {
  return state_->selected;
}
std::string SnippetEditorSession::title() const {
  return state_->doc      ? state_->doc->title
         : state_->folder ? state_->folder->title
                          : "";
}
std::string SnippetEditorSession::content() const {
  return state_->doc ? state_->doc->content : "";
}
bool SnippetEditorSession::enabled() const {
  return state_->doc      ? state_->doc->enabled
         : state_->folder ? state_->folder->enabled
                          : false;
}
Result<void> SnippetEditorSession::select(std::optional<SnippetItemId> item) {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    bool reset = !item || !s.selected || !same(*item, *s.selected);
    s.select(std::move(item), reset);
    return {};
  });
}
Result<FolderId> SnippetEditorSession::add_folder() {
  return detail::guarded<FolderId>([&]() -> Result<FolderId> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    auto id = need(create_folder(s.store, "New Folder"));
    s.catalog();
    s.select(SnippetItemId{SnippetItemKind::Folder, id}, true);
    s.changed();
    return id;
  });
}
Result<SnippetId> SnippetEditorSession::add_snippet() {
  return detail::guarded<SnippetId>([&]() -> Result<SnippetId> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    if (!s.selected)
      return Error{ErrorCode::InvalidArgument, "Select a folder first"};
    Snippet next;
    next.folder_id = s.folder ? s.folder->id : s.doc->folder_id;
    next.title = "New Snippet";
    auto id = need(save_snippet(s.store, next));
    s.catalog();
    s.select(SnippetItemId{SnippetItemKind::Snippet, id}, true);
    s.changed();
    return id;
  });
}
Result<void> SnippetEditorSession::background_saves() {
  if(!state_->worker) {
    auto filename=sqlite3_db_filename(state_->store.connection(),"main");
    state_->worker=std::make_shared<State::Worker>();
    state_->worker->path=filename?filename:"";
    state_->worker->limits=state_->store.limits();
  }
  return {};
}
Result<void> SnippetEditorSession::queue_title(const std::string &text) {
  if(!state_->worker) return edit_title(text);
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s=*state_;if(!s.selected) return Error{ErrorCode::InvalidArgument,"Select an item first"};
    s.desired_title=text;s.dirty_title=true;s.start_save();return {};
  });
}
Result<void> SnippetEditorSession::queue_content(const std::string &text) {
  if(!state_->worker) return edit_content(text);
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s=*state_;if(!s.doc) return Error{ErrorCode::InvalidArgument,"Only snippets have text"};
    s.desired_content=text;s.dirty_content=true;s.start_save();return {};
  });
}
Result<bool> SnippetEditorSession::poll_saves() {
  return detail::guarded<bool>([&]() -> Result<bool> {
    auto &s=*state_;
    if(!s.pending.valid() || s.pending.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return false;
    auto result=s.pending.get();if(!result) return result.error();
    if(result.value().doc) {
      auto next=std::move(*result.value().doc);
      if(s.doc->content!=next.content) {s.undo.push_back(s.doc->content);trim(s.undo);s.redo.clear();}
      s.doc=std::move(next);
      for(auto &h:s.snippets) if(h.id==s.doc->id) {h=static_cast<const SnippetInfo&>(*s.doc);break;}
    } else {
      s.folder=std::move(result.value().folder);
      for(auto &f:s.folders) if(f.id==s.folder->id) {f=*s.folder;break;}
    }
    if(s.desired_title && *s.desired_title==title()) s.desired_title.reset();
    if(s.desired_content && *s.desired_content==content()) s.desired_content.reset();
    s.dirty_title=bool(s.desired_title);s.dirty_content=bool(s.desired_content);
    s.changed();s.start_save();return true;
  });
}
bool SnippetEditorSession::saving() const { return state_->pending.valid(); }
Result<void> SnippetEditorSession::edit_title(const std::string &text) {
  auto result = detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    if (s.doc) {
      auto next = *s.doc;
      if (next.title == text && !s.dirty_title)
        return {};
      next.title = text;
      s.persist(std::move(next));
    } else if (s.folder) {
      auto next = *s.folder;
      if (next.title == text && !s.dirty_title)
        return {};
      next.title = text;
      s.persist(std::move(next));
    } else
      return Error{ErrorCode::InvalidArgument, "Select an item first"};
    return {};
  });
  if (state_->selected)
    state_->dirty_title = !result;
  return result;
}
Result<void> SnippetEditorSession::edit_content(const std::string &text) {
  auto result = detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    if (!s.doc)
      return Error{ErrorCode::InvalidArgument, "Only snippets have text"};
    if (text == s.doc->content && !s.dirty_content)
      return {};
    auto history = s.undo;
    history.push_back(s.doc->content);
    trim(history);
    auto next = *s.doc;
    next.content = text;
    s.persist(std::move(next));
    s.undo.swap(history);
    s.redo.clear();
    return {};
  });
  if (state_->doc)
    state_->dirty_content = !result;
  return result;
}
Result<void> SnippetEditorSession::enable(bool enabled) {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    if (s.doc) {
      auto next = *s.doc;
      next.enabled = enabled;
      s.persist(std::move(next));
    } else if (s.folder) {
      auto next = *s.folder;
      next.enabled = enabled;
      s.persist(std::move(next));
    } else
      return Error{ErrorCode::InvalidArgument, "Select an item first"};
    return {};
  });
}
Result<void> SnippetEditorSession::erase_selected(bool confirmed) {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    if (!confirmed)
      return {};
    s.require_saved();
    if (!s.selected)
      return Error{ErrorCode::InvalidArgument, "Select an item first"};
    need(erase_snippet_item(s.store, *s.selected));
    s.clear();
    s.catalog();
    s.changed();
    return {};
  });
}
bool SnippetEditorSession::has_unsaved() const { return state_->dirty(); }
bool SnippetEditorSession::can_undo() const {
  return !state_->dirty() && !state_->undo.empty();
}
bool SnippetEditorSession::can_redo() const {
  return !state_->dirty() && !state_->redo.empty();
}
Result<void> SnippetEditorSession::undo() {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    if (!s.doc || s.undo.empty())
      return Error{ErrorCode::InvalidArgument,
                   "Nothing to undo for this snippet"};
    auto undo = s.undo, redo = s.redo;
    auto next = *s.doc;
    next.content = undo.back();
    undo.pop_back();
    redo.push_back(s.doc->content);
    trim(redo);
    s.persist(std::move(next));
    s.undo.swap(undo);
    s.redo.swap(redo);
    return {};
  });
}
Result<void> SnippetEditorSession::redo() {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    if (!s.doc || s.redo.empty())
      return Error{ErrorCode::InvalidArgument,
                   "Nothing to redo for this snippet"};
    auto undo = s.undo, redo = s.redo;
    auto next = *s.doc;
    next.content = redo.back();
    redo.pop_back();
    undo.push_back(s.doc->content);
    trim(undo);
    s.persist(std::move(next));
    s.undo.swap(undo);
    s.redo.swap(redo);
    return {};
  });
}
Result<std::uint64_t>
SnippetEditorSession::begin_drag(const SnippetItemId &item) {
  return detail::guarded<std::uint64_t>([&]() -> Result<std::uint64_t> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    s.require_saved();
    auto revision = s.item_version(item);
    s.drag = State::Drag{++s.token, item, revision};
    return s.token;
  });
}
Result<void> SnippetEditorSession::finish_drag(std::uint64_t token,
                                               const FolderId &target,
                                               std::size_t index,
                                               bool committed) {
  return detail::guarded<void>([&]() -> Result<void> {
    auto &s = *state_;
    UiBusyScope no_wait(s.store,bool(s.worker));
    if (!s.drag || s.drag->token != token)
      return Error{ErrorCode::InvalidArgument, "Unknown or consumed drag"};
    auto drag = *s.drag;
    s.drag.reset();
    if (!committed)
      return {};
    s.require_saved();
    if (s.item_version(drag.item) != drag.revision)
      return Error{ErrorCode::Busy, "Dragged item changed; reload"};
    if (drag.item.kind == SnippetItemKind::Folder) {
      if (!target.empty())
        detail::invalid("Folders cannot be nested");
      auto catalog_revision = need(data_revision(s.store));
      auto folders = need(list_folders(s.store));
      std::vector<FolderId> order;
      for (auto &f : folders)
        if (f.id != drag.item.value)
          order.push_back(f.id);
      if (index == snippet_append)
        index = order.size();
      if (index > order.size())
        detail::invalid("Folder drop index out of range");
      order.insert(order.begin() + std::ptrdiff_t(index), drag.item.value);
      need(reorder_folders(
          s.store, order,
          SnippetItemVersion{drag.item, drag.revision, catalog_revision}));
    } else
      need(
          move_snippet(s.store, drag.item.value, target, index, drag.revision));
    s.catalog();
    s.select(drag.item, true);
    s.changed();
    return {};
  });
}
Result<void> SnippetEditorSession::bind_folder(std::optional<KeyChord> chord) {
  auto &s = *state_;
  if (!s.folder || !s.callbacks.bind_folder)
    return Error{ErrorCode::Unsupported,
                 "Select a folder; shortcut service unavailable"};
  return s.callbacks.bind_folder(s.folder->id, std::move(chord));
}
std::optional<KeyChord> SnippetEditorSession::folder_chord() const {
  auto &s = *state_;
  return s.folder && s.callbacks.folder_chord
             ? s.callbacks.folder_chord(s.folder->id)
             : std::optional<KeyChord>{};
}
std::string SnippetEditorSession::binding_status() const {
  auto &s = *state_;
  return s.folder && s.callbacks.binding_status
             ? s.callbacks.binding_status(s.folder->id)
             : "";
}
} // namespace cliptiq

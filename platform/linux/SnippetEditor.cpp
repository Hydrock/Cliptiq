#include "../LocaleNative.hpp"
#include "cliptiq/editor.hpp"
#ifdef CLIPTIQ_EDITOR_XML
#include "../SnippetXml.hpp"
#endif
#include <algorithm>
#include <gtk/gtk.h>
#include <thread>
namespace cliptiq {
struct NativeSnippetEditor::State : std::enable_shared_from_this<State> {
  std::thread::id owner = std::this_thread::get_id();
  GtkWidget *window = nullptr, *tree = nullptr, *title = nullptr,
            *body = nullptr, *enabled = nullptr, *shortcut = nullptr,
            *bind = nullptr, *message = nullptr, *undo = nullptr,
            *redo = nullptr, *add_snippet = nullptr;
  GtkTreeStore *rows = nullptr;
  std::shared_ptr<SnippetEditorSession> model;
#ifdef CLIPTIQ_EDITOR_XML
  SnippetXmlTransfer xml;
#endif
  bool updating = false;
  std::uint64_t drag = 0;
  std::optional<SnippetItemId> dragged;
  ~State() {
    if (window)
      gtk_widget_destroy(window);
    if (rows)
      g_object_unref(rows);
  }
  bool transfer_locked() const {
#ifdef CLIPTIQ_EDITOR_XML
    return xml.busy();
#else
    return false;
#endif
  }
  void status(Result<void> r) {
    gtk_label_set_text(
        GTK_LABEL(message),
        r ? ((model->saving()
                  ? std::string(lc("Saving locally..."))
                  : lc("Saved locally. ") + model->binding_status()))
                .c_str()
          : r.error().message.c_str());
  }
#ifdef CLIPTIQ_EDITOR_XML
  void xml_sensitive(bool value) { gtk_widget_set_sensitive(window, value); }
  Result<void> start_xml(const std::string &path, bool importing) {
    if (!window || !model)
      return Error{ErrorCode::InvalidArgument, "Open the snippet editor first"};
    auto result = xml.start(*model, path, importing);
    if (result) {
      xml_sensitive(false);
      status(Error{ErrorCode::Busy, importing ? lc("Importing XML...")
                                              : lc("Exporting XML...")});
    }
    return result;
  }
  bool poll_xml() {
    if (!xml.busy())
      return false;
    auto result = xml.poll(*model);
    if (xml.busy())
      return true;
    xml_sensitive(true);
    if (result) {
      if (xml.importing())
        rebuild();
      else
        detail();
      gtk_label_set_text(GTK_LABEL(message), xml.message().c_str());
    } else {
      detail();
      status(result.error());
    }
    return true;
  }
  void xml_picker(bool importing) {
    auto keep = shared_from_this();
    if (xml.busy() || model->has_unsaved()) {
      status(Error{
          ErrorCode::Busy,
          "Save or Reload the draft and finish the current transfer first"});
      return;
    }
    auto dialog = gtk_file_chooser_dialog_new(
        importing ? lc("Import Clipy XML") : lc("Export Clipy XML"),
        GTK_WINDOW(window),
        importing ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE,
        lc("Cancel"), GTK_RESPONSE_CANCEL,
        importing ? lc("Import") : lc("Export"), GTK_RESPONSE_ACCEPT, nullptr);
    auto filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (!importing) {
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
                                        "snippets.xml");
      gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
                                                     TRUE);
    }
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      auto path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      std::string chosen = path ? path : "";
      g_free(path);
      gtk_widget_destroy(dialog);
      status(start_xml(chosen, importing));
      return;
    }
    gtk_widget_destroy(dialog);
  }
#endif
  void poll() {
    if(!model || !window) return;
#ifdef CLIPTIQ_EDITOR_XML
    if (poll_xml())
      return;
#endif
    auto result=model->poll_saves();
    if(!result) status(result.error());
    else if(result.value()) {status({});selected_label();}
    history();
  }
  void history() {
    gtk_widget_set_sensitive(undo, model->can_undo());
    gtk_widget_set_sensitive(redo, model->can_redo());
  }
  void restore_selection() {
    auto selected = model->selected();
    if (!selected) {
      updating=true;gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));updating=false;return;
    }
    struct Find {
      State *state;
      SnippetItemId id;
      GtkTreePath *path = nullptr;
    } find{this, *selected};
    gtk_tree_model_foreach(
        GTK_TREE_MODEL(rows),
        +[](GtkTreeModel *, GtkTreePath *path, GtkTreeIter *iter,
            gpointer data) -> gboolean {
          auto f = static_cast<Find *>(data);
          auto item = f->state->row(*iter);
          if (item && item->kind == f->id.kind && item->value == f->id.value) {
            f->path = gtk_tree_path_copy(path);
            return TRUE;
          }
          return FALSE;
        },
        &find);
    if (find.path) {
      updating = true;
      gtk_tree_selection_select_path(
          gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), find.path);
      updating = false;
      gtk_tree_path_free(find.path);
    }
  }
  bool confirm_close() {
#ifdef CLIPTIQ_EDITOR_XML
    if (xml.busy()) {
      status(
          Error{ErrorCode::Busy, "XML transfer pending; wait before closing"});
      return false;
    }
#endif
    if(model && model->saving()) {status(Error{ErrorCode::Busy,"Save pending; wait before closing"});return false;}

    if (!model->has_unsaved())
      return true;
    auto dialog = gtk_message_dialog_new(
        GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Discard the unsaved draft? Choose No to keep editing.");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
    bool discard = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(dialog);
    if (!discard)
      return false;
    auto loaded = model->reload();
    status(loaded);
    if (loaded)
      rebuild();
    return bool(loaded);
  }
  void detail() {
    if (transfer_locked())
      return;
    updating = true;
    auto selected = model->selected();
    bool folder = selected && selected->kind == SnippetItemKind::Folder;
    gtk_widget_set_sensitive(title, bool(selected));
    gtk_widget_set_sensitive(enabled, bool(selected));
    gtk_entry_set_text(GTK_ENTRY(title), model->title().c_str());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), model->enabled());
    gtk_widget_set_sensitive(body, selected && !folder);
    auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(body));
    auto text = model->content();
    gtk_text_buffer_set_text(buffer, text.c_str(), int(text.size()));
    gtk_widget_set_sensitive(shortcut, folder);
    gtk_widget_set_sensitive(bind, folder);
    auto chord = model->folder_chord();
    gtk_entry_set_text(GTK_ENTRY(shortcut),
                       chord ? format_chord(*chord).c_str() : "");
    gtk_widget_set_sensitive(add_snippet, bool(selected));
    history();
    updating = false;
  }
  std::optional<SnippetItemId> row(GtkTreeIter iter) {
    gchar *id = nullptr;
    gint kind = 0;
    gtk_tree_model_get(GTK_TREE_MODEL(rows), &iter, 1, &id, 2, &kind, -1);
    SnippetItemId item{SnippetItemKind(kind), id ? id : ""};
    g_free(id);
    if (item.value.empty())
      return {};
    return item;
  }
  std::optional<SnippetItemId> selected_row() {
    GtkTreeIter iter;
    auto selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    if (!gtk_tree_selection_get_selected(selection, nullptr, &iter))
      return {};
    return row(iter);
  }
  void selected_label() {
    GtkTreeIter iter;
    auto sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    if (gtk_tree_selection_get_selected(sel, nullptr, &iter))
      gtk_tree_store_set(rows, &iter, 0,
                         model->title().empty() ? "Untitled"
                                                : model->title().c_str(),
                         3, model->enabled(), -1);
    history();
  }
  void rebuild() {
    updating = true;
    auto selected = model->selected();
    gtk_tree_store_clear(rows);
    std::map<std::string, GtkTreeIter> parents;
    GtkTreePath *active = nullptr;
    for (auto &f : model->folders()) {
      GtkTreeIter it;
      gtk_tree_store_append(rows, &it, nullptr);
      gtk_tree_store_set(
          rows, &it, 0, f.title.empty() ? "Untitled folder" : f.title.c_str(),
          1, f.id.c_str(), 2, int(SnippetItemKind::Folder), 3, f.enabled, -1);
      parents[f.id] = it;
      if (selected && selected->kind == SnippetItemKind::Folder &&
          selected->value == f.id)
        active = gtk_tree_model_get_path(GTK_TREE_MODEL(rows), &it);
    }
    for (auto &s : model->snippets()) {
      auto parent = parents.find(s.folder_id);
      if (parent == parents.end()) {
        GtkTreeIter it;
        gtk_tree_store_append(rows, &it, nullptr);
        gtk_tree_store_set(rows, &it, 0,
                           "Unfiled legacy items: move to a folder", 1, "", 2,
                           int(SnippetItemKind::Folder), 3, FALSE, -1);
        parents[s.folder_id] = it;
        parent = parents.find(s.folder_id);
      }
      GtkTreeIter it;
      gtk_tree_store_append(rows, &it, &parent->second);
      gtk_tree_store_set(
          rows, &it, 0, s.title.empty() ? "Untitled snippet" : s.title.c_str(),
          1, s.id.c_str(), 2, int(SnippetItemKind::Snippet), 3, s.enabled, -1);
      if (selected && selected->kind == SnippetItemKind::Snippet &&
          selected->value == s.id)
        active = gtk_tree_model_get_path(GTK_TREE_MODEL(rows), &it);
    }
    gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
    if (active) {
      gtk_tree_selection_select_path(
          gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), active);
      gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), active, nullptr, FALSE,
                                   0, 0);
      gtk_tree_path_free(active);
    }
    updating = false;
    detail();
  }
  void add(bool folder) {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto keep = shared_from_this();
    auto r = folder ? model->add_folder() : model->add_snippet();
    status(r ? Result<void>{} : Result<void>{r.error()});
    if (r) {
      rebuild();
      gtk_widget_grab_focus(folder ? title : body);
    }
  }
  void remove() {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto keep = shared_from_this();
    if (!model->selected())
      return;
    auto dialog = gtk_message_dialog_new(
        GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        lc("Delete selected item? A folder also deletes its snippets."));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
    bool yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(dialog);
    auto r = model->erase_selected(yes);
    status(r);
    if (r && yes)
      rebuild();
  }
  void undo_action(bool forward) {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto keep = shared_from_this();
    auto r = forward ? model->redo() : model->undo();
    status(r);
    if (r)
      detail();
  }
  void bind_action() {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto text = std::string(gtk_entry_get_text(GTK_ENTRY(shortcut)));
    std::optional<KeyChord> chord;
    if (!text.empty()) {
      auto c = parse_chord(text);
      if (!c) {
        status(c.error());
        return;
      }
      chord = c.value();
    }
    status(model->bind_folder(chord));
  }
  bool drop(int x, int y) {
    if (transfer_locked())
      return false;
    if (!drag || !dragged)
      return false;
    GtkTreePath *path = nullptr;
    GtkTreeViewDropPosition position;
    FolderId folder;
    std::size_t index = snippet_append;
    if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(tree), x, y, &path,
                                          &position)) {
      GtkTreeIter iter;
      gtk_tree_model_get_iter(GTK_TREE_MODEL(rows), &iter, path);
      auto target = row(iter);
      gtk_tree_path_free(path);
      if (!target)
        return false;
      bool after = position == GTK_TREE_VIEW_DROP_AFTER;
      if (dragged->kind == SnippetItemKind::Folder) {
        FolderId dest = target->value;
        if (target->kind == SnippetItemKind::Snippet)
          for (auto &s : model->snippets())
            if (s.id == target->value)
              dest = s.folder_id;
        index = 0;
        std::size_t source = 0;
        for (std::size_t n = 0; n < model->folders().size(); ++n) {
          if (model->folders()[n].id == dest)
            index = n;
          if (model->folders()[n].id == dragged->value)
            source = n;
        }
        if (after)
          ++index;
        if (source < index)
          --index;
      } else if (target->kind == SnippetItemKind::Folder)
        folder = target->value;
      else {
        std::size_t source = snippet_append;
        for (auto &s : model->snippets())
          if (s.id == target->value)
            folder = s.folder_id;
        index = 0;
        for (auto &s : model->snippets())
          if (s.folder_id == folder) {
            if (s.id == dragged->value)
              source = index;
            if (s.id == target->value)
              break;
            ++index;
          }
        if (after)
          ++index;
        if (source < index)
          --index;
      }
    } else if (dragged->kind == SnippetItemKind::Snippet)
      return false;
    auto r = model->finish_drag(drag, folder, index, true);
    drag = 0;
    dragged.reset();
    status(r);
    if (r)
      rebuild();
    return bool(r);
  }
};
NativeSnippetEditor::NativeSnippetEditor()
    : state_(std::make_shared<State>()) {}
NativeSnippetEditor::~NativeSnippetEditor() = default;
Result<void>
NativeSnippetEditor::show(std::shared_ptr<SnippetEditorSession> model) {
  auto s = state_;
  if (s->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires GUI thread"};
#ifdef CLIPTIQ_EDITOR_XML
  if (s->xml.busy())
    return Error{ErrorCode::Busy, "XML transfer pending in existing editor"};
#endif
  if (!model)
    return Error{ErrorCode::InvalidArgument, "No editor session"};
  if (!gtk_init_check(nullptr, nullptr))
    return Error{ErrorCode::Unsupported, "No GTK display"};
  if (s->window && s->model && s->model->has_unsaved())
    return Error{ErrorCode::Busy, "Unsaved draft retained in the existing "
                                  "editor; correct it or Reload to discard"};
  if (s->window) {
    gtk_widget_destroy(s->window);
    s->window = nullptr;
  }
  if (s->rows) {
    g_object_unref(s->rows);
    s->rows = nullptr;
  }
  s->model = std::move(model);
  auto background=s->model->background_saves();if(!background) return background;
  s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(s->window), lc("Cliptiq Snippets"));
  gtk_window_set_default_size(GTK_WINDOW(s->window), 900, 650);
  auto root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(root), 10);
  gtk_container_add(GTK_CONTAINER(s->window), root);
  auto tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(root), tools, FALSE, FALSE, 0);
  auto button = [&](const char *label, const char *name,
                    void (*fn)(GtkButton *, gpointer)) {
    auto b = gtk_button_new_with_label(label);
    gtk_widget_set_name(b, name);
    g_signal_connect(b, "clicked", G_CALLBACK(fn), s.get());
    gtk_box_pack_start(GTK_BOX(tools), b, FALSE, FALSE, 0);
    return b;
  };
  button(
      lc("Add Folder"), "editor-add-folder",
      +[](GtkButton *, gpointer p) { static_cast<State *>(p)->add(true); });
  s->add_snippet = button(
      lc("Add Snippet"), "editor-add-snippet",
      +[](GtkButton *, gpointer p) { static_cast<State *>(p)->add(false); });
  button(
      lc("Delete…"), "editor-delete",
      +[](GtkButton *, gpointer p) { static_cast<State *>(p)->remove(); });
  s->undo = button(
      lc("Undo"), "editor-undo", +[](GtkButton *, gpointer p) {
        static_cast<State *>(p)->undo_action(false);
      });
  s->redo = button(
      lc("Redo"), "editor-redo", +[](GtkButton *, gpointer p) {
        static_cast<State *>(p)->undo_action(true);
      });
  button(
      lc("Reload"), "editor-reload", +[](GtkButton *, gpointer p) {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        auto r = s->model->reload();
        s->status(r);
        if (r)
          s->rebuild();
      });
#ifdef CLIPTIQ_EDITOR_XML
  button(
      lc("Import XML…"), "editor-import", +[](GtkButton *, gpointer p) {
        static_cast<State *>(p)->xml_picker(true);
      });
  button(
      lc("Export XML…"), "editor-export", +[](GtkButton *, gpointer p) {
        static_cast<State *>(p)->xml_picker(false);
      });
#endif
  auto split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start(GTK_BOX(root), split, TRUE, TRUE, 0);
  auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
  s->rows = gtk_tree_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
                               G_TYPE_BOOLEAN);
  s->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s->rows));
  gtk_widget_set_name(s->tree, "editor-tree");
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(s->tree), FALSE);
  auto cell = gtk_cell_renderer_toggle_new();
  gtk_tree_view_insert_column_with_attributes(
      GTK_TREE_VIEW(s->tree), -1, lc("Enabled"), cell, "active", 3, nullptr);
  gtk_tree_view_insert_column_with_attributes(
      GTK_TREE_VIEW(s->tree), -1, "Item", gtk_cell_renderer_text_new(), "text",
      0, nullptr);
  gtk_container_add(GTK_CONTAINER(scroll), s->tree);
  gtk_widget_set_size_request(scroll, 260, -1);
  gtk_paned_pack1(GTK_PANED(split), scroll, FALSE, FALSE);
  auto right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_paned_pack2(GTK_PANED(split), right, TRUE, FALSE);
  s->title = gtk_entry_new();
  gtk_widget_set_name(s->title, "editor-title");
  gtk_entry_set_max_length(GTK_ENTRY(s->title), 4096);
  auto label = gtk_label_new_with_mnemonic("_Title");
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), s->title);
  gtk_box_pack_start(GTK_BOX(right), label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(right), s->title, FALSE, FALSE, 0);
  s->enabled = gtk_check_button_new_with_label(lc("Enabled in menu"));
  gtk_widget_set_name(s->enabled, "editor-enabled");
  gtk_box_pack_start(GTK_BOX(right), s->enabled, FALSE, FALSE, 0);
  auto hotkeys = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(right), hotkeys, FALSE, FALSE, 0);
  s->shortcut = gtk_entry_new();
  gtk_widget_set_name(s->shortcut, "editor-shortcut");
  gtk_entry_set_placeholder_text(GTK_ENTRY(s->shortcut),
                                 lc("Folder shortcut: press chord, then Bind"));
  gtk_box_pack_start(GTK_BOX(hotkeys), s->shortcut, TRUE, TRUE, 0);
  s->bind = gtk_button_new_with_label("Bind / Clear");
  gtk_widget_set_name(s->bind, "editor-bind");
  gtk_box_pack_start(GTK_BOX(hotkeys), s->bind, FALSE, FALSE, 0);
  g_signal_connect(s->bind, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) {
                     static_cast<State *>(p)->bind_action();
                   }),
                   s.get());
  scroll = gtk_scrolled_window_new(nullptr, nullptr);
  s->body = gtk_text_view_new();
  gtk_widget_set_name(s->body, "editor-content");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s->body), GTK_WRAP_WORD_CHAR);
  gtk_container_add(GTK_CONTAINER(scroll), s->body);
  gtk_box_pack_start(GTK_BOX(right), scroll, TRUE, TRUE, 0);
  s->message = gtk_label_new("Changes save locally. Undo history resets when "
                             "you change the selected item.");
  gtk_widget_set_name(s->message, "editor-status");
  gtk_label_set_line_wrap(GTK_LABEL(s->message), TRUE);
  gtk_label_set_xalign(GTK_LABEL(s->message), 0);
  gtk_box_pack_end(GTK_BOX(root), s->message, FALSE, FALSE, 0);
  g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->tree)),
                   "changed", G_CALLBACK(+[](GtkTreeSelection *, gpointer p) {
                     auto s = static_cast<State *>(p);
                     auto keep = s->shared_from_this();
                     if (s->updating || s->transfer_locked())
                       return;
                     if (s->transfer_locked()) {
                       s->restore_selection();
                       return;
                     }
                     auto r = s->model->select(s->selected_row());
                     s->status(r);
                     if (r)
                       s->detail();
                     else
                       s->restore_selection();
                   }),
                   s.get());
  g_signal_connect(
      cell, "toggled",
      G_CALLBACK(+[](GtkCellRendererToggle *, gchar *path, gpointer p) {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        if (s->transfer_locked())
          return;
        GtkTreeIter iter;
        if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(s->rows), &iter,
                                                 path))
          return;
        auto item = s->row(iter);
        if (!item)
          return;
        auto r = s->model->select(item);
        if (r)
          r = s->model->enable(!s->model->enabled());
        s->status(r);
        if (r)
          s->rebuild();
      }),
      s.get());
  g_signal_connect(
      s->title, "changed", G_CALLBACK(+[](GtkEditable *, gpointer p) {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        if (s->updating || s->transfer_locked())
          return;
        auto r = s->model->queue_title(gtk_entry_get_text(GTK_ENTRY(s->title)));
        s->status(r);
        if (r)
          s->selected_label();
      }),
      s.get());
  g_signal_connect(s->enabled, "toggled",
                   G_CALLBACK(+[](GtkToggleButton *b, gpointer p) {
                     auto s = static_cast<State *>(p);
                     auto keep = s->shared_from_this();
                     if (s->updating || s->transfer_locked())
                       return;
                     auto r = s->model->enable(gtk_toggle_button_get_active(b));
                     s->status(r);
                     if (r)
                       s->selected_label();
                     else {
                       s->updating = true;
                       gtk_toggle_button_set_active(b, s->model->enabled());
                       s->updating = false;
                     }
                   }),
                   s.get());
  g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->body)), "changed",
                   G_CALLBACK(+[](GtkTextBuffer *b, gpointer p) {
                     auto s = static_cast<State *>(p);
                     auto keep = s->shared_from_this();
                     if (s->updating || s->transfer_locked())
                       return;
                     GtkTextIter start;
                     GtkTextIter end;
                     gtk_text_buffer_get_bounds(b, &start, &end);
                     auto text =
                         gtk_text_buffer_get_text(b, &start, &end, FALSE);
                     auto r = s->model->queue_content(text);
                     g_free(text);
                     s->status(r);
                     s->history();
                   }),
                   s.get());
  g_signal_connect(
      s->body, "key-press-event",
      G_CALLBACK(+[](GtkWidget *, GdkEventKey *e, gpointer p) -> gboolean {
        if (!(e->state & GDK_CONTROL_MASK))
          return FALSE;
        auto key = gdk_keyval_to_lower(e->keyval);
        if (key != GDK_KEY_z && key != GDK_KEY_y)
          return FALSE;
        static_cast<State *>(p)->undo_action(key == GDK_KEY_y ||
                                             (e->state & GDK_SHIFT_MASK));
        return TRUE;
      }),
      s.get());
  g_signal_connect(
      s->tree, "key-press-event",
      G_CALLBACK(+[](GtkWidget *, GdkEventKey *e, gpointer p) -> gboolean {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        if (e->keyval == GDK_KEY_Delete) {
          s->remove();
          return TRUE;
        }
        if (e->keyval == GDK_KEY_F2) {
          gtk_widget_grab_focus(s->title);
          return TRUE;
        }
        return FALSE;
      }),
      s.get());
  g_signal_connect(
      s->shortcut, "key-press-event",
      G_CALLBACK(+[](GtkWidget *w, GdkEventKey *e, gpointer) -> gboolean {
        unsigned mods =
            ((e->state & GDK_CONTROL_MASK) ? unsigned(Control) : 0u) |
            ((e->state & GDK_SHIFT_MASK) ? unsigned(Shift) : 0u) |
            ((e->state & GDK_MOD1_MASK) ? unsigned(Alt) : 0u) |
            ((e->state & GDK_SUPER_MASK) ? unsigned(Super) : 0u);
        if (!mods)
          return FALSE;
        auto value = gdk_keyval_to_unicode(gdk_keyval_to_upper(e->keyval));
        std::string key;
        if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
          key = char(value);
        else if (e->keyval >= GDK_KEY_F1 && e->keyval <= GDK_KEY_F11)
          key = "F" + std::to_string(e->keyval - GDK_KEY_F1 + 1);
        if (key.empty())
          return FALSE;
        auto chord = format_chord({mods, key});
        if (!parse_chord(chord))
          return FALSE;
        gtk_entry_set_text(GTK_ENTRY(w), chord.c_str());
        return TRUE;
      }),
      nullptr);
  GtkTargetEntry target{
      const_cast<gchar *>("application/x-cliptiq-snippet-move"),
      GTK_TARGET_SAME_WIDGET, 1};
  gtk_drag_source_set(s->tree, GDK_BUTTON1_MASK, &target, 1, GDK_ACTION_MOVE);
  gtk_drag_dest_set(s->tree, GTK_DEST_DEFAULT_ALL, &target, 1, GDK_ACTION_MOVE);
  g_signal_connect(s->tree, "drag-begin",
                   G_CALLBACK(+[](GtkWidget *, GdkDragContext *, gpointer p) {
                     auto s = static_cast<State *>(p);
                     auto keep = s->shared_from_this();
                     if (s->transfer_locked())
                       return;
                     s->dragged = s->selected_row();
                     if (s->dragged) {
                       auto r = s->model->begin_drag(*s->dragged);
                       if (r)
                         s->drag = r.value();
                       else
                         s->status(r.error());
                     }
                   }),
                   s.get());
  g_signal_connect(
      s->tree, "drag-data-get",
      G_CALLBACK(+[](GtkWidget *, GdkDragContext *, GtkSelectionData *d, guint,
                     guint, gpointer p) {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        auto token = std::to_string(s->drag);
        gtk_selection_data_set(d, gtk_selection_data_get_target(d), 8,
                               reinterpret_cast<const guchar *>(token.data()),
                               int(token.size()));
        g_signal_stop_emission_by_name(s->tree, "drag-data-get");
      }),
      s.get());
  g_signal_connect(
      s->tree, "drag-data-received",
      G_CALLBACK(+[](GtkWidget *, GdkDragContext *ctx, gint x, gint y,
                     GtkSelectionData *d, guint, guint time, gpointer p) {
        auto s = static_cast<State *>(p);
        auto keep = s->shared_from_this();
        auto expected = std::to_string(s->drag);
        int length = gtk_selection_data_get_length(d);
        bool valid = s->drag && gtk_drag_get_source_widget(ctx) == s->tree &&
                     length == int(expected.size()) &&
                     std::equal(expected.begin(), expected.end(),
                                gtk_selection_data_get_data(d));
        bool done = valid && s->drop(x, y);
        g_signal_stop_emission_by_name(s->tree, "drag-data-received");
        gtk_drag_finish(ctx, done, FALSE, time);
      }),
      s.get());
  g_signal_connect(s->tree, "drag-end",
                   G_CALLBACK(+[](GtkWidget *, GdkDragContext *, gpointer p) {
                     auto s = static_cast<State *>(p);
                     auto keep = s->shared_from_this();
                     if (s->drag)
                       s->model->finish_drag(s->drag, "", 0, false);
                     s->drag = 0;
                     s->dragged.reset();
                   }),
                   s.get());
  g_signal_connect(
      s->window, "delete-event",
      G_CALLBACK(+[](GtkWidget *w, GdkEvent *, gpointer data) -> gboolean {
        auto s = static_cast<State *>(data);
        auto keep = s->shared_from_this();
        if (s->confirm_close())
          gtk_widget_hide(w);
        return TRUE;
      }),
      s.get());
  s->rebuild();
  gtk_widget_show_all(s->window);
  return {};
}
Result<void> NativeSnippetEditor::process_events() {
  if (state_->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires GUI thread"};
  for (unsigned n = 0; n < 64 && gtk_events_pending(); ++n)
    gtk_main_iteration_do(FALSE);
  auto keep=state_;keep->poll();
  return {};
}
Result<bool> NativeSnippetEditor::request_close() {
  auto s = state_;
  if (!s->window)
    return true;
  if (!s->confirm_close())
    return false;
  gtk_widget_hide(s->window);
  return true;
}
std::uintptr_t NativeSnippetEditor::native_handle() const {
  return reinterpret_cast<std::uintptr_t>(state_->window);
}
bool NativeSnippetEditor::record_chord(const KeyChord &chord) {
  auto s = state_;
  if (s->transfer_locked())
    return false;
  if (!s->window || !gtk_widget_get_visible(s->window) ||
      !gtk_window_is_active(GTK_WINDOW(s->window)) ||
      gtk_window_get_focus(GTK_WINDOW(s->window)) != s->shortcut)
    return false;
  auto selected = s->model->selected();
  if (!selected || selected->kind != SnippetItemKind::Folder)
    return false;
  gtk_entry_set_text(GTK_ENTRY(s->shortcut), format_chord(chord).c_str());
  return true;
}
Result<void> NativeSnippetEditor::import_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (s->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, true);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
Result<void> NativeSnippetEditor::export_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (s->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, false);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
bool NativeSnippetEditor::xml_transfer_busy() const {
#ifdef CLIPTIQ_EDITOR_XML
  return state_->xml.busy();
#else
  return false;
#endif
}
} // namespace cliptiq

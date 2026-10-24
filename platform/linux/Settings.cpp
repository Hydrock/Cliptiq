#include "cliptiq/settings.hpp"
#include <gtk/gtk.h>
#include <thread>
namespace cliptiq {
struct NativeSettings::State : std::enable_shared_from_this<State> {
  std::thread::id owner = std::this_thread::get_id();
  GtkWidget *window = nullptr, *message = nullptr;
  Settings settings;
  std::function<Result<void>(Settings)> apply;
  std::function<Result<void>(const HotkeyMap &)> authorize;
  std::map<std::string, GtkWidget *> controls;
  ~State() {
    if (window)
      gtk_widget_destroy(window);
  }
  void save() {
    auto keep = shared_from_this();
    std::map<std::string, std::string> values;
    for (auto &p : controls) {
      if (GTK_IS_TOGGLE_BUTTON(p.second))
        values[p.first] =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p.second)) ? "1"
                                                                      : "0";
      else if (GTK_IS_TEXT_VIEW(p.second)) {
        auto buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(p.second));
        GtkTextIter begin, end;
        gtk_text_buffer_get_bounds(buffer, &begin, &end);
        auto text = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
        values[p.first] = text;
        g_free(text);
      } else
        values[p.first] = gtk_entry_get_text(GTK_ENTRY(p.second));
    }
    auto draft = edit_settings(settings, values);
    if (!draft) {
      gtk_label_set_text(GTK_LABEL(message), draft.error().message.c_str());
      return;
    }
    auto result = apply(draft.value());
    if (!result) {
      gtk_label_set_text(GTK_LABEL(message), result.error().message.c_str());
      return;
    }
    settings = std::move(draft.value());
    ++settings.revision;
    gtk_label_set_text(GTK_LABEL(message),
                       "Saved. Unavailable functions remain disabled.");
  }
};
NativeSettings::NativeSettings() : state_(std::make_shared<State>()) {}
NativeSettings::~NativeSettings() = default;
Result<void>
NativeSettings::show(Settings settings, const Capabilities &caps,
                     std::function<Result<void>(Settings)> callback,
                     std::function<Result<void>(const HotkeyMap &)> authorize) {
  auto s = state_;
  if (s->owner != std::this_thread::get_id() ||
      !gtk_init_check(nullptr, nullptr))
    return Error{ErrorCode::Unsupported,
                 "Settings require owner GTK display thread"};
  auto valid = validate_settings(settings);
  if (!valid)
    return valid;
  if (s->window)
    gtk_widget_destroy(s->window);
  s->controls.clear();
  s->settings = std::move(settings);
  s->apply = std::move(callback);
  s->authorize = std::move(authorize);
  s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(s->window), "Cliptiq Settings");
  gtk_window_set_default_size(GTK_WINDOW(s->window), 760, 680);
  auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add(GTK_CONTAINER(s->window), box);
  auto tabs = gtk_notebook_new();
  gtk_widget_set_name(tabs, "settings-tabs");
  gtk_box_pack_start(GTK_BOX(box), tabs, TRUE, TRUE, 0);
  const char *names[] = {"General",   "Menu",    "Types", "Excluded",
                         "Shortcuts", "Updates", "Beta",  "Donate"};
  GtkWidget *grids[8]{};
  int rows[8]{};
  for (unsigned i = 0; i < 8; ++i) {
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    grids[i] = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grids[i]), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grids[i]), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grids[i]), 12);
    gtk_container_add(GTK_CONTAINER(scroll), grids[i]);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), scroll,
                             gtk_label_new(names[i]));
  }
  for (auto &f : settings_fields(s->settings, caps)) {
    auto label = gtk_label_new(f.label.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    auto control = f.boolean ? gtk_check_button_new()
                             : (f.key == "excluded" ? gtk_text_view_new()
                                                    : gtk_entry_new());
    gtk_widget_set_name(control, f.key.c_str());
    if (f.boolean)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control), f.value == "1");
    else if (GTK_IS_TEXT_VIEW(control)) {
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(control)),
                               f.value.c_str(), -1);
      gtk_widget_set_size_request(control, 340, 100);
    } else {
      gtk_entry_set_text(GTK_ENTRY(control), f.value.c_str());
      gtk_entry_set_max_length(GTK_ENTRY(control), 65536);
    }
    gtk_widget_set_sensitive(control, f.enabled);
    gtk_widget_set_tooltip_text(
        control, (f.reason.empty() ? f.label : f.reason).c_str());
    if (f.shortcut)
      g_signal_connect(
          control, "key-press-event",
          G_CALLBACK(+[](GtkWidget *w, GdkEventKey *e, gpointer) -> gboolean {
            unsigned modifiers = ((e->state & GDK_CONTROL_MASK) ? 1u : 0u) |
                                 ((e->state & GDK_SHIFT_MASK) ? 2u : 0u) |
                                 ((e->state & GDK_MOD1_MASK) ? 4u : 0u) |
                                 ((e->state & GDK_SUPER_MASK) ? 8u : 0u);
            if (!modifiers)
              return FALSE;
            auto c = gdk_keyval_to_unicode(gdk_keyval_to_upper(e->keyval));
            std::string key;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
              key = char(c);
            else if (e->keyval >= GDK_KEY_F1 && e->keyval <= GDK_KEY_F11)
              key = "F" + std::to_string(e->keyval - GDK_KEY_F1 + 1);
            if (key.empty())
              return FALSE;
            auto value = format_chord({modifiers, key});
            if (parse_chord(value)) {
              gtk_entry_set_text(GTK_ENTRY(w), value.c_str());
              return TRUE;
            }
            return FALSE;
          }),
          nullptr);
    auto group = unsigned(f.group);
    int row = rows[group]++;
    gtk_grid_attach(GTK_GRID(grids[group]), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grids[group]), control, 1, row, 1, 1);
    gtk_widget_set_hexpand(control, TRUE);
    if (!f.reason.empty()) {
      auto reason = gtk_label_new(f.reason.c_str());
      gtk_label_set_line_wrap(GTK_LABEL(reason), TRUE);
      gtk_label_set_xalign(GTK_LABEL(reason), 0);
      gtk_grid_attach(GTK_GRID(grids[group]), reason, 0, rows[group]++, 2, 1);
    }
    s->controls[f.key] = control;
  }
  if (caps.platform == CapturePlatform::Wayland && s->authorize) {
    auto authorize =
        gtk_button_new_with_label("Authorize portal global shortcuts");
    g_signal_connect(
        authorize, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
          auto s = static_cast<State *>(data);
          auto before = s->settings.revision;
          s->save();
          if (s->settings.revision == before)
            return;
          auto result = s->authorize(s->settings.hotkeys);
          gtk_label_set_text(GTK_LABEL(s->message),
                             result ? "Authorization requested; the portal "
                                      "chooses actual triggers."
                                    : result.error().message.c_str());
        }),
        s.get());
    gtk_grid_attach(GTK_GRID(grids[4]), authorize, 0, rows[4]++, 2, 1);
  }
  s->message =
      gtk_label_new("Shortcut fields record Ctrl/Alt/Super chords; Backspace "
                    "clears optional bindings. Conflicts keep prior settings.");
  gtk_label_set_line_wrap(GTK_LABEL(s->message), TRUE);
  gtk_box_pack_start(GTK_BOX(box), s->message, FALSE, FALSE, 0);
  auto apply = gtk_button_new_with_label("Apply");
  gtk_widget_set_name(apply, "apply");
  g_signal_connect(apply, "clicked",
                   G_CALLBACK(+[](GtkButton *, gpointer data) {
                     static_cast<State *>(data)->save();
                   }),
                   s.get());
  gtk_box_pack_end(GTK_BOX(box), apply, FALSE, FALSE, 0);
  g_signal_connect(
      s->window, "delete-event",
      G_CALLBACK(+[](GtkWidget *w, GdkEvent *, gpointer) -> gboolean {
        gtk_widget_hide(w);
        return TRUE;
      }),
      nullptr);
  gtk_widget_show_all(s->window);
  return {};
}
Result<void> NativeSettings::process_events() {
  if (state_->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Settings require GUI thread"};
  for (int n = 0; n < 64 && gtk_events_pending(); ++n)
    gtk_main_iteration_do(FALSE);
  return {};
}
std::uintptr_t NativeSettings::native_handle() const {
  return reinterpret_cast<std::uintptr_t>(state_->window);
}
bool NativeSettings::confirm_clear() const {
  auto dialog = gtk_message_dialog_new(
      state_->window ? GTK_WINDOW(state_->window) : nullptr, GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
      "Clear Cliptiq history? Snippets and current system clipboard remain.");
  bool confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
  gtk_widget_destroy(dialog);
  return confirmed;
}
void NativeSettings::status(const std::string &value) {
  if (state_->message)
    gtk_label_set_text(GTK_LABEL(state_->message), value.c_str());
}
bool NativeSettings::record_chord(const KeyChord &chord) {
  auto s = state_;
  if (!s->window || !gtk_widget_get_visible(s->window))
    return false;
  auto focus = gtk_window_get_focus(GTK_WINDOW(s->window));
  for (auto &p : s->controls)
    if (p.second == focus && p.first.rfind("hotkey:", 0) == 0) {
      gtk_entry_set_text(GTK_ENTRY(focus), format_chord(chord).c_str());
      return true;
    }
  return false;
}
} // namespace cliptiq

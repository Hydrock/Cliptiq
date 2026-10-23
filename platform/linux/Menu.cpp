#include "cliptiq/native_menu.hpp"
#include <chrono>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <map>
#include <thread>
namespace cliptiq {
namespace {
constexpr const char *menu_path = "/MenuBar";
constexpr const char *item_path = "/StatusNotifierItem";
constexpr const char *xml = R"XML(<node>
<interface name="org.kde.StatusNotifierItem"><method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method><method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method><method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method><method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method><property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/><property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/><property name="IconName" type="s" access="read"/><property name="IconPixmap" type="a(iiay)" access="read"/><property name="ItemIsMenu" type="b" access="read"/><property name="Menu" type="o" access="read"/><property name="WindowId" type="i" access="read"/><property name="ToolTip" type="(sa(iiay)ss)" access="read"/><signal name="NewIcon"/><signal name="NewStatus"><arg type="s"/></signal></interface>
<interface name="com.canonical.dbusmenu"><method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/><arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method><method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method><method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method><method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method><method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method><method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method><method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method><property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/><property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/><signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal></interface></node>)XML";
std::string caption(const MenuItem &i) {
  std::string s;
  if (i.number)
    s = std::to_string(*i.number) + ". ";
  s += i.label;
  return s;
}
GdkPixbuf *pixbuf(const MenuImage &i) {
  auto bytes = g_bytes_new(i.rgba.data(), i.rgba.size());
  auto p = gdk_pixbuf_new_from_bytes(bytes, GDK_COLORSPACE_RGB, TRUE, 8,
                                     i.width, i.height, i.width * 4);
  g_bytes_unref(bytes);
  return p;
}
} // namespace
struct NativeMenu::State : std::enable_shared_from_this<State> {
  NativeMenuOptions options;
  std::function<void(MenuCommand)> callback;
  MenuPreviewProvider provider;
  std::thread::id owner = std::this_thread::get_id();
  GtkWidget *window = nullptr, *button = nullptr, *menu = nullptr;
  MenuModel model;
  InvocationContext invocation;
  std::uint64_t generation = 0;
  unsigned serial = 0;
  int next_id = 100;
  bool closing = false;
  NativeMenuStatus status;
  struct Node {
    MenuItem item;
    std::vector<int> children;
  };
  std::map<int, Node> nodes;
  std::vector<int> roots;
  GDBusConnection *bus = nullptr;
  GDBusNodeInfo *info = nullptr;
  guint item_registration = 0, menu_registration = 0, watch = 0;
  GCancellable *cancel = nullptr;
  State(NativeMenuOptions o, std::function<void(MenuCommand)> c,
        MenuPreviewProvider p)
      : options(o), callback(std::move(c)), provider(std::move(p)) {}
  ~State() {
    closing = true;
    if (cancel)
      g_cancellable_cancel(cancel);
    if (watch)
      g_bus_unwatch_name(watch);
    if (bus && item_registration)
      g_dbus_connection_unregister_object(bus, item_registration);
    if (bus && menu_registration)
      g_dbus_connection_unregister_object(bus, menu_registration);
    if (info)
      g_dbus_node_info_unref(info);
    if (bus)
      g_object_unref(bus);
    if (cancel)
      g_object_unref(cancel);
    if (window)
      gtk_widget_destroy(window);
    if (menu) {
      gtk_widget_destroy(menu);
      g_object_unref(menu);
    }
  }
  bool thread() const { return owner == std::this_thread::get_id(); }
  void invoke(int id) {
    auto keep = shared_from_this();
    auto it = nodes.find(id);
    if (it == nodes.end() || !it->second.item.enabled ||
        it->second.item.separator || !it->second.children.empty() ||
        it->second.item.action == MenuAction::None)
      return;
    auto cb = callback;
    MenuCommand c{it->second.item.action, it->second.item.id, model.revision};
    c.invocation = invocation;
    if (cb)
      cb(std::move(c));
  }
  struct Token {
    std::weak_ptr<State> owner;
    int id;
    std::uint64_t generation;
  };
  static void clicked(GtkMenuItem *, gpointer data) {
    auto token = static_cast<Token *>(data);
    if (auto s = token->owner.lock())
      if (s->generation == token->generation)
        s->invoke(token->id);
  }
  GtkWidget *build(const std::vector<MenuItem> &items, std::vector<int> &ids) {
    auto result = gtk_menu_new();
    g_signal_connect(result, "map", G_CALLBACK(+[](GtkWidget *w, gpointer) {
                       gtk_menu_shell_select_first(GTK_MENU_SHELL(w), TRUE);
                     }),
                     nullptr);
    g_signal_connect(result, "key-press-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEventKey *event,
                                    gpointer data) -> gboolean {
                       auto s = static_cast<State *>(data)->shared_from_this();
                       if (event->state &
                           (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))
                         return FALSE;
                       unsigned key = event->keyval;
                       if (key >= GDK_KEY_KP_0 && key <= GDK_KEY_KP_9)
                         key = '0' + key - GDK_KEY_KP_0;
                       if (key < '0' || key > '9')
                         return FALSE;
                       for (const auto &node : s->nodes)
                         if (node.second.item.enabled &&
                             node.second.item.shortcut ==
                                 std::string(1, char(key))) {
                           auto id = node.first;
                           gtk_menu_popdown(GTK_MENU(s->menu));
                           s->invoke(id);
                           return TRUE;
                         }
                       return FALSE;
                     }),
                     this);
    for (const auto &i : items) {
      int id = next_id++;
      ids.push_back(id);
      nodes[id].item = i;
      GtkWidget *w;
      if (i.separator)
        w = gtk_separator_menu_item_new();
      else {
        w = gtk_menu_item_new();
        auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        auto label = gtk_label_new(caption(i).c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
        auto image = menu_preview(i, options, provider);
        if (image) {
          auto p = pixbuf(*image);
          auto icon = gtk_image_new_from_pixbuf(p);
          g_object_unref(p);
          gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
        } else if (options.icons && i.action == MenuAction::SelectHistory) {
          auto icon = gtk_image_new_from_icon_name(
              i.kind == CanonicalKind::Image ? "image-x-generic"
                                             : "text-x-generic",
              GTK_ICON_SIZE_MENU);
          gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
        }
        gtk_container_add(GTK_CONTAINER(w), box);
        gtk_widget_set_tooltip_text(w, i.tooltip.empty() ? nullptr
                                                         : i.tooltip.c_str());
        atk_object_set_name(gtk_widget_get_accessible(w), caption(i).c_str());
        auto token = new Token{weak_from_this(), id, generation};
        g_signal_connect_data(
            w, "activate", G_CALLBACK(clicked), token,
            [](gpointer p, GClosure *) { delete static_cast<Token *>(p); },
            GConnectFlags(0));
        if (!i.children.empty()) {
          auto sub = build(i.children, nodes[id].children);
          gtk_menu_item_set_submenu(GTK_MENU_ITEM(w), sub);
        }
      }
      gtk_widget_set_sensitive(w, i.enabled);
      gtk_menu_shell_append(GTK_MENU_SHELL(result), w);
    }
    gtk_widget_show_all(result);
    return result;
  }
  void present() {
    gtk_widget_show(window);
    gtk_window_present(GTK_WINDOW(window));
    status.launcher_visible = true;
  }
  void popup(const InvocationContext &ctx = {}) {
    invocation = ctx;
    present();
    if (!menu)
      return;
    auto event = gtk_get_current_event();
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(window)) && !event)
      return;
    gtk_menu_popup_at_widget(GTK_MENU(menu), button, GDK_GRAVITY_SOUTH_WEST,
                             GDK_GRAVITY_NORTH_WEST, event);
    gtk_menu_shell_select_first(GTK_MENU_SHELL(menu), TRUE);
    if (event)
      gdk_event_free(event);
  }
  std::string dbus_caption(const MenuItem &i) {
    auto text = caption(i);
    std::string out;
    for (char c : text) {
      out += c;
      if (c == '_')
        out += '_';
    }
    return out;
  }
  GVariant *props(int id) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    auto found = nodes.find(id);
    if (found == nodes.end())
      return g_variant_builder_end(&b);
    auto &i = found->second.item;
    g_variant_builder_add(&b, "{sv}", "label",
                          g_variant_new_string(dbus_caption(i).c_str()));
    g_variant_builder_add(&b, "{sv}", "enabled",
                          g_variant_new_boolean(i.enabled));
    g_variant_builder_add(&b, "{sv}", "visible", g_variant_new_boolean(TRUE));
    if (i.separator)
      g_variant_builder_add(&b, "{sv}", "type",
                            g_variant_new_string("separator"));
    if (!found->second.children.empty())
      g_variant_builder_add(&b, "{sv}", "children-display",
                            g_variant_new_string("submenu"));
    auto image = menu_preview(i, options, provider);
    if (image) {
      auto p = pixbuf(*image);
      gchar *data = nullptr;
      gsize n = 0;
      if (gdk_pixbuf_save_to_buffer(p, &data, &n, "png", nullptr, nullptr)) {
        g_variant_builder_add(
            &b, "{sv}", "icon-data",
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, n, 1));
        g_free(data);
      }
      g_object_unref(p);
    }
    return g_variant_builder_end(&b);
  }
  GVariant *layout(int id, int depth) {
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    auto found = nodes.find(id);
    const auto &list = id == 0                ? roots
                       : found == nodes.end() ? empty
                                              : found->second.children;
    if (depth != 0)
      for (auto child : list)
        g_variant_builder_add(&children, "v",
                              layout(child, depth < 0 ? -1 : depth - 1));
    return g_variant_new("(i@a{sv}@av)", id, props(id),
                         g_variant_builder_end(&children));
  }
  const std::vector<int> empty;
  GVariant *icons() {
    GVariantBuilder a;
    g_variant_builder_init(&a, G_VARIANT_TYPE("a(iiay)"));
    unsigned char bytes[16 * 16 * 4];
    for (unsigned i = 0; i < sizeof(bytes); i += 4) {
      bool stroke = (i / 4 % 16 == 3 || i / 4 % 16 == 12 || i / 4 / 16 == 3 ||
                     i / 4 / 16 == 12);
      auto v = options.tray_style == TrayStyle::Reversed ? 240 : 32;
      bytes[i] = stroke ? 255 : 0;
      bytes[i + 1] = bytes[i + 2] = bytes[i + 3] = v;
    }
    g_variant_builder_add(&a, "(ii@ay)", 16, 16,
                          g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes,
                                                    sizeof(bytes), 1));
    return g_variant_builder_end(&a);
  }
  static GVariant *get(GDBusConnection *, const gchar *, const gchar *,
                       const gchar *iface, const gchar *name, GError **,
                       gpointer data) {
    auto s = static_cast<State *>(data);
    if (std::string(iface) == "com.canonical.dbusmenu") {
      if (std::string(name) == "Version")
        return g_variant_new_uint32(3);
      if (std::string(name) == "TextDirection")
        return g_variant_new_string("ltr");
      if (std::string(name) == "Status")
        return g_variant_new_string("normal");
      if (std::string(name) == "IconThemePath")
        return g_variant_new_strv(nullptr, 0);
    } else {
      std::string n = name;
      if (n == "Category")
        return g_variant_new_string("ApplicationStatus");
      if (n == "Id")
        return g_variant_new_string("cliptiq");
      if (n == "Title")
        return g_variant_new_string("Cliptiq");
      if (n == "Status")
        return g_variant_new_string(
            s->options.tray_style == TrayStyle::Hidden ? "Passive" : "Active");
      if (n == "IconName")
        return g_variant_new_string("");
      if (n == "IconPixmap")
        return s->icons();
      if (n == "Menu")
        return g_variant_new_object_path(menu_path);
      if (n == "ItemIsMenu")
        return g_variant_new_boolean(TRUE);
      if (n == "WindowId")
        return g_variant_new_int32(0);
      if (n == "ToolTip")
        return g_variant_new("(s@a(iiay)ss)", "", s->icons(), "Cliptiq",
                             "Clipboard history");
    }
    return nullptr;
  }
  static void method(GDBusConnection *, const gchar *, const gchar *,
                     const gchar *iface, const gchar *name, GVariant *params,
                     GDBusMethodInvocation *inv, gpointer data) {
    auto s = static_cast<State *>(data)->shared_from_this();
    try {
      std::string n = name;
      if (std::string(iface) == "org.kde.StatusNotifierItem") {
        if (n != "Scroll")
          s->popup();
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
      }
      if (n == "GetLayout") {
        gint parent, depth;
        GVariant *requested;
        g_variant_get(params, "(ii@as)", &parent, &depth, &requested);
        g_variant_unref(requested);
        if (parent != 0 && !s->nodes.count(parent)) {
          g_dbus_method_invocation_return_dbus_error(
              inv, "com.canonical.dbusmenu.Error.InvalidMenuId",
              "Unknown menu ID");
          return;
        }
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(u@(ia{sv}av))", s->serial,
                               s->layout(parent, depth)));
        return;
      }
      if (n == "GetGroupProperties") {
        GVariant *ids = g_variant_get_child_value(params, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, ids);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
        gint id;
        while (g_variant_iter_next(&iter, "i", &id))
          if (s->nodes.count(id))
            g_variant_builder_add(&b, "(i@a{sv})", id, s->props(id));
        g_variant_unref(ids);
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&b)));
        return;
      }
      if (n == "GetProperty") {
        gint id;
        const char *property;
        g_variant_get(params, "(i&s)", &id, &property);
        auto properties = g_variant_ref_sink(s->props(id));
        auto value = g_variant_lookup_value(properties, property, nullptr);
        g_variant_unref(properties);
        if (!value) {
          g_dbus_method_invocation_return_dbus_error(
              inv, "com.canonical.dbusmenu.Error.UnknownProperty",
              "Unknown property");
          return;
        }
        g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", value));
        g_variant_unref(value);
        return;
      }
      if (n == "Event") {
        gint id;
        const char *event;
        GVariant *payload;
        guint time;
        g_variant_get(params, "(i&s@vu)", &id, &event, &payload, &time);
        g_variant_unref(payload);
        g_dbus_method_invocation_return_value(inv, nullptr);
        if (std::string(event) == "clicked")
          s->invoke(id);
        return;
      }
      if (n == "EventGroup") {
        GVariant *events = g_variant_get_child_value(params, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, events);
        GVariant *entry;
        std::vector<int> clicked;
        while ((entry = g_variant_iter_next_value(&iter))) {
          gint id;
          const char *event;
          GVariant *payload;
          guint time;
          g_variant_get(entry, "(i&s@vu)", &id, &event, &payload, &time);
          if (std::string(event) == "clicked")
            clicked.push_back(id);
          g_variant_unref(payload);
          g_variant_unref(entry);
        }
        g_variant_unref(events);
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(@ai)", g_variant_builder_end(&b)));
        for (auto id : clicked)
          s->invoke(id);
        return;
      }
      if (n == "AboutToShow") {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
        return;
      }
      if (n == "AboutToShowGroup") {
        GVariantBuilder a, b;
        g_variant_builder_init(&a, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(@ai@ai)", g_variant_builder_end(&a),
                               g_variant_builder_end(&b)));
        return;
      }
      g_dbus_method_invocation_return_dbus_error(
          inv, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method");
    } catch (...) {
      g_dbus_method_invocation_return_dbus_error(
          inv, "org.freedesktop.DBus.Error.Failed", "Menu request failed");
    }
  }
  void register_item() {
    if (!bus || options.tray_style == TrayStyle::Hidden)
      return;
    g_dbus_connection_call(
        bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        g_variant_new("(s)", g_dbus_connection_get_unique_name(bus)), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 1000, cancel,
        [](GObject *object, GAsyncResult *result, gpointer data) {
          std::unique_ptr<std::weak_ptr<State>> weak(
              static_cast<std::weak_ptr<State> *>(data));
          GError *e = nullptr;
          auto reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
                                                     result, &e);
          if (auto s = weak->lock()) {
            s->status.tray_available =
                reply && s->options.tray_style != TrayStyle::Hidden;
            s->status.tray_reason = reply ? "StatusNotifierWatcher registered"
                                          : "Watcher registration unavailable; "
                                            "launcher remains visible";
          }
          if (reply)
            g_variant_unref(reply);
          if (e)
            g_error_free(e);
        },
        new std::weak_ptr<State>(weak_from_this()));
  }
  void setup_bus() {
    GError *e = nullptr;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &e);
    if (!bus) {
      status.tray_reason = "Session bus unavailable; use launcher";
      if (e)
        g_error_free(e);
      return;
    }
    cancel = g_cancellable_new();
    info = g_dbus_node_info_new_for_xml(xml, &e);
    if (!info) {
      if (e)
        g_error_free(e);
      status.tray_reason = "Tray introspection failed";
      return;
    }
    static const GDBusInterfaceVTable vtable = {
        method, get, nullptr, {nullptr}};
    item_registration = g_dbus_connection_register_object(
        bus, item_path, info->interfaces[0], &vtable, this, nullptr, &e);
    if (e) {
      g_error_free(e);
      e = nullptr;
    }
    menu_registration = g_dbus_connection_register_object(
        bus, menu_path, info->interfaces[1], &vtable, this, nullptr, &e);
    if (e)
      g_error_free(e);
    watch = g_bus_watch_name_on_connection(
        bus, "org.kde.StatusNotifierWatcher", G_BUS_NAME_WATCHER_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, gpointer data) {
          static_cast<State *>(data)->register_item();
        },
        [](GDBusConnection *, const gchar *, gpointer data) {
          auto s = static_cast<State *>(data);
          s->status.tray_available = false;
          s->status.tray_reason = "No StatusNotifierWatcher; use launcher";
        },
        this, nullptr);
  }
};
NativeMenu::NativeMenu(NativeMenuOptions o, std::function<void(MenuCommand)> c,
                       MenuPreviewProvider p)
    : state_(std::make_shared<State>(o, std::move(c), std::move(p))) {}
NativeMenu::~NativeMenu() = default;
Result<void> NativeMenu::initialize() {
  auto s = state_;
  if (!s->thread() || s->window)
    return Error{ErrorCode::InvalidArgument, "Invalid GTK menu initialize"};
  if (!gtk_init_check(nullptr, nullptr))
    return Error{ErrorCode::ReadOnly, "GTK display unavailable"};
  s->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(s->window), "Cliptiq");
  gtk_window_set_default_size(GTK_WINDOW(s->window), 280, 100);
  s->button = gtk_menu_button_new();
  gtk_button_set_label(GTK_BUTTON(s->button), "Clipboard History");
  gtk_container_add(GTK_CONTAINER(s->window), s->button);
  g_signal_connect(
      s->window, "delete-event",
      G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
        auto s = static_cast<State *>(data)->shared_from_this();
        auto cb = s->callback;
        if (cb)
          cb({MenuAction::Quit, "", s->model.revision});
        return TRUE;
      }),
      s.get());
  gtk_widget_show_all(s->window);
  s->status.launcher_visible = true;
  s->setup_bus();
  return update({});
}
Result<void> NativeMenu::update(MenuModel model) {
  auto valid = validate_menu_model(model);
  if (!valid)
    return valid;
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "GTK menu not initialized"};
  ++s->generation;
  ++s->serial;
  if (s->menu) {
    gtk_menu_popdown(GTK_MENU(s->menu));
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(s->button), nullptr);
    gtk_widget_destroy(s->menu);
    g_object_unref(s->menu);
    s->menu = nullptr;
  }
  s->model = std::move(model);
  s->nodes.clear();
  s->roots.clear();
  s->menu = s->build(s->model.items, s->roots);
  g_object_ref_sink(s->menu);
  gtk_menu_button_set_popup(GTK_MENU_BUTTON(s->button), s->menu);
  if (s->bus && s->menu_registration)
    g_dbus_connection_emit_signal(s->bus, nullptr, menu_path,
                                  "com.canonical.dbusmenu", "LayoutUpdated",
                                  g_variant_new("(ui)", s->serial, 0), nullptr);
  return {};
}
Result<void> NativeMenu::show(const InvocationContext &ctx) {
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "GTK menu not initialized"};
  s->popup(ctx);
  return {};
}
Result<void> NativeMenu::set_tray_style(TrayStyle style) {
  auto s = state_;
  if (!s->thread() || static_cast<unsigned>(style) > 2)
    return Error{ErrorCode::InvalidArgument, "Invalid tray style"};
  s->options.tray_style = style;
  if (style == TrayStyle::Hidden) {
    s->status.tray_available = false;
  } else
    s->register_item();
  if (s->bus) {
    g_dbus_connection_emit_signal(
        s->bus, nullptr, item_path, "org.kde.StatusNotifierItem", "NewStatus",
        g_variant_new("(s)", style == TrayStyle::Hidden ? "Passive" : "Active"),
        nullptr);
    g_dbus_connection_emit_signal(s->bus, nullptr, item_path,
                                  "org.kde.StatusNotifierItem", "NewIcon",
                                  nullptr, nullptr);
  }
  return {};
}
Result<void> NativeMenu::process_events(unsigned wait) {
  auto s = state_;
  if (!s->thread())
    return Error{ErrorCode::InvalidArgument, "GTK events require owner thread"};
  for (unsigned i = 0; i < 64 && g_main_context_pending(nullptr); ++i)
    g_main_context_iteration(nullptr, FALSE);
  if (wait)
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(wait, 10u)));
  return {};
}
NativeMenuStatus NativeMenu::status() const { return state_->status; }
NativeMenuHandles NativeMenu::native_handles() const {
  return {reinterpret_cast<uintptr_t>(state_->window),
          reinterpret_cast<uintptr_t>(state_->menu)};
}
} // namespace cliptiq

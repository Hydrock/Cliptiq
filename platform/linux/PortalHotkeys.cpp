#include "PortalHotkeys.hpp"
#include <atomic>
#include <cstring>
#include <gio/gio.h>
namespace cliptiq {
namespace {
constexpr const char *service = "org.freedesktop.portal.Desktop";
constexpr const char *path = "/org/freedesktop/portal/desktop";
constexpr const char *iface = "org.freedesktop.portal.GlobalShortcuts";
std::string hint(const KeyChord &c) {
  std::string out;
  for (auto p : {std::pair<unsigned, const char *>{Control, "CTRL+"},
                 {Shift, "SHIFT+"},
                 {Alt, "ALT+"},
                 {Super, "LOGO+"}})
    if (c.modifiers & p.first)
      out += p.second;
  auto key = c.key;
  if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z')
    key[0] += 32;
  return out + key;
}
} // namespace
struct PortalHotkeys::State : std::enable_shared_from_this<State> {
  GDBusConnection *bus = nullptr;
  GCancellable *cancel = g_cancellable_new();
  guint response = 0, activated = 0, closed = 0, changed = 0, owner_watch = 0;
  std::function<void(ActionId, InvocationContext)> callback;
  HotkeyMap candidate;
  std::map<ActionId, BindingStatus> bound;
  std::string current, pending_session, request;
  unsigned serial = 0, stage = 0;
  ~State() {
    if (bus) {
      close_request();
      close(pending_session);
      close(current);
      for (auto id : {response, activated, closed, changed, owner_watch})
        if (id)
          g_dbus_connection_signal_unsubscribe(bus, id);
      g_object_unref(bus);
    }
    g_cancellable_cancel(cancel);
    g_object_unref(cancel);
  }
  void close(const std::string &s) {
    if (!bus || s.empty())
      return;
    g_dbus_connection_call(bus, service, s.c_str(),
                           "org.freedesktop.portal.Session", "Close", nullptr,
                           nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
                           nullptr, nullptr);
  }
  void close_request() {
    if (bus && !request.empty())
      g_dbus_connection_call(bus, service, request.c_str(),
                             "org.freedesktop.portal.Request", "Close", nullptr,
                             nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
                             nullptr, nullptr);
  }
  void failed(const std::string &reason) {
    close_request();
    close(pending_session);
    pending_session.clear();
    request.clear();
    stage = 0;
    if (current.empty())
      for (auto &p : candidate)
        if (p.second)
          bound[p.first] = {false, reason, {}};
  }
  static void release_weak(gpointer p) {
    delete static_cast<std::weak_ptr<State> *>(p);
  }
  struct Call {
    std::weak_ptr<State> state;
    std::string request;
  };
  void call(const char *method, GVariant *params) {
    g_dbus_connection_call(
        bus, service, path, iface, method, params, G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, cancel,
        [](GObject *object, GAsyncResult *result, gpointer data) {
          std::unique_ptr<Call> call(static_cast<Call *>(data));
          GError *error = nullptr;
          auto reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),
                                                     result, &error);
          if (auto s = call->state.lock()) {
            if (s->request != call->request) {
              if (reply)
                g_variant_unref(reply);
              if (error)
                g_error_free(error);
              return;
            }
            if (!reply)
              s->failed(error ? error->message : "Portal request failed");
            else {
              const char *handle = nullptr;
              g_variant_get(reply, "(&o)", &handle);
              if (s->stage && s->request != handle)
                s->failed(
                    "Unexpected portal request path; use desktop binding");
            }
          }
          if (reply)
            g_variant_unref(reply);
          if (error)
            g_error_free(error);
        },
        new Call{weak_from_this(), request});
  }
  std::string token() {
    static std::atomic<std::uint64_t> counter{0};
    return "cliptiq_hotkeys_" + std::to_string(++counter);
  }
  void expected(const std::string &token) {
    std::string sender = g_dbus_connection_get_unique_name(bus);
    sender.erase(0, 1);
    for (auto &c : sender)
      if (c == '.')
        c = '_';
    request = std::string(path) + "/request/" + sender + "/" + token;
  }
  void bind() {
    auto handle = token();
    expected(handle);
    stage = 2;
    GVariantBuilder keys;
    g_variant_builder_init(&keys, G_VARIANT_TYPE("a(sa{sv})"));
    for (auto &p : candidate)
      if (p.second) {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(
            &options, "{sv}", "description",
            g_variant_new_string(("Cliptiq " + p.first).c_str()));
        auto preferred = hint(*p.second);
        g_variant_builder_add(&options, "{sv}", "preferred_trigger",
                              g_variant_new_string(preferred.c_str()));
        g_variant_builder_add(&keys, "(s@a{sv})", p.first.c_str(),
                              g_variant_builder_end(&options));
      }
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token",
                          g_variant_new_string(handle.c_str()));
    call("BindShortcuts",
         g_variant_new("(o@a(sa{sv})s@a{sv})", pending_session.c_str(),
                       g_variant_builder_end(&keys), "",
                       g_variant_builder_end(&options)));
  }
  void on_response(const char *object, GVariant *parameters) {
    if (!stage || request != object)
      return;
    guint code;
    GVariant *results = nullptr;
    g_variant_get(parameters, "(u@a{sv})", &code, &results);
    std::unique_ptr<GVariant, decltype(&g_variant_unref)> owned(
        results, g_variant_unref);
    if (code) {
      failed("Portal authorization cancelled/denied; prior session retained");
      return;
    }
    if (stage == 1) {
      const char *session = nullptr;
      if (!g_variant_lookup(results, "session_handle", "&s", &session) ||
          !g_variant_is_object_path(session) ||
          std::string(session).rfind(std::string(path) + "/session/", 0) != 0) {
        failed("Invalid portal session handle");
        return;
      }
      pending_session = session;
      bind();
      return;
    }
    auto shortcuts = g_variant_lookup_value(results, "shortcuts",
                                            G_VARIANT_TYPE("a(sa{sv})"));
    if (!shortcuts) {
      failed("Portal returned no confirmed shortcuts");
      return;
    }
    if (g_variant_n_children(shortcuts) > candidate.size()) {
      g_variant_unref(shortcuts);
      failed("Too many portal bindings");
      return;
    }
    std::map<ActionId, BindingStatus> confirmed;
    GVariantIter iter;
    g_variant_iter_init(&iter, shortcuts);
    const char *id;
    GVariant *properties;
    while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &properties)) {
      const char *trigger = nullptr;
      auto wanted = strlen(id) <= 1031 ? candidate.find(id) : candidate.end();
      if (wanted != candidate.end() && wanted->second &&
          g_variant_lookup(properties, "trigger_description", "&s", &trigger) &&
          strlen(trigger) <= 1024)
        confirmed[id] = {
            true,
            "User-controlled portal binding (preferred chord is only a hint)",
            trigger};
      g_variant_unref(properties);
    }
    g_variant_unref(shortcuts);
    if (!confirmed.count("settings")) {
      failed("Portal did not grant recovery Settings action; prior session "
             "retained");
      return;
    }
    for (auto &p : candidate)
      if (p.second && !confirmed.count(p.first))
        confirmed[p.first] = {
            false, "Action not authorized by portal; use desktop binding", {}};
    close(current);
    current = std::move(pending_session);
    pending_session.clear();
    bound = std::move(confirmed);
    stage = 0;
    request.clear();
  }
  void initialize() {
    GError *error = nullptr;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, cancel, &error);
    if (error)
      g_error_free(error);
    if (!bus)
      return;
    response = g_dbus_connection_signal_subscribe(
        bus, service, "org.freedesktop.portal.Request", "Response", nullptr,
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *object, const gchar *,
           const gchar *, GVariant *params, gpointer data) {
          if (auto s = static_cast<std::weak_ptr<State> *>(data)->lock())
            s->on_response(object, params);
        },
        new std::weak_ptr<State>(weak_from_this()), release_weak);
    activated = g_dbus_connection_signal_subscribe(
        bus, service, iface, "Activated", path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
           const gchar *, GVariant *params, gpointer data) {
          if (auto s = static_cast<std::weak_ptr<State> *>(data)->lock()) {
            const char *session, *id;
            guint64 stamp;
            GVariant *options;
            g_variant_get(params, "(&o&st@a{sv})", &session, &id, &stamp,
                          &options);
            auto found = s->bound.find(id);
            if (s->current == session && found != s->bound.end() &&
                found->second.registered) {
              InvocationContext ctx;
              ctx.keyboard = true;
              const char *token = nullptr;
              if (g_variant_lookup(options, "activation_token", "&s", &token) &&
                  strlen(token) <= 4096)
                ctx.activation_token = token;
              auto cb = s->callback;
              if (cb)
                cb(id, std::move(ctx));
            }
            g_variant_unref(options);
          }
        },
        new std::weak_ptr<State>(weak_from_this()), release_weak);
    changed = g_dbus_connection_signal_subscribe(
        bus, service, iface, "ShortcutsChanged", path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
           const gchar *, GVariant *params, gpointer data) {
          if (auto s = static_cast<std::weak_ptr<State> *>(data)->lock()) {
            const char *session;
            GVariant *keys;
            g_variant_get(params, "(&o@a(sa{sv}))", &session, &keys);
            if (s->current == session &&
                g_variant_n_children(keys) <= s->bound.size()) {
              auto next = s->bound;
              for (auto &p : next)
                p.second = {false,
                            "Action removed by portal; reauthorize or use "
                            "desktop binding",
                            {}};
              GVariantIter iter;
              g_variant_iter_init(&iter, keys);
              const char *id;
              GVariant *props;
              while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &props)) {
                const char *trigger = nullptr;
                auto found = strlen(id) <= 1031 ? next.find(id) : next.end();
                if (found != next.end() &&
                    g_variant_lookup(props, "trigger_description", "&s",
                                     &trigger) &&
                    strlen(trigger) <= 1024)
                  found->second = {true, "User-controlled portal binding",
                                   trigger};
                g_variant_unref(props);
              }
              s->bound.swap(next);
            }
            g_variant_unref(keys);
          }
        },
        new std::weak_ptr<State>(weak_from_this()), release_weak);
    closed = g_dbus_connection_signal_subscribe(
        bus, service, "org.freedesktop.portal.Session", "Closed", nullptr,
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *object, const gchar *,
           const gchar *, GVariant *, gpointer data) {
          if (auto s = static_cast<std::weak_ptr<State> *>(data)->lock())
            if (s->current == object) {
              s->current.clear();
              for (auto &p : s->bound)
                p.second = {
                    false,
                    "Portal session closed; reauthorize or use desktop binding",
                    {}};
            }
        },
        new std::weak_ptr<State>(weak_from_this()), release_weak);
    owner_watch = g_dbus_connection_signal_subscribe(
        bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
        "/org/freedesktop/DBus", service, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
           const gchar *, GVariant *params, gpointer data) {
          if (auto s = static_cast<std::weak_ptr<State> *>(data)->lock()) {
            const char *name, *old, *now;
            g_variant_get(params, "(&s&s&s)", &name, &old, &now);
            if (*old) {
              s->failed("Portal service restarted");
              s->current.clear();
              for (auto &p : s->bound)
                p.second = {false, "Portal owner changed; reauthorize", {}};
            }
          }
        },
        new std::weak_ptr<State>(weak_from_this()), release_weak);
  }
};
PortalHotkeys::PortalHotkeys(
    std::function<void(ActionId, InvocationContext)> callback)
    : state_(std::make_shared<State>()) {
  state_->callback = std::move(callback);
  state_->initialize();
}
PortalHotkeys::~PortalHotkeys() = default;
void PortalHotkeys::reconcile_folders(const std::map<FolderId, bool> &catalog) {
  auto &s = *state_;
  auto invalid = [&](const ActionId &action) {
    if (action.rfind("folder:", 0) != 0)
      return false;
    auto found = catalog.find(action.substr(7));
    return found == catalog.end() || !found->second;
  };
  bool cancel_pending = false, close_current = false;
  if (s.stage)
    for (const auto &p : s.candidate)
      if (p.second && invalid(p.first))
        cancel_pending = true;
  for (const auto &p : s.bound)
    if (p.second.registered && invalid(p.first))
      close_current = true;
  if (cancel_pending)
    s.failed("Folder changed; retry portal authorization");
  if (close_current) {
    s.close(s.current);
    s.current.clear();
    for (auto &p : s.bound)
      p.second = {false,
                  "Folder disabled/deleted: portal session closed; reauthorize "
                  "remaining shortcuts in Settings",
                  {}};
  }
}
Result<void> PortalHotkeys::configure(const HotkeyMap &keys) {
  auto &s = *state_;
  auto valid = validate_hotkeys(keys);
  if (!valid)
    return valid;
  if (!s.bus)
    return Error{ErrorCode::Unsupported, "No session bus; use desktop binding"};
  if (s.stage)
    return Error{ErrorCode::Busy, "Portal authorization already pending"};
  for (auto &p : keys)
    if (p.second && p.second->meaning != KeyMeaning::Logical)
      return Error{ErrorCode::Unsupported,
                   "Portal preferred triggers use logical keys"};
  s.candidate = keys;
  auto token = s.token(), session = s.token();
  s.expected(token);
  s.stage = 1;
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(token.c_str()));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string(session.c_str()));
  s.call("CreateSession",
         g_variant_new("(@a{sv})", g_variant_builder_end(&options)));
  return {};
}
std::map<ActionId, BindingStatus> PortalHotkeys::statuses() const {
  return state_->bound;
}
} // namespace cliptiq

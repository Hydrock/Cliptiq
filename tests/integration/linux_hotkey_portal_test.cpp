#include "cliptiq/settings.hpp"
#include <atomic>
#include <gio/gio.h>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
struct Peer {
  std::atomic<bool> ready{false}, stop{false}, deny{false}, emit{false},
      wrong{false}, close_session{false}, changed{false};
  std::atomic<unsigned> binds{0};
  std::thread thread;
  std::string error;
  GDBusConnection *bus = nullptr;
  std::string session, granted;
  std::vector<guint> objects;
  GDBusNodeInfo *info = nullptr;
  void response(GDBusMethodInvocation *inv, const std::string &token,
                guint result, GVariant *dict) {
    std::string sender = g_dbus_method_invocation_get_sender(inv);
    sender.erase(0, 1);
    for (auto &c : sender)
      if (c == '.')
        c = '_';
    auto request =
        "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    g_dbus_connection_emit_signal(
        bus, nullptr, request.c_str(), "org.freedesktop.portal.Request",
        "Response", g_variant_new("(u@a{sv})", result, dict), nullptr);
    g_dbus_method_invocation_return_value(
        inv, g_variant_new("(o)", request.c_str()));
  }
  static void method(GDBusConnection *, const gchar *, const gchar *object,
                     const gchar *, const gchar *method, GVariant *params,
                     GDBusMethodInvocation *inv, gpointer data) {
    auto p = static_cast<Peer *>(data);
    if (std::string(method) == "Close") {
      g_dbus_method_invocation_return_value(inv, nullptr);
      return;
    }
    GVariant *opts = nullptr;
    if (std::string(method) == "CreateSession") {
      g_variant_get(params, "(@a{sv})", &opts);
      const char *token, *session_token;
      g_variant_lookup(opts, "handle_token", "&s", &token);
      g_variant_lookup(opts, "session_handle_token", "&s", &session_token);
      p->session =
          std::string("/org/freedesktop/portal/desktop/session/test/") +
          session_token;
      static const GDBusInterfaceVTable table{
          Peer::method, nullptr, nullptr, {nullptr}};
      p->objects.push_back(g_dbus_connection_register_object(
          p->bus, p->session.c_str(), p->info->interfaces[1], &table, p,
          nullptr, nullptr));
      GVariantBuilder dict;
      g_variant_builder_init(&dict, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(&dict, "{sv}", "session_handle",
                            g_variant_new_string(p->session.c_str()));
      p->response(inv, token, 0, g_variant_builder_end(&dict));
      g_variant_unref(opts);
      return;
    }
    const char *session, *parent;
    GVariant *keys;
    g_variant_get(params, "(&o@a(sa{sv})&s@a{sv})", &session, &keys, &parent,
                  &opts);
    const char *token;
    g_variant_lookup(opts, "handle_token", "&s", &token);
    ++p->binds;
    GVariantBuilder dict;
    g_variant_builder_init(&dict, G_VARIANT_TYPE_VARDICT);
    GVariantBuilder shortcuts;
    g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
    for (auto id : {"main", "settings"}) {
      GVariantBuilder props;
      g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(
          &props, "{sv}", "trigger_description",
          g_variant_new_string(
              std::string(id) == "main" ? "Alt+F7" : "Portal chosen settings"));
      g_variant_builder_add(&shortcuts, "(s@a{sv})", id,
                            g_variant_builder_end(&props));
    }
    g_variant_builder_add(&dict, "{sv}", "shortcuts",
                          g_variant_builder_end(&shortcuts));
    if (!p->deny)
      p->granted = session;
    p->response(inv, token, p->deny ? 1 : 0, g_variant_builder_end(&dict));
    g_variant_unref(keys);
    g_variant_unref(opts);
    (void)object;
  }
  Peer() {
    thread = std::thread([this] {
      auto context = g_main_context_new();
      g_main_context_push_thread_default(context);
      GError *err = nullptr;
      bus = g_dbus_connection_new_for_address_sync(
          g_getenv("DBUS_SESSION_BUS_ADDRESS"),
          GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                               G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
          nullptr, nullptr, &err);
      if (!bus) {
        error = err ? err->message : "No bus";
        if (err)
          g_error_free(err);
        ready = true;
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        return;
      }
      info = g_dbus_node_info_new_for_xml(
          "<node><interface "
          "name='org.freedesktop.portal.GlobalShortcuts'><method "
          "name='CreateSession'><arg type='a{sv}' direction='in'/><arg "
          "type='o' direction='out'/></method><method "
          "name='BindShortcuts'><arg type='o' direction='in'/><arg "
          "type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/><arg "
          "type='a{sv}' direction='in'/><arg type='o' "
          "direction='out'/></method></interface><interface "
          "name='org.freedesktop.portal.Session'><method "
          "name='Close'/></interface></node>",
          nullptr);
      static const GDBusInterfaceVTable table{
          Peer::method, nullptr, nullptr, {nullptr}};
      objects.push_back(g_dbus_connection_register_object(
          bus, "/org/freedesktop/portal/desktop", info->interfaces[0], &table,
          this, nullptr, nullptr));
      auto reply = g_dbus_connection_call_sync(
          bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
          "org.freedesktop.DBus", "RequestName",
          g_variant_new("(su)", "org.freedesktop.portal.Desktop", 4u), nullptr,
          G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
      if (reply)
        g_variant_unref(reply);
      ready = true;
      while (!stop) {
        g_main_context_iteration(context, FALSE);
        if (emit.exchange(false)) {
          auto object = wrong ? "/org/freedesktop/portal/desktop/session/wrong"
                              : granted.c_str();
          GVariantBuilder opts;
          g_variant_builder_init(&opts, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add(&opts, "{sv}", "activation_token",
                                g_variant_new_string("portal-token"));
          g_dbus_connection_emit_signal(
              bus, nullptr, "/org/freedesktop/portal/desktop",
              "org.freedesktop.portal.GlobalShortcuts", "Activated",
              g_variant_new("(ost@a{sv})", object, "main", guint64(1),
                            g_variant_builder_end(&opts)),
              nullptr);
        }
        if (changed.exchange(false)) {
          GVariantBuilder keys;
          g_variant_builder_init(&keys, G_VARIANT_TYPE("a(sa{sv})"));
          for (auto id : {"main", "settings"}) {
            GVariantBuilder props;
            g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(
                &props, "{sv}", "trigger_description",
                g_variant_new_string(std::string(id) == "main"
                                         ? "Ctrl+F8"
                                         : "Settings changed"));
            g_variant_builder_add(&keys, "(s@a{sv})", id,
                                  g_variant_builder_end(&props));
          }
          g_dbus_connection_emit_signal(
              bus, nullptr, "/org/freedesktop/portal/desktop",
              "org.freedesktop.portal.GlobalShortcuts", "ShortcutsChanged",
              g_variant_new("(o@a(sa{sv}))", granted.c_str(),
                            g_variant_builder_end(&keys)),
              nullptr);
        }
        if (close_session.exchange(false))
          g_dbus_connection_emit_signal(bus, nullptr, granted.c_str(),
                                        "org.freedesktop.portal.Session",
                                        "Closed", nullptr, nullptr);
        g_usleep(1000);
      }
      for (auto id : objects)
        g_dbus_connection_unregister_object(bus, id);
      g_dbus_node_info_unref(info);
      g_object_unref(bus);
      g_main_context_pop_thread_default(context);
      g_main_context_unref(context);
    });
    for (int n = 0; n < 3000 && !ready; ++n)
      g_usleep(1000);
  }
  ~Peer() {
    stop = true;
    if (thread.joinable())
      thread.join();
  }
};
int main() {
  try {
    setenv("WAYLAND_DISPLAY", "private-portal-fixture", 1);
    Peer peer;
    CHECK(peer.ready && peer.error.empty());
    unsigned calls = 0;
    InvocationContext received;
    NativeHotkeys native([&](ActionId action, InvocationContext ctx) {
      CHECK(action == "main");
      ++calls;
      received = std::move(ctx);
    });
    auto keys = default_settings(CapturePlatform::Wayland).hotkeys;
    CHECK(native.configure_portal(keys));
    auto pump = [&](auto predicate) {
      for (int n = 0; n < 1000 && !predicate(); ++n) {
        CHECK(native.dispatch());
        g_usleep(3000);
      }
      CHECK(predicate());
    };
    pump([&] {
      auto s = native.portal_statuses();
      return s.count("main") && s.at("main").registered;
    });
    CHECK(native.portal_statuses().at("main").trigger == "Alt+F7");
    peer.emit = true;
    pump([&] { return calls == 1; });
    CHECK(received.activation_token == "portal-token");
    peer.deny = true;
    CHECK(native.configure_portal(keys));
    pump([&] { return peer.binds >= 2; });
    for (int n = 0; n < 30; ++n) {
      CHECK(native.dispatch());
      g_usleep(3000);
    }
    CHECK(native.portal_statuses().at("main").registered);
    peer.wrong = true;
    peer.emit = true;
    for (int n = 0; n < 30; ++n) {
      CHECK(native.dispatch());
      g_usleep(3000);
    }
    CHECK(calls == 1);
    peer.changed = true;
    pump([&] {
      return native.portal_statuses().at("main").trigger == "Ctrl+F8";
    });
    peer.close_session = true;
    pump([&] { return !native.portal_statuses().at("main").registered; });
    std::cout << "PASS "
                 "independent_portal_grant_actual_trigger_activation_denial_"
                 "rollback_and_closed_session\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL portal_hotkeys: " << e.what() << '\n';
    return 1;
  }
}

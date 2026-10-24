#include "cliptiq/hotkeys.hpp"
#include "PortalHotkeys.hpp"
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <gio/gio.h>
#include <thread>
#include <xcb/xcb.h>
namespace cliptiq {
struct NativeHotkeys::State {
  std::thread::id owner = std::this_thread::get_id();
  std::function<void(ActionId, InvocationContext)> callback;
  std::unique_ptr<PortalHotkeys> portal;
  xcb_connection_t *connection = nullptr;
  xcb_window_t root = 0;
  Display *display = nullptr;
  unsigned serial = 0;
  bool wayland = false;
  struct Entry {
    ActionId action;
    KeyChord chord;
    unsigned code, mods;
    std::vector<unsigned> locks;
  };
  std::map<unsigned, Entry> entries;
  State(std::function<void(ActionId, InvocationContext)> cb)
      : callback(std::move(cb)) {
    wayland = getenv("WAYLAND_DISPLAY") ||
              (getenv("XDG_SESSION_TYPE") &&
               std::string(getenv("XDG_SESSION_TYPE")) == "wayland");
    if (!wayland) {
      connection = xcb_connect(nullptr, nullptr);
      if (!xcb_connection_has_error(connection))
        root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
      display = XOpenDisplay(nullptr);
    } else
      portal = std::make_unique<PortalHotkeys>(callback);
  }
  ~State() {
    if (connection)
      xcb_disconnect(connection);
    if (display)
      XCloseDisplay(display);
  }
  unsigned resolve(const KeyChord &c) {
    if (c.meaning == KeyMeaning::Physical) {
      auto setup = xcb_get_setup(connection);
      return c.physical_code >= setup->min_keycode &&
                     c.physical_code <= setup->max_keycode
                 ? c.physical_code
                 : 0;
    }
    auto keys = XkbGetKeyboard(display, XkbAllComponentsMask, XkbUseCoreKbd);
    if (!keys)
      return 0;
    auto symbol =
        c.key.size() == 1
            ? KeySym(c.key[0] >= 'A' && c.key[0] <= 'Z' ? c.key[0] + 32
                                                        : c.key[0])
            : XK_F1 + unsigned(std::stoi(c.key.substr(1)) - 1);
    unsigned code = 0;
    for (unsigned i = keys->min_key_code; i <= keys->max_key_code && !code; ++i)
      for (unsigned group = 0; group < XkbKeyNumGroups(keys, i); ++group)
        if (XkbKeySymEntry(keys, i, 0, group) == symbol) {
          code = i;
          break;
        }
    XkbFreeKeyboard(keys, 0, True);
    return code;
  }
  std::vector<unsigned> lock_masks() {
    unsigned ignored = LockMask;
    auto mapping = XGetModifierMapping(display);
    if (mapping) {
      for (int modifier = 0; modifier < 8; ++modifier)
        for (int j = 0; j < mapping->max_keypermod; ++j) {
          auto code =
              mapping->modifiermap[modifier * mapping->max_keypermod + j];
          if (!code)
            continue;
          auto key = XkbKeycodeToKeysym(display, code, 0, 0);
          if (key == XK_Num_Lock || key == XK_Scroll_Lock)
            ignored |= 1u << modifier;
        }
      XFreeModifiermap(mapping);
    }
    std::vector<unsigned> masks{0};
    for (unsigned bit = 1; bit <= 128; bit <<= 1)
      if (ignored & bit) {
        auto size = masks.size();
        for (std::size_t i = 0; i < size; ++i)
          masks.push_back(masks[i] | bit);
      }
    return masks;
  }
};
NativeHotkeys::NativeHotkeys(
    std::function<void(ActionId, InvocationContext)> cb)
    : state_(std::make_unique<State>(std::move(cb))) {}
NativeHotkeys::~NativeHotkeys() = default;
Capability NativeHotkeys::capability() const {
  auto &s = *state_;
  if (s.wayland)
    return {CapabilityStatus::Unavailable,
            "Wayland requires user-authorized GlobalShortcuts portal; desktop "
            "binding: cliptiq_menu_host --show-main/--show-settings"};
  if (!s.root || !s.display)
    return {CapabilityStatus::Unavailable,
            "X11 unavailable; use visible launcher/desktop binding"};
  return {CapabilityStatus::Limited,
          "X11 checked passive grabs, lock variants; Latin logical keymap or "
          "explicit physical code required"};
}
Result<unsigned> NativeHotkeys::acquire(const ActionId &a, const KeyChord &c) {
  auto &s = *state_;
  if (s.owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Hotkeys require GUI thread"};
  if (capability().status == CapabilityStatus::Unavailable)
    return Error{ErrorCode::Unsupported, capability().reason};
  unsigned code = s.resolve(c);
  if (!code)
    return Error{ErrorCode::Unsupported,
                 "Key is unavailable in XKB; choose an explicit physical code "
                 "or Latin mapping"};
  unsigned mods = ((c.modifiers & Control) ? ControlMask : 0) |
                  ((c.modifiers & Shift) ? ShiftMask : 0) |
                  ((c.modifiers & Alt) ? Mod1Mask : 0) |
                  ((c.modifiers & Super) ? Mod4Mask : 0);
  for (auto &p : s.entries)
    if (p.second.code == code && p.second.mods == mods)
      return Error{ErrorCode::Busy, "Native shortcut already registered"};
  auto locks = s.lock_masks();
  std::vector<unsigned> acquired;
  for (auto mask : locks) {
    auto request =
        xcb_grab_key_checked(s.connection, 0, s.root, mods | mask, code,
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    auto error = xcb_request_check(s.connection, request);
    if (error) {
      free(error);
      for (auto held : acquired)
        xcb_ungrab_key(s.connection, code, s.root, mods | held);
      xcb_flush(s.connection);
      return Error{
          ErrorCode::Busy,
          "Shortcut is owned by another X11 client; previous binding retained"};
    }
    acquired.push_back(mask);
  }
  unsigned id = ++s.serial;
  s.entries[id] = {a, c, code, mods, std::move(locks)};
  return id;
}
void NativeHotkeys::release(unsigned id) {
  auto &s = *state_;
  auto found = s.entries.find(id);
  if (found == s.entries.end())
    return;
  for (auto lock : found->second.locks)
    xcb_ungrab_key(s.connection, found->second.code, s.root,
                   found->second.mods | lock);
  xcb_flush(s.connection);
  s.entries.erase(found);
}
Result<void> NativeHotkeys::dispatch() {
  auto &s = *state_;
  if (s.owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Hotkeys require GUI thread"};
  if (s.wayland) {
    for (unsigned n = 0; n < 64 && g_main_context_pending(nullptr); ++n)
      g_main_context_iteration(nullptr, FALSE);
    return {};
  }
  if (!s.connection)
    return {};
  // Refresh logical registrations proactively: an unchanged preference may
  // resolve to a different native key after XKB remapping. Keep stable registry
  // IDs.
  std::vector<unsigned> changed;
  for (const auto &p : s.entries)
    if (p.second.chord.meaning == KeyMeaning::Logical &&
        s.resolve(p.second.chord) != p.second.code)
      changed.push_back(p.first);
  for (auto id : changed) {
    auto &old = s.entries.at(id);
    auto replacement = acquire(old.action, old.chord);
    if (!replacement)
      return Error{replacement.error().code,
                   "Keyboard remap could not be registered; choose a free "
                   "shortcut in Settings: " +
                       replacement.error().message};
    std::swap(s.entries.at(id), s.entries.at(replacement.value()));
    release(replacement.value());
  }
  for (unsigned n = 0; n < 128; ++n) {
    auto event = xcb_poll_for_event(s.connection);
    if (!event)
      break;
    std::unique_ptr<xcb_generic_event_t, decltype(&free)> owned(event, free);
    if ((event->response_type & 127) != XCB_KEY_PRESS)
      continue;
    auto key = reinterpret_cast<xcb_key_press_event_t *>(event);
    for (auto &p : s.entries) {
      auto &e = p.second;
      unsigned ignored = 0;
      for (auto lock : e.locks)
        ignored |= lock;
      if (e.code == key->detail && (key->state & ~ignored) == e.mods) {
        if (e.chord.meaning == KeyMeaning::Logical &&
            s.resolve(e.chord) != e.code)
          return Error{
              ErrorCode::Busy,
              "Keyboard mapping changed during dispatch; retry action"};
        auto callback = s.callback;
        auto action = e.action;
        InvocationContext ctx;
        ctx.keyboard = true;
        if (callback)
          callback(std::move(action), std::move(ctx));
        break;
      }
    }
  }
  return {};
}
Result<void> NativeHotkeys::configure_portal(const HotkeyMap &keys) {
  if (!state_->portal)
    return Error{ErrorCode::Unsupported, "Portal mode is only used on Wayland"};
  return state_->portal->configure(keys);
}
std::map<ActionId, BindingStatus> NativeHotkeys::portal_statuses() const {
  return state_->portal ? state_->portal->statuses()
                        : std::map<ActionId, BindingStatus>{};
}
} // namespace cliptiq

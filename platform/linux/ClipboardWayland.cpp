#include "Clipboard.hpp"
#include "Transfer.hpp"
#include "ext-data-control-v1-client-header.h"
#include "wlr-data-control-unstable-v1-client-header.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <poll.h>
#include <sys/random.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>
namespace cliptiq::linux_backend {
struct ClipboardWayland::State {
  ClipboardOptions options;
  std::thread::id thread = std::this_thread::get_id();
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  const char *failure = nullptr;
  bool ext = false;
  std::uint32_t manager_name = 0, ext_name = 0, wlr_name = 0, ext_version = 0,
                wlr_version = 0, seat_id = 0;
  ext_data_control_manager_v1 *em = nullptr;
  zwlr_data_control_manager_v1 *wm = nullptr;
  ext_data_control_device_v1 *ed = nullptr;
  zwlr_data_control_device_v1 *wd = nullptr;
  struct Seat {
    wl_seat *proxy;
    std::string name;
    State *owner;
  };
  std::map<std::uint32_t, std::unique_ptr<Seat>> seats;
  struct Offer {
    void *proxy;
    bool ext;
    State *owner;
    std::vector<std::string> mimes;
  };
  std::map<void *, std::unique_ptr<Offer>> offers;
  Offer *selection = nullptr;
  struct Source {
    void *proxy;
    bool ext;
    State *owner;
    std::shared_ptr<const ClipboardSnapshot> snapshot;
  };
  std::unique_ptr<Source> source;
  std::string nonce, expected_tag;
  std::uint64_t write_counter = 0, epoch = 0, observed = 0;
  std::atomic<bool> active{false};
  std::atomic<std::uint64_t> monitor_generation{0};
  CaptureCallback callback;
  FdSendQueue sends;
  struct Capture {
    std::uint64_t epoch, monitor;
    bool sync;
    Clock::time_point deadline;
    ClipboardSnapshot snapshot;
    std::vector<Representation> selected;
    std::size_t index = 0;
    std::uint64_t total = 0;
    std::unique_ptr<FdReceiver> input;
    wl_callback *barrier = nullptr;
    bool finished = false;
  };
  std::unique_ptr<Capture> capture;
  std::optional<Result<ClipboardSnapshot>> sync_result;
  std::vector<std::pair<std::uint64_t, Result<ClipboardSnapshot>>> deliveries;
  template <class F> static void protect(State *state, F work) noexcept {
    try {
      work();
    } catch (...) {
      state->failure =
          "Wayland protocol callback failed or exceeded allocation capacity";
    }
  }
  explicit State(ClipboardOptions o) : options(std::move(o)) {
    if (!valid_options(options)) {
      failure = "Invalid Wayland limits or transfer deadline";
      return;
    }
    unsigned char random[16];
    auto amount = getrandom(random, sizeof(random), 0);
    if (amount != sizeof(random)) {
      failure = "Cannot allocate Wayland owner token";
      return;
    }
    const char *hex = "0123456789abcdef";
    for (auto c : random) {
      nonce += hex[c >> 4];
      nonce += hex[c & 15];
    }
    display = wl_display_connect(
        options.display_name.empty() ? nullptr : options.display_name.c_str());
    if (!display) {
      failure = "Wayland socket connection unavailable";
      return;
    }
    registry = wl_display_get_registry(display);
    static const wl_registry_listener listener{global, removed};
    wl_registry_add_listener(registry, &listener, this);
    if (!roundtrip() || !roundtrip()) {
      failure = "Wayland registry discovery timed out or disconnected";
      return;
    }
    if (!ext_name && !wlr_name) {
      failure = "No ext-data-control or wlr-data-control; background clipboard "
                "unavailable (no focus/portal fallback)";
      return;
    }
    Seat *chosen = nullptr;
    for (const auto &entry : seats)
      if (options.seat_name.empty() ||
          entry.second->name == options.seat_name) {
        if (chosen) {
          failure = "Multiple Wayland seats require explicit seat_name";
          return;
        }
        chosen = entry.second.get();
        seat_id = entry.first;
      }
    if (!chosen) {
      failure = "Requested Wayland seat unavailable";
      return;
    }
    ext = ext_name != 0;
    manager_name = ext ? ext_name : wlr_name;
    if (ext) {
      em = static_cast<ext_data_control_manager_v1 *>(wl_registry_bind(
          registry, ext_name, &ext_data_control_manager_v1_interface,
          std::min(ext_version, 1u)));
      ed = ext_data_control_manager_v1_get_data_device(em, chosen->proxy);
      static const ext_data_control_device_v1_listener l{
          ext_offer, ext_selection, ext_finished, ext_primary};
      ext_data_control_device_v1_add_listener(ed, &l, this);
    } else {
      wm = static_cast<zwlr_data_control_manager_v1 *>(wl_registry_bind(
          registry, wlr_name, &zwlr_data_control_manager_v1_interface,
          std::min(wlr_version, 2u)));
      wd = zwlr_data_control_manager_v1_get_data_device(wm, chosen->proxy);
      static const zwlr_data_control_device_v1_listener l{
          wlr_offer, wlr_selection, wlr_finished, wlr_primary};
      zwlr_data_control_device_v1_add_listener(wd, &l, this);
    }
    if (!roundtrip() && !failure)
      failure = "Wayland initial selection discovery failed";
  }
  ~State() {
    cancel(false);
    sends.clear();
    destroy_source();
    for (auto &item : offers)
      destroy_offer(item.second.get());
    offers.clear();
    if (ed)
      ext_data_control_device_v1_destroy(ed);
    if (wd)
      zwlr_data_control_device_v1_destroy(wd);
    if (em)
      ext_data_control_manager_v1_destroy(em);
    if (wm)
      zwlr_data_control_manager_v1_destroy(wm);
    for (auto &item : seats)
      wl_seat_destroy(item.second->proxy);
    if (registry)
      wl_registry_destroy(registry);
    if (display) {
      wl_display_flush(display);
      wl_display_disconnect(display);
    }
  }
  bool connected() const {
    return display && !failure && (ed || wd) &&
           wl_display_get_error(display) == 0;
  }
  bool own_thread() const { return thread == std::this_thread::get_id(); }
  void destroy_offer(Offer *offer) {
    if (!offer)
      return;
    if (offer->ext)
      ext_data_control_offer_v1_destroy(
          static_cast<ext_data_control_offer_v1 *>(offer->proxy));
    else
      zwlr_data_control_offer_v1_destroy(
          static_cast<zwlr_data_control_offer_v1 *>(offer->proxy));
  }
  void erase_offer(void *proxy) {
    auto it = offers.find(proxy);
    if (it != offers.end()) {
      destroy_offer(it->second.get());
      offers.erase(it);
    }
  }
  void destroy_source() {
    if (!source)
      return;
    if (source->ext)
      ext_data_control_source_v1_destroy(
          static_cast<ext_data_control_source_v1 *>(source->proxy));
    else
      zwlr_data_control_source_v1_destroy(
          static_cast<zwlr_data_control_source_v1 *>(source->proxy));
    source.reset();
  }
  static void global(void *data, wl_registry *registry, std::uint32_t name,
                     const char *interface, std::uint32_t version) {
    auto s = static_cast<State *>(data);
    protect(s, [&] {
      if (strcmp(interface, "ext_data_control_manager_v1") == 0) {
        s->ext_name = name;
        s->ext_version = version;
      } else if (strcmp(interface, "zwlr_data_control_manager_v1") == 0) {
        s->wlr_name = name;
        s->wlr_version = version;
      } else if (strcmp(interface, "wl_seat") == 0) {
        if (s->seats.size() >= 16) {
          s->failure = "Too many Wayland seats";
          return;
        }
        auto seat = std::make_unique<Seat>();
        seat->owner = s;
        seat->proxy = static_cast<wl_seat *>(wl_registry_bind(
            registry, name, &wl_seat_interface, std::min(version, 2u)));
        static const wl_seat_listener listener{seat_capabilities, seat_name};
        wl_seat_add_listener(seat->proxy, &listener, seat.get());
        s->seats.emplace(name, std::move(seat));
      }
    });
  }
  static void removed(void *data, wl_registry *, std::uint32_t name) {
    auto s = static_cast<State *>(data);
    protect(s, [&] {
      if (name == s->manager_name || name == s->seat_id) {
        s->failure = "Wayland data-control manager or selected seat removed";
        s->cancel();
        s->sends.clear();
      }
      auto seat = s->seats.find(name);
      if (seat != s->seats.end() && name != s->seat_id) {
        wl_seat_destroy(seat->second->proxy);
        s->seats.erase(seat);
      }
    });
  }
  static void seat_capabilities(void *, wl_seat *, std::uint32_t) {}
  static void seat_name(void *data, wl_seat *, const char *name) {
    auto seat = static_cast<Seat *>(data);
    protect(seat->owner, [&] {
      if (strlen(name) > 4096) {
        seat->owner->failure = "Wayland seat name too long";
        return;
      }
      seat->name = name;
    });
  }
  void new_offer(void *proxy, bool is_ext) {
    if (offers.size() >= 16) {
      failure = "Too many pending Wayland offers";
      if (is_ext)
        ext_data_control_offer_v1_destroy(
            static_cast<ext_data_control_offer_v1 *>(proxy));
      else
        zwlr_data_control_offer_v1_destroy(
            static_cast<zwlr_data_control_offer_v1 *>(proxy));
      return;
    }
    auto offer = std::make_unique<Offer>();
    offer->proxy = proxy;
    offer->ext = is_ext;
    offer->owner = this;
    if (is_ext) {
      static const ext_data_control_offer_v1_listener listener{ext_mime};
      ext_data_control_offer_v1_add_listener(
          static_cast<ext_data_control_offer_v1 *>(proxy), &listener,
          offer.get());
    } else {
      static const zwlr_data_control_offer_v1_listener listener{wlr_mime};
      zwlr_data_control_offer_v1_add_listener(
          static_cast<zwlr_data_control_offer_v1 *>(proxy), &listener,
          offer.get());
    }
    offers.emplace(proxy, std::move(offer));
  }
  static void ext_offer(void *data, ext_data_control_device_v1 *,
                        ext_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] { s->new_offer(offer, true); });
  }
  static void wlr_offer(void *data, zwlr_data_control_device_v1 *,
                        zwlr_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] { s->new_offer(offer, false); });
  }
  static void mime(Offer *offer, const char *value) {
    protect(offer->owner, [&] {
      if (offer->mimes.size() >= 4096 || strlen(value) > 4096) {
        offer->owner->failure = "Wayland MIME offers exceed metadata limit";
        return;
      }
      if (std::find(offer->mimes.begin(), offer->mimes.end(), value) ==
          offer->mimes.end())
        offer->mimes.emplace_back(value);
    });
  }
  static void ext_mime(void *data, ext_data_control_offer_v1 *,
                       const char *value) {
    mime(static_cast<Offer *>(data), value);
  }
  static void wlr_mime(void *data, zwlr_data_control_offer_v1 *,
                       const char *value) {
    mime(static_cast<Offer *>(data), value);
  }
  void changed(void *proxy) {
    auto old = selection;
    auto found = offers.find(proxy);
    selection = found == offers.end() ? nullptr : found->second.get();
    ++epoch;
    cancel();
    if (old && old != selection)
      erase_offer(old->proxy);
    if (selection && !expected_tag.empty() && source &&
        std::find(selection->mimes.begin(), selection->mimes.end(),
                  expected_tag) != selection->mimes.end()) {
      observed = epoch;
      expected_tag.clear();
    }
  }
  static void ext_selection(void *data, ext_data_control_device_v1 *,
                            ext_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] { s->changed(offer); });
  }
  static void wlr_selection(void *data, zwlr_data_control_device_v1 *,
                            zwlr_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] { s->changed(offer); });
  }
  static void finished(State *s) {
    protect(s, [&] {
      s->failure = "Wayland data-control device finished/revoked; background "
                   "capture unavailable";
      s->cancel();
      s->sends.clear();
      s->expected_tag.clear();
    });
  }
  static void ext_finished(void *data, ext_data_control_device_v1 *) {
    finished(static_cast<State *>(data));
  }
  static void wlr_finished(void *data, zwlr_data_control_device_v1 *) {
    finished(static_cast<State *>(data));
  }
  static void ext_primary(void *data, ext_data_control_device_v1 *,
                          ext_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] {
      if (!s->selection || s->selection->proxy != offer)
        s->erase_offer(offer);
    });
  }
  static void wlr_primary(void *data, zwlr_data_control_device_v1 *,
                          zwlr_data_control_offer_v1 *offer) {
    auto s = static_cast<State *>(data);
    protect(s, [&] {
      if (!s->selection || s->selection->proxy != offer)
        s->erase_offer(offer);
    });
  }
  static void source_send(Source *source, const char *mime, int fd) {
    auto s = source->owner;
    protect(s, [&] {
      auto found =
          std::find_if(source->snapshot->representations.begin(),
                       source->snapshot->representations.end(),
                       [&](const auto &r) { return r.native_type == mime; });
      if (found == source->snapshot->representations.end()) {
        close(fd);
        return;
      }
      std::shared_ptr<const Bytes> bytes;
      try {
        bytes = std::shared_ptr<const Bytes>(source->snapshot, &found->data);
      } catch (...) {
        close(fd);
        throw;
      }
      s->sends.add(fd, std::move(bytes),
                   Clock::now() +
                       std::chrono::milliseconds(s->options.timeout_ms));
    });
  }
  static void ext_send(void *data, ext_data_control_source_v1 *,
                       const char *mime, int fd) {
    source_send(static_cast<Source *>(data), mime, fd);
  }
  static void wlr_send(void *data, zwlr_data_control_source_v1 *,
                       const char *mime, int fd) {
    source_send(static_cast<Source *>(data), mime, fd);
  }
  static void canceled(Source *source) {
    auto s = source->owner;
    protect(s, [&] {
      if (s->source.get() == source) {
        s->expected_tag.clear();
        s->destroy_source();
      }
    });
  }
  static void ext_cancelled(void *data, ext_data_control_source_v1 *) {
    canceled(static_cast<Source *>(data));
  }
  static void wlr_cancelled(void *data, zwlr_data_control_source_v1 *) {
    canceled(static_cast<Source *>(data));
  }
  bool roundtrip() {
    bool done = false;
    auto barrier = wl_display_sync(display);
    static const wl_callback_listener listener{
        [](void *data, wl_callback *callback, std::uint32_t) {
          *static_cast<bool *>(data) = true;
          wl_callback_destroy(callback);
        }};
    wl_callback_add_listener(barrier, &listener, &done);
    auto end = Clock::now() + std::chrono::milliseconds(options.timeout_ms);
    while (!done && Clock::now() < end && !wl_display_get_error(display) &&
           !failure) {
      io(5);
    }
    if (!done)
      wl_callback_destroy(barrier);
    return done;
  }
  void cancel(bool report = true) {
    if (!capture)
      return;
    if (capture->barrier)
      wl_callback_destroy(capture->barrier);
    auto sync = capture->sync;
    auto monitor = capture->monitor;
    capture.reset();
    if (report) {
      Error error{ErrorCode::Busy,
                  "Wayland offer changed, canceled or timed out"};
      if (sync)
        sync_result = error;
      else
        deliveries.emplace_back(monitor, error);
    }
  }
  void complete(Result<ClipboardSnapshot> result) {
    if (!capture)
      return;
    auto sync = capture->sync;
    auto monitor = capture->monitor;
    if (capture->barrier)
      wl_callback_destroy(capture->barrier);
    capture.reset();
    if (sync)
      sync_result = std::move(result);
    else {
      observed = epoch;
      deliveries.emplace_back(monitor, std::move(result));
    }
  }
  void begin(bool sync) {
    if (capture)
      return;
    if (!selection) {
      if (sync)
        sync_result = Error{ErrorCode::NotFound, "Wayland selection is empty"};
      return;
    }
    capture = std::make_unique<Capture>();
    auto &c = *capture;
    c.epoch = epoch;
    c.monitor = monitor_generation;
    c.sync = sync;
    c.deadline = Clock::now() + std::chrono::milliseconds(options.timeout_ms);
    c.snapshot.sequence_token =
        "wayland:" + nonce + ":" + std::to_string(epoch);
    for (const auto &mime : selection->mimes)
      c.snapshot.offered_types.push_back({"wayland", mime});
    c.snapshot.flags = native_flags(c.snapshot.offered_types);
    for (const auto &mime : selection->mimes) {
      auto t =
          std::find_if(native_types(false).begin(), native_types(false).end(),
                       [&](const auto &t) { return t.name == mime; });
      if (t == native_types(false).end())
        continue;
      auto has = [&](const char *n) {
        return std::find(selection->mimes.begin(), selection->mimes.end(), n) !=
               selection->mimes.end();
      };
      if (mime == "text/plain" && has("text/plain;charset=utf-8"))
        continue;
      if (mime == "image/tiff" && has("image/png"))
        continue;
      c.snapshot.representations.push_back(
          {0, "wayland", mime, t->kind, {}, t->encoding});
    }
    auto decision = evaluate_capture(c.snapshot, options.policy,
                                     native_capabilities(false, true));
    c.snapshot.representations.clear();
    if (decision.snapshot)
      c.selected = std::move(decision.snapshot->representations);
    if (c.selected.size() > options.limits.max_representations) {
      complete(Error{ErrorCode::TooLarge, "Too many Wayland representations"});
      return;
    }
    next();
  }
  void next() {
    if (!capture)
      return;
    auto &c = *capture;
    if (c.index == c.selected.size()) {
      c.barrier = wl_display_sync(display);
      static const wl_callback_listener listener{
          [](void *data, wl_callback *callback, std::uint32_t) {
            auto s = static_cast<State *>(data);
            if (s->capture && s->capture->barrier == callback) {
              s->capture->barrier = nullptr;
              s->capture->finished = true;
            }
            wl_callback_destroy(callback);
          }};
      wl_callback_add_listener(c.barrier, &listener, this);
      return;
    }
    if (!selection || c.epoch != epoch) {
      cancel();
      return;
    }
    int fd[2];
    if (pipe2(fd, O_CLOEXEC | O_NONBLOCK) != 0) {
      complete(Error{ErrorCode::Io, "Cannot create Wayland receive pipe"});
      return;
    }
    c.input = std::make_unique<FdReceiver>(
        fd[0],
        std::min(options.limits.representation_bytes,
                 options.limits.entry_bytes - c.total),
        c.deadline, c.epoch);
    auto mime = c.selected[c.index].native_type.c_str();
    if (selection->ext)
      ext_data_control_offer_v1_receive(
          static_cast<ext_data_control_offer_v1 *>(selection->proxy), mime,
          fd[1]);
    else
      zwlr_data_control_offer_v1_receive(
          static_cast<zwlr_data_control_offer_v1 *>(selection->proxy), mime,
          fd[1]);
    close(fd[1]);
    wl_display_flush(display);
  }
  void advance() {
    if (!capture)
      return;
    if (capture->epoch != epoch || Clock::now() >= capture->deadline) {
      cancel();
      return;
    }
    if (capture->finished) {
      complete(std::move(capture->snapshot));
      return;
    }
    if (!capture->input)
      return;
    auto result = capture->input->step(epoch);
    if (!result)
      return;
    if (!*result) {
      complete(result->error());
      return;
    }
    auto &c = *capture;
    auto r = c.selected[c.index];
    r.data = std::move(result->value());
    c.input.reset();
    auto valid = validate_payload(r);
    if (!valid) {
      complete(valid.error());
      return;
    }
    c.total += r.data.size();
    c.snapshot.representations.push_back(std::move(r));
    ++c.index;
    next();
  }
  void io(unsigned timeout) {
    if (!display || failure)
      return;
    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) < 0) {
        failure = "Wayland compositor disconnected";
        return;
      }
      if (failure)
        return;
    }
    bool needs_write = wl_display_flush(display) < 0 && errno == EAGAIN;
    std::vector<pollfd> fds = {
        {wl_display_get_fd(display),
         static_cast<short>(POLLIN | (needs_write ? POLLOUT : 0)), 0}};
    if (capture && capture->input)
      fds.push_back({capture->input->fd(), POLLIN, 0});
    for (auto fd : sends.fds())
      fds.push_back({fd, POLLOUT, 0});
    int polled = poll(fds.data(), fds.size(), std::min(timeout, 10u));
    if (polled > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(display) < 0)
        failure = "Wayland compositor read failed";
    } else
      wl_display_cancel_read(display);
    if (polled > 0 && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)))
      failure = "Wayland compositor disconnected";
    if (!failure && wl_display_dispatch_pending(display) < 0)
      failure = "Wayland compositor dispatch failed";
    if (!failure) {
      sends.step();
      advance();
      if (wl_display_flush(display) < 0 && errno != EAGAIN)
        failure = "Wayland compositor flush failed";
    }
  }
  Result<void> pump(unsigned timeout, bool deliver = true) {
    if (!own_thread())
      return Error{ErrorCode::InvalidArgument,
                   "Wayland backend requires owner event thread"};
    if (!connected())
      return Error{ErrorCode::ReadOnly,
                   failure ? failure : "Wayland data-control unavailable"};
    io(timeout);
    if (failure) {
      cancel();
      return Error{ErrorCode::ReadOnly, failure};
    }
    if (active && !capture && selection && observed != epoch)
      begin(false);
    if (deliver) {
      auto ready = std::move(deliveries);
      deliveries.clear();
      for (auto &item : ready)
        if (active && item.first == monitor_generation && callback) {
          auto cb = callback;
          cb(std::move(item.second));
        }
    }
    return {};
  }
};
ClipboardWayland::ClipboardWayland(ClipboardOptions options)
    : state_(std::make_shared<State>(std::move(options))) {}
ClipboardWayland::~ClipboardWayland() { stop(); }
Capabilities ClipboardWayland::capabilities() const {
  return native_capabilities(
      false, state_->connected(),
      state_->failure
          ? state_->failure
          : (state_->ext
                 ? "Native ext-data-control v1; source app unavailable"
                 : "Native wlr-data-control fallback; source app unavailable"));
}
int ClipboardWayland::connection_fd() const {
  return state_->display ? wl_display_get_fd(state_->display) : -1;
}
Result<void> ClipboardWayland::dispatch(unsigned timeout) {
  auto s = state_;
  return s->pump(timeout);
}
Result<void> ClipboardWayland::set_capture_policy(CapturePolicy p) {
  auto s = state_;
  if (!s->own_thread())
    return Error{ErrorCode::InvalidArgument,
                 "Wayland policy requires owner thread"};
  if (evaluate_capture({}, p, capabilities()).reason ==
      CaptureReason::InvalidInput)
    return Error{ErrorCode::InvalidArgument, "Invalid capture policy"};
  s->options.policy = std::move(p);
  return {};
}
Result<void> ClipboardWayland::start(CaptureCallback callback) {
  auto s = state_;
  if (!s->own_thread() || !callback || s->active)
    return Error{ErrorCode::InvalidArgument, "Invalid Wayland monitor start"};
  if (!s->connected())
    return Error{ErrorCode::ReadOnly,
                 s->failure ? s->failure : "Wayland data-control unavailable"};
  s->cancel(false);
  ++s->monitor_generation;
  s->callback = std::move(callback);
  s->active = true;
  return {};
}
void ClipboardWayland::stop() {
  auto s = state_;
  s->active = false;
  ++s->monitor_generation;
  if (s->own_thread()) {
    s->callback = {};
    s->cancel(false);
  }
}
Result<ClipboardSnapshot> ClipboardWayland::read_snapshot() {
  auto s = state_;
  if (!s->own_thread())
    return Error{ErrorCode::InvalidArgument,
                 "Wayland read requires owner thread"};
  auto r = s->pump(0);
  if (!r)
    return r.error();
  if (s->capture)
    return Error{ErrorCode::Busy, "A Wayland capture is already pending"};
  s->sync_result.reset();
  s->begin(true);
  while (!s->sync_result) {
    auto result = s->pump(5);
    if (!result) {
      s->cancel(false);
      return result.error();
    }
  }
  auto result = std::move(*s->sync_result);
  s->sync_result.reset();
  return result;
}
Result<std::string>
ClipboardWayland::write_snapshot(const ClipboardSnapshot &input) {
  auto s = state_;
  if (!s->own_thread())
    return Error{ErrorCode::InvalidArgument,
                 "Wayland replay requires owner thread"};
  if (!s->connected())
    return Error{ErrorCode::ReadOnly,
                 s->failure ? s->failure : "Wayland replay unavailable"};
  auto checked = prepare_replay(input, s->options, false);
  if (!checked)
    return checked.error();
  auto prepared =
      std::make_shared<ClipboardSnapshot>(std::move(checked.value()));
  s->destroy_source();
  auto source = std::make_unique<State::Source>();
  source->owner = s.get();
  source->ext = s->ext;
  source->snapshot = prepared;
  if (s->ext) {
    auto proxy = ext_data_control_manager_v1_create_data_source(s->em);
    source->proxy = proxy;
    static const ext_data_control_source_v1_listener listener{
        State::ext_send, State::ext_cancelled};
    ext_data_control_source_v1_add_listener(proxy, &listener, source.get());
  } else {
    auto proxy = zwlr_data_control_manager_v1_create_data_source(s->wm);
    source->proxy = proxy;
    static const zwlr_data_control_source_v1_listener listener{
        State::wlr_send, State::wlr_cancelled};
    zwlr_data_control_source_v1_add_listener(proxy, &listener, source.get());
  }
  auto offer = [&](const char *mime) {
    if (s->ext)
      ext_data_control_source_v1_offer(
          static_cast<ext_data_control_source_v1 *>(source->proxy), mime);
    else
      zwlr_data_control_source_v1_offer(
          static_cast<zwlr_data_control_source_v1 *>(source->proxy), mime);
  };
  for (const auto &r : prepared->representations)
    offer(r.native_type.c_str());
  if (prepared->flags.transient == Flag::Yes)
    offer("org.nspasteboard.TransientType");
  if (prepared->flags.concealed == Flag::Yes)
    offer("org.nspasteboard.ConcealedType");
  s->expected_tag = "application/x-cliptiq-owner-" + s->nonce + "-" +
                    std::to_string(++s->write_counter);
  offer(s->expected_tag.c_str());
  if (s->ext)
    ext_data_control_device_v1_set_selection(
        s->ed, static_cast<ext_data_control_source_v1 *>(source->proxy));
  else
    zwlr_data_control_device_v1_set_selection(
        s->wd, static_cast<zwlr_data_control_source_v1 *>(source->proxy));
  s->source = std::move(source);
  if (!s->roundtrip())
    return Error{ErrorCode::Busy,
                 "Wayland replay acknowledgement failed or timed out"};
  return "wayland:" + s->nonce + ":" + std::to_string(s->epoch);
}
} // namespace cliptiq::linux_backend

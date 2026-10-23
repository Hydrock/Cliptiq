#pragma once
#include "ext-data-control-v1-server-header.h"
#include "wlr-data-control-unstable-v1-server-header.h"
#include <atomic>
#include <fcntl.h>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-server.h>
// Real libwayland-server wire peer for deterministic faults; NOT a compositor.
class WaylandPeer {
public:
  enum class Mode {
    Normal,
    Hang,
    Cancel,
    Change,
    Finished,
    NoManager,
    MultiSeat
  };
  using Bytes = std::vector<std::uint8_t>;
  Mode mode;
  std::string socket;
  std::atomic<unsigned> receives{0}, ext_binds{0}, wlr_binds{0};

private:
  struct Data {
    WaylandPeer *peer;
    wl_resource *source = nullptr;
    std::vector<std::string> mimes;
    std::map<std::string, Bytes> values;
  };
  struct Offer {
    WaylandPeer *peer;
    std::shared_ptr<Data> data;
  };
  struct Output {
    int fd;
    Bytes bytes;
    std::size_t offset = 0;
  };
  std::atomic<bool> stop{false};
  std::thread thread;
  wl_display *display = nullptr;
  std::vector<wl_resource *> devices;
  std::vector<Output> outputs;
  std::shared_ptr<Data> selection;
  bool shutting_down = false;
  struct SeatInfo {
    std::string name;
  };
  std::vector<std::unique_ptr<SeatInfo>> seat_infos;
  static void destroy(wl_client *, wl_resource *resource) {
    wl_resource_destroy(resource);
  }
  static void create_source(wl_client *client, wl_resource *manager,
                            std::uint32_t id) {
    auto peer = static_cast<WaylandPeer *>(wl_resource_get_user_data(manager));
    auto data = std::make_shared<Data>();
    data->peer = peer;
    auto resource = wl_resource_create(
        client, &ext_data_control_source_v1_interface, 1, id);
    data->source = resource;
    auto box = new std::shared_ptr<Data>(data);
    static const struct ext_data_control_source_v1_interface implementation {
      source_offer, destroy
    };
    wl_resource_set_implementation(resource, &implementation, box,
                                   source_destroyed);
  }
  static void source_offer(wl_client *, wl_resource *resource,
                           const char *mime) {
    auto data = *static_cast<std::shared_ptr<Data> *>(
        wl_resource_get_user_data(resource));
    data->mimes.emplace_back(mime);
  }
  static void source_destroyed(wl_resource *resource) {
    auto box = static_cast<std::shared_ptr<Data> *>(
        wl_resource_get_user_data(resource));
    auto data = *box;
    data->source = nullptr;
    if (!data->peer->shutting_down && data->peer->selection == data) {
      data->peer->selection.reset();
      data->peer->publish();
    }
    delete box;
  }
  static void set_selection(wl_client *, wl_resource *resource,
                            wl_resource *source) {
    auto peer = static_cast<WaylandPeer *>(wl_resource_get_user_data(resource));
    if (peer->selection && peer->selection->source)
      ext_data_control_source_v1_send_cancelled(peer->selection->source);
    peer->selection = source ? *static_cast<std::shared_ptr<Data> *>(
                                   wl_resource_get_user_data(source))
                             : nullptr;
    peer->publish();
  }
  static void set_primary(wl_client *, wl_resource *, wl_resource *) {}
  static void device_destroyed(wl_resource *resource) {
    auto peer = static_cast<WaylandPeer *>(wl_resource_get_user_data(resource));
    peer->devices.erase(
        std::remove(peer->devices.begin(), peer->devices.end(), resource),
        peer->devices.end());
  }
  static void get_device(wl_client *client, wl_resource *manager,
                         std::uint32_t id, wl_resource *) {
    auto peer = static_cast<WaylandPeer *>(wl_resource_get_user_data(manager));
    auto resource = wl_resource_create(
        client, &ext_data_control_device_v1_interface, 1, id);
    static const struct ext_data_control_device_v1_interface implementation {
      set_selection, destroy, set_primary
    };
    wl_resource_set_implementation(resource, &implementation, peer,
                                   device_destroyed);
    peer->devices.push_back(resource);
    if (peer->mode == Mode::Finished)
      ext_data_control_device_v1_send_finished(resource);
    else
      peer->publish_one(resource);
  }
  static void receive(wl_client *, wl_resource *resource, const char *mime,
                      int fd) {
    auto offer = static_cast<Offer *>(wl_resource_get_user_data(resource));
    auto peer = offer->peer;
    ++peer->receives;
    if (offer->data->source) {
      ext_data_control_source_v1_send_send(offer->data->source, mime, fd);
      close(fd);
      return;
    }
    if (peer->mode == Mode::Hang) {
      peer->outputs.push_back({fd, {}, 0});
      return;
    }
    if (peer->mode == Mode::Cancel || peer->mode == Mode::Change) {
      write(fd, "partial", 7);
      if (peer->mode == Mode::Change) {
        auto replacement = std::make_shared<Data>();
        replacement->peer = peer;
        replacement->mimes = {"text/plain;charset=utf-8"};
        replacement->values[replacement->mimes[0]] = Bytes{'n', 'e', 'w'};
        peer->selection = replacement;
        peer->mode = Mode::Normal;
      } else
        peer->selection.reset();
      peer->publish();
      wl_display_flush_clients(peer->display);
      close(fd);
      return;
    }
    auto value = offer->data->values.find(mime);
    if (value == offer->data->values.end()) {
      close(fd);
      return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    peer->outputs.push_back({fd, value->second, 0});
  }
  void publish_one(wl_resource *device) {
    if (!selection) {
      ext_data_control_device_v1_send_selection(device, nullptr);
      return;
    }
    auto client = wl_resource_get_client(device);
    auto resource =
        wl_resource_create(client, &ext_data_control_offer_v1_interface, 1, 0);
    static const struct ext_data_control_offer_v1_interface implementation {
      receive, destroy
    };
    wl_resource_set_implementation(
        resource, &implementation, new Offer{this, selection},
        [](wl_resource *r) {
          delete static_cast<Offer *>(wl_resource_get_user_data(r));
        });
    ext_data_control_device_v1_send_data_offer(device, resource);
    for (const auto &mime : selection->mimes)
      ext_data_control_offer_v1_send_offer(resource, mime.c_str());
    ext_data_control_device_v1_send_selection(device, resource);
  }
  void publish() {
    for (auto device : devices)
      publish_one(device);
  }
  static void bind_manager(wl_client *client, void *data, std::uint32_t,
                           std::uint32_t id) {
    auto peer = static_cast<WaylandPeer *>(data);
    ++peer->ext_binds;
    auto resource = wl_resource_create(
        client, &ext_data_control_manager_v1_interface, 1, id);
    static const struct ext_data_control_manager_v1_interface implementation {
      create_source, get_device, destroy
    };
    wl_resource_set_implementation(resource, &implementation, peer, nullptr);
  }
  static void bind_wlr(wl_client *client, void *data, std::uint32_t version,
                       std::uint32_t id) {
    auto peer = static_cast<WaylandPeer *>(data);
    ++peer->wlr_binds;
    auto resource =
        wl_resource_create(client, &zwlr_data_control_manager_v1_interface,
                           version > 2 ? 2 : version, id);
    wl_resource_set_implementation(resource, nullptr, peer, nullptr);
  }
  static void seat_get(wl_client *client, wl_resource *resource,
                       std::uint32_t) {
    wl_resource_post_error(resource, 0,
                           "Input devices are not part of clipboard fixture");
    (void)client;
  }
  static void bind_seat(wl_client *client, void *data, std::uint32_t version,
                        std::uint32_t id) {
    auto seat = static_cast<SeatInfo *>(data);
    auto resource = wl_resource_create(client, &wl_seat_interface,
                                       version > 2 ? 2 : version, id);
    static const struct wl_seat_interface implementation {
      seat_get, seat_get, seat_get, destroy
    };
    wl_resource_set_implementation(resource, &implementation, nullptr, nullptr);
    wl_seat_send_capabilities(resource, 0);
    if (version >= 2)
      wl_seat_send_name(resource, seat->name.c_str());
  }

public:
  explicit WaylandPeer(Mode m = Mode::Normal,
                       std::map<std::string, Bytes> values =
                           {{"text/plain;charset=utf-8", {'h', 'i'}}})
      : mode(m) {
    std::promise<void> ready;
    auto future = ready.get_future();
    thread = std::thread([&, values] {
      sigset_t signals;
      sigemptyset(&signals);
      sigaddset(&signals, SIGPIPE);
      pthread_sigmask(SIG_BLOCK, &signals, nullptr);
      display = wl_display_create();
      selection = std::make_shared<Data>();
      selection->peer = this;
      selection->values = values;
      for (const auto &value : values)
        selection->mimes.push_back(value.first);
      for (unsigned i = 0; i < (mode == Mode::MultiSeat ? 2u : 1u); ++i) {
        auto seat = std::make_unique<SeatInfo>();
        seat->name = "seat" + std::to_string(i);
        wl_global_create(display, &wl_seat_interface, 2, seat.get(), bind_seat);
        seat_infos.push_back(std::move(seat));
      }
      if (mode != Mode::NoManager) {
        wl_global_create(display, &ext_data_control_manager_v1_interface, 1,
                         this, bind_manager);
        wl_global_create(display, &zwlr_data_control_manager_v1_interface, 2,
                         this, bind_wlr);
      }
      auto name = wl_display_add_socket_auto(display);
      if (!name) {
        ready.set_exception(std::make_exception_ptr(
            std::runtime_error("fixture socket creation failed")));
        return;
      }
      socket = name;
      ready.set_value();
      auto loop = wl_display_get_event_loop(display);
      while (!stop) {
        wl_event_loop_dispatch(loop, 2);
        wl_display_flush_clients(display);
        for (auto it = outputs.begin(); it != outputs.end();) {
          if (mode == Mode::Hang) {
            ++it;
            continue;
          }
          auto remaining = it->bytes.size() - it->offset;
          auto amount = remaining > 65536 ? 65536 : remaining;
          auto n =
              amount ? write(it->fd, it->bytes.data() + it->offset, amount) : 0;
          if (n > 0)
            it->offset += n;
          bool done = it->offset == it->bytes.size() ||
                      (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                       errno != EINTR);
          if (done) {
            close(it->fd);
            it = outputs.erase(it);
          } else
            ++it;
        }
      }
      for (auto &output : outputs)
        close(output.fd);
      outputs.clear();
      shutting_down = true;
      wl_display_destroy_clients(display);
      wl_display_destroy(display);
    });
    future.get();
  }
  ~WaylandPeer() {
    stop = true;
    thread.join();
  }
};

#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>
#include <xcb/xcb.h>
// Independent X server client fixture, not production backend implementation.
class X11Peer {
public:
  enum class Mode { Normal, Incr, Hang, Oversize, OwnerDeath };
  using Bytes = std::vector<std::uint8_t>;
  std::atomic<unsigned> payload_reads{0};

private:
  std::thread thread;
  std::atomic<bool> stop{false};
  std::mutex mutex;
  std::deque<std::function<void()>> commands;
  xcb_connection_t *connection = nullptr;
  xcb_window_t window = 0, root = 0;
  xcb_atom_t clipboard = 0, targets = 0, incr = 0;
  std::uint32_t acquired = 0;
  Mode mode;
  std::map<xcb_atom_t, Bytes> values;
  std::map<std::string, xcb_atom_t> names;
  struct Transfer {
    xcb_window_t window;
    xcb_atom_t property, target;
    Bytes data;
    std::size_t offset = 0;
  };
  std::vector<Transfer> transfers;
  xcb_atom_t atom(const std::string &n) {
    auto it = names.find(n);
    if (it != names.end())
      return it->second;
    auto reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 0, n.size(), n.c_str()),
        nullptr);
    if (!reply)
      throw std::runtime_error("fixture atom query");
    auto a = reply->atom;
    free(reply);
    names[n] = a;
    return a;
  }
  void notify(const xcb_selection_request_event_t &r, xcb_atom_t property) {
    xcb_selection_notify_event_t reply{};
    reply.response_type = XCB_SELECTION_NOTIFY;
    reply.time = r.time;
    reply.requestor = r.requestor;
    reply.selection = r.selection;
    reply.target = r.target;
    reply.property = property;
    xcb_send_event(connection, 0, r.requestor, 0,
                   reinterpret_cast<const char *>(&reply));
  }
  void request(const xcb_selection_request_event_t &r) {
    auto property = r.property ? r.property : r.target;
    if (r.target == targets) {
      std::vector<xcb_atom_t> types;
      for (const auto &v : values)
        types.push_back(v.first);
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, r.requestor,
                          property, XCB_ATOM_ATOM, 32, types.size(),
                          types.data());
      notify(r, property);
      return;
    }
    auto found = values.find(r.target);
    if (found == values.end()) {
      notify(r, 0);
      return;
    }
    ++payload_reads;
    if (mode == Mode::Hang)
      return;
    if (mode == Mode::Normal) {
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, r.requestor,
                          property, r.target, 8, found->second.size(),
                          found->second.data());
      notify(r, property);
      return;
    }
    std::uint32_t mask =
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(connection, r.requestor, XCB_CW_EVENT_MASK,
                                 &mask);
    std::uint32_t size =
        mode == Mode::Oversize ? 33554433u : found->second.size();
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, r.requestor,
                        property, incr, 32, 1, &size);
    notify(r, property);
    if (mode != Mode::Oversize)
      transfers.push_back({r.requestor, property, r.target, found->second, 0});
  }

public:
  explicit X11Peer(Mode m = Mode::Normal,
                   std::map<std::string, Bytes> formats = {{"UTF8_STRING",
                                                            {'h', 'i'}}},
                   std::string klass = {})
      : mode(m) {
    std::promise<void> ready;
    auto future = ready.get_future();
    thread = std::thread([&, formats, klass] {
      int screen = 0;
      connection = xcb_connect(nullptr, &screen);
      auto iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
      while (screen--)
        xcb_screen_next(&iterator);
      root = iterator.data->root;
      clipboard = atom("CLIPBOARD");
      targets = atom("TARGETS");
      incr = atom("INCR");
      window = xcb_generate_id(connection);
      std::uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
      xcb_create_window(connection, 0, window, root, 0, 0, 1, 1, 0,
                        XCB_WINDOW_CLASS_INPUT_ONLY, 0, XCB_CW_EVENT_MASK,
                        &mask);
      for (const auto &f : formats)
        values[atom(f.first)] = f.second;
      if (!klass.empty()) {
        std::string hint = "fixture";
        hint += '\0';
        hint += klass;
        hint += '\0';
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
                            atom("WM_CLASS"), XCB_ATOM_STRING, 8, hint.size(),
                            hint.data());
      }
      auto clock = atom("FIXTURE_CLOCK");
      std::uint8_t marker = 1;
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, clock,
                          XCB_ATOM_INTEGER, 8, 1, &marker);
      xcb_flush(connection);
      while (!acquired) {
        auto event = xcb_wait_for_event(connection);
        if (!event)
          break;
        if ((event->response_type & 127) == XCB_PROPERTY_NOTIFY)
          acquired =
              reinterpret_cast<xcb_property_notify_event_t *>(event)->time;
        free(event);
      }
      xcb_set_selection_owner(connection, window, clipboard, acquired);
      xcb_flush(connection);
      ready.set_value();
      while (!stop) {
        std::function<void()> command;
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (!commands.empty()) {
            command = std::move(commands.front());
            commands.pop_front();
          }
        }
        if (command)
          command();
        while (auto event = xcb_poll_for_event(connection)) {
          auto type = event->response_type & 127;
          if (type == XCB_SELECTION_REQUEST)
            request(*reinterpret_cast<xcb_selection_request_event_t *>(event));
          if (type == XCB_PROPERTY_NOTIFY) {
            auto e = reinterpret_cast<xcb_property_notify_event_t *>(event);
            if (e->state == XCB_PROPERTY_DELETE)
              for (auto it = transfers.begin(); it != transfers.end(); ++it)
                if (it->window == e->window && it->property == e->atom) {
                  auto amount =
                      std::min<std::size_t>(4096, it->data.size() - it->offset);
                  xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
                                      it->window, it->property, it->target, 8,
                                      amount, it->data.data() + it->offset);
                  it->offset += amount;
                  if (mode == Mode::OwnerDeath) {
                    xcb_destroy_window(connection, window);
                    window = 0;
                    transfers.clear();
                  } else if (!amount)
                    transfers.erase(it);
                  break;
                }
          }
          free(event);
        }
        xcb_flush(connection);
        pollfd fd{xcb_get_file_descriptor(connection), POLLIN, 0};
        poll(&fd, 1, 1);
      }
      if (window)
        xcb_destroy_window(connection, window);
      xcb_disconnect(connection);
    });
    future.get();
  }
  ~X11Peer() {
    stop = true;
    thread.join();
  }
  void reassert_same_timestamp(Bytes bytes) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex);
      commands.push_back([&, bytes = std::move(bytes), promise] {
        values[atom("UTF8_STRING")] = bytes;
        xcb_set_selection_owner(connection, window, clipboard, acquired);
        xcb_flush(connection);
        auto acknowledged = xcb_get_input_focus_reply(
            connection, xcb_get_input_focus(connection), nullptr);
        free(acknowledged);
        promise->set_value();
      });
    }
    future.get();
  }
};

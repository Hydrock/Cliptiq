#include "../fixtures/linux/X11Peer.hpp"
#include "Application.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
using namespace cliptiq::linux_backend;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
void ok(Result<void> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
}
#include <xcb/xcbext.h>
// Test-only linker wrappers put a real reassertion after the final owner reply
// is available but before the adapter validates it. No production hook.
std::function<void()> after_owner_reply;
unsigned owner_queries = 0, finish_sequence = 0;
extern "C" xcb_get_selection_owner_cookie_t
__real_xcb_get_selection_owner(xcb_connection_t *, xcb_atom_t);
extern "C" int __real_xcb_poll_for_reply(xcb_connection_t *, unsigned, void **,
                                         xcb_generic_error_t **);
extern "C" xcb_get_selection_owner_cookie_t
__wrap_xcb_get_selection_owner(xcb_connection_t *c, xcb_atom_t a) {
  auto result = __real_xcb_get_selection_owner(c, a);
  if (after_owner_reply && ++owner_queries == 2)
    finish_sequence = result.sequence;
  return result;
}
extern "C" int __wrap_xcb_poll_for_reply(xcb_connection_t *c, unsigned sequence,
                                         void **reply,
                                         xcb_generic_error_t **error) {
  auto result = __real_xcb_poll_for_reply(c, sequence, reply, error);
  if (result && sequence == finish_sequence && after_owner_reply) {
    auto action = std::move(after_owner_reply);
    after_owner_reply = {};
    action();
  }
  return result;
}
int main() {
  if (!getenv("CLIPTIQ_TEST_ISOLATED") ||
      std::string(getenv("CLIPTIQ_TEST_ISOLATED")) != "x11")
    return 2;
  int failures = 0;
  auto test = [&](const char *n, auto f) {
    try {
      f();
      std::cout << "PASS " << n << '\n';
    } catch (const std::exception &e) {
      ++failures;
      std::cerr << "FAIL " << n << ": " << e.what() << '\n';
    }
  };
  test("independent_targets_incr_roundtrip", [] {
    Bytes value(200000, 'x');
    X11Peer source(X11Peer::Mode::Incr, {{"UTF8_STRING", value}});
    ClipboardX11 b;
    auto s = take(b.read_snapshot());
    CHECK(s.representations.size() == 1 && s.representations[0].data == value);
  });
  test("incr_owner_death_discards_partial", [] {
    X11Peer source(X11Peer::Mode::OwnerDeath,
                   {{"UTF8_STRING", Bytes(200000, 'x')}});
    ClipboardX11 b;
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("incr_announcement_over_limit_rejected", [] {
    X11Peer source(X11Peer::Mode::Oversize);
    ClipboardX11 b;
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::TooLarge);
  });
  test("selection_owner_stall_has_deadline", [] {
    X11Peer source(X11Peer::Mode::Hang);
    ClipboardOptions options;
    options.timeout_ms = 50;
    ClipboardX11 b(options);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("disabled_unknown_no_payload_requests", [] {
    X11Peer source(X11Peer::Mode::Normal, {{"UTF8_STRING", {'x'}},
                                           {"text/html", {'<', 'b', '>'}},
                                           {"custom/secret", {1, 2}}});
    ClipboardX11 b;
    auto s = take(b.read_snapshot());
    CHECK(s.representations.size() == 1 && source.payload_reads == 1);
  });
  test("owner_wm_class_exclusion_prevents_payload", [] {
    X11Peer source(X11Peer::Mode::Normal, {{"UTF8_STRING", {'x'}}},
                   "Private.App");
    ClipboardOptions options;
    options.policy.excluded_apps = {"x11.wm_class:private.app"};
    ClipboardX11 b(options);
    auto s = take(b.read_snapshot());
    CHECK(s.representations.empty() && source.payload_reads == 0);
  });
  test("latin1_string_becomes_utf8", [] {
    X11Peer source(X11Peer::Mode::Normal, {{"STRING", {0xe9}}});
    ClipboardX11 b;
    auto s = take(b.read_snapshot());
    CHECK(s.representations.size() == 1 &&
          s.representations[0].native_type == "UTF8_STRING" &&
          s.representations[0].data == Bytes({0xc3, 0xa9}));
  });
  test("same_owner_timestamp_still_notifies_new_copy", [] {
    X11Peer source;
    ClipboardX11 b;
    unsigned calls = 0;
    Bytes last;
    ok(b.start([&](Result<ClipboardSnapshot> r) {
      if (r && !r.value().representations.empty()) {
        ++calls;
        last = r.value().representations[0].data;
      }
    }));
    auto wait = [&](unsigned count) {
      auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (calls < count && std::chrono::steady_clock::now() < end)
        ok(b.dispatch(5));
      return calls == count;
    };
    CHECK(wait(1));
    source.reassert_same_timestamp(Bytes{'n', 'e', 'w'});
    CHECK(wait(2));
    CHECK(last == Bytes({'n', 'e', 'w'}));
    source.reassert_same_timestamp(Bytes{'t', 'h', 'i', 'r', 'd'});
    CHECK(wait(3));
    CHECK(last == Bytes({'t', 'h', 'i', 'r', 'd'}));
  });
  test("final_owner_reply_does_not_hide_queued_reassertion", [] {
    X11Peer source;
    ClipboardX11 b;
    owner_queries = 0;
    finish_sequence = 0;
    after_owner_reply = [&] {
      source.reassert_same_timestamp(Bytes{'n', 'e', 'w'});
    };
    auto r = b.read_snapshot();
    after_owner_reply = {};
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  return failures ? 1 : 0;
}

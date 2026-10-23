#include "../fixtures/linux/WaylandPeer.hpp"
#include "Application.hpp"
#include "Transfer.hpp"
#include <filesystem>
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
int main() {
  if (!getenv("CLIPTIQ_TEST_ISOLATED") ||
      std::string(getenv("CLIPTIQ_TEST_ISOLATED")) != "fixture") {
    std::cerr << "Private runtime required\n";
    return 2;
  }
  int failures = 0;
  auto test = [&](const char *name, auto f) {
    try {
      f();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &e) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
  };
  test("ext_preferred_over_wlr_and_fd_roundtrip", [] {
    WaylandPeer peer;
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    CHECK(b.capabilities().capture.status == CapabilityStatus::Available);
    auto s = take(b.read_snapshot());
    CHECK(s.representations.size() == 1 &&
          s.representations[0].data == Bytes({'h', 'i'}));
    CHECK(peer.ext_binds == 1 && peer.wlr_binds == 0);
    take(b.write_snapshot(s));
    CHECK(take(b.read_snapshot()).representations[0].data == Bytes({'h', 'i'}));
  });
  test("no_data_control_unavailable_without_fallback", [] {
    WaylandPeer peer(WaylandPeer::Mode::NoManager);
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    CHECK(b.capabilities().capture.status == CapabilityStatus::Unavailable);
    CHECK(b.capabilities().write.status == CapabilityStatus::Unavailable);
    CHECK(!b.read_snapshot());
  });
  test("finished_device_is_unavailable", [] {
    WaylandPeer peer(WaylandPeer::Mode::Finished);
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    CHECK(b.capabilities().capture.status == CapabilityStatus::Unavailable);
    CHECK(!b.read_snapshot());
  });
  test("stalled_fd_times_out_without_partial", [] {
    WaylandPeer peer(WaylandPeer::Mode::Hang);
    ClipboardOptions o;
    o.display_name = peer.socket;
    o.timeout_ms = 50;
    ClipboardWayland b(o);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("offer_cancel_discards_partial", [] {
    WaylandPeer peer(WaylandPeer::Mode::Cancel);
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("offer_change_discards_partial_and_next_read_recovers", [] {
    WaylandPeer peer(WaylandPeer::Mode::Change);
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::Busy);
    auto next = take(b.read_snapshot());
    CHECK(next.representations[0].data == Bytes({'n', 'e', 'w'}));
  });
  test("fd_byte_limit_rejects_before_snapshot", [] {
    WaylandPeer peer(WaylandPeer::Mode::Normal,
                     {{"text/plain;charset=utf-8", Bytes(10000, 'x')}});
    ClipboardOptions o;
    o.display_name = peer.socket;
    o.limits.representation_bytes = 100;
    ClipboardWayland b(o);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::TooLarge);
  });
  test("disabled_html_and_unknown_never_requested", [] {
    WaylandPeer peer(WaylandPeer::Mode::Normal,
                     {{"text/html", {'<', 'b', '>'}},
                      {"custom/secret", {1, 2}},
                      {"text/plain;charset=utf-8", {'a'}}});
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    auto s = take(b.read_snapshot());
    CHECK(s.representations.size() == 1 && peer.receives == 1);
  });
  test("transient_marker_prevents_all_fd_requests", [] {
    WaylandPeer peer(WaylandPeer::Mode::Normal,
                     {{"org.nspasteboard.TransientType", {}},
                      {"text/plain;charset=utf-8", {'a'}}});
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    CHECK(take(b.read_snapshot()).representations.empty());
    CHECK(peer.receives == 0);
  });
  test("unknown_source_exclusions_prevent_fd_requests", [] {
    WaylandPeer peer;
    ClipboardOptions o;
    o.display_name = peer.socket;
    o.policy.excluded_apps = {"private.app"};
    ClipboardWayland b(o);
    CHECK(take(b.read_snapshot()).representations.empty() &&
          peer.receives == 0);
  });
  test("multi_seat_requires_explicit_selection", [] {
    WaylandPeer peer(WaylandPeer::Mode::MultiSeat);
    ClipboardOptions o;
    o.display_name = peer.socket;
    ClipboardWayland b(o);
    CHECK(b.capabilities().capture.status == CapabilityStatus::Unavailable);
    o.seat_name = "seat0";
    ClipboardWayland explicit_seat(o);
    CHECK(explicit_seat.capabilities().capture.status ==
          CapabilityStatus::Available);
  });
  test("cancelled_fd_never_opens_history", [] {
    WaylandPeer peer(WaylandPeer::Mode::Cancel);
    ClipboardOptions o;
    o.display_name = peer.socket;
    auto path =
        std::filesystem::temp_directory_path() / "cliptiq-t06-cancel.sqlite";
    std::filesystem::remove(path);
    LinuxCaptureSession session(std::make_unique<ClipboardWayland>(o),
                                path.string());
    bool failed = false;
    CHECK(session.start({}, {}, [&](Result<CaptureEvent> r) { failed = !r; }));
    auto end = Clock::now() + std::chrono::seconds(2);
    while (!failed && Clock::now() < end)
      CHECK(session.dispatch(5));
    session.stop();
    CHECK(failed && !std::filesystem::exists(path));
  });
  test("kde_sensitive_hint_blocks_payload_and_database", [] {
    WaylandPeer peer(
        WaylandPeer::Mode::Normal,
        {{"text/plain;charset=utf-8", {'s'}},
         {"x-kde-passwordManagerHint", {'s', 'e', 'c', 'r', 'e', 't'}}});
    ClipboardOptions o;
    o.display_name = peer.socket;
    CapturePolicy policy;
    policy.ignore_concealed = true;
    o.policy = policy;
    auto path = std::filesystem::temp_directory_path() /
                "cliptiq-t06-wayland-secret.sqlite";
    std::filesystem::remove(path);
    LinuxCaptureSession session(std::make_unique<ClipboardWayland>(o),
                                path.string());
    bool rejected = false;
    CHECK(session.start(policy, {}, [&](Result<CaptureEvent> r) {
      rejected = bool(r) && r.value().disposition == CaptureDisposition::Reject;
    }));
    auto end = Clock::now() + std::chrono::seconds(2);
    while (!rejected && Clock::now() < end)
      CHECK(session.dispatch(5));
    session.stop();
    CHECK(rejected && peer.receives == 0 && !std::filesystem::exists(path));
  });
  test("actual_32_mib_plus_one_fd_rejected", [] {
    WaylandPeer peer(
        WaylandPeer::Mode::Normal,
        {{"application/pdf", Bytes(32ull * 1024 * 1024 + 1, 'p')}});
    ClipboardOptions o;
    o.display_name = peer.socket;
    o.timeout_ms = 10000;
    ClipboardWayland b(o);
    auto r = b.read_snapshot();
    CHECK(!r && r.error().code == ErrorCode::TooLarge);
  });
  return failures ? 1 : 0;
}

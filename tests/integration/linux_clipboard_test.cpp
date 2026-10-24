#include "Transfer.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
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
int main() {
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
  test("stream_bytes_and_complete_eof", [] {
    StreamBuffer s(8, Clock::now() + std::chrono::seconds(1), 3);
    const std::uint8_t a[] = {1, 0, 2};
    ok(s.append(a, 3, 3));
    CHECK(take(s.finish(3)) == Bytes({1, 0, 2}));
  });
  test("stream_overflow_is_sticky", [] {
    StreamBuffer s(2, Clock::now() + std::chrono::seconds(1), 1);
    const std::uint8_t a[] = {1, 2, 3};
    auto r = s.append(a, 3, 1);
    CHECK(!r && r.error().code == ErrorCode::TooLarge);
    CHECK(!s.finish(1));
  });
  test("stream_generation_change_cancels_partial", [] {
    StreamBuffer s(8, Clock::now() + std::chrono::seconds(1), 1);
    const std::uint8_t a[] = {1};
    ok(s.append(a, 1, 1));
    auto r = s.finish(2);
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("stream_timeout_never_returns_partial", [] {
    StreamBuffer s(8, Clock::now() - std::chrono::seconds(1), 1);
    auto r = s.finish(1);
    CHECK(!r && r.error().code == ErrorCode::Busy);
  });
  test("mime_stream_over_32_mib_rejected", [] {
    StreamBuffer s(Limits{}.representation_bytes,
                   Clock::now() + std::chrono::seconds(10), 1);
    Bytes block(1024 * 1024, 1);
    for (int i = 0; i < 32; ++i)
      ok(s.append(block.data(), block.size(), 1));
    auto r = s.append(block.data(), 1, 1);
    CHECK(!r && r.error().code == ErrorCode::TooLarge);
  });
  test("fd_receiver_nonblocking_eof", [] {
    int fd[2];
    CHECK(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0);
    FdReceiver input(fd[0], 8, Clock::now() + std::chrono::seconds(1), 1);
    CHECK(!input.step(1));
    CHECK(write(fd[1], "abc", 3) == 3);
    close(fd[1]);
    auto r = input.step(1);
    CHECK(r && take(std::move(*r)) == Bytes({'a', 'b', 'c'}));
  });
  test("fd_send_closed_receiver_survives_sigpipe", [] {
    int fd[2];
    CHECK(pipe2(fd, O_NONBLOCK | O_CLOEXEC) == 0);
    close(fd[0]);
    FdSendQueue out;
    ok(out.add(fd[1], std::make_shared<const Bytes>(Bytes{1, 2}),
               Clock::now() + std::chrono::seconds(1)));
    out.step();
    CHECK(out.fds().empty());
  });
  test("gnome_cut_replay_becomes_copy", [] {
    ClipboardSnapshot s;
    s.representations = {{0, "x11", "x-special/gnome-copied-files",
                          CanonicalKind::Files,
                          Bytes{'c', 'u', 't', '\n', 'f', 'i', 'l', 'e', ':',
                                '/', '/', '/', 'a', '\n'}}};
    auto r = take(prepare_replay(s, {}, true));
    CHECK(r.representations[0].data ==
          Bytes({'c', 'o', 'p', 'y', '\n', 'f', 'i', 'l', 'e', ':', '/', '/',
                 '/', 'a', '\n'}));
  });
  test("binary_rtf_preserved", [] {
    Representation r{0, "wayland", "text/rtf", CanonicalKind::Rtf,
                     Bytes{'{', 0, 1, '}'}};
    auto expected = r.data;
    ok(validate_payload(r));
    CHECK(r.data == expected);
  });
  test("unknown_type_not_persistable", [] {
    auto c = native_capabilities(false, true);
    ClipboardSnapshot s;
    s.representations = {
        {0, "wayland", "custom/secret", CanonicalKind::Text, Bytes{'x'}}};
    CHECK(evaluate_capture(s, {}, c).disposition == CaptureDisposition::Reject);
  });
  test("wayland_unknown_source_exclusions", [] {
    auto c = native_capabilities(false, true);
    CHECK(c.platform == CapturePlatform::Wayland &&
          c.source_app.status == CapabilityStatus::Unavailable);
    ClipboardSnapshot s;
    s.representations = {{0, "wayland", "text/plain;charset=utf-8",
                          CanonicalKind::Text, Bytes{'x'}}};
    CapturePolicy p;
    p.excluded_apps = {"app.private"};
    CHECK(evaluate_capture(s, p, c).reason == CaptureReason::UnknownSource);
  });
  test("markers_precede_payload_filtering", [] {
    auto c = native_capabilities(true, true);
    ClipboardSnapshot s;
    s.offered_types = {{"x11", "org.nspasteboard.TransientType"}};
    s.representations = {
        {0, "x11", "UTF8_STRING", CanonicalKind::Text, Bytes{'x'}}};
    s.flags = native_flags(s.offered_types);
    CHECK(evaluate_capture(s, {}, c).reason == CaptureReason::Transient);
  });
  test("no_display_capability_fails_closed", [] {
    ClipboardOptions o;
    o.display_name = "cliptiq-does-not-exist";
    ClipboardWayland b(o);
    CHECK(b.capabilities().platform == CapturePlatform::Wayland);
    CHECK(b.capabilities().capture.status == CapabilityStatus::Unavailable);
    CHECK(!b.read_snapshot());
  });
  test("stalled_sends_do_not_retain_unrequested_snapshot_formats", [] {
    FdSendQueue queue;
    std::vector<int> readers;
    for (unsigned i = 0; i < 8; ++i) {
      auto parent = std::make_shared<ClipboardSnapshot>();
      parent->representations = {{0, "wayland", "text/plain",
                                  CanonicalKind::Text, Bytes(128 * 1024, 'x')},
                                 {0, "wayland", "application/pdf",
                                  CanonicalKind::Pdf,
                                  Bytes(8 * 1024 * 1024, 'p')}};
      std::weak_ptr<ClipboardSnapshot> old = parent;
      auto alias = std::shared_ptr<const Bytes>(
          parent, &parent->representations[0].data);
      int fd[2];
      CHECK(pipe(fd) == 0);
      readers.push_back(fd[0]);
      ok(queue.add(fd[1], alias, Clock::now() + std::chrono::seconds(5)));
      parent.reset();
      alias.reset();
      CHECK(old.expired());
      queue.step();
    }
    CHECK(queue.fds().size() == 8);
    int fd[2];
    CHECK(pipe(fd) == 0);
    auto denied = queue.add(fd[1], std::make_shared<const Bytes>(1, 'x'),
                            Clock::now() + std::chrono::seconds(5));
    CHECK(!denied && denied.error().code == ErrorCode::TooLarge);
    close(fd[0]);
    queue.clear();
    for (auto fd : readers)
      close(fd);
  });
  return failures ? 1 : 0;
}

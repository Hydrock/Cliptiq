#pragma once
#include "cliptiq/clipboard.hpp"
#include <memory>
namespace cliptiq::linux_backend {
struct ClipboardOptions {
  Limits limits;
  CapturePolicy policy;
  std::string display_name, seat_name;
  unsigned timeout_ms = 2000;
};
class LinuxClipboardBackend : public ClipboardBackend {
public:
  virtual Result<void> dispatch(unsigned timeout_ms = 0) = 0;
  virtual int connection_fd() const = 0;
  virtual Result<void> set_capture_policy(CapturePolicy) = 0;
};
#define CLIPTIQ_LINUX_BACKEND(Name)                                            \
  class Name final : public LinuxClipboardBackend {                            \
    struct State;                                                              \
    std::shared_ptr<State> state_;                                             \
                                                                               \
  public:                                                                      \
    explicit Name(ClipboardOptions = {});                                      \
    ~Name() override;                                                          \
    Name(const Name &) = delete;                                               \
    Name &operator=(const Name &) = delete;                                    \
    Capabilities capabilities() const override;                                \
    Result<ClipboardSnapshot> read_snapshot() override;                        \
    Result<std::string> write_snapshot(const ClipboardSnapshot &) override;    \
    Result<void> start(CaptureCallback) override;                              \
    void stop() override;                                                      \
    Result<void> dispatch(unsigned timeout_ms = 0) override;                   \
    int connection_fd() const override;                                        \
    Result<void> set_capture_policy(CapturePolicy) override;                   \
  };
CLIPTIQ_LINUX_BACKEND(ClipboardX11)
CLIPTIQ_LINUX_BACKEND(ClipboardWayland)
#undef CLIPTIQ_LINUX_BACKEND
// Auto never substitutes XWayland for missing native Wayland data-control.
std::unique_ptr<LinuxClipboardBackend> open_backend(ClipboardOptions = {});
} // namespace cliptiq::linux_backend

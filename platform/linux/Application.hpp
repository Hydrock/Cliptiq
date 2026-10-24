#pragma once
#include "Clipboard.hpp"
namespace cliptiq::linux_backend {
struct CaptureEvent {
  CaptureDisposition disposition;
  CaptureReason reason;
  std::optional<EntryId> entry_id;
};
using CaptureEventCallback = std::function<void(Result<CaptureEvent>)>;
struct CaptureQueueLimits {
  std::size_t max_pending_count = 2;
  std::uint64_t max_pending_bytes = 128ull * 1024 * 1024;
};
class LinuxCaptureSession {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  LinuxCaptureSession(std::unique_ptr<LinuxClipboardBackend>,
                      std::string database_path, Limits = {},
                      CaptureQueueLimits = {});
  ~LinuxCaptureSession();
  LinuxCaptureSession(const LinuxCaptureSession &) = delete;
  LinuxCaptureSession &operator=(const LinuxCaptureSession &) = delete;
  LinuxClipboardBackend &clipboard();
  Result<void> start(CapturePolicy, HistoryPolicy, CaptureEventCallback);
  Result<void> dispatch(unsigned timeout_ms = 0);
  void stop();
};
} // namespace cliptiq::linux_backend

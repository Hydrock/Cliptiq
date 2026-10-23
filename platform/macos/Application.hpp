#pragma once
#include "Clipboard.hpp"

namespace cliptiq::macos {
struct CaptureSummary {
    CaptureDisposition disposition;
    CaptureReason reason;
};
struct CaptureEvent {
    CaptureSummary decision;
    std::optional<EntryId> entry_id;
};
using CaptureEventCallback = std::function<void(Result<CaptureEvent>)>;
struct CaptureQueueLimits {
    std::size_t max_pending_count = 2;
    std::uint64_t max_pending_bytes = 128ull * 1024 * 1024;
};
class MacCaptureSession {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    MacCaptureSession(std::string database_path, std::string pasteboard_name = {}, Limits = {}, CaptureQueueLimits = {});
    ~MacCaptureSession();
    MacCaptureSession(const MacCaptureSession&) = delete;
    MacCaptureSession& operator=(const MacCaptureSession&) = delete;
    Result<void> start(CapturePolicy, HistoryPolicy, CaptureEventCallback);
    void stop();
    MacClipboardBackend& clipboard();
};
}

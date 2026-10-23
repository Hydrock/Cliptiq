#pragma once
#include "Clipboard.hpp"

namespace cliptiq::macos {
struct CaptureEvent {
    CaptureDecision decision;
    std::optional<EntryId> entry_id;
};
using CaptureEventCallback = std::function<void(Result<CaptureEvent>)>;
class MacCaptureSession {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    MacCaptureSession(std::string database_path, std::string pasteboard_name = {}, Limits = {});
    ~MacCaptureSession();
    MacCaptureSession(const MacCaptureSession&) = delete;
    MacCaptureSession& operator=(const MacCaptureSession&) = delete;
    Result<void> start(CapturePolicy, HistoryPolicy, CaptureEventCallback);
    void stop();
    MacClipboardBackend& clipboard();
};
}

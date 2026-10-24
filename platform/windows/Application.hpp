#pragma once
#include "Clipboard.hpp"
namespace cliptiq::windows {
struct CaptureEvent { CaptureDisposition disposition; CaptureReason reason; std::optional<EntryId> entry_id; };
using CaptureEventCallback=std::function<void(Result<CaptureEvent>)>;
struct CaptureQueueLimits { std::size_t max_pending_count=2; std::uint64_t max_pending_bytes=128ull*1024*1024; };
class WindowsCaptureSession {
 struct Impl;
 std::unique_ptr<Impl> impl_;
public:
 WindowsCaptureSession(std::string database_path,ClipboardOptions = {},CaptureQueueLimits = {});
 ~WindowsCaptureSession();
 WindowsCaptureSession(const WindowsCaptureSession&)=delete;
 WindowsCaptureSession& operator=(const WindowsCaptureSession&)=delete;
 WindowsClipboardBackend& clipboard();
 Result<void> start(CapturePolicy,HistoryPolicy,CaptureEventCallback);
 void stop();
};
}

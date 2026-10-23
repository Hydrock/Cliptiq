#pragma once
#include "cliptiq/clipboard.hpp"
#include <memory>
#ifdef __OBJC__
@class NSPasteboard;
#endif
namespace cliptiq::macos {
struct ClipboardOptions {
    Limits limits;
    CapturePolicy policy;
    // Foreground attribution is always downgraded to BestEffort, even for a
    // custom provider. Useful for sandbox integration or disabling attribution.
    std::function<SourceInfo()> source_reader;
};
class MacClipboardBackend final : public ClipboardBackend {
    struct State;
    std::shared_ptr<State> state_;
public:
    explicit MacClipboardBackend(std::string pasteboard_name = {}, ClipboardOptions = {});
#ifdef __OBJC__
    explicit MacClipboardBackend(NSPasteboard*, ClipboardOptions = {});
#endif
    ~MacClipboardBackend() override;
    MacClipboardBackend(const MacClipboardBackend&) = delete;
    MacClipboardBackend& operator=(const MacClipboardBackend&) = delete;
    Capabilities capabilities() const override;
    Result<ClipboardSnapshot> read_snapshot() override;
    // Only an explicit UI action may opt in to a system access prompt.
    Result<ClipboardSnapshot> read_snapshot(bool user_initiated);
    Result<std::string> write_snapshot(const ClipboardSnapshot&) override;
    Result<void> start(CaptureCallback) override;
    void stop() override;
    Result<void> set_capture_policy(CapturePolicy);
};
}

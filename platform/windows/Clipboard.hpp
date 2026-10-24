#pragma once
#include "cliptiq/clipboard.hpp"
#include <memory>

namespace cliptiq::windows {
struct ClipboardOptions {
 Limits limits;
 CapturePolicy policy;
 std::function<SourceInfo()> source_reader;
};
struct HtmlFragment { std::size_t begin, end; };
Result<HtmlFragment> html_fragment(const Bytes&);
struct DecodedImage { std::uint32_t width, height, stride; Bytes bgra; };
Result<DecodedImage> decode_image(const Representation&, std::uint64_t max_pixels=40000000);
class WindowsClipboardBackend final:public ClipboardBackend {
 struct State;
 std::shared_ptr<State> state_;
public:
 explicit WindowsClipboardBackend(ClipboardOptions = {});
 ~WindowsClipboardBackend() override;
 WindowsClipboardBackend(const WindowsClipboardBackend&)=delete;
 WindowsClipboardBackend& operator=(const WindowsClipboardBackend&)=delete;
 Capabilities capabilities()const override;
 Result<ClipboardSnapshot> read_snapshot()override;
 Result<std::string> write_snapshot(const ClipboardSnapshot&)override;
 Result<void> start(CaptureCallback)override;
 void stop()override;
 Result<void> set_capture_policy(CapturePolicy);
 Result<void> dispatch_to_owner(std::function<void()>);
};
}

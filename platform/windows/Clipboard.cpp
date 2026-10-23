#include "Clipboard.hpp"
namespace cliptiq::windows {
struct WindowsClipboardBackend::State{};
WindowsClipboardBackend::WindowsClipboardBackend(ClipboardOptions):state_(std::make_shared<State>()){}
WindowsClipboardBackend::~WindowsClipboardBackend()=default;
Capabilities WindowsClipboardBackend::capabilities()const{return {};}
Result<ClipboardSnapshot> WindowsClipboardBackend::read_snapshot(){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<std::string> WindowsClipboardBackend::write_snapshot(const ClipboardSnapshot&){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<void> WindowsClipboardBackend::start(CaptureCallback){return Error{ErrorCode::Unsupported,"Not implemented"};}
void WindowsClipboardBackend::stop(){}
Result<void> WindowsClipboardBackend::set_capture_policy(CapturePolicy){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<void> WindowsClipboardBackend::dispatch_to_owner(std::function<void()>){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<HtmlFragment> html_fragment(const Bytes&){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<DecodedImage> decode_image(const Representation&,std::uint64_t){return Error{ErrorCode::Unsupported,"Not implemented"};}
}

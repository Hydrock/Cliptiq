#import <AppKit/AppKit.h>
#include "Clipboard.hpp"
namespace cliptiq::macos {
struct MacClipboardBackend::State {};
MacClipboardBackend::MacClipboardBackend(std::string,ClipboardOptions):state_(std::make_shared<State>()){}
MacClipboardBackend::MacClipboardBackend(NSPasteboard*,ClipboardOptions):state_(std::make_shared<State>()){}
MacClipboardBackend::~MacClipboardBackend()=default;
Capabilities MacClipboardBackend::capabilities() const {Capabilities c;c.platform=CapturePlatform::MacOS;return c;}
Result<ClipboardSnapshot> MacClipboardBackend::read_snapshot(){return Error{ErrorCode::Internal,"macOS backend not implemented"};}
Result<ClipboardSnapshot> MacClipboardBackend::read_snapshot(bool){return read_snapshot();}
Result<std::string> MacClipboardBackend::write_snapshot(const ClipboardSnapshot&){return Error{ErrorCode::Internal,"macOS backend not implemented"};}
Result<void> MacClipboardBackend::start(CaptureCallback){return Error{ErrorCode::Internal,"macOS backend not implemented"};}
void MacClipboardBackend::stop(){}
Result<void> MacClipboardBackend::set_capture_policy(CapturePolicy){return {};}
}

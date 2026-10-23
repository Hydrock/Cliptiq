#include "Application.hpp"
namespace cliptiq::macos {
struct MacCaptureSession::Impl {MacClipboardBackend backend;};
MacCaptureSession::MacCaptureSession(std::string,std::string,Limits):impl_(new Impl){}
MacCaptureSession::~MacCaptureSession()=default;
Result<void> MacCaptureSession::start(CapturePolicy,HistoryPolicy,CaptureEventCallback){return Error{ErrorCode::Internal,"macOS session not implemented"};}
void MacCaptureSession::stop(){}
MacClipboardBackend& MacCaptureSession::clipboard(){return impl_->backend;}
}

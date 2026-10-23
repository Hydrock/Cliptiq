#include "Application.hpp"
namespace cliptiq::windows {
struct WindowsCaptureSession::Impl{WindowsClipboardBackend backend;};
WindowsCaptureSession::WindowsCaptureSession(std::string,ClipboardOptions,CaptureQueueLimits):impl_(new Impl){}
WindowsCaptureSession::~WindowsCaptureSession()=default;
WindowsClipboardBackend& WindowsCaptureSession::clipboard(){return impl_->backend;}
Result<void> WindowsCaptureSession::start(CapturePolicy,HistoryPolicy,CaptureEventCallback){return Error{ErrorCode::Unsupported,"Session not implemented"};}
void WindowsCaptureSession::stop(){}
}

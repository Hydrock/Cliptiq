#include "Clipboard.hpp"
namespace cliptiq::linux_backend {
struct ClipboardWayland::State{};
ClipboardWayland::ClipboardWayland(ClipboardOptions):state_(std::make_shared<State>()){}
ClipboardWayland::~ClipboardWayland()=default;
Capabilities ClipboardWayland::capabilities()const{return {};}
Result<ClipboardSnapshot> ClipboardWayland::read_snapshot(){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<std::string> ClipboardWayland::write_snapshot(const ClipboardSnapshot&){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<void> ClipboardWayland::start(CaptureCallback){return Error{ErrorCode::Unsupported,"Not implemented"};}
void ClipboardWayland::stop(){}
Result<void> ClipboardWayland::dispatch(unsigned){return Error{ErrorCode::Unsupported,"Not implemented"};}
int ClipboardWayland::connection_fd()const{return -1;}
Result<void> ClipboardWayland::set_capture_policy(CapturePolicy){return Error{ErrorCode::Unsupported,"Not implemented"};}
}

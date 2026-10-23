#include "Clipboard.hpp"
namespace cliptiq::linux_backend {
struct ClipboardX11::State{};
ClipboardX11::ClipboardX11(ClipboardOptions):state_(std::make_shared<State>()){}
ClipboardX11::~ClipboardX11()=default;
Capabilities ClipboardX11::capabilities()const{return {};}
Result<ClipboardSnapshot> ClipboardX11::read_snapshot(){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<std::string> ClipboardX11::write_snapshot(const ClipboardSnapshot&){return Error{ErrorCode::Unsupported,"Not implemented"};}
Result<void> ClipboardX11::start(CaptureCallback){return Error{ErrorCode::Unsupported,"Not implemented"};}
void ClipboardX11::stop(){}
Result<void> ClipboardX11::dispatch(unsigned){return Error{ErrorCode::Unsupported,"Not implemented"};}
int ClipboardX11::connection_fd()const{return -1;}
Result<void> ClipboardX11::set_capture_policy(CapturePolicy){return Error{ErrorCode::Unsupported,"Not implemented"};}
}

#include "cliptiq/settings.hpp"
namespace cliptiq {
struct NativeSettings::State {};
NativeSettings::NativeSettings():state_(std::make_shared<State>()){}
NativeSettings::~NativeSettings()=default;
Result<void> NativeSettings::show(Settings,const Capabilities &,std::function<Result<void>(Settings)>){return Error{ErrorCode::Unsupported,"T09 native UI not implemented"};}
Result<void> NativeSettings::process_events(){return {};}
std::uintptr_t NativeSettings::native_handle()const{return 0;}
bool NativeSettings::confirm_clear()const{return false;}
}

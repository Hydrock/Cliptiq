#include "cliptiq/hotkeys.hpp"
namespace cliptiq {
struct NativeHotkeys::State {};
NativeHotkeys::NativeHotkeys(std::function<void(ActionId,InvocationContext)>):state_(std::make_unique<State>()){}
NativeHotkeys::~NativeHotkeys()=default;
Result<unsigned> NativeHotkeys::acquire(const ActionId &,const KeyChord &){return Error{ErrorCode::Unsupported,"T09 native bindings not implemented"};}
void NativeHotkeys::release(unsigned){}
Result<void> NativeHotkeys::dispatch(){return {};}
Capability NativeHotkeys::capability()const{return {};}
}

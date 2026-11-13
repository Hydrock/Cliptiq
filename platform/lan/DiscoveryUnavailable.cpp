#include "NativeDiscovery.hpp"
namespace cliptiq::lan {
struct NativeDiscovery::Impl{};
NativeDiscovery::NativeDiscovery(std::string,bool):impl_(std::make_unique<Impl>()){}
NativeDiscovery::~NativeDiscovery()=default;
NativeDiscovery::Capability NativeDiscovery::capability()const{return {false,"Device discovery is unavailable on this platform. Use manual connection."};}
Result<void> NativeDiscovery::publish(std::optional<DiscoveryAnnouncement> a){if(!a)return {};return Error{ErrorCode::Unsupported,capability().message};}
Result<void> NativeDiscovery::browse(std::int64_t){return Error{ErrorCode::Unsupported,capability().message};}
void NativeDiscovery::stop_browse(){}std::vector<DiscoveryEvent> NativeDiscovery::poll(){return {};}
void NativeDiscovery::stop(){}
}

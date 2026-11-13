#include "EndpointProbe.hpp"
#include <mutex>
#include <chrono>
#include <mbedtls/platform_util.h>
namespace cliptiq::lan {
struct EndpointProbe::State {Transport transport;std::mutex mutex;bool verified=false,started=false;std::chrono::steady_clock::time_point deadline;};
EndpointProbe::EndpointProbe():state_(std::make_unique<State>()){}EndpointProbe::~EndpointProbe(){state_->transport.stop();}
Result<void> EndpointProbe::start(const Config& config,const Peer& peer,const DiscoveryDevice& device,std::string key){
 auto fingerprint=key_fingerprint(key);if(!fingerprint||fingerprint.value()!=peer.auth_id||peer.remote_device_id!=device.device_id||!valid_numeric_endpoint(device.address,device.interface_index)||!device.data_port){if(!key.empty())mbedtls_platform_zeroize(key.data(),key.size());return Error{ErrorCode::InvalidArgument,"Invalid endpoint verification"};}
 Bytes bytes;auto digit=[](char c){return c<='9'?c-'0':(c>='a'?c-'a':c-'A')+10;};for(size_t i=0;i<key.size();i+=2)bytes.push_back(static_cast<std::uint8_t>((digit(key[i])<<4)|digit(key[i+1])));mbedtls_platform_zeroize(key.data(),key.size());
 auto address=device.address;if(address.rfind("fe",0)==0&&address.find(':')!=std::string::npos&&address.find('%')==std::string::npos)address+="%"+std::to_string(device.interface_index);
 TransportConfig probe;probe.device_id=config.device_id;probe.bind_address=config.bind_address;probe.port=config.port;probe.probe_only=true;probe.peers.push_back({peer.id,address,peer.remote_device_id,peer.auth_id,device.data_port,std::move(bytes),true,false});
 TransportCallbacks callbacks;callbacks.identity=[s=state_.get(),expected=peer.remote_device_id](std::string,std::string id)->Result<void>{if(id!=expected)return Error{ErrorCode::InvalidArgument,"Peer identity mismatch"};std::lock_guard<std::mutex> lock(s->mutex);s->verified=true;return {};};auto result=state_->transport.start(std::move(probe),std::move(callbacks));if(result){state_->started=true;state_->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);}return result;
}
std::optional<Result<void>> EndpointProbe::poll(){if(!state_->started)return {};bool verified;{std::lock_guard<std::mutex> lock(state_->mutex);verified=state_->verified;}if(verified){state_->transport.stop();state_->started=false;return Result<void>{};}if(std::chrono::steady_clock::now()>=state_->deadline){state_->transport.stop();state_->started=false;return Error{ErrorCode::Io,"Device endpoint could not be verified."};}return {};}
}

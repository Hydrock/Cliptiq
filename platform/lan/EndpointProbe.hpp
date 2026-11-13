#pragma once
#include "Transport.hpp"
#include "Discovery.hpp"
namespace cliptiq::lan {
// TLS/HELLO only: no listener, clipboard callbacks, or publication API.
class EndpointProbe {
 struct State;std::unique_ptr<State> state_;
public:
 EndpointProbe();~EndpointProbe();
 Result<void> start(const Config&,const Peer&,const DiscoveryDevice&,std::string key64hex);
 std::optional<Result<void>> poll();
};
}

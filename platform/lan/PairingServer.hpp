#pragma once
#include "PairingSession.hpp"
#include "Discovery.hpp"
namespace cliptiq::lan {
class PairingServer {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 PairingServer(std::string device_id,std::string name,std::string platform,std::uint16_t data_port=49731);
 ~PairingServer();
 Result<std::uint16_t> listen(std::string local_address,std::uint16_t data_port,std::uint32_t interface_index=0);
 Result<std::string> connect(DiscoveryDevice);
 Result<void> accept(const std::string&);Result<void> enter_code(const std::string&,std::string);
 void prepared(const std::string&,Result<void>);void committed(const std::string&,Result<void>);
 void cancel(const std::string&);std::vector<PairingEvent> poll();void stop();
};
}

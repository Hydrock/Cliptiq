#pragma once
#include "Service.hpp"
#include "NativeDiscovery.hpp"
#include "PairingServer.hpp"
#include "NetworkInterfaces.hpp"
namespace cliptiq::lan {
struct DiscoverySnapshot {
 bool available=false,enabled=false,published=false,searching=false;std::string message,interface_id;
 std::vector<NetworkInterface> interfaces;std::vector<DiscoveryDevice> devices;std::vector<Peer> peers;
 std::optional<PairingEvent::Kind> pairing;std::string session_id,peer_name,code;
};
struct DiscoveryHooks {
 std::function<NativeDiscovery::Capability()> capability;
 std::function<Result<void>(std::optional<DiscoveryAnnouncement>)> publish;
 std::function<Result<void>()> browse;std::function<void()> stop_browse,stop_pairing;
 std::function<std::vector<DiscoveryEvent>()> discovery_events;
 std::function<Result<std::vector<NetworkInterface>>()> interfaces;
 std::function<Result<std::uint16_t>(const NetworkInterface&,std::uint16_t)> listen;
 std::function<Result<std::string>(DiscoveryDevice)> connect;
 std::function<Result<void>(std::string)> accept;
 std::function<Result<void>(std::string,std::string)> enter_code;
 std::function<void(std::string,Result<void>)> prepared,committed;
 std::function<void(std::string)> cancel;
 std::function<std::vector<PairingEvent>()> pairing_events;
 std::function<Result<void>(std::string,std::string)> write_secret;
 std::function<Result<void>(std::string)> erase_secret;
 std::function<Result<void>()> refresh;
 std::function<std::int64_t()> steady_clock,wall_clock;
};
class DiscoveryService {
 struct State;std::unique_ptr<State> state_;
public:
 DiscoveryService(Store&,Service&,CapturePlatform);
 DiscoveryService(Store&,DiscoveryHooks);~DiscoveryService();
 Result<void> initialize();DiscoverySnapshot snapshot() const;
 Result<void> available(bool);Result<void> select_interface(std::string);
 Result<void> search();void stop_search();Result<void> connect(const std::string& device_id);
 Result<void> accept(const std::string&);Result<void> enter_code(const std::string&,std::string);
 void cancel(const std::string&);bool poll();void close_view();void stop();
};
}

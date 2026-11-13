#pragma once
#include "lan/DiscoveryService.hpp"
namespace cliptiq {
struct DiscoveryUiCallbacks {
 std::function<lan::DiscoverySnapshot()> snapshot;
 std::function<Result<void>(bool)> available;
 std::function<Result<void>(std::string)> select_interface;
 std::function<Result<void>()> search;std::function<void()> stop_search;
 std::function<Result<void>(std::string)> connect,accept;
 std::function<Result<void>(std::string,std::string)> enter_code;
 std::function<void(std::string)> cancel;std::function<void()> close_view;
};
inline std::string discovery_signature(const lan::DiscoverySnapshot& s){
 std::string out;auto field=[&](const std::string& v){out+=std::to_string(v.size())+":"+v;};
 field(std::to_string(s.available)+std::to_string(s.enabled)+std::to_string(s.published)+std::to_string(s.searching));field(s.message);field(s.interface_id);field(s.session_id);field(s.peer_name);field(s.code);field(s.pairing?std::to_string(static_cast<int>(*s.pairing)):"");
 for(const auto& i:s.interfaces){field(i.id);field(i.name);field(i.address);}for(const auto& d:s.devices){field(d.device_id);field(d.name);field(d.platform);}for(const auto& p:s.peers){field(p.id);field(p.name);field(p.remote_device_id);field(std::to_string(p.enabled));}return out;
}
}

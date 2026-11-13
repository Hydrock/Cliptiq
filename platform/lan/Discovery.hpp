#pragma once
#include "cliptiq/lan.hpp"
#include <map>
namespace cliptiq::lan {
struct DiscoveryDevice {
 std::string device_id,name,platform,host,address,interface_address;
 std::uint32_t interface_index=0;
 std::uint16_t control_port=0,data_port=0;
 std::int64_t expires_ms=0;
};
class DiscoveryRegistry {
 std::string local_id_;
 std::map<std::string,std::vector<DiscoveryDevice>> entries_;
public:
 explicit DiscoveryRegistry(std::string local_id):local_id_(std::move(local_id)){}
 Result<void> update(DiscoveryDevice,std::int64_t now_ms);
 void remove(const std::string& device_id,std::uint32_t interface_index);
 void expire(std::int64_t now_ms);
 std::vector<DiscoveryDevice> devices() const;
 std::vector<DiscoveryDevice> endpoints(const std::string& device_id) const;
};
}

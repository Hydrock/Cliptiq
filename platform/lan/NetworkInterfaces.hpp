#pragma once
#include "Discovery.hpp"
namespace cliptiq::lan {
struct NetworkInterface {std::string id,name,address;std::uint32_t index=0;};
Result<std::vector<NetworkInterface>> network_interfaces();
std::string local_device_name();
}

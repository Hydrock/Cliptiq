#include "Discovery.hpp"
#include "Text.hpp"
#include <algorithm>
#include <climits>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
namespace cliptiq::lan {
bool valid_numeric_endpoint(const std::string& value,std::uint32_t index){
 if(value.empty()||value.size()>128)return false;
 auto address=value;auto zone=address.find('%');
 if(zone!=std::string::npos){
  auto scope=address.substr(zone+1);address.resize(zone);
  if(!index||scope.empty()||scope.size()>32||!std::all_of(scope.begin(),scope.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';}))return false;
 }
 in_addr v4{};if(zone==std::string::npos&&inet_pton(AF_INET,address.c_str(),&v4)==1){auto first=ntohl(v4.s_addr)>>24;return first!=0&&first<224;}
 in6_addr v6{};if(inet_pton(AF_INET6,address.c_str(),&v6)!=1)return false;
 if(v6.s6_addr[0]==0xff||std::all_of(v6.s6_addr,v6.s6_addr+16,[](unsigned char b){return b==0;}))return false;
 const bool mapped=std::all_of(v6.s6_addr,v6.s6_addr+10,[](unsigned char b){return b==0;})&&v6.s6_addr[10]==0xff&&v6.s6_addr[11]==0xff;
 if(mapped&&(v6.s6_addr[12]==0||v6.s6_addr[12]>=224))return false;
 return !(v6.s6_addr[0]==0xfe && (v6.s6_addr[1]&0xc0)==0x80) || index!=0;
}
Result<void> DiscoveryRegistry::update(DiscoveryDevice d,std::int64_t now){
 if(now<0||now>INT64_MAX-30000||d.expires_ms<=now||!valid_device_id(d.device_id)||d.device_id==local_id_||
    !valid_display_text(d.name,256)||!valid_display_text(d.platform,32)||!valid_display_text(d.host,253)||
    !d.control_port||!d.data_port||!valid_numeric_endpoint(d.address,d.interface_index)||!valid_numeric_endpoint(d.interface_address,d.interface_index))
  return Error{ErrorCode::InvalidArgument,"Invalid discovered device"};
 expire(now);
 auto found=entries_.find(d.device_id);
 if(found==entries_.end()&&entries_.size()>=64)return Error{ErrorCode::TooLarge,"Discovery device limit reached"};
 d.expires_ms=std::min(d.expires_ms,now+30000);
 auto& endpoints=entries_[d.device_id];
 auto existing=std::find_if(endpoints.begin(),endpoints.end(),[&](const DiscoveryDevice& old){
  return old.interface_index==d.interface_index&&old.address==d.address&&old.control_port==d.control_port;
 });
 if(existing!=endpoints.end())*existing=std::move(d);
 else {if(endpoints.size()>=4)return Error{ErrorCode::TooLarge,"Discovery endpoint limit reached"};endpoints.push_back(std::move(d));}
 return {};
}
void DiscoveryRegistry::remove(const std::string& id,std::uint32_t index){
 auto it=entries_.find(id);if(it==entries_.end())return;
 auto& values=it->second;values.erase(std::remove_if(values.begin(),values.end(),[&](const auto& v){return v.interface_index==index;}),values.end());
 if(values.empty())entries_.erase(it);
}
void DiscoveryRegistry::expire(std::int64_t now){
 for(auto it=entries_.begin();it!=entries_.end();){auto& values=it->second;
  values.erase(std::remove_if(values.begin(),values.end(),[&](const auto& d){return d.expires_ms<=now;}),values.end());
  if(values.empty())it=entries_.erase(it);else ++it;
 }
}
std::vector<DiscoveryDevice> DiscoveryRegistry::devices() const {
 std::vector<DiscoveryDevice> result;for(const auto& p:entries_)if(!p.second.empty())result.push_back(p.second.front());
 std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name==b.name?a.device_id<b.device_id:a.name<b.name;});return result;
}
std::vector<DiscoveryDevice> DiscoveryRegistry::endpoints(const std::string& id) const {
 auto found=entries_.find(id);return found==entries_.end()?std::vector<DiscoveryDevice>{}:found->second;
}
}

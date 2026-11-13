#include "NetworkInterfaces.hpp"
#include <algorithm>
#include <map>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
namespace cliptiq::lan {
Result<std::vector<NetworkInterface>> network_interfaces(){
 std::map<std::string,NetworkInterface> unique;
 auto add=[&](std::string id,std::string name,std::string address,std::uint32_t index){if(!valid_numeric_endpoint(address,index)||address.rfind("127.",0)==0||address=="::1")return;auto it=unique.find(id);if(it==unique.end()||(it->second.address.find(':')!=std::string::npos&&address.find(':')==std::string::npos))unique[id]={id,std::move(name),std::move(address),index};};
#ifdef _WIN32
 WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)return Error{ErrorCode::Io,"Windows networking unavailable"};
 ULONG size=16384;std::vector<unsigned char> storage(size);auto* adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());auto status=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,adapters,&size);if(status==ERROR_BUFFER_OVERFLOW&&size<=65536){storage.resize(size);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());status=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,adapters,&size);}if(status!=NO_ERROR){WSACleanup();return Error{ErrorCode::Io,"Cannot list local network interfaces"};}
 for(auto* a=adapters;a;a=a->Next){if(a->OperStatus!=IfOperStatusUp||a->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;std::string name=a->AdapterName;int count=WideCharToMultiByte(CP_UTF8,0,a->FriendlyName,-1,nullptr,0,nullptr,nullptr);if(count>1&&count<=512){std::string utf8(count,0);WideCharToMultiByte(CP_UTF8,0,a->FriendlyName,-1,utf8.data(),count,nullptr,nullptr);utf8.pop_back();name=std::move(utf8);}for(auto* p=a->FirstUnicastAddress;p;p=p->Next){auto* sa=p->Address.lpSockaddr;if(!sa||(sa->sa_family!=AF_INET&&sa->sa_family!=AF_INET6))continue;char text[INET6_ADDRSTRLEN]{};const void* raw=sa->sa_family==AF_INET?static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(sa)->sin_addr):static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr);if(inet_ntop(sa->sa_family,raw,text,sizeof text))add(a->AdapterName,name,text,sa->sa_family==AF_INET?a->IfIndex:a->Ipv6IfIndex);}}
 WSACleanup();
#else
 ifaddrs* all=nullptr;if(getifaddrs(&all)!=0)return Error{ErrorCode::Io,"Cannot list local network interfaces"};
 for(auto* it=all;it;it=it->ifa_next){if(!it->ifa_addr||!(it->ifa_flags&IFF_UP)||(it->ifa_flags&IFF_LOOPBACK))continue;auto* sa=it->ifa_addr;if(sa->sa_family!=AF_INET&&sa->sa_family!=AF_INET6)continue;char text[INET6_ADDRSTRLEN]{};const void* raw=sa->sa_family==AF_INET?static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(sa)->sin_addr):static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr);if(inet_ntop(sa->sa_family,raw,text,sizeof text))add(it->ifa_name,it->ifa_name,text,if_nametoindex(it->ifa_name));}freeifaddrs(all);
#endif
 std::vector<NetworkInterface> result;for(auto& item:unique)if(result.size()<32)result.push_back(std::move(item.second));return result;
}
}

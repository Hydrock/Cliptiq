#include "NativeDiscovery.hpp"
#include <dns_sd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/select.h>
#include <chrono>
#include <algorithm>
#include <map>
#include <set>
namespace cliptiq::lan {
namespace {
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string local_address(std::uint32_t index,int family){
 ifaddrs* all=nullptr;if(getifaddrs(&all)!=0)return {};std::string result;
 for(auto* it=all;it;it=it->ifa_next){if(!it->ifa_addr||it->ifa_addr->sa_family!=family||if_nametoindex(it->ifa_name)!=index)continue;
  char text[INET6_ADDRSTRLEN]{};const void* address=family==AF_INET?static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(it->ifa_addr)->sin_addr):static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(it->ifa_addr)->sin6_addr);
  if(inet_ntop(family,address,text,sizeof text)&&valid_numeric_endpoint(text,index)){result=text;break;}
 }freeifaddrs(all);return result;
}
}
struct NativeDiscovery::Impl {
 enum class Kind {Publish,Browse,Resolve,Address};
 struct Op {Impl* owner;Kind kind;DNSServiceRef ref=nullptr;std::string key;DiscoveryDevice device;Bytes txt;bool done=false;~Op(){if(ref)DNSServiceRefDeallocate(ref);}};
 std::string id;bool local_only;DiscoveryInbox inbox,publication;std::uint64_t generation=0;std::int64_t deadline=0;
 std::vector<std::unique_ptr<Op>> ops;std::map<std::string,std::vector<DiscoveryDevice>> found;std::set<std::string> resolving;
 Impl(std::string id,bool local):id(std::move(id)),local_only(local),inbox(this->id),publication(this->id){publication.begin();}
 void failure(DNSServiceErrorType code,bool pub=false){(pub?publication:inbox).accept(pub?1:generation,{DiscoveryEvent::Kind::Failure,{},code==kDNSServiceErr_PolicyDenied?"Local network access was denied. Allow Cliptiq in System Settings.":"Bonjour discovery failed ("+std::to_string(code)+")."});}
 std::unique_ptr<Op> operation(Kind k,std::string key={}){auto op=std::make_unique<Op>();op->owner=this;op->kind=k;op->key=std::move(key);return op;}
 static void registered(DNSServiceRef,DNSServiceFlags,DNSServiceErrorType e,const char*,const char*,const char*,void* context){auto& op=*static_cast<Op*>(context);if(e){op.owner->failure(e,true);op.done=true;}}
 static void browsed(DNSServiceRef,DNSServiceFlags flags,std::uint32_t index,DNSServiceErrorType e,const char* name,const char* type,const char* domain,void* context){
  auto& self=*static_cast<Op*>(context)->owner;if(e){self.failure(e);return;}
  std::string key=std::to_string(index)+"/"+name+"/"+type+"/"+domain;
  if(!(flags&kDNSServiceFlagsAdd)){
   auto it=self.found.find(key);if(it!=self.found.end()){for(auto& d:it->second)self.inbox.accept(self.generation,{DiscoveryEvent::Kind::Removed,d,{}});self.found.erase(it);}
   for(auto& op:self.ops)if(op->key==key)op->done=true;self.resolving.erase(key);return;
  }
  if(self.ops.size()>=128||!self.resolving.insert(key).second)return;
  auto op=self.operation(Kind::Resolve,key);auto* raw=op.get();auto result=DNSServiceResolve(&op->ref,0,index,name,type,domain,resolved,raw);
  if(result){self.failure(result);self.resolving.erase(key);}else self.ops.push_back(std::move(op));
 }
 static void resolved(DNSServiceRef,DNSServiceFlags,std::uint32_t index,DNSServiceErrorType e,const char*,const char* host,std::uint16_t port,std::uint16_t size,const unsigned char* txt,void* context){
  auto& op=*static_cast<Op*>(context);auto& self=*op.owner;op.done=true;
  if(e){self.failure(e);self.resolving.erase(op.key);return;}
  if(size>1024||!host||self.ops.size()>=128)return;
  auto address=self.operation(Kind::Address,op.key);address->device.host=host;address->device.interface_index=index;address->device.control_port=ntohs(port);address->txt.assign(txt,txt+size);
  auto result=DNSServiceGetAddrInfo(&address->ref,0,index,kDNSServiceProtocol_IPv4|kDNSServiceProtocol_IPv6,host,addressed,address.get());
  if(result){self.failure(result);self.resolving.erase(op.key);}else self.ops.push_back(std::move(address));
 }
 static void addressed(DNSServiceRef,DNSServiceFlags flags,std::uint32_t index,DNSServiceErrorType e,const char*,const sockaddr* address,std::uint32_t ttl,void* context){
  auto& op=*static_cast<Op*>(context);auto& self=*op.owner;if(e){self.failure(e);return;}
  if(!address||(address->sa_family!=AF_INET&&address->sa_family!=AF_INET6))return;
  auto d=op.device;d.interface_index=index;char text[INET6_ADDRSTRLEN]{};
  const void* raw=address->sa_family==AF_INET?static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr):static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
  if(!inet_ntop(address->sa_family,raw,text,sizeof text))return;d.address=text;
  // Local-only DNS callbacks use the pseudo interface index. Resolve loopback for tests.
  if(self.local_only){d.interface_index=if_nametoindex("lo0");d.interface_address=address->sa_family==AF_INET?"127.0.0.1":"::1";}
  else d.interface_address=local_address(index,address->sa_family);
  d.expires_ms=now()+std::min<std::uint32_t>(ttl,30)*1000;
  auto parsed=decode_discovery_txt(op.txt,d);if(!parsed)return;d=std::move(parsed.value());
  bool added=(flags&kDNSServiceFlagsAdd)&&ttl>0;
  if(!self.inbox.accept(self.generation,{added?DiscoveryEvent::Kind::Found:DiscoveryEvent::Kind::Removed,d,{}}))return;
  auto& endpoints=self.found[op.key];if(added){
   auto old=std::find_if(endpoints.begin(),endpoints.end(),[&](const auto& x){return x.address==d.address;});
   if(old!=endpoints.end())*old=d;else if(endpoints.size()<4)endpoints.push_back(d);
  }else endpoints.erase(std::remove_if(endpoints.begin(),endpoints.end(),[&](const auto& x){return x.address==d.address;}),endpoints.end());
 }
 void cancel_browse(){inbox.cancel();deadline=0;ops.erase(std::remove_if(ops.begin(),ops.end(),[](const auto& op){return op->kind!=Kind::Publish;}),ops.end());found.clear();resolving.clear();}
};
NativeDiscovery::NativeDiscovery(std::string id,bool local):impl_(std::make_unique<Impl>(std::move(id),local)){}
NativeDiscovery::~NativeDiscovery(){stop();}
NativeDiscovery::Capability NativeDiscovery::capability()const{return {true,{}};}
Result<void> NativeDiscovery::publish(std::optional<DiscoveryAnnouncement> announcement){
 auto& s=*impl_;s.ops.erase(std::remove_if(s.ops.begin(),s.ops.end(),[](const auto& op){return op->kind==Impl::Kind::Publish;}),s.ops.end());
 if(!announcement)return {};auto txt=encode_discovery_txt(announcement->device);if(!txt)return txt.error();if(!announcement->device.control_port)return Error{ErrorCode::InvalidArgument,"Missing pairing listener"};
 auto op=s.operation(Impl::Kind::Publish);op->txt=txt.value();auto name="Cliptiq-"+s.id.substr(0,40)+"-"+std::to_string(std::hash<std::string>{}(s.id)%1000000);
 auto result=DNSServiceRegister(&op->ref,0,s.local_only?kDNSServiceInterfaceIndexLocalOnly:announcement->device.interface_index,name.c_str(),"_cliptiq._tcp","local.",s.local_only?"localhost.":nullptr,htons(announcement->device.control_port),static_cast<std::uint16_t>(op->txt.size()),op->txt.data(),Impl::registered,op.get());
 if(result)return Error{ErrorCode::Io,"Bonjour publication failed ("+std::to_string(result)+")."};s.ops.push_back(std::move(op));return {};
}
Result<void> NativeDiscovery::browse(std::int64_t duration){
 auto& s=*impl_;s.cancel_browse();if(duration<1||duration>10000)return Error{ErrorCode::InvalidArgument,"Search duration must be between 1 and 10000 ms"};
 s.generation=s.inbox.begin();auto op=s.operation(Impl::Kind::Browse);
 auto result=DNSServiceBrowse(&op->ref,0,s.local_only?kDNSServiceInterfaceIndexLocalOnly:0,"_cliptiq._tcp","local.",Impl::browsed,op.get());
 if(result){s.cancel_browse();return Error{ErrorCode::Io,"Bonjour search failed ("+std::to_string(result)+")."};}
 s.deadline=now()+duration;s.ops.push_back(std::move(op));return {};
}
void NativeDiscovery::stop_browse(){impl_->cancel_browse();}
std::vector<DiscoveryEvent> NativeDiscovery::poll(){
 auto& s=*impl_;if(s.deadline&&now()>=s.deadline){auto pending=s.inbox.drain();s.cancel_browse();auto pub=s.publication.drain();pending.insert(pending.end(),pub.begin(),pub.end());return pending;}
 // Snapshot pointers: a callback can append operations, but cannot free one here.
 std::vector<Impl::Op*> snapshot;for(auto& op:s.ops)snapshot.push_back(op.get());
 for(auto* op:snapshot){if(op->done)continue;int fd=DNSServiceRefSockFD(op->ref);if(fd<0||fd>=FD_SETSIZE){op->done=true;continue;}fd_set fds;FD_ZERO(&fds);FD_SET(fd,&fds);timeval timeout{};
  if(select(fd+1,&fds,nullptr,nullptr,&timeout)>0){auto e=DNSServiceProcessResult(op->ref);if(e){s.failure(e,op->kind==Impl::Kind::Publish);op->done=true;}}
 }
 s.ops.erase(std::remove_if(s.ops.begin(),s.ops.end(),[](const auto& op){return op->done;}),s.ops.end());auto events=s.inbox.drain();auto pub=s.publication.drain();events.insert(events.end(),pub.begin(),pub.end());return events;
}
void NativeDiscovery::stop(){if(!impl_)return;impl_->cancel_browse();impl_->ops.clear();}
}

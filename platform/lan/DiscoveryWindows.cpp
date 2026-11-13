#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <iphlpapi.h>
#include "NativeDiscovery.hpp"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <map>
#include <set>
namespace cliptiq::lan {
namespace {
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::wstring wide(const std::string& text){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);if(n<=0)return {};std::wstring s(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),s.data(),n);return s;}
std::string utf8(const wchar_t* p){if(!p)return {};size_t n=wcsnlen(p,1025);if(n>1024)return {};int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,p,static_cast<int>(n),nullptr,0,nullptr,nullptr);if(size<=0)return {};std::string s(size,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,p,static_cast<int>(n),s.data(),size,nullptr,nullptr);return s;}
std::string local_address(ULONG index,int family,bool link_local){
 ULONG size=16384;std::vector<unsigned char> data(size);auto* adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data());auto result=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,adapters,&size);
 if(result==ERROR_BUFFER_OVERFLOW&&size<=65536){data.resize(size);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(data.data());result=GetAdaptersAddresses(AF_UNSPEC,0,nullptr,adapters,&size);}if(result!=NO_ERROR)return {};
 for(auto* a=adapters;a;a=a->Next){if((family==AF_INET?a->IfIndex:a->Ipv6IfIndex)!=index)continue;for(auto* p=a->FirstUnicastAddress;p;p=p->Next){auto* sa=p->Address.lpSockaddr;if(!sa||sa->sa_family!=family)continue;if(family==AF_INET6&&static_cast<bool>(IN6_IS_ADDR_LINKLOCAL(&reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr))!=link_local)continue;char text[INET6_ADDRSTRLEN]{};
  auto* raw=family==AF_INET?static_cast<void*>(&reinterpret_cast<sockaddr_in*>(sa)->sin_addr):static_cast<void*>(&reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr);
  if(inet_ntop(family,raw,text,sizeof text)&&valid_numeric_endpoint(text,index))return text;
 }}return {};
}
struct State {
 DiscoveryInbox inbox,publication;std::mutex mutex;std::map<std::wstring,bool> names;std::map<std::wstring,std::vector<DiscoveryDevice>> found;std::atomic<std::uint64_t> active{0};bool sockets=false;
 explicit State(std::string id):inbox(id),publication(std::move(id)){publication.begin();WSADATA data{};sockets=WSAStartup(MAKEWORD(2,2),&data)==0;}
 ~State(){if(sockets)WSACleanup();}
 void failure(std::uint64_t gen,DWORD code,bool pub=false){auto event=DiscoveryEvent{DiscoveryEvent::Kind::Failure,{},"Windows device discovery failed ("+std::to_string(code)+"). Check local network access and firewall settings.",pub};if(pub)publication.accept(1,std::move(event));else inbox.accept(gen,std::move(event));}
};
struct Query {std::shared_ptr<State> state;std::uint64_t generation=0;DNS_SERVICE_CANCEL cancel{};std::atomic<bool> done{false},cancelled{false};std::wstring name;DNS_SERVICE_BROWSE_REQUEST browse{};DNS_SERVICE_RESOLVE_REQUEST resolve{};};
using QueryHold=std::shared_ptr<Query>;
void WINAPI browsed(DWORD status,void* context,DNS_RECORD* records){
 auto* hold=static_cast<QueryHold*>(context);auto q=*hold;
 if(status==ERROR_CANCELLED){if(records)DnsRecordListFree(records,DnsFreeRecordList);q->done=true;delete hold;return;}
 if(status){q->state->failure(q->generation,status);}else if(q->state->active==q->generation){
  std::lock_guard<std::mutex> lock(q->state->mutex);size_t count=0;
  for(auto* r=records;r&&count++<256;r=r->pNext){const wchar_t* raw=r->wType==DNS_TYPE_PTR?r->Data.PTR.pNameHost:r->wType==DNS_TYPE_SRV?r->pName:nullptr;if(!raw)continue;size_t length=wcsnlen(raw,256);if(length==256)continue;std::wstring name(raw,length);if(!name.empty()&&name.back()==L'.')name.pop_back();
   const std::wstring suffix=L"._cliptiq._tcp.local";if(name.size()<=suffix.size()||_wcsicmp(name.c_str()+name.size()-suffix.size(),suffix.c_str())!=0)continue;
   if(q->state->active==q->generation&&q->state->names.size()<64)q->state->names[name]=r->dwTtl!=0;
  }
 }if(records)DnsRecordListFree(records,DnsFreeRecordList);
}
void WINAPI resolved(DWORD status,void* context,DNS_SERVICE_INSTANCE* instance){
 std::unique_ptr<QueryHold> hold(static_cast<QueryHold*>(context));auto q=*hold;
 if(status&&status!=ERROR_CANCELLED)q->state->failure(q->generation,status);
 if(!status&&!q->cancelled&&instance&&instance->dwPropertyCount<=32&&instance->keys&&instance->values){
  Bytes txt;bool valid=true;for(DWORD i=0;i<instance->dwPropertyCount;++i){auto key=utf8(instance->keys[i]),value=utf8(instance->values[i]);auto s=key+"="+value;if(s.size()>255||key.empty()||txt.size()+s.size()+1>1024){valid=false;break;}txt.push_back(static_cast<std::uint8_t>(s.size()));txt.insert(txt.end(),s.begin(),s.end());}
  if(valid)for(int family:{AF_INET,AF_INET6}){const void* raw=family==AF_INET?static_cast<const void*>(instance->ip4Address):static_cast<const void*>(instance->ip6Address);if(!raw)continue;char text[INET6_ADDRSTRLEN]{};if(!inet_ntop(family,raw,text,sizeof text))continue;
   DiscoveryDevice d;d.host=utf8(instance->pszHostName);d.address=text;d.interface_index=instance->dwInterfaceIndex;d.interface_address=local_address(d.interface_index,family,family==AF_INET6&&IN6_IS_ADDR_LINKLOCAL(static_cast<const IN6_ADDR*>(raw)));d.control_port=instance->wPort;d.expires_ms=now()+30000;
   auto parsed=decode_discovery_txt(txt,std::move(d));if(parsed&&q->state->inbox.accept(q->generation,{DiscoveryEvent::Kind::Found,parsed.value(),{}})){
    std::lock_guard<std::mutex> lock(q->state->mutex);if(q->state->active==q->generation&&q->state->found.size()<64){auto& endpoints=q->state->found[q->name];if(endpoints.size()<4)endpoints.push_back(std::move(parsed.value()));}
   }
  }
 }if(instance)DnsServiceFreeInstance(instance);q->done=true;
}
struct Registration {std::shared_ptr<State> state;DNS_SERVICE_REGISTER_REQUEST request{};DNS_SERVICE_INSTANCE* instance=nullptr;std::mutex mutex;bool wanted=true,complete=false,registered_ok=false,deregistering=false;~Registration(){if(instance)DnsServiceFreeInstance(instance);}};
using RegistrationHold=std::shared_ptr<Registration>;
void deregister(const RegistrationHold&);
void WINAPI registered(DWORD status,void* context,DNS_SERVICE_INSTANCE* instance){
 std::unique_ptr<RegistrationHold> hold(static_cast<RegistrationHold*>(context));auto op=*hold;
 if(instance)DnsServiceFreeInstance(instance);bool remove=false;
 {std::lock_guard<std::mutex> lock(op->mutex);op->complete=true;op->registered_ok=status==ERROR_SUCCESS;if(status)op->wanted=false;else remove=!op->wanted;}
 if(status&&status!=ERROR_CANCELLED)op->state->failure(0,status,true);if(remove)deregister(op);
}
void WINAPI deregistered(DWORD status,void* context,DNS_SERVICE_INSTANCE* instance){std::unique_ptr<RegistrationHold> hold(static_cast<RegistrationHold*>(context));if(instance)DnsServiceFreeInstance(instance);if(status)(*hold)->state->failure(0,status,true);}
void deregister(const RegistrationHold& op){
 {std::lock_guard<std::mutex> lock(op->mutex);op->wanted=false;if(!op->complete||!op->registered_ok||op->deregistering)return;op->deregistering=true;}
 auto* hold=new RegistrationHold(op);op->request.pQueryContext=hold;op->request.pRegisterCompletionCallback=deregistered;auto result=DnsServiceDeRegister(&op->request,nullptr);if(result!=DNS_REQUEST_PENDING){delete hold;op->state->failure(0,result,true);}
}
}
struct NativeDiscovery::Impl {
 std::string id;bool local_only;std::shared_ptr<State> state;QueryHold browse;std::vector<QueryHold> resolves;RegistrationHold registration;std::set<std::wstring> seen;std::int64_t deadline=0;std::uint64_t generation=0;
 Impl(std::string id,bool local):id(std::move(id)),local_only(local),state(std::make_shared<State>(this->id)){}
 void cancel(){state->active=0;state->inbox.cancel();{std::lock_guard<std::mutex> lock(state->mutex);state->names.clear();state->found.clear();}deadline=0;if(browse&&!browse->done)DnsServiceBrowseCancel(&browse->cancel);browse.reset();for(auto& q:resolves){q->cancelled=true;if(!q->done)DnsServiceResolveCancel(&q->cancel);}resolves.clear();seen.clear();}
};
NativeDiscovery::NativeDiscovery(std::string id,bool local):impl_(std::make_unique<Impl>(std::move(id),local)){}
NativeDiscovery::~NativeDiscovery(){stop();}
NativeDiscovery::Capability NativeDiscovery::capability()const{return {impl_->state->sockets&&!impl_->local_only,impl_->local_only?"Local-only DNS-SD test mode is supported on macOS only.":impl_->state->sockets?"":"Windows networking is unavailable."};}
Result<void> NativeDiscovery::publish(std::optional<DiscoveryAnnouncement> a){
 auto& s=*impl_;if(s.registration){deregister(s.registration);s.registration.reset();}if(!a)return {};if(!capability().available)return Error{ErrorCode::Unsupported,capability().message};auto txt=encode_discovery_txt(a->device);if(!txt)return txt.error();if(!a->device.control_port)return Error{ErrorCode::InvalidArgument,"Missing pairing listener"};
 auto op=std::make_shared<Registration>();op->state=s.state;auto name=wide("Cliptiq-"+s.id.substr(0,40)+"-"+std::to_string(std::hash<std::string>{}(s.id)%1000000)+"._cliptiq._tcp.local");wchar_t host[256]{};DWORD hostsize=256;if(!GetComputerNameExW(ComputerNameDnsHostname,host,&hostsize))return Error{ErrorCode::Io,"Cannot read local DNS host name"};std::wstring hostname=std::wstring(host)+L".local";
 std::vector<std::wstring> keys,values;for(size_t at=0;at<txt.value().size();){size_t n=txt.value()[at++];std::string pair(txt.value().begin()+at,txt.value().begin()+at+n);at+=n;auto eq=pair.find('=');keys.push_back(wide(pair.substr(0,eq)));values.push_back(wide(pair.substr(eq+1)));}
 std::vector<PCWSTR> kp,vp;for(size_t i=0;i<keys.size();++i){kp.push_back(keys[i].c_str());vp.push_back(values[i].c_str());}
 op->instance=DnsServiceConstructInstance(name.c_str(),hostname.c_str(),nullptr,nullptr,a->device.control_port,0,0,static_cast<DWORD>(kp.size()),kp.data(),vp.data());if(!op->instance)return Error{ErrorCode::Io,"Cannot construct DNS-SD announcement"};
 op->request.Version=DNS_QUERY_REQUEST_VERSION1;op->request.InterfaceIndex=a->device.interface_index;op->request.pServiceInstance=op->instance;op->request.pRegisterCompletionCallback=registered;
 auto* hold=new RegistrationHold(op);op->request.pQueryContext=hold;auto result=DnsServiceRegister(&op->request,nullptr);if(result!=DNS_REQUEST_PENDING){delete hold;return Error{ErrorCode::Io,"Windows publication failed ("+std::to_string(result)+")."};}s.registration=std::move(op);return {};
}
Result<void> NativeDiscovery::browse(std::int64_t duration){
 auto& s=*impl_;s.cancel();if(!capability().available)return Error{ErrorCode::Unsupported,capability().message};if(duration<1||duration>10000)return Error{ErrorCode::InvalidArgument,"Search duration must be between 1 and 10000 ms"};s.generation=s.state->inbox.begin();s.state->active=s.generation;auto q=std::make_shared<Query>();q->state=s.state;q->generation=s.generation;q->name=L"_cliptiq._tcp.local";q->browse.Version=DNS_QUERY_REQUEST_VERSION1;q->browse.QueryName=q->name.c_str();q->browse.pBrowseCallback=browsed;
 auto* hold=new QueryHold(q);q->browse.pQueryContext=hold;auto result=DnsServiceBrowse(&q->browse,&q->cancel);if(result!=DNS_REQUEST_PENDING){delete hold;s.cancel();return Error{ErrorCode::Io,"Windows search failed ("+std::to_string(result)+")."};}s.browse=std::move(q);s.deadline=now()+duration;return {};
}
void NativeDiscovery::stop_browse(){impl_->cancel();}
std::vector<DiscoveryEvent> NativeDiscovery::poll(){
 auto& s=*impl_;auto publication=s.state->publication.drain();if(s.deadline&&now()>=s.deadline){auto pending=s.state->inbox.drain();s.cancel();publication.insert(publication.end(),pending.begin(),pending.end());return publication;}
 std::map<std::wstring,bool> names;{std::lock_guard<std::mutex> lock(s.state->mutex);names.swap(s.state->names);}
 for(auto& item:names){if(!item.second){std::lock_guard<std::mutex> lock(s.state->mutex);auto it=s.state->found.find(item.first);if(it!=s.state->found.end()){for(auto& d:it->second)s.state->inbox.accept(s.generation,{DiscoveryEvent::Kind::Removed,d,{}});s.state->found.erase(it);}s.seen.erase(item.first);for(auto& q:s.resolves)if(q->name==item.first){q->cancelled=true;if(!q->done)DnsServiceResolveCancel(&q->cancel);}continue;}
  if(s.resolves.size()>=64||s.seen.size()>=64||!s.seen.insert(item.first).second)continue;auto q=std::make_shared<Query>();q->state=s.state;q->generation=s.generation;q->name=item.first;q->resolve.Version=DNS_QUERY_REQUEST_VERSION1;q->resolve.QueryName=q->name.data();q->resolve.pResolveCompletionCallback=resolved;
  auto* hold=new QueryHold(q);q->resolve.pQueryContext=hold;auto result=DnsServiceResolve(&q->resolve,&q->cancel);if(result!=DNS_REQUEST_PENDING){delete hold;s.state->failure(s.generation,result);}else s.resolves.push_back(std::move(q));
 }
 s.resolves.erase(std::remove_if(s.resolves.begin(),s.resolves.end(),[](const auto& q){return q->done.load();}),s.resolves.end());auto events=s.state->inbox.drain();
 publication.insert(publication.end(),std::make_move_iterator(events.begin()),std::make_move_iterator(events.end()));return publication;
}
void NativeDiscovery::stop(){if(!impl_)return;impl_->cancel();if(impl_->registration){deregister(impl_->registration);impl_->registration.reset();}}
}

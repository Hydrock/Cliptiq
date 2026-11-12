#include "Resolver.hpp"
#include <cstring>
#include <thread>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#ifdef __APPLE__
#include <dns_sd.h>
#include <sys/select.h>
#else
#include <gio/gio.h>
#endif
#endif
namespace cliptiq::lan {
namespace {
using Clock=std::chrono::steady_clock;
using Addresses=std::vector<ResolvedAddress>;
Error failure(const char* message){return {ErrorCode::Io,message};}
void append(Addresses& out,const sockaddr* p,int length){
 if(out.size()>=16 || !p || length<=0 || static_cast<size_t>(length)>sizeof(sockaddr_storage) ||
    (p->sa_family!=AF_INET && p->sa_family!=AF_INET6))return;
 ResolvedAddress a;a.length=length;std::memcpy(&a.address,p,length);out.push_back(a);
}
#ifdef _WIN32
struct Query {
 OVERLAPPED overlap{};std::atomic<unsigned> refs{2};std::atomic<bool> done{false};
 PADDRINFOEXW result=nullptr;HANDLE cancellation=nullptr;DWORD error=0;
 void release(){if(refs.fetch_sub(1)==1){if(result)FreeAddrInfoExW(result);delete this;}}
 static void CALLBACK complete(DWORD error,DWORD,OVERLAPPED* overlap){
  auto* q=reinterpret_cast<Query*>(overlap);q->error=error;q->done.store(true,std::memory_order_release);q->release();
 }
};
#endif
}
struct ResolverJob::State {
 Clock::time_point deadline;std::uint16_t port;Addresses addresses;
 std::optional<Result<Addresses>> result;
#ifdef _WIN32
 Query* query=nullptr;
#elif defined(__APPLE__)
 DNSServiceRef query=nullptr;bool done=false,failed=false;
 static void callback(DNSServiceRef,DNSServiceFlags flags,uint32_t,DNSServiceErrorType error,const char*,const sockaddr* a,uint32_t,void* data){
  auto& q=*static_cast<State*>(data);
  if(error){q.failed=true;q.done=true;return;}
  if(a && (flags & kDNSServiceFlagsAdd)){
   const auto before=q.addresses.size();append(q.addresses,a,a->sa_family==AF_INET?sizeof(sockaddr_in):sizeof(sockaddr_in6));
   if(q.addresses.size()>before){auto& stored=q.addresses.back().address;if(stored.ss_family==AF_INET)reinterpret_cast<sockaddr_in*>(&stored)->sin_port=htons(q.port);else reinterpret_cast<sockaddr_in6*>(&stored)->sin6_port=htons(q.port);}
  }
  q.done=!(flags & kDNSServiceFlagsMoreComing);
 }
#else
 GMainContext* context=nullptr;GResolver* resolver=nullptr;GCancellable* cancellable=nullptr;
 bool done=false;GList* native=nullptr;GError* error=nullptr;
 static void callback(GObject* source,GAsyncResult* result,gpointer data){
  auto& q=*static_cast<State*>(data);q.native=g_resolver_lookup_by_name_finish(G_RESOLVER(source),result,&q.error);q.done=true;
 }
#endif
 State(const std::string& host,std::uint16_t p,std::chrono::milliseconds timeout):deadline(Clock::now()+timeout),port(p){
  if(host.empty() || host.size()>253 || host.find('\0')!=std::string::npos || !port){result=Error{ErrorCode::InvalidArgument,"Invalid peer hostname or port"};return;}
  if(timeout.count()<=0){result=failure("Hostname resolution timed out");return;}
#ifdef _WIN32
  static const bool sockets=[](){WSADATA data{};return WSAStartup(MAKEWORD(2,2),&data)==0;}();
  if(!sockets){result=failure("Winsock initialization failed");return;}
#endif
  addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_flags=AI_NUMERICHOST;
  addrinfo* numeric=nullptr;const auto service=std::to_string(port);
  if(getaddrinfo(host.c_str(),service.c_str(),&hints,&numeric)==0){
   for(auto* a=numeric;a;a=a->ai_next)append(addresses,a->ai_addr,static_cast<int>(a->ai_addrlen));
   freeaddrinfo(numeric);result=addresses;return;
  }
#ifdef _WIN32
  const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,host.c_str(),-1,nullptr,0);
  if(!count){result=Error{ErrorCode::InvalidArgument,"Hostname is not UTF-8"};return;}
  std::wstring wide(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,host.c_str(),-1,wide.data(),count);
  query=new Query;ADDRINFOEXW wh{};wh.ai_family=AF_UNSPEC;wh.ai_socktype=SOCK_STREAM;
  const auto wideport=std::to_wstring(port);
  const int status=GetAddrInfoExW(wide.c_str(),wideport.c_str(),NS_DNS,nullptr,&wh,&query->result,nullptr,&query->overlap,&Query::complete,&query->cancellation);
  if(status!=WSA_IO_PENDING){query->error=status;query->done=true;query->release();}
#elif defined(__APPLE__)
  if(DNSServiceGetAddrInfo(&query,0,0,kDNSServiceProtocol_IPv4|kDNSServiceProtocol_IPv6,host.c_str(),callback,this)){
   result=failure("Hostname resolution failed");return;
  }
  const int fd=DNSServiceRefSockFD(query);
  if(fd<0 || fd>=FD_SETSIZE){result=failure("Resolver descriptor unavailable");release();}
#else
  context=g_main_context_new();g_main_context_push_thread_default(context);
  resolver=g_resolver_get_default();cancellable=g_cancellable_new();
  g_resolver_lookup_by_name_async(resolver,host.c_str(),cancellable,callback,this);
  g_main_context_pop_thread_default(context);
#endif
 }
 void release(){
#ifdef _WIN32
  if(query){if(!query->done.load(std::memory_order_acquire))GetAddrInfoExCancel(&query->cancellation);query->release();query=nullptr;}
#elif defined(__APPLE__)
  if(query){DNSServiceRefDeallocate(query);query=nullptr;}
#else
  if(cancellable){
   if(!done){
    // The default GResolver schedules cancellation completion independently of
    // blocked libc DNS work. Dispatch only ready sources, never wait for DNS.
    g_cancellable_cancel(cancellable);
    while(!done)g_main_context_iteration(context,FALSE);
   }
   if(native)g_resolver_free_addresses(native);if(error)g_error_free(error);
   g_object_unref(cancellable);g_object_unref(resolver);g_main_context_unref(context);
   cancellable=nullptr;resolver=nullptr;context=nullptr;native=nullptr;error=nullptr;
  }
#endif
 }
 ~State(){release();}
 void poll(){
  if(result)return;
  if(Clock::now()>=deadline){result=failure("Hostname resolution timed out");release();return;}
#ifdef _WIN32
  if(!query->done.load(std::memory_order_acquire))return;
  if(query->error)result=failure("Hostname resolution failed");
  else for(auto* a=query->result;a;a=a->ai_next)append(addresses,a->ai_addr,static_cast<int>(a->ai_addrlen));
#elif defined(__APPLE__)
  const int fd=DNSServiceRefSockFD(query);fd_set reads;FD_ZERO(&reads);FD_SET(fd,&reads);timeval wait{0,0};
  const int ready=select(fd+1,&reads,nullptr,nullptr,&wait);
  if(ready<0 || (ready>0 && DNSServiceProcessResult(query))){done=true;failed=true;}
  if(!done)return;
  if(failed)result=failure("Hostname resolution failed");
#else
  g_main_context_iteration(context,FALSE);
  if(!done)return;
  if(error)result=failure("Hostname resolution failed");
  else for(auto* a=native;a;a=a->next){
   auto* address=G_INET_ADDRESS(a->data);sockaddr_storage ss{};
   if(g_inet_address_get_family(address)==G_SOCKET_FAMILY_IPV4){auto* v=reinterpret_cast<sockaddr_in*>(&ss);v->sin_family=AF_INET;v->sin_port=htons(port);std::memcpy(&v->sin_addr,g_inet_address_to_bytes(address),4);append(addresses,reinterpret_cast<sockaddr*>(v),sizeof(*v));}
   else{auto* v=reinterpret_cast<sockaddr_in6*>(&ss);v->sin6_family=AF_INET6;v->sin6_port=htons(port);std::memcpy(&v->sin6_addr,g_inet_address_to_bytes(address),16);append(addresses,reinterpret_cast<sockaddr*>(v),sizeof(*v));}
  }
#endif
  if(!result){if(addresses.empty())result=failure("Hostname resolved without usable addresses");else result=std::move(addresses);}
  release();
 }
};
ResolverJob::ResolverJob(const std::string& host,std::uint16_t port,std::chrono::milliseconds timeout):state_(std::make_unique<State>(host,port,timeout)){}
ResolverJob::~ResolverJob()=default;
std::optional<Result<Addresses>> ResolverJob::poll(){state_->poll();return state_->result;}
void ResolverJob::cancel(){if(!state_->result)state_->result=failure("Hostname resolution cancelled");state_->release();}
Result<Addresses> resolve_host(const std::string& host,std::uint16_t port,const std::atomic<bool>& stopping,std::chrono::milliseconds timeout){
 if(stopping.load())return failure("Hostname resolution cancelled");
 ResolverJob job(host,port,timeout);
 for(;;){if(stopping.load()){job.cancel();return failure("Hostname resolution cancelled");}if(auto result=job.poll())return std::move(*result);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
}

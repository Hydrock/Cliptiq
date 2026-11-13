#include "PairingServer.hpp"
#include "Text.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <cstring>
#include <mbedtls/platform_util.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace cliptiq::lan {
namespace {
#ifdef _WIN32
using socklen_t=int;using Socket=SOCKET;constexpr Socket invalid=INVALID_SOCKET;void close_socket(Socket s){if(s!=invalid)closesocket(s);}bool again(){auto e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEINTR;}bool nonblock(Socket s){u_long one=1;return ioctlsocket(s,FIONBIO,&one)==0;}
#else
using Socket=int;constexpr Socket invalid=-1;void close_socket(Socket s){if(s!=invalid)::close(s);}bool again(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==EINTR;}bool nonblock(Socket s){
#ifdef SO_NOSIGPIPE
 int one=1;if(setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one)!=0)return false;
#endif
 return s<FD_SETSIZE&&fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK)==0;
}
#endif
struct Address {sockaddr_storage storage{};socklen_t size=0;int family=0;};
Result<Address> endpoint(std::string value,std::uint16_t port,std::uint32_t index=0){
 if(!valid_numeric_endpoint(value,index))return Error{ErrorCode::InvalidArgument,"Invalid local pairing endpoint"};auto zone=value.find('%');if(zone!=std::string::npos)value.resize(zone);Address a;
 auto* v4=reinterpret_cast<sockaddr_in*>(&a.storage);auto* v6=reinterpret_cast<sockaddr_in6*>(&a.storage);
 if(inet_pton(AF_INET,value.c_str(),&v4->sin_addr)==1){a.family=AF_INET;a.size=sizeof(sockaddr_in);v4->sin_family=AF_INET;v4->sin_port=htons(port);}
 else if(inet_pton(AF_INET6,value.c_str(),&v6->sin6_addr)==1){a.family=AF_INET6;a.size=sizeof(sockaddr_in6);v6->sin6_family=AF_INET6;v6->sin6_port=htons(port);v6->sin6_scope_id=index;}else return Error{ErrorCode::InvalidArgument,"Invalid pairing address"};return a;
}
std::string address_of(const sockaddr_storage& storage){char text[INET6_ADDRSTRLEN]{};if(storage.ss_family==AF_INET)inet_ntop(AF_INET,&reinterpret_cast<const sockaddr_in*>(&storage)->sin_addr,text,sizeof text);else if(storage.ss_family==AF_INET6)inet_ntop(AF_INET6,&reinterpret_cast<const sockaddr_in6*>(&storage)->sin6_addr,text,sizeof text);return text;}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Result<std::string> fresh_id(){auto random=pairing_random(16);if(!random)return random.error();std::string s;const char hex[]="0123456789abcdef";for(auto b:random.value()){s+=hex[b>>4];s+=hex[b&15];}return s;}
void wipe(PairingEvent& event){if(!event.key.empty())mbedtls_platform_zeroize(event.key.data(),event.key.size());if(!event.code.empty())mbedtls_platform_zeroize(event.code.data(),event.code.size());}
}
struct PairingServer::Impl {
 enum class CommandKind {Connect,Accept,Code,Prepared,Committed,Cancel};
 struct Command {CommandKind kind;std::string id,code;DiscoveryDevice device;bool success=true;};
 struct Connection {Socket socket=invalid;bool connecting=false,initiator=false,claimed=false,close_after_write=false;std::string id,expected;std::int64_t activity=0;Bytes input,output;size_t sent=0;std::unique_ptr<PairingSession> session;~Connection(){close_socket(socket);}};
 std::string id,name,platform;std::uint16_t data_port=49731;Socket listener=invalid;std::thread worker;std::atomic<bool> stopped{true};std::mutex mutex;std::condition_variable cv;std::deque<Command> commands;std::vector<PairingEvent> events;std::vector<std::unique_ptr<Connection>> connections;PairingRateLimiter rate;std::string active;bool sockets=true;
 Impl(std::string id,std::string name,std::string platform):id(std::move(id)),name(std::move(name)),platform(std::move(platform)){
#ifdef _WIN32
 WSADATA data{};sockets=WSAStartup(MAKEWORD(2,2),&data)==0;
#endif
 }
 ~Impl(){
#ifdef _WIN32
 if(sockets)WSACleanup();
#endif
 }
 void push(PairingEvent e){std::lock_guard<std::mutex> lock(mutex);if(events.size()<64)events.push_back(std::move(e));else wipe(e);}
 Result<void> queue(Command command){std::lock_guard<std::mutex> lock(mutex);if(stopped||commands.size()>=64){if(!command.code.empty())mbedtls_platform_zeroize(command.code.data(),command.code.size());return Error{ErrorCode::Busy,"Pairing is not ready. Try again."};}commands.push_back(std::move(command));cv.notify_one();return {};}
 void start_worker(){if(!stopped)return;stopped=false;worker=std::thread([this]{run();});}
 void failed(std::string session){push({PairingEvent::Kind::Failed,std::move(session),{},{},"Pairing failed. Start again with a new code.",{}});}
 bool initialize(Connection& c){
  sockaddr_storage remote{},local{};socklen_t size=sizeof remote;if(getpeername(c.socket,reinterpret_cast<sockaddr*>(&remote),&size)!=0)return false;size=sizeof local;if(getsockname(c.socket,reinterpret_cast<sockaddr*>(&local),&size)!=0)return false;
  auto nonce=pairing_random(32);if(!nonce)return false;PairingHello hello{c.id,id,name,platform,address_of(local),data_port,std::move(nonce.value())};if(!encode_pairing_hello(hello))return false;
  c.session=std::make_unique<PairingSession>(c.initiator?PairingRole::Initiator:PairingRole::Responder,std::move(hello),address_of(remote),address_of(local),now,pairing_random,c.expected);c.activity=now();return !c.initiator||static_cast<bool>(c.session->start());
 }
 void execute(Command& command){
  if(command.kind==CommandKind::Connect){if(!active.empty()||connections.size()>=4){failed(command.id);return;}auto address=endpoint(command.device.address,command.device.control_port,command.device.interface_index);if(!address){failed(command.id);return;}
   auto c=std::make_unique<Connection>();c->id=command.id;c->initiator=true;c->expected=command.device.device_id;c->socket=socket(address.value().family,SOCK_STREAM,IPPROTO_TCP);if(c->socket==invalid||!nonblock(c->socket)){failed(command.id);return;}
   // Bind to the interface selected from the resolved endpoint. Never silently use another network.
   auto local=endpoint(command.device.interface_address,0,command.device.interface_index);if(!local||local.value().family!=address.value().family||bind(c->socket,reinterpret_cast<sockaddr*>(&local.value().storage),local.value().size)!=0){failed(command.id);return;}
   int result=::connect(c->socket,reinterpret_cast<sockaddr*>(&address.value().storage),address.value().size);if(result!=0&&!again()){failed(command.id);return;}c->connecting=result!=0;c->activity=now();active=c->id;c->claimed=true;if(!c->connecting&&!initialize(*c)){active.clear();failed(command.id);return;}connections.push_back(std::move(c));return;
  }
  auto it=std::find_if(connections.begin(),connections.end(),[&](const auto& c){return c->session&&c->session->session_id()==command.id;});if(it==connections.end())return;auto& session=*(*it)->session;
  switch(command.kind){case CommandKind::Accept:session.accept();break;case CommandKind::Code:session.enter_code(std::move(command.code));break;case CommandKind::Prepared:session.prepared(command.success?Result<void>{}:Result<void>{Error{ErrorCode::Io,"Credential storage failed"}});break;case CommandKind::Committed:session.committed(command.success?Result<void>{}:Result<void>{Error{ErrorCode::Io,"Peer storage failed"}});break;case CommandKind::Cancel:session.cancel();break;default:break;}
 }
 void accept_socket(){if(listener==invalid)return;for(int i=0;i<4;++i){sockaddr_storage remote{};socklen_t size=sizeof remote;Socket sock=::accept(listener,reinterpret_cast<sockaddr*>(&remote),&size);if(sock==invalid)return;
   auto c=std::make_unique<Connection>();c->socket=sock;if(connections.size()>=4||!rate.admit(address_of(remote),now())||!nonblock(sock))continue;auto fresh=fresh_id();if(!fresh)continue;c->id=fresh.value();if(!initialize(*c))continue;connections.push_back(std::move(c));
  }
 }
 bool pump(Connection& c){
  if(c.connecting){fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(c.socket,&write);FD_SET(c.socket,&error);timeval t{};if(select(static_cast<int>(c.socket+1),nullptr,&write,&error,&t)>0){int status=0;socklen_t size=sizeof status;if(getsockopt(c.socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&status),&size)!=0||status!=0||!initialize(c))return false;c.connecting=false;}else return now()-c.activity<10000;}
  if(!c.session)return false;c.session->tick();
  if(!c.session->finished())for(int reads=0;reads<4;++reads){unsigned char buffer[8192];int size=recv(c.socket,reinterpret_cast<char*>(buffer),sizeof buffer,0);if(size==0)return false;if(size<0){if(again())break;return false;}c.activity=now();if(c.input.size()+size>16384)return false;c.input.insert(c.input.end(),buffer,buffer+size);
   while(c.input.size()>=4){std::uint32_t n=0;for(int i=0;i<4;++i)n=(n<<8)|c.input[i];if(n>8188||n<8)return false;if(c.input.size()<n+4)break;Bytes frame(c.input.begin(),c.input.begin()+n+4);c.input.erase(c.input.begin(),c.input.begin()+n+4);if(!c.session->receive(frame))break;
    auto nextId=c.session->session_id();if(!c.claimed){if(!active.empty()){c.session->cancel();break;}active=nextId;c.id=nextId;c.claimed=true;}
   }if(c.session->finished())break;
  }
  for(auto& event:c.session->take_events())push(std::move(event));for(auto& wire:c.session->take_wire()){if(c.output.size()-c.sent+wire.size()>65536)return false;if(c.sent){c.output.erase(c.output.begin(),c.output.begin()+c.sent);c.sent=0;}c.output.insert(c.output.end(),wire.begin(),wire.end());}
  while(c.sent<c.output.size()){
#ifdef MSG_NOSIGNAL
   constexpr int flags=MSG_NOSIGNAL;
#else
   constexpr int flags=0;
#endif
   int n=send(c.socket,reinterpret_cast<const char*>(c.output.data()+c.sent),static_cast<int>(c.output.size()-c.sent),flags);if(n<0){if(again())break;return false;}if(n==0)return false;c.sent+=n;c.activity=now();
  }
  if(c.sent==c.output.size()){c.sent=0;c.output.clear();}
  // Human approval/code entry gets the session deadline. Stalled framing/writes and pre-Request sockets get only ten seconds.
  if((c.session->awaiting_request()||!c.input.empty()||!c.output.empty())&&now()-c.activity>=10000)return false;
  return !c.session->finished()||!c.output.empty();
 }
 void run(){while(!stopped){std::deque<Command> work;{std::lock_guard<std::mutex> lock(mutex);work.swap(commands);}for(auto& cmd:work)execute(cmd);accept_socket();
   for(auto it=connections.begin();it!=connections.end();){auto& c=**it;if(pump(c)){++it;continue;}if(c.session&&!c.session->finished()){c.session->cancel();for(auto& e:c.session->take_events())push(std::move(e));}else if(!c.session)failed(c.id);if(c.claimed&&active==c.id)active.clear();it=connections.erase(it);}
   std::unique_lock<std::mutex> lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(10),[&]{return stopped||!commands.empty();});
  }connections.clear();active.clear();}
};
PairingServer::PairingServer(std::string id,std::string name,std::string platform,std::uint16_t data_port):impl_(std::make_unique<Impl>(std::move(id),std::move(name),std::move(platform))){impl_->data_port=data_port;}
PairingServer::~PairingServer(){stop();}
Result<std::uint16_t> PairingServer::listen(std::string address,std::uint16_t data_port,std::uint32_t index){
 stop();auto& s=*impl_;if(!s.sockets||!data_port)return Error{ErrorCode::InvalidArgument,"Pairing networking is unavailable"};auto local=endpoint(address,0,index);if(!local)return local.error();Socket sock=socket(local.value().family,SOCK_STREAM,IPPROTO_TCP);if(sock==invalid)return Error{ErrorCode::Io,"Cannot create pairing listener"};
 int one=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&one),sizeof one);
#ifdef SO_NOSIGPIPE
 setsockopt(sock,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
 if(!nonblock(sock)||bind(sock,reinterpret_cast<sockaddr*>(&local.value().storage),local.value().size)!=0||::listen(sock,4)!=0){close_socket(sock);return Error{ErrorCode::Io,"Cannot bind pairing listener to the selected interface"};}
 sockaddr_storage bound{};socklen_t size=sizeof bound;if(getsockname(sock,reinterpret_cast<sockaddr*>(&bound),&size)!=0){close_socket(sock);return Error{ErrorCode::Io,"Cannot read pairing port"};}s.listener=sock;s.data_port=data_port;s.start_worker();return ntohs(bound.ss_family==AF_INET?reinterpret_cast<sockaddr_in*>(&bound)->sin_port:reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port);
}
Result<std::string> PairingServer::connect(DiscoveryDevice device){auto& s=*impl_;if(!s.sockets||!valid_device_id(device.device_id)||device.device_id==s.id||!device.control_port||!valid_numeric_endpoint(device.address,device.interface_index)||!valid_numeric_endpoint(device.interface_address,device.interface_index))return Error{ErrorCode::InvalidArgument,"Invalid discovered pairing endpoint"};auto id=fresh_id();if(!id)return id.error();s.start_worker();Impl::Command cmd{Impl::CommandKind::Connect,id.value(),{},std::move(device),true};auto result=s.queue(std::move(cmd));if(!result)return result.error();return id.value();}
Result<void> PairingServer::accept(const std::string& id){return impl_->queue({Impl::CommandKind::Accept,id,{},{},true});}
Result<void> PairingServer::enter_code(const std::string& id,std::string code){return impl_->queue({Impl::CommandKind::Code,id,std::move(code),{},true});}
void PairingServer::prepared(const std::string& id,Result<void> result){impl_->queue({Impl::CommandKind::Prepared,id,{},{},static_cast<bool>(result)});}
void PairingServer::committed(const std::string& id,Result<void> result){impl_->queue({Impl::CommandKind::Committed,id,{},{},static_cast<bool>(result)});}
void PairingServer::cancel(const std::string& id){impl_->queue({Impl::CommandKind::Cancel,id,{},{},true});}
std::vector<PairingEvent> PairingServer::poll(){std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<PairingEvent> events;events.swap(impl_->events);return events;}
void PairingServer::stop(){auto& s=*impl_;s.stopped=true;s.cv.notify_all();if(s.worker.joinable())s.worker.join();close_socket(s.listener);s.listener=invalid;std::lock_guard<std::mutex> lock(s.mutex);for(auto& c:s.commands)if(!c.code.empty())mbedtls_platform_zeroize(c.code.data(),c.code.size());s.commands.clear();for(auto& e:s.events)wipe(e);s.events.clear();}
}

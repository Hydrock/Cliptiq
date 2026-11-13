#include "platform/lan/Service.hpp"
#include "platform/lan/DiscoveryService.hpp"
#include <functional>
#include "platform/lan/Secrets.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <thread>
#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T>r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void>r){if(!r)throw std::runtime_error(r.error().message);}
namespace cliptiq::lan {
static std::map<std::string,std::string> test_keys;
Result<std::string> read_secret(const std::string&r){auto i=test_keys.find(r);if(i==test_keys.end())return Error{ErrorCode::NotFound,"missing fixture key"};return i->second;}
Result<void> write_secret(const std::string&r,const std::string&k){test_keys[r]=k;return {};}
Result<void> erase_secret(const std::string&r){test_keys.erase(r);return {};}
Result<std::string> generate_key(){return std::string(64,'a');}
}
static unsigned short free_port(){
#ifdef _WIN32
 static bool initialized=[] {WSADATA data{};return WSAStartup(MAKEWORD(2,2),&data)==0;}();CHECK(initialized);
#endif
 auto s=socket(AF_INET,SOCK_STREAM,0);CHECK(s!=decltype(s)(-1));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);CHECK(bind(s,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
#ifdef _WIN32
 int size=sizeof(address);
#else
 socklen_t size=sizeof(address);
#endif
 CHECK(getsockname(s,reinterpret_cast<sockaddr*>(&address),&size)==0);
#ifdef _WIN32
 closesocket(s);
#else
 close(s);
#endif
 return ntohs(address.sin_port);
}
struct Device {
 std::filesystem::path directory=std::filesystem::temp_directory_path()/("cliptiq-real-lan-"+lan::new_id());
 std::optional<Store> store;std::unique_ptr<lan::Service> service;std::string token="start";std::vector<ClipboardSnapshot> writes;unsigned short port=free_port();
 Device(){std::filesystem::create_directories(directory);auto path=(directory/"history.db").u8string();store.emplace(take(open_store(path)));lan::ClipboardHooks hooks;hooks.token=[&]()->Result<std::string>{return token;};hooks.write=[&](const ClipboardSnapshot&s)->Result<std::string>{writes.push_back(s);token="own-"+std::to_string(writes.size());return token;};hooks.compare_write=[&,write=hooks.write](const ClipboardSnapshot&s,const std::string&e)->Result<std::string>{if(token!=e)return Error{ErrorCode::Busy,"local copy wins"};return write(s);};
#ifdef _WIN32
 auto platform=CapturePlatform::Windows;
#elif defined(__APPLE__)
 auto platform=CapturePlatform::MacOS;
#else
 auto platform=CapturePlatform::X11;
#endif
 service=std::make_unique<lan::Service>(*store,path,platform,std::move(hooks));ok(service->initialize());}
 ~Device(){service.reset();store.reset();std::error_code e;std::filesystem::remove_all(directory,e);}
 void configure(const std::string&name,unsigned short remote){auto c=take(service->config());c.device_name=name;c.port=port;lan::Peer p;p.id=lan::new_id();p.name=name=="A"?"B":"A";p.host="localhost";p.port=remote;c.peers.push_back(p);ok(service->save(c,{{p.id,std::string(64,'a')}}));}
 void send(std::vector<Representation> representations){lan::Event e{take(service->config()).device_id,lan::new_id(),1,1,std::move(representations)};
#ifdef _WIN32
 auto native=take(lan::native_snapshot(e,CapturePlatform::Windows));
#elif defined(__APPLE__)
 auto native=take(lan::native_snapshot(e,CapturePlatform::MacOS));
#else
 auto native=take(lan::native_snapshot(e,CapturePlatform::X11));
#endif
 token="local-"+lan::new_id();auto id=take(ingest(*store,std::move(native),{},100+take(data_revision(*store))));CHECK(id);service->captured(*id);}
 size_t count(){return take(list_history(*store,SortOrder::LastUsed,1000)).size();}
};
struct Fixture:Device {
 std::unique_ptr<lan::NativeDiscovery> dns;std::unique_ptr<lan::PairingServer> server;std::unique_ptr<lan::DiscoveryService> discovery;
 Fixture(const std::string& name){auto config=take(service->config());config.device_name=name;config.port=port;ok(service->save(config,{}));config=take(service->config());dns=std::make_unique<lan::NativeDiscovery>(config.device_id,true);server=std::make_unique<lan::PairingServer>(config.device_id,name,"test",port);
 lan::DiscoveryHooks hooks;hooks.capability=[&]{return dns->capability();};hooks.publish=[&](auto a){return dns->publish(std::move(a));};hooks.browse=[&]{return dns->browse();};hooks.stop_browse=[&]{dns->stop_browse();};hooks.discovery_events=[&]{return dns->poll();};hooks.interfaces=[]()->Result<std::vector<lan::NetworkInterface>>{return std::vector<lan::NetworkInterface>{{"loopback","Test loopback","127.0.0.1",1}};};hooks.listen=[&](const lan::NetworkInterface& i,std::uint16_t p){return server->listen(i.address,p,i.index);};hooks.connect=[&](auto d){return server->connect(std::move(d));};hooks.accept=[&](auto id){return server->accept(id);};hooks.enter_code=[&](auto id,auto code){return server->enter_code(id,std::move(code));};hooks.prepared=[&](auto id,auto result){server->prepared(id,result);};hooks.committed=[&](auto id,auto result){server->committed(id,result);};hooks.cancel=[&](auto id){server->cancel(id);};hooks.pairing_events=[&]{return server->poll();};hooks.stop_pairing=[&]{server->stop();};hooks.read_secret=lan::read_secret;hooks.write_secret=lan::write_secret;hooks.erase_secret=lan::erase_secret;hooks.refresh=[&]{return service->refresh();};hooks.steady_clock=[]{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};hooks.wall_clock=[]{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};discovery=std::make_unique<lan::DiscoveryService>(*store,std::move(hooks));ok(discovery->initialize());}
 ~Fixture(){discovery.reset();server.reset();dns.reset();}
};
int main(){try{
 Fixture a("Same name"),b("Same name");CHECK(!a.service->running()&&!b.service->running());CHECK(!a.discovery->snapshot().enabled);ok(b.discovery->available(true));ok(a.discovery->search());
 std::string stage="discovery";auto pump=[&](auto predicate){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(!predicate()){a.discovery->poll();b.discovery->poll();a.service->poll();b.service->poll();if(std::chrono::steady_clock::now()>until)throw std::runtime_error(stage+": "+a.discovery->snapshot().message+" / "+b.discovery->snapshot().message);std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
 auto remote=take(b.service->config()).device_id;pump([&]{return !a.discovery->snapshot().devices.empty();});CHECK(a.discovery->snapshot().devices.front().device_id==remote);ok(a.discovery->connect(remote));stage="approval";pump([&]{return b.discovery->snapshot().pairing==lan::PairingEvent::Kind::Incoming;});ok(b.discovery->accept(b.discovery->snapshot().session_id));stage="code";pump([&]{return b.discovery->snapshot().pairing==lan::PairingEvent::Kind::Code&&a.discovery->snapshot().pairing==lan::PairingEvent::Kind::EnterCode;});ok(a.discovery->enter_code(a.discovery->snapshot().session_id,b.discovery->snapshot().code));stage="commit";pump([&]{return a.discovery->snapshot().pairing==lan::PairingEvent::Kind::Complete&&b.discovery->snapshot().pairing==lan::PairingEvent::Kind::Complete;});CHECK(lan::test_keys.size()==2);CHECK(take(lan::pending_pairs(*a.store)).empty()&&take(lan::pending_pairs(*b.store)).empty());
 stage="TLS";pump([&]{return a.service->status().find("Connected")!=std::string::npos&&b.service->status().find("Connected")!=std::string::npos;});std::string text="PAKE paired clipboard";a.send({{0,"portable","text/plain",CanonicalKind::Text,Bytes(text.begin(),text.end())}});stage="text";pump([&]{return b.writes.size()==1;});CHECK(b.writes[0].representations[0].data==Bytes(text.begin(),text.end()));
 Bytes png={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};b.send({{0,"portable","image/png",CanonicalKind::Image,png}});stage="image";pump([&]{return a.writes.size()==1;});CHECK(a.writes[0].representations[0].data==png);
 a.service->stop();b.service->stop();ok(a.service->initialize());ok(b.service->initialize());stage="reconnect";pump([&]{return a.service->status().find("Connected")!=std::string::npos&&b.service->status().find("Connected")!=std::string::npos;});CHECK(lan::test_keys.size()==2);// Discover a new authenticated data port without accepting metadata as identity.
 auto moved=take(b.service->config());auto previousPort=moved.port;do{moved.port=free_port();}while(moved.port==previousPort);ok(b.service->save(moved,{}));ok(b.discovery->initialize());ok(a.discovery->search());stage="authenticated endpoint migration";pump([&]{return take(a.service->config()).peers.front().port==moved.port;});CHECK(lan::test_keys.size()==2);
 a.discovery->close_view();CHECK(b.discovery->snapshot().published);ok(b.discovery->available(false));CHECK(!b.discovery->snapshot().published);auto retained=take(b.service->config()).peers.front().secret_ref;auto forgotten=take(a.service->config());forgotten.peers.clear();ok(a.service->save(forgotten,{}));CHECK(lan::test_keys.size()==1&&lan::read_secret(retained));
 std::cout<<"PASS_isolated_bonjour_code_pairing_storage_text_image_and_restart\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

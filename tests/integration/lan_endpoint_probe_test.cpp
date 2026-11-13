#include "platform/lan/Service.hpp"
#include "platform/lan/EndpointProbe.hpp"
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
int main(){try{
 Device a,b;a.configure("A",b.port);b.configure("B",a.port);a.service->stop();auto local=take(a.service->config()),remote=take(b.service->config());ok(lan::bind_peer_identity(*a.store,local.peers[0].id,remote.device_id));local=take(a.service->config());auto peer=local.peers[0];
 lan::DiscoveryDevice candidate;candidate.device_id=remote.device_id;candidate.address="127.0.0.1";candidate.interface_address="127.0.0.1";candidate.data_port=b.port;
 auto key=take(lan::read_secret(peer.secret_ref));lan::EndpointProbe good;ok(good.start(local,peer,candidate,key));
 auto wait=[&](lan::EndpointProbe& probe){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(7);while(std::chrono::steady_clock::now()<until){b.service->poll();if(auto result=probe.poll())return *result;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return Result<void>{Error{ErrorCode::Io,"fixture timeout"}};};ok(wait(good));CHECK(a.writes.empty()&&b.writes.empty());
 // A valid PSK alone must not authorize a different pinned device identity.
 auto impostorPeer=peer;impostorPeer.remote_device_id=lan::new_id();candidate.device_id=impostorPeer.remote_device_id;lan::EndpointProbe wrongIdentity;ok(wrongIdentity.start(local,impostorPeer,candidate,key));CHECK(!wait(wrongIdentity));CHECK(take(a.service->config()).peers[0].host==peer.host);
 lan::EndpointProbe wrongKey;candidate.device_id=peer.remote_device_id;CHECK(!wrongKey.start(local,peer,candidate,std::string(64,'b')));CHECK(a.writes.empty()&&b.writes.empty());
 std::cout<<"PASS_tls_only_probe_pins_key_and_device_without_clipboard_access\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

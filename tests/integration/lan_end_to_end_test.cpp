#include "platform/lan/Service.hpp"
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
int main(){try{Device a,b;if(a.port==b.port)b.port=free_port();CHECK(!a.service->running()&&!b.service->running());a.configure("A",b.port);b.configure("B",a.port);
 std::string stage="connect";
 auto pump=[&](auto predicate){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(!predicate()){a.service->poll();b.service->poll();if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(stage+" A="+a.service->status()+" B="+b.service->status()+" counts="+std::to_string(a.count())+","+std::to_string(b.count())+" writes="+std::to_string(a.writes.size())+","+std::to_string(b.writes.size()));std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
 pump([&]{return a.service->status().find("Connected")!=std::string::npos&&b.service->status().find("Connected")!=std::string::npos;});
 stage="text";std::string text="  Cross-platform clipboard\nwith whitespace  ";a.send({{0,"portable","text/plain",CanonicalKind::Text,Bytes(text.begin(),text.end())}});pump([&]{return b.count()==1&&b.writes.size()==1;});
 auto first=take(list_history(*b.store))[0].id;CHECK(take(lan::entry_origin(*b.store,first))=="A");auto snapshot=take(load_snapshot(*b.store,first));CHECK(snapshot.representations[0].platform!="portable");
 b.service->captured(first);for(int i=0;i<100;++i){a.service->poll();b.service->poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}CHECK(a.count()==1);
 stage="PNG";Bytes png={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};b.send({{0,"portable","image/png",CanonicalKind::Image,png}});pump([&]{return a.count()==2&&a.writes.size()==1;});CHECK(a.writes.back().representations[0].data==png);
 stage="history-only reconnect";auto config=take(b.service->config());config.receive_mode=lan::ReceiveMode::HistoryOnly;ok(b.service->save(config,{}));pump([&]{return b.service->status().find("Connected")!=std::string::npos;});
 stage="URL history-only";std::string url="https://example.com/cliptiq";a.send({{0,"portable","text/uri-list",CanonicalKind::Url,Bytes(url.begin(),url.end())}});pump([&]{return b.count()==3;});CHECK(b.writes.size()==1);
 stage="three device mesh";Device c;
 auto ca=take(a.service->config()),cb=take(b.service->config()),cc=take(c.service->config());
 lan::Peer ac;ac.id=lan::new_id();ac.name="C";ac.host="localhost";ac.port=c.port;ca.peers.push_back(ac);
 lan::Peer bc=ac;bc.id=lan::new_id();cb.peers.push_back(bc);
 lan::Peer cpa;cpa.id=lan::new_id();cpa.name="A";cpa.host="localhost";cpa.port=a.port;
 lan::Peer cpb=cpa;cpb.id=lan::new_id();cpb.name="B";cpb.port=b.port;cc.device_name="C";cc.port=c.port;cc.peers={cpa,cpb};
 ok(a.service->save(ca,{{ac.id,std::string(64,'b')}}));ok(b.service->save(cb,{{bc.id,std::string(64,'c')}}));ok(c.service->save(cc,{{cpa.id,std::string(64,'b')},{cpb.id,std::string(64,'c')}}));
 // Receiving a queued event can replace the peer's UI status immediately
 // after handshake. Wait on the durable HELLO identity bindings instead of
 // requiring all six temporary "Connected" labels in the same poll.
 auto paired=[](Device& device){auto cfg=take(device.service->config());return cfg.peers.size()==2&&std::all_of(cfg.peers.begin(),cfg.peers.end(),[](const lan::Peer& peer){return !peer.remote_device_id.empty();});};
 auto mesh=[&](auto predicate){auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!predicate()){a.service->poll();b.service->poll();c.service->poll();if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(stage+" A="+a.service->status()+" B="+b.service->status()+" C="+c.service->status());std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
 mesh([&]{return paired(a)&&paired(b)&&paired(c);});
 auto an=a.count(),bn=b.count();std::string third="copy from C";c.send({{0,"portable","text/plain",CanonicalKind::Text,Bytes(third.begin(),third.end())}});
 mesh([&]{return a.count()==an+1&&b.count()==bn+1&&a.writes.size()==2;});
 CHECK(take(lan::entry_origin(*a.store,take(list_history(*a.store))[0].id))=="C");CHECK(c.count()==1);CHECK(b.writes.size()==1);
 if(std::getenv("CLIPTIQ_LAN_IDLE_BENCH")){std::cout<<"READY three connected services idle"<<std::endl;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(40);while(std::chrono::steady_clock::now()<until){a.service->poll();b.service->poll();c.service->poll();std::this_thread::sleep_for(std::chrono::milliseconds(20));}}
 a.service->stop();b.service->stop();c.service->stop();CHECK(!a.service->running()&&!b.service->running()&&!c.service->running());std::cout<<"PASS real TLS Service pair: text/PNG/URL, provenance, no echo, history-only, stop\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

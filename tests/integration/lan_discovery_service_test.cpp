#include "platform/lan/DiscoveryService.hpp"
#include <iostream>
#include <stdexcept>
#include "sqlite3.h"
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
int main(){try{
 auto store=take(open_store(":memory:"));auto cfg=take(load_config(store));std::vector<DiscoveryEvent> discovered;std::vector<PairingEvent> pairing;std::map<std::string,std::string> secrets;bool published=false;unsigned refresh=0;std::int64_t time=1000;
 DiscoveryHooks h;h.capability=[]{return NativeDiscovery::Capability{true,{}};};h.publish=[&](std::optional<DiscoveryAnnouncement> a)->Result<void>{published=a.has_value();return {};};h.browse=[]()->Result<void>{return {};};h.stop_browse=[]{};h.stop_pairing=[]{};h.discovery_events=[&]{auto events=std::move(discovered);discovered.clear();return events;};h.interfaces=[]()->Result<std::vector<NetworkInterface>>{return std::vector<NetworkInterface>{{"en0","Wi-Fi","192.168.1.1",2}};};h.listen=[](const NetworkInterface&,std::uint16_t)->Result<std::uint16_t>{return std::uint16_t(34567);};h.connect=[](DiscoveryDevice)->Result<std::string>{return std::string("session");};h.accept=[](std::string)->Result<void>{return {};};h.enter_code=[](std::string,std::string)->Result<void>{return {};};h.prepared=[](std::string,Result<void> r){CHECK(r);};h.committed=[](std::string,Result<void> r){CHECK(r);};h.cancel=[](std::string){};h.pairing_events=[&]{auto events=std::move(pairing);pairing.clear();return events;};h.write_secret=[&](std::string ref,std::string key)->Result<void>{secrets[ref]=key;return {};};h.erase_secret=[&](std::string ref)->Result<void>{secrets.erase(ref);return {};};h.refresh=[&]()->Result<void>{++refresh;return {};};h.steady_clock=[&]{return time;};h.wall_clock=h.steady_clock;
 DiscoveryService service(store,h);CHECK(service.initialize());CHECK(!published&&!service.snapshot().enabled);CHECK(service.available(true));CHECK(published);CHECK(take(load_config(store)).peers.empty());CHECK(service.search());
 DiscoveryDevice device{"remote","PC","windows","pc.local.","192.168.1.2","192.168.1.1",2,34568,49731,31000};discovered.push_back({DiscoveryEvent::Kind::Found,device,{}});CHECK(service.poll());CHECK(service.snapshot().devices.size()==1);CHECK(service.connect("remote"));
 PairingPeer peer{{"session","remote","PC","windows","192.168.1.2",49731,Bytes(32,3)},"192.168.1.2","192.168.1.1"};pairing.push_back({PairingEvent::Kind::Prepare,"session",peer,{},{},Bytes(32,5)});service.poll();CHECK(secrets.size()==1&&take(pending_pairs(store)).size()==1&&take(load_config(store)).peers.empty());
 pairing.push_back({PairingEvent::Kind::Commit,"session",peer,{},{},{}});service.poll();CHECK(refresh==1&&take(load_config(store)).peers.size()==1);
 pairing.push_back({PairingEvent::Kind::Failed,"session",peer,{},"Disconnected",{}});service.poll();CHECK(secrets.size()==1&&take(pending_pairs(store)).size()==1);CHECK(finish_authenticated_pair(store,take(load_config(store)).peers[0].id,"remote"));service.poll();CHECK(take(pending_pairs(store)).empty());
 service.close_view();CHECK(service.search());discovered.push_back({DiscoveryEvent::Kind::Found,device,{}});service.poll();
 pairing.push_back({PairingEvent::Kind::Incoming,"late",peer,{},{},{}});service.poll();service.cancel("late");
 pairing.push_back({PairingEvent::Kind::Prepare,"late",peer,{},{},Bytes(32,9)});service.poll();CHECK(secrets.size()==1&&take(pending_pairs(store)).empty());
 CHECK(service.available(false));CHECK(!published);CHECK(take(load_config(store)).peers.size()==1);service.stop();
 // A failed metadata transaction removes only the just-created credential.
 auto failedStore=take(open_store(":memory:"));take(load_config(failedStore));bool preparationRejected=false;auto badHooks=h;badHooks.prepared=[&](std::string,Result<void> result){preparationRejected=!result;};DiscoveryService badService(failedStore,badHooks);CHECK(badService.initialize());
 CHECK(sqlite3_exec(failedStore.connection(),"CREATE TRIGGER deny_stage BEFORE INSERT ON settings WHEN NEW.key GLOB 'lan.pairing.*' BEGIN SELECT RAISE(ABORT,'fixture'); END",nullptr,nullptr,nullptr)==SQLITE_OK);
 pairing.push_back({PairingEvent::Kind::Prepare,"storage-failure",peer,{},{},Bytes(32,8)});badService.poll();CHECK(preparationRejected&&secrets.size()==1&&take(pending_pairs(failedStore)).empty());badService.stop();
 // Restart discards uncommitted staging, but never an unrelated saved key.
 CHECK(sqlite3_exec(failedStore.connection(),"DROP TRIGGER deny_stage",nullptr,nullptr,nullptr)==SQLITE_OK);
 PendingPair pending;pending.session_id="restart";pending.peer_id="restart-peer";pending.remote_device_id="restart-remote";pending.secret_ref="pairing-restart";pending.auth_id=std::string(64,'c');pending.name="PC";pending.host="192.168.1.2";pending.bind_address="192.168.1.1";pending.port=49731;pending.expires_ms=time+300000;CHECK(stage_pair(failedStore,pending));secrets[pending.secret_ref]=std::string(64,'4');
 DiscoveryService restarted(failedStore,h);CHECK(restarted.initialize());CHECK(secrets.size()==1&&take(pending_pairs(failedStore)).empty());restarted.stop();
 auto unavailableHooks=h;unavailableHooks.capability=[]{return NativeDiscovery::Capability{false,"Unavailable"};};DiscoveryService unsupported(failedStore,unavailableHooks);CHECK(unsupported.initialize());CHECK(!unsupported.search());unsupported.stop();
 std::cout<<"PASS_opt_in_zero_peers_prepare_commit_disconnect_and_authenticated_recovery\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

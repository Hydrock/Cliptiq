#include "platform/lan/Discovery.hpp"
#include "cliptiq/lan.hpp"
#include "sqlite3.h"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
using namespace cliptiq::lan;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
DiscoveryDevice device(std::string id=new_id()){
 DiscoveryDevice d;d.device_id=std::move(id);d.name="Desk";d.platform="macos";
 d.host="desk.local.";d.address="192.168.1.20";d.interface_address="192.168.1.10";
 d.interface_index=3;d.control_port=49000;d.data_port=49731;d.expires_ms=100000;
 return d;
}
int main(){try{
 auto store=take(open_store(":memory:"));auto config=take(load_config(store));
 config.device_name="Original";Peer p;p.id=new_id();p.name="Existing";p.host="192.168.1.30";
 p.secret_ref="existing-ref";p.auth_id=std::string(64,'a');config.peers.push_back(p);ok(save_config(store,config));
 auto before=take(load_config(store));auto prefs=take(load_discovery_preferences(store));
 CHECK(!prefs.enabled && prefs.interface_id.empty() && prefs.revision==0);
 auto stale=prefs;prefs.enabled=true;prefs.interface_id="en0";ok(save_discovery_preferences(store,prefs));
 auto current=take(load_discovery_preferences(store));CHECK(current.enabled&&current.interface_id=="en0"&&current.revision==1);
 auto collision=save_discovery_preferences(store,stale);CHECK(!collision&&collision.error().code==ErrorCode::Busy);
 auto after=take(load_config(store));CHECK(after.device_id==before.device_id&&after.revision==before.revision);
 CHECK(after.device_name=="Original"&&after.peers.size()==1&&after.peers[0].secret_ref=="existing-ref"&&after.peers[0].host==p.host);
 CHECK(sqlite3_exec(store.connection(),"CREATE TRIGGER fail_discovery BEFORE INSERT ON settings WHEN NEW.key='lan.discovery.interface' BEGIN SELECT RAISE(ABORT,'test failure'); END",nullptr,nullptr,nullptr)==SQLITE_OK);
 current.enabled=false;current.interface_id="en1";CHECK(!save_discovery_preferences(store,current));
 auto restored=take(load_discovery_preferences(store));CHECK(restored.enabled&&restored.interface_id=="en0"&&restored.revision==1);
 CHECK(sqlite3_exec(store.connection(),"DROP TRIGGER fail_discovery; UPDATE settings SET value='yes' WHERE key='lan.discovery.enabled'",nullptr,nullptr,nullptr)==SQLITE_OK);
 CHECK(!load_discovery_preferences(store));
 std::cout<<"PASS opt_in_default_cas_rollback_and_old_pairs_unchanged\n";

 const auto local=new_id();DiscoveryRegistry registry(local);auto d=device();
 ok(registry.update(d,1000));ok(registry.update(d,1000));CHECK(registry.devices().size()==1);
 CHECK(registry.devices()[0].expires_ms==31000);
 auto alternative=d;alternative.address="192.168.2.20";alternative.interface_address="192.168.2.10";alternative.interface_index=4;
 ok(registry.update(alternative,1000));CHECK(registry.devices().size()==1&&registry.endpoints(d.device_id).size()==2);
 auto sameName=device();ok(registry.update(sameName,1000));CHECK(registry.devices().size()==2);
 registry.remove(d.device_id,3);CHECK(registry.endpoints(d.device_id).size()==1&&registry.endpoints(d.device_id)[0].interface_index==4);
 registry.expire(31001);CHECK(registry.devices().empty());
 d.expires_ms=1100;ok(registry.update(d,1000));registry.expire(1100);CHECK(registry.devices().empty());
 CHECK(!registry.update(d,1100));
 std::cout<<"PASS_duplicate_names_endpoints_expiry_and_removal\n";

 d=device(local);CHECK(!registry.update(d,1000));d=device("bad/device/id");CHECK(!registry.update(d,1000));
 d=device();d.name=std::string("bad\0name",8);CHECK(!registry.update(d,1000));
 d.name="bad\nname";CHECK(!registry.update(d,1000));d.name="bad\x7fname";CHECK(!registry.update(d,1000));
 d.name=std::string("\xc0\x80",2);CHECK(!registry.update(d,1000));d.name=std::string(257,'x');CHECK(!registry.update(d,1000));
 d.name="Рабочий Mac";ok(registry.update(d,1000));
 d=device();d.control_port=0;CHECK(!registry.update(d,1000));d=device();d.data_port=0;CHECK(!registry.update(d,1000));
 d=device();d.address="http://example.com/";CHECK(!registry.update(d,1000));
 for(auto address:{"0.0.0.0","255.255.255.255","224.0.0.1","::","ff02::fb"}){d=device();d.address=address;CHECK(!registry.update(d,1000));}
 d=device();d.name="bad\xc2\x85name";CHECK(!registry.update(d,1000));
 d.name="bad\xe2\x80\xae";CHECK(!registry.update(d,1000));
 d=device();d.address="fe80::20";d.interface_address="fe80::10";d.interface_index=0;CHECK(!registry.update(d,1000));
 d.interface_index=7;ok(registry.update(d,1000));
 std::cout<<"PASS_untrusted_identity_text_ports_and_address_validation\n";

 DiscoveryRegistry bounded(local);
 for(unsigned n=0;n<64;++n)ok(bounded.update(device(),1000));
 CHECK(!bounded.update(device(),1000));CHECK(bounded.devices().size()==64);
 bounded.expire(31001);CHECK(bounded.devices().empty());ok(bounded.update(device(),31001));
 DiscoveryRegistry perDevice(local);auto endpoint=device();
 for(unsigned n=1;n<=4;++n){endpoint.address="10.0.0."+std::to_string(n);ok(perDevice.update(endpoint,1000));}
 endpoint.address="10.0.0.5";CHECK(!perDevice.update(endpoint,1000));CHECK(perDevice.endpoints(endpoint.device_id).size()==4);
 std::cout<<"PASS_bounded_registry\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL discovery: "<<e.what()<<'\n';return 1;}}

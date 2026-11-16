#include "platform/lan/NativeDiscovery.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>
using namespace cliptiq::lan;
int main(){
 NativeDiscovery publisher("fixture-first",true),replacement("fixture-first",true),other("fixture-second",true),browser("fixture-browser",true);
 if(!publisher.capability().available)return 0;
 DiscoveryDevice a{"fixture-first","Fixture A","macos","","","",0,45991,45992,0};auto b=a;b.device_id="fixture-second";b.name="Fixture B";
 if(!publisher.publish(DiscoveryAnnouncement{a})||!other.publish(DiscoveryAnnouncement{b})||!browser.browse())return 1;
 // Keep the old registration alive to deterministically model daemon-side
 // teardown overlapping a new publication of the same identity on a new port.
 auto moved=a;moved.control_port=45993;moved.data_port=45994;
 auto republished=replacement.publish(DiscoveryAnnouncement{moved});
 if(!republished){std::cerr<<"Overlapping same-device publication: "<<republished.error().message<<'\n';return 9;}
 DiscoveryRegistry registry("fixture-browser");
 auto steady=[] {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();};
 bool first=false,second=false,migration=false;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(std::chrono::steady_clock::now()<until&&!(first&&second&&migration)){
  for(auto* pub:{&publisher,&other,&replacement})for(const auto& e:pub->poll())if(e.kind==DiscoveryEvent::Kind::Failure){std::cerr<<"Publication: "<<e.message<<'\n';return 2;}
  for(const auto& e:browser.poll()){
   if(e.kind==DiscoveryEvent::Kind::Failure){std::cerr<<e.message<<'\n';return 2;}
   if(e.kind==DiscoveryEvent::Kind::Found){if(!registry.update(e.device,steady()))return 10;first|=e.device.device_id==a.device_id&&e.device.control_port==a.control_port&&e.device.data_port==a.data_port;second|=e.device.device_id==b.device_id;migration|=e.device.device_id==a.device_id&&e.device.control_port==moved.control_port&&e.device.data_port==moved.data_port;}
  }std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 if(!first||!second||!migration){std::cerr<<"Missing isolated announcements first="<<first<<" second="<<second<<" migrated="<<migration<<'\n';return 3;}
 // The old instance's goodbye must leave the replacement discoverable.
 if(!publisher.publish({}))return 11;bool retired=false;
 auto replacement_retained=[&]{auto endpoints=registry.endpoints(a.device_id);return !endpoints.empty()&&std::all_of(endpoints.begin(),endpoints.end(),[&](const auto& endpoint){return endpoint.control_port==moved.control_port&&endpoint.data_port==moved.data_port;});};
 while(std::chrono::steady_clock::now()<until&&!(retired&&replacement_retained())){
  replacement.poll();other.poll();for(const auto& e:browser.poll()){
   if(e.kind==DiscoveryEvent::Kind::Failure){std::cerr<<e.message<<'\n';return 2;}
   if(e.kind==DiscoveryEvent::Kind::Found){if(!registry.update(e.device,steady()))return 10;}
   if(e.kind==DiscoveryEvent::Kind::Removed){registry.remove(e.device);retired|=e.device.device_id==a.device_id&&e.device.control_port==a.control_port;}
  }std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 if(!retired||!replacement_retained()){std::cerr<<"Old goodbye removed replacement endpoint\n";return 12;}
 browser.stop_browse();if(!browser.poll().empty())return 4;
 if(!browser.browse(1))return 5;std::this_thread::sleep_for(std::chrono::milliseconds(5));browser.poll();
 // Publication survives a search deadline, and own-device responses stay hidden.
 NativeDiscovery selfBrowser("fixture-first",true);if(!selfBrowser.browse())return 6;bool peerSeen=false;
 auto retryUntil=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(std::chrono::steady_clock::now()<retryUntil&&!peerSeen){publisher.poll();other.poll();for(const auto& e:selfBrowser.poll())if(e.kind==DiscoveryEvent::Kind::Found){if(e.device.device_id==a.device_id)return 7;peerSeen|=e.device.device_id==b.device_id;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 if(!peerSeen)return 8;selfBrowser.stop();publisher.publish({});replacement.publish({});other.publish({});browser.stop();std::cout<<"PASS_isolated_bonjour_two_devices_cancel_and_deadline\n";return 0;
}

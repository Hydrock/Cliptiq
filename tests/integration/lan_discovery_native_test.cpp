#include "platform/lan/NativeDiscovery.hpp"
#include <chrono>
#include <thread>
#include <iostream>
using namespace cliptiq::lan;
int main(){
 NativeDiscovery publisher("fixture-first",true),other("fixture-second",true),browser("fixture-browser",true);
 if(!publisher.capability().available)return 0;
 DiscoveryDevice a{"fixture-first","Fixture A","macos","","","",0,45991,45992,0};auto b=a;b.device_id="fixture-second";b.name="Fixture B";
 if(!publisher.publish(DiscoveryAnnouncement{a})||!other.publish(DiscoveryAnnouncement{b})||!browser.browse())return 1;
 bool first=false,second=false;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
 while(std::chrono::steady_clock::now()<until&&!(first&&second)){
  publisher.poll();other.poll();for(const auto& e:browser.poll()){
   if(e.kind==DiscoveryEvent::Kind::Failure){std::cerr<<e.message<<'\n';return 2;}
   if(e.kind==DiscoveryEvent::Kind::Found){first|=e.device.device_id==a.device_id;second|=e.device.device_id==b.device_id;}
  }std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 if(!first||!second){std::cerr<<"Missing isolated announcements "<<first<<second<<'\n';return 3;}
 browser.stop_browse();if(!browser.poll().empty())return 4;
 if(!browser.browse(1))return 5;std::this_thread::sleep_for(std::chrono::milliseconds(5));browser.poll();
 // Publication survives a search deadline, and own-device responses stay hidden.
 NativeDiscovery selfBrowser("fixture-first",true);if(!selfBrowser.browse())return 6;bool peerSeen=false;
 auto retryUntil=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(std::chrono::steady_clock::now()<retryUntil&&!peerSeen){publisher.poll();other.poll();for(const auto& e:selfBrowser.poll())if(e.kind==DiscoveryEvent::Kind::Found){if(e.device.device_id==a.device_id)return 7;peerSeen|=e.device.device_id==b.device_id;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 if(!peerSeen)return 8;selfBrowser.stop();publisher.publish({});other.publish({});browser.stop();std::cout<<"PASS_isolated_bonjour_two_devices_cancel_and_deadline\n";return 0;
}

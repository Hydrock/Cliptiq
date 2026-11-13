#include "platform/lan/PairingServer.hpp"
#include <chrono>
#include <thread>
#include <iostream>
using namespace cliptiq;using namespace cliptiq::lan;
int main(){
 PairingServer a("control-a","A","test"),b("control-b","B","test");auto portA=a.listen("127.0.0.1",49731),portB=b.listen("127.0.0.1",49732);if(!portA||!portB)return 1;
 DiscoveryDevice d{"control-b","B","test","localhost","127.0.0.1","127.0.0.1",0,portB.value(),49732,0};auto id=a.connect(d);if(!id)return 2;
 std::string code;bool enter=false,completeA=false,completeB=false;Bytes keyA,keyB;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(6);
 while(std::chrono::steady_clock::now()<until&&!(completeA&&completeB)){
  for(auto& e:a.poll()){switch(e.kind){case PairingEvent::Kind::EnterCode:enter=true;break;case PairingEvent::Kind::Prepare:keyA=e.key;a.prepared(e.session_id,{});break;case PairingEvent::Kind::Commit:a.committed(e.session_id,{});break;case PairingEvent::Kind::Complete:completeA=true;break;case PairingEvent::Kind::Failed:std::cerr<<e.message;return 3;default:break;}}
  for(auto& e:b.poll()){switch(e.kind){case PairingEvent::Kind::Incoming:if(!b.accept(e.session_id))return 4;break;case PairingEvent::Kind::Code:code=e.code;break;case PairingEvent::Kind::Prepare:keyB=e.key;b.prepared(e.session_id,{});break;case PairingEvent::Kind::Commit:b.committed(e.session_id,{});break;case PairingEvent::Kind::Complete:completeB=true;break;case PairingEvent::Kind::Failed:std::cerr<<e.message;return 5;default:break;}}
  if(enter&&!code.empty()){if(!a.enter_code(id.value(),code))return 6;enter=false;code.clear();}
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 a.stop();if(!completeA||!completeB||keyA.size()!=32||keyA!=keyB)return 7;
 PairingServer first("concurrent-a","First","test"),second("concurrent-c","Second","test");auto firstId=first.connect(d),secondId=second.connect(d);if(!firstId||!secondId||firstId.value()==secondId.value())return 8;
 unsigned incoming=0,codes=0;std::string activeId;auto concurrentUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(600);
 while(std::chrono::steady_clock::now()<concurrentUntil){for(auto& event:b.poll()){if(event.kind==PairingEvent::Kind::Incoming){++incoming;activeId=event.session_id;b.accept(event.session_id);}if(event.kind==PairingEvent::Kind::Code)++codes;}first.poll();second.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 if(incoming!=1||codes!=1)return 9;first.stop();second.stop();bool disconnected=false;auto disconnectUntil=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(std::chrono::steady_clock::now()<disconnectUntil&&!disconnected){for(auto& event:b.poll())if(event.kind==PairingEvent::Kind::Failed&&event.session_id==activeId)disconnected=true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}b.stop();if(!disconnected)return 10;
 std::cout<<"PASS_nonblocking_loopback_control_and_staged_completion\n";return 0;
}

#include "platform/lan/PairingSession.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
PairingHello hello(const char* id,int nonce){return {"session",id,id,"test","127.0.0.1",49731,Bytes(32,static_cast<std::uint8_t>(nonce))};}
int main(){try{
 std::int64_t clock=100;auto time=[&]{return clock;};auto random=[](size_t n)->Result<Bytes>{return Bytes(n,7);};
 PairingSession a(PairingRole::Initiator,hello("a",1),"127.0.0.1","127.0.0.1",time,random),b(PairingRole::Responder,hello("b",2),"127.0.0.1","127.0.0.1",time,random);
 CHECK(a.start());for(auto& wire:a.take_wire())CHECK(b.receive(wire));auto e=b.take_events();CHECK(e.size()==1&&e[0].kind==PairingEvent::Kind::Incoming);CHECK(b.take_wire().empty());
 CHECK(b.accept());e=b.take_events();CHECK(e.size()==1&&e[0].kind==PairingEvent::Kind::Code);auto code=e[0].code;CHECK(code.size()==6);
 for(auto& wire:b.take_wire())CHECK(a.receive(wire));CHECK(a.enter_code(code));
 bool preparedA=false,preparedB=false,completeA=false,completeB=false;Bytes keyA,keyB;
 for(int i=0;i<20;++i){for(auto& w:a.take_wire())CHECK(b.receive(w));for(auto& w:b.take_wire())CHECK(a.receive(w));
  for(auto& event:a.take_events()){if(event.kind==PairingEvent::Kind::Prepare){keyA=event.key;preparedA=true;CHECK(a.prepared({}));}if(event.kind==PairingEvent::Kind::Commit)CHECK(a.committed({}));if(event.kind==PairingEvent::Kind::Complete)completeA=true;CHECK(event.kind!=PairingEvent::Kind::Failed);}
  for(auto& event:b.take_events()){if(event.kind==PairingEvent::Kind::Prepare){keyB=event.key;preparedB=true;CHECK(b.prepared({}));}if(event.kind==PairingEvent::Kind::Commit)CHECK(b.committed({}));if(event.kind==PairingEvent::Kind::Complete)completeB=true;CHECK(event.kind!=PairingEvent::Kind::Failed);}
 }
 CHECK(preparedA&&preparedB&&keyA.size()==32&&keyA==keyB&&completeA&&completeB);
 PairingSession expired(PairingRole::Initiator,hello("a",3),"127.0.0.1","127.0.0.1",time,random);CHECK(expired.start());clock+=120001;expired.tick();CHECK(expired.finished());CHECK(!expired.enter_code("123456"));
 // Invalid input must be rejected before the peer round arrives; repeat entry cannot reset PAKE.
 PairingSession invalidCode(PairingRole::Initiator,hello("a",4),"127.0.0.1","127.0.0.1",time,random),receiver(PairingRole::Responder,hello("b",5),"127.0.0.1","127.0.0.1",time,random);
 CHECK(invalidCode.start());for(auto& w:invalidCode.take_wire())CHECK(receiver.receive(w));receiver.take_events();CHECK(receiver.accept());auto accepted=receiver.take_wire();CHECK(invalidCode.receive(accepted.front()));CHECK(!invalidCode.enter_code("bad"));
 PairingSession wrong(PairingRole::Initiator,hello("a",6),"127.0.0.1","127.0.0.1",time,random),target(PairingRole::Responder,hello("b",7),"127.0.0.1","127.0.0.1",time,random);
 CHECK(wrong.start());for(auto& w:wrong.take_wire())CHECK(target.receive(w));target.take_events();CHECK(target.accept());target.take_events();for(auto& w:target.take_wire())CHECK(wrong.receive(w));CHECK(wrong.enter_code("000000"));bool exposed=false;
 for(int i=0;i<8;++i){for(auto& w:wrong.take_wire())target.receive(w);for(auto& w:target.take_wire())wrong.receive(w);for(auto& event:wrong.take_events())exposed|=!event.key.empty();for(auto& event:target.take_events())exposed|=!event.key.empty();}CHECK(!exposed);CHECK(wrong.finished()||target.finished());
 PairingSession duplicate(PairingRole::Responder,hello("b",8),"127.0.0.1","127.0.0.1",time,random),requester(PairingRole::Initiator,hello("a",9),"127.0.0.1","127.0.0.1",time,random);
 CHECK(requester.start());auto request=requester.take_wire().front();CHECK(duplicate.receive(request));CHECK(!duplicate.receive(request));CHECK(duplicate.finished());
 PairingSession denied(PairingRole::Responder,hello("b",10),"127.0.0.1","127.0.0.1",time,random);CHECK(denied.receive(request));denied.cancel();CHECK(!denied.accept());CHECK(denied.take_wire().empty());
 for(bool storageFailure:{false,true}){
  PairingSession left(PairingRole::Initiator,hello("a",11),"127.0.0.1","127.0.0.1",time,random),right(PairingRole::Responder,hello("b",12),"127.0.0.1","127.0.0.1",time,random);
  CHECK(left.start());for(auto& w:left.take_wire())CHECK(right.receive(w));right.take_events();CHECK(right.accept());auto pin=right.take_events().front().code;for(auto& w:right.take_wire())CHECK(left.receive(w));left.take_events();CHECK(left.enter_code(pin));
  bool doneLeft=false,doneRight=false,committedRight=false;unsigned commits=0;
  for(int loop=0;loop<16;++loop){for(auto& w:left.take_wire()){auto f=decode_pairing_frame(w);CHECK(f);if(f.value().kind!=PairingMessage::Done)right.receive(w);}for(auto& w:right.take_wire())left.receive(w);
   for(auto& event:left.take_events()){if(event.kind==PairingEvent::Kind::Prepare)left.prepared(storageFailure?Result<void>{Error{ErrorCode::Io,"fixture storage failure"}}:Result<void>{});if(event.kind==PairingEvent::Kind::Commit){++commits;left.committed({});}if(event.kind==PairingEvent::Kind::Complete)doneLeft=true;}
   for(auto& event:right.take_events()){if(event.kind==PairingEvent::Kind::Prepare)right.prepared({});if(event.kind==PairingEvent::Kind::Commit){++commits;committedRight=true;right.committed({});}if(event.kind==PairingEvent::Kind::Complete)doneRight=true;}
  }
  if(storageFailure){CHECK(!doneLeft&&!doneRight&&!committedRight&&commits==0);}else{CHECK(doneLeft&&!doneRight&&committedRight&&commits==2);clock+=120001;right.tick();CHECK(right.finished());}
 }
 PairingRateLimiter rate;for(int i=0;i<3;++i)CHECK(rate.admit("192.168.1.1",100));CHECK(!rate.admit("192.168.1.1",100));for(int i=0;i<7;++i)CHECK(rate.admit("192.168.1."+std::to_string(i+2),100));CHECK(!rate.admit("other",100));CHECK(rate.admit("192.168.1.1",60101));
 std::cout<<"PASS_approval_code_mutual_proof_staging_commit_expiry_and_rate_caps\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

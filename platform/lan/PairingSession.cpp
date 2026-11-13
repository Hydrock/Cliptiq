#include "PairingSession.hpp"
#include "CryptoRuntime.hpp"
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#include <algorithm>
#include <cstdio>
namespace cliptiq::lan {
namespace {Error failure(){return {ErrorCode::InvalidArgument,"Pairing failed. Start again with a new code."};}void wipe(Bytes& b){if(!b.empty())mbedtls_platform_zeroize(b.data(),b.size());b.clear();}void wipe(std::string& s){if(!s.empty())mbedtls_platform_zeroize(s.data(),s.size());s.clear();}}
Result<Bytes> pairing_random(size_t size){
 if(size==0||size>1024)return Error{ErrorCode::InvalidArgument,"Invalid random byte count"};auto init=initialize_crypto_runtime();if(!init)return init.error();
 mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context drbg;mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&drbg);const unsigned char label[]="Cliptiq pairing randomness v1";
 int result=mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&entropy,label,sizeof label-1);Bytes bytes(size);if(!result)result=mbedtls_ctr_drbg_random(&drbg,bytes.data(),bytes.size());mbedtls_ctr_drbg_free(&drbg);mbedtls_entropy_free(&entropy);if(result){wipe(bytes);return Error{ErrorCode::Io,"Secure randomness unavailable"};}return bytes;
}
bool PairingRateLimiter::admit(const std::string& address,std::int64_t now){
 auto trim=[&](auto& v){v.erase(std::remove_if(v.begin(),v.end(),[&](auto t){return now<t||now-t>=60000;}),v.end());};trim(global_);for(auto it=requests_.begin();it!=requests_.end();){trim(it->second);if(it->second.empty())it=requests_.erase(it);else ++it;}
 if(address.empty()||address.size()>128||global_.size()>=10)return false;auto it=requests_.find(address);if(it!=requests_.end()&&it->second.size()>=3)return false;
 global_.push_back(now);requests_[address].push_back(now);return true;
}
struct PairingSession::State {
 enum class Phase {New,Request,Approval,Accept,Code,Pake,Proof,Prepare,Commit,Committed,Done,Failed};
 std::uint32_t interface_index=0;PairingRole role;PairingHello local,remote;std::string remote_address,local_address,expected;Clock clock;Random random;std::int64_t deadline;Phase phase=Phase::New;
 std::unique_ptr<PairingCrypto> crypto;Bytes r1a,r1b,r2a,r2b,context;std::vector<Bytes> outgoing;std::vector<PairingEvent> events;bool own_prepared=false,peer_prepared=false;
 State(PairingRole r,PairingHello l,std::string ra,std::string la,Clock c,Random rng,std::string expected):role(r),local(std::move(l)),remote_address(std::move(ra)),local_address(std::move(la)),expected(std::move(expected)),clock(std::move(c)),random(std::move(rng)),deadline(clock()+120000){}
 ~State(){for(auto& e:events){wipe(e.key);wipe(e.code);}}
 bool terminal()const{return phase==Phase::Done||phase==Phase::Failed;}
 void event(PairingEvent::Kind kind,std::string code={},Bytes key={}){if(events.size()>=64){fail();return;}events.push_back({kind,local.session_id,{remote,remote_address,local_address,interface_index},std::move(code),{},std::move(key)});}
 Result<void> fail(){if(!terminal()){phase=Phase::Failed;crypto.reset();for(auto& e:events){wipe(e.key);wipe(e.code);}events.clear();outgoing.clear();events.push_back({PairingEvent::Kind::Failed,local.session_id,{remote,remote_address,local_address,interface_index},{},failure().message,{}});}return failure();}
 Result<void> send(PairingMessage kind,Bytes payload={}){
  PairingFrame frame{kind,local.session_id,std::move(payload)};auto wire=kind>=PairingMessage::Prepared?crypto->seal_control(context,std::move(frame)):encode_pairing_frame(frame);if(!wire)return fail();size_t bytes=wire.value().size();for(const auto& w:outgoing)bytes+=w.size();if(bytes>65536)return fail();outgoing.push_back(std::move(wire.value()));return {};
 }
 Result<void> prepare_proof(){
  auto t=role==PairingRole::Initiator?pairing_transcript(local,remote,r1a,r1b,r2a,r2b):pairing_transcript(remote,local,r1a,r1b,r2a,r2b);if(!t)return fail();context=std::move(t.value());auto p=crypto->proof(context,role);if(!p)return fail();phase=Phase::Proof;return send(PairingMessage::Proof,std::move(p.value()));
 }
 Result<void> initiator_rounds(){if(r1b.empty())return {};if(!crypto->read_round_one(r1b))return fail();auto first=crypto->round_one();if(!first)return fail();r1a=first.value();auto second=crypto->round_two();if(!second)return fail();r2a=second.value();if(!send(PairingMessage::RoundOne,r1a))return fail();phase=Phase::Pake;return send(PairingMessage::RoundTwo,r2a);}
 Result<void> maybe_commit(){if(role==PairingRole::Initiator&&phase==Phase::Prepare&&own_prepared&&peer_prepared){phase=Phase::Commit;return send(PairingMessage::Commit);}return {};}
};
PairingSession::PairingSession(PairingRole role,PairingHello local,std::string remote_address,std::string local_address,Clock clock,Random rng,std::string expected,std::uint32_t index):state_(std::make_unique<State>(role,std::move(local),std::move(remote_address),std::move(local_address),std::move(clock),std::move(rng),std::move(expected))){state_->interface_index=index;}
PairingSession::~PairingSession()=default;
Result<void> PairingSession::start(){auto& s=*state_;if(s.role!=PairingRole::Initiator||s.phase!=State::Phase::New)return s.fail();auto hello=encode_pairing_hello(s.local);if(!hello)return s.fail();s.phase=State::Phase::Request;return s.send(PairingMessage::Request,std::move(hello.value()));}
Result<void> PairingSession::accept(){
 auto& s=*state_;if(s.phase!=State::Phase::Approval||s.role!=PairingRole::Responder||s.clock()>=s.deadline)return s.fail();
 std::string code;for(int tries=0;tries<32&&code.empty();++tries){auto random=s.random(4);if(!random||random.value().size()!=4)return s.fail();std::uint32_t n=0;for(auto b:random.value())n=(n<<8)|b;wipe(random.value());if(n<4294000000u){char text[7]{};std::snprintf(text,sizeof text,"%06u",n%1000000u);code=text;mbedtls_platform_zeroize(text,sizeof text);}}
 if(code.empty())return s.fail();s.deadline=s.clock()+120000;s.crypto=std::make_unique<PairingCrypto>(s.role,code);auto first=s.crypto->round_one();if(!first){wipe(code);return s.fail();}s.r1b=first.value();auto hello=encode_pairing_hello(s.local);if(!hello){wipe(code);return s.fail();}s.phase=State::Phase::Pake;s.event(PairingEvent::Kind::Code,std::move(code));if(!s.send(PairingMessage::Accept,std::move(hello.value())))return s.fail();return s.send(PairingMessage::RoundOne,s.r1b);
}
Result<void> PairingSession::enter_code(std::string code){auto& s=*state_;if(s.role!=PairingRole::Initiator||s.phase!=State::Phase::Code||s.crypto||code.size()!=6||!std::all_of(code.begin(),code.end(),[](unsigned char c){return c>='0'&&c<='9';})||s.clock()>=s.deadline){wipe(code);return s.fail();}s.crypto=std::make_unique<PairingCrypto>(s.role,code);wipe(code);return s.initiator_rounds();}
Result<void> PairingSession::receive(const Bytes& wire){
 auto& s=*state_;if(s.terminal()||s.clock()>=s.deadline)return s.fail();auto parsed=decode_pairing_frame(wire);if(!parsed)return s.fail();auto frame=std::move(parsed.value());
 if(frame.kind==PairingMessage::Request||frame.kind==PairingMessage::Accept){
  if((frame.kind==PairingMessage::Request&&(s.role!=PairingRole::Responder||s.phase!=State::Phase::New))||(frame.kind==PairingMessage::Accept&&(s.role!=PairingRole::Initiator||s.phase!=State::Phase::Request)))return s.fail();
  auto hello=decode_pairing_hello(frame.payload);if(!hello||hello.value().session_id!=frame.session_id||hello.value().device_id==s.local.device_id||hello.value().data_address!=s.remote_address||(!s.expected.empty()&&hello.value().device_id!=s.expected))return s.fail();
  if(s.role==PairingRole::Initiator&&frame.session_id!=s.local.session_id)return s.fail();
  if(s.role==PairingRole::Responder)s.local.session_id=frame.session_id;s.remote=std::move(hello.value());if(s.role==PairingRole::Initiator)s.deadline=s.clock()+120000;
  if(s.local.nonce==s.remote.nonce)return s.fail();
  s.phase=s.role==PairingRole::Responder?State::Phase::Approval:State::Phase::Code;s.event(s.role==PairingRole::Responder?PairingEvent::Kind::Incoming:PairingEvent::Kind::EnterCode);return {};
 }
 if(frame.session_id!=s.local.session_id)return s.fail();
 if(frame.kind>=PairingMessage::Prepared){if(!s.crypto||s.context.empty())return s.fail();auto checked=s.crypto->open_control(s.context,wire);if(!checked)return s.fail();frame=std::move(checked.value());if(!frame.payload.empty())return s.fail();}
 switch(frame.kind){
 case PairingMessage::RoundOne:
  if(s.role==PairingRole::Initiator){if(s.phase!=State::Phase::Code||!s.r1b.empty())return s.fail();s.r1b=std::move(frame.payload);return s.crypto?s.initiator_rounds():Result<void>{};}
  if(s.phase!=State::Phase::Pake||!s.r1a.empty()||!s.crypto->read_round_one(frame.payload))return s.fail();s.r1a=std::move(frame.payload);return {};
 case PairingMessage::RoundTwo:
  if(s.phase!=State::Phase::Pake||!s.crypto)return s.fail();
  if(s.role==PairingRole::Responder){if(s.r1a.empty()||!s.r2a.empty()||!s.crypto->read_round_two(frame.payload))return s.fail();s.r2a=std::move(frame.payload);auto second=s.crypto->round_two();if(!second)return s.fail();s.r2b=second.value();if(!s.send(PairingMessage::RoundTwo,s.r2b))return s.fail();}
  else {if(!s.r2b.empty()||!s.crypto->read_round_two(frame.payload))return s.fail();s.r2b=std::move(frame.payload);}return s.prepare_proof();
 case PairingMessage::Proof:{
  if(s.phase!=State::Phase::Proof||!s.crypto->verify_proof(s.context,s.role==PairingRole::Initiator?PairingRole::Responder:PairingRole::Initiator,frame.payload))return s.fail();auto key=s.crypto->derive(s.context);if(!key)return s.fail();s.phase=State::Phase::Prepare;s.event(PairingEvent::Kind::Prepare,{},std::move(key.value()));return {};}
 case PairingMessage::Prepared:
  if(s.phase!=State::Phase::Prepare||s.peer_prepared)return s.fail();s.peer_prepared=true;return s.maybe_commit();
 case PairingMessage::Commit:
  if(s.role!=PairingRole::Responder||s.phase!=State::Phase::Prepare||!s.peer_prepared||!s.own_prepared)return s.fail();s.phase=State::Phase::Commit;s.event(PairingEvent::Kind::Commit);return {};
 case PairingMessage::Committed:
  if(s.role!=PairingRole::Initiator||s.phase!=State::Phase::Commit)return s.fail();s.phase=State::Phase::Committed;s.event(PairingEvent::Kind::Commit);return {};
 case PairingMessage::Done:
  if(s.role!=PairingRole::Responder||s.phase!=State::Phase::Committed)return s.fail();s.phase=State::Phase::Done;s.event(PairingEvent::Kind::Complete);return {};
 case PairingMessage::Abort:return s.fail();
 default:return s.fail();
 }
}
Result<void> PairingSession::prepared(Result<void> result){auto& s=*state_;if(!result||s.phase!=State::Phase::Prepare||s.own_prepared)return s.fail();s.own_prepared=true;if(!s.send(PairingMessage::Prepared))return s.fail();return s.maybe_commit();}
Result<void> PairingSession::committed(Result<void> result){auto& s=*state_;if(!result)return s.fail();if(s.role==PairingRole::Responder&&s.phase==State::Phase::Commit){s.phase=State::Phase::Committed;return s.send(PairingMessage::Committed);}if(s.role==PairingRole::Initiator&&s.phase==State::Phase::Committed){if(!s.send(PairingMessage::Done))return s.fail();s.phase=State::Phase::Done;s.event(PairingEvent::Kind::Complete);return {};}return s.fail();}
void PairingSession::cancel(){state_->fail();}void PairingSession::tick(){if(!state_->terminal()&&state_->clock()>=state_->deadline)state_->fail();}
bool PairingSession::finished()const{return state_->terminal();}bool PairingSession::awaiting_request()const{return state_->phase==State::Phase::New;}
std::vector<Bytes> PairingSession::take_wire(){std::vector<Bytes> out;out.swap(state_->outgoing);return out;}
std::vector<PairingEvent> PairingSession::take_events(){std::vector<PairingEvent> out;out.swap(state_->events);return out;}
std::string PairingSession::session_id()const{return state_->local.session_id;}
}

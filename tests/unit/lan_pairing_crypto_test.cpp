#include "platform/lan/PairingCrypto.hpp"
#include "platform/lan/PairingWire.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void rounds(PairingCrypto& a,PairingCrypto& b){
 auto a1=take(a.round_one()),b1=take(b.round_one());CHECK(a.read_round_one(b1));CHECK(b.read_round_one(a1));
 auto a2=take(a.round_two()),b2=take(b.round_two());CHECK(a.read_round_two(b2));CHECK(b.read_round_two(a2));
}
Bytes unhex(const std::string& s){Bytes b;for(size_t n=0;n<s.size();n+=2)b.push_back(static_cast<unsigned char>(std::stoul(s.substr(n,2),nullptr,16)));return b;}
int main(int argc,char** argv){try{
 Bytes context{'t','r','a','n','s','c','r','i','p','t',1};
 PairingCrypto a(PairingRole::Initiator,"123456"),b(PairingRole::Responder,"123456");
 CHECK(!a.round_two());CHECK(!a.derive(context));rounds(a,b);
 CHECK(!a.round_one());CHECK(!a.derive(context));
 auto ap=take(a.proof(context,PairingRole::Initiator)),bp=take(b.proof(context,PairingRole::Responder));
 CHECK(ap.size()==32&&bp.size()==32&&ap!=bp);
 CHECK(a.verify_proof(context,PairingRole::Responder,bp));CHECK(b.verify_proof(context,PairingRole::Initiator,ap));
 auto key=take(a.derive(context));CHECK(key.size()==32&&key==take(b.derive(context)));
 CHECK(std::any_of(key.begin(),key.end(),[](unsigned char c){return c!=0;}));
 CHECK(key!=ap&&key!=bp);CHECK(!a.verify_proof(context,PairingRole::Initiator,ap));
 auto prepared=take(a.seal_control(context,{PairingMessage::Prepared,"session",{}}));
 auto opened=take(b.open_control(context,prepared));CHECK(opened.kind==PairingMessage::Prepared&&opened.session_id=="session"&&opened.payload.empty());
 auto reply=take(b.seal_control(context,{PairingMessage::Prepared,"session",{1}}));CHECK(take(a.open_control(context,reply)).payload==Bytes({1}));
 auto committed=take(a.seal_control(context,{PairingMessage::Commit,"session",{}}));CHECK(take(b.open_control(context,committed)).kind==PairingMessage::Commit);
 CHECK(!b.open_control(context,prepared));CHECK(!a.open_control(context,prepared));
 std::cout<<"PASS_pake_mutual_confirmation_and_separated_keys\n";

 PairingCrypto authA(PairingRole::Initiator,"123456"),authB(PairingRole::Responder,"123456");rounds(authA,authB);
 auto authAp=take(authA.proof(context,PairingRole::Initiator)),authBp=take(authB.proof(context,PairingRole::Responder));
 CHECK(authA.verify_proof(context,PairingRole::Responder,authBp));CHECK(authB.verify_proof(context,PairingRole::Initiator,authAp));
 auto tampered=take(authA.seal_control(context,{PairingMessage::Commit,"session",{}}));
 CHECK(!authA.seal_control(context,{PairingMessage::Request,"session",{}}));
 CHECK(!authA.open_control(context,tampered)); // reflection with the expected first sequence
 tampered.back()^=1;CHECK(!authB.open_control(context,tampered));
 std::cout<<"PASS_authenticated_control_tampering_replay_and_reflection\n";

 PairingCrypto wrongA(PairingRole::Initiator,"123456"),wrongB(PairingRole::Responder,"123457");rounds(wrongA,wrongB);
 auto wrongProof=take(wrongA.proof(context,PairingRole::Initiator));CHECK(!wrongB.verify_proof(context,PairingRole::Initiator,wrongProof));CHECK(!wrongB.derive(context));
 PairingCrypto changedA(PairingRole::Initiator,"123456"),changedB(PairingRole::Responder,"123456");rounds(changedA,changedB);
 auto changedProof=take(changedA.proof(context,PairingRole::Initiator));auto altered=context;altered.back()^=1;
 CHECK(!changedB.verify_proof(altered,PairingRole::Initiator,changedProof));
 PairingCrypto replayA(PairingRole::Initiator,"123456"),replayB(PairingRole::Responder,"123456");rounds(replayA,replayB);
 CHECK(!replayB.verify_proof(context,PairingRole::Initiator,changedProof));
 CHECK(!PairingCrypto(PairingRole::Initiator,"12345").round_one());CHECK(!PairingCrypto(PairingRole::Initiator,"12a456").round_one());
 PairingCrypto malformed(PairingRole::Responder,"000000");CHECK(!malformed.read_round_one(Bytes(8193,0)));
 std::cout<<"PASS_wrong_code_context_replay_and_input_bounds\n";

 CHECK(argc==2);std::ifstream vectors(argv[1]);CHECK(vectors.good());std::string line;unsigned count=0,valid=0;
 while(std::getline(vectors,line))if(line.rfind("read_round_one:",0)==0){
  auto first=line.find('"'),last=line.find('"',first+1);CHECK(first!=std::string::npos&&last!=std::string::npos);
  auto role=line.find("MBEDTLS_ECJPAKE_CLIENT")!=std::string::npos?PairingRole::Initiator:PairingRole::Responder;
  PairingCrypto fixture(role,"123456");CHECK(fixture.round_one());auto result=fixture.read_round_one(unhex(line.substr(first+1,last-first-1)));
  bool success=line.substr(last+1)==":0";CHECK(bool(result)==success);++count;if(success)++valid;
 }
 CHECK(count>=10&&valid>=2);std::cout<<"PASS_upstream_round_one_vectors "<<count<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"FAIL pairing crypto: "<<e.what()<<'\n';return 1;}}

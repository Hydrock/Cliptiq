#include "platform/lan/PairingWire.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
int main(){try{
 PairingFrame frame{PairingMessage::Request,"a",{1,2}};
 Bytes known{0,0,0,11,'C','T','P',1,0,1,'a',0,2,1,2};
 CHECK(take(encode_pairing_frame(frame))==known);
 auto decoded=take(decode_pairing_frame(known));CHECK(decoded.kind==frame.kind&&decoded.session_id=="a"&&decoded.payload==frame.payload);
 for(size_t n=0;n<known.size();++n)CHECK(!decode_pairing_frame(Bytes(known.begin(),known.begin()+n)));
 auto bad=known;bad.push_back(0);CHECK(!decode_pairing_frame(bad));bad=known;bad[7]=2;CHECK(!decode_pairing_frame(bad));bad=known;bad[8]=255;CHECK(!decode_pairing_frame(bad));
 frame.payload.resize(8192);CHECK(!encode_pairing_frame(frame));frame.payload.clear();frame.session_id="bad/id";CHECK(!encode_pairing_frame(frame));
 CHECK(!decode_pairing_frame(Bytes(8193,0)));
 std::cout<<"PASS_frame_vector_truncation_version_and_bounds\n";

 PairingHello a{"session","first","Mac","macos","192.168.1.10",49731,Bytes(32,1)};
 PairingHello b{"session","second","PC","windows","192.168.1.20",49731,Bytes(32,2)};
 auto hello=take(encode_pairing_hello(a));auto back=take(decode_pairing_hello(hello));CHECK(back.device_id==a.device_id&&back.nonce==a.nonce&&back.data_port==49731);
 for(size_t n=0;n<hello.size();++n)CHECK(!decode_pairing_hello(Bytes(hello.begin(),hello.begin()+n)));
 auto invalid=a;invalid.name="bad\nname";CHECK(!encode_pairing_hello(invalid));invalid=a;invalid.nonce.resize(31);CHECK(!encode_pairing_hello(invalid));invalid=a;invalid.data_address="example.com";CHECK(!encode_pairing_hello(invalid));
 auto transcript=take(pairing_transcript(a,b,{1},{2},{3},{4}));
 auto changed=b;changed.nonce.back()^=1;CHECK(transcript!=take(pairing_transcript(a,changed,{1},{2},{3},{4})));
 changed=b;changed.data_port=49732;CHECK(transcript!=take(pairing_transcript(a,changed,{1},{2},{3},{4})));
 CHECK(transcript!=take(pairing_transcript(b,a,{2},{1},{4},{3})));
 CHECK(transcript!=take(pairing_transcript(a,b,{2},{1},{3},{4})));
 changed=b;changed.session_id="another";CHECK(!pairing_transcript(a,changed,{1},{2},{3},{4}));
 CHECK(!pairing_transcript(a,a,{1},{2},{3},{4}));CHECK(!pairing_transcript(a,b,{},{2},{3},{4}));
 std::cout<<"PASS_hello_and_canonical_transcript_binding\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL pairing wire: "<<e.what()<<'\n';return 1;}}

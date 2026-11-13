#include "PairingWire.hpp"
#include "Discovery.hpp"
#include "Text.hpp"
#include <algorithm>
#include <array>
namespace cliptiq::lan {
namespace {
Error invalid(){return {ErrorCode::InvalidArgument,"Invalid pairing message"};}
void number(Bytes& out,std::uint64_t value,unsigned n){while(n--)out.push_back(static_cast<unsigned char>(value>>(n*8)));}
void text(Bytes& out,const std::string& value){number(out,value.size(),2);out.insert(out.end(),value.begin(),value.end());}
struct Reader {
 const Bytes& data;size_t at=0;bool good=true;
 std::uint64_t num(unsigned n){if(n>data.size()-at){good=false;return 0;}std::uint64_t v=0;while(n--)v=(v<<8)|data[at++];return v;}
 Bytes bytes(size_t n){if(n>data.size()-at){good=false;return {};}Bytes b(data.begin()+at,data.begin()+at+n);at+=n;return b;}
 std::string string(size_t limit){auto n=num(2);if(!good||n>limit){good=false;return {};}auto b=bytes(size_t(n));return {b.begin(),b.end()};}
};
bool hello_valid(const PairingHello& h){return valid_device_id(h.session_id)&&valid_device_id(h.device_id)&&valid_display_text(h.name,256)&&valid_display_text(h.platform,32)&&h.data_port&&h.data_address.find('%')==std::string::npos&&valid_numeric_endpoint(h.data_address,1)&&h.nonce.size()==32&&std::any_of(h.nonce.begin(),h.nonce.end(),[](unsigned char c){return c!=0;});}
}
Result<Bytes> encode_pairing_frame(const PairingFrame& f){
 if(!valid_device_id(f.session_id)||unsigned(f.kind)>unsigned(PairingMessage::Abort)||f.payload.size()>8192||12+f.session_id.size()+f.payload.size()>8192)return invalid();
 Bytes b;number(b,8+f.session_id.size()+f.payload.size(),4);b.insert(b.end(),{'C','T','P',1});number(b,unsigned(f.kind),1);number(b,f.session_id.size(),1);b.insert(b.end(),f.session_id.begin(),f.session_id.end());number(b,f.payload.size(),2);b.insert(b.end(),f.payload.begin(),f.payload.end());return b;
}
Result<PairingFrame> decode_pairing_frame(const Bytes& b){
 if(b.size()<13||b.size()>8192)return invalid();Reader r{b};if(r.num(4)!=b.size()-4||r.num(4)!=0x43545001)return invalid();
 PairingFrame f;f.kind=static_cast<PairingMessage>(r.num(1));auto n=r.num(1);if(!n||n>128)return invalid();auto id=r.bytes(size_t(n));f.session_id.assign(id.begin(),id.end());n=r.num(2);f.payload=r.bytes(size_t(n));
 if(!r.good||r.at!=b.size()||!encode_pairing_frame(f))return invalid();return f;
}
Result<Bytes> encode_pairing_hello(const PairingHello& h){
 if(!hello_valid(h))return invalid();Bytes b{'C','T','H',1};for(auto* s:{&h.session_id,&h.device_id,&h.name,&h.platform,&h.data_address})text(b,*s);number(b,h.data_port,2);b.insert(b.end(),h.nonce.begin(),h.nonce.end());return b;
}
Result<PairingHello> decode_pairing_hello(const Bytes& b){
 if(b.size()>1024)return invalid();Reader r{b};if(r.num(4)!=0x43544801)return invalid();PairingHello h;
 h.session_id=r.string(128);h.device_id=r.string(128);h.name=r.string(256);h.platform=r.string(32);h.data_address=r.string(128);h.data_port=std::uint16_t(r.num(2));h.nonce=r.bytes(32);
 if(!r.good||r.at!=b.size()||!hello_valid(h))return invalid();return h;
}
Result<Bytes> pairing_transcript(const PairingHello& a,const PairingHello& b,const Bytes& a1,const Bytes& b1,const Bytes& a2,const Bytes& b2){
 auto ah=encode_pairing_hello(a),bh=encode_pairing_hello(b);
 if(!ah||!bh||a.session_id!=b.session_id||a.device_id==b.device_id||a.nonce==b.nonce)return invalid();
 for(auto* r:{&a1,&b1,&a2,&b2})if(r->empty()||r->size()>2048)return invalid();
 const std::string label="Cliptiq pairing transcript v1";Bytes out(label.begin(),label.end());out.push_back(0);unsigned tag=1;
 for(const Bytes* part:std::array<const Bytes*,6>{&ah.value(),&bh.value(),&a1,&b1,&a2,&b2}){number(out,tag++,1);number(out,part->size(),4);out.insert(out.end(),part->begin(),part->end());}
 return out;
}
}

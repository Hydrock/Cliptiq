#include "PairingCrypto.hpp"
#include "CryptoRuntime.hpp"
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecjpake.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace cliptiq::lan {
namespace {
Error failed(){return {ErrorCode::InvalidArgument,"Pairing could not be verified"};}
struct WipeBytes{Bytes& bytes;~WipeBytes(){mbedtls_platform_zeroize(bytes.data(),bytes.size());}};
struct WipeString{std::string& value;~WipeString(){mbedtls_platform_zeroize(value.data(),value.size());}};
bool control_kind(PairingMessage kind){return kind>=PairingMessage::Prepared&&kind<=PairingMessage::Abort;}
}
struct PairingCrypto::State {
 PairingRole role;bool initialized=false,good=false,sent1=false,read1=false,sent2=false,read2=false,bound=false,own_proof=false,verified=false;
 mbedtls_ecjpake_context jpake{};mbedtls_entropy_context entropy{};mbedtls_ctr_drbg_context rng{};
 std::array<unsigned char,32> secret{},context_hash{};
 std::uint64_t sent_sequence=0,received_sequence=0;
 State(PairingRole r,std::string& code):role(r){
  if((r!=PairingRole::Initiator&&r!=PairingRole::Responder)||code.size()!=6||!std::all_of(code.begin(),code.end(),[](unsigned char c){return c>='0'&&c<='9';})||!initialize_crypto_runtime())return;
  mbedtls_ecjpake_init(&jpake);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);initialized=true;
  static const unsigned char label[]="Cliptiq pairing PAKE v1";
  if(mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,label,sizeof(label)-1)!=0)return;
  good=mbedtls_ecjpake_setup(&jpake,r==PairingRole::Initiator?MBEDTLS_ECJPAKE_CLIENT:MBEDTLS_ECJPAKE_SERVER,MBEDTLS_MD_SHA256,MBEDTLS_ECP_DP_SECP256R1,reinterpret_cast<const unsigned char*>(code.data()),code.size())==0;
 }
 ~State(){
  mbedtls_platform_zeroize(secret.data(),secret.size());mbedtls_platform_zeroize(context_hash.data(),context_hash.size());
  if(initialized){mbedtls_ecjpake_free(&jpake);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);}
 }
 bool material(const Bytes& context){
  if(!good||!sent2||!read2||context.empty()||context.size()>32768)return false;
  std::array<unsigned char,32> hash{};if(mbedtls_sha256(context.data(),context.size(),hash.data(),0)!=0)return false;
  if(bound)return mbedtls_ct_memcmp(hash.data(),context_hash.data(),32)==0;
  size_t n=0;if(mbedtls_ecjpake_derive_secret(&jpake,secret.data(),secret.size(),&n,mbedtls_ctr_drbg_random,&rng)!=0||n!=32){good=false;mbedtls_platform_zeroize(secret.data(),secret.size());return false;}
  context_hash=hash;bound=true;return true;
 }
 Result<Bytes> expand(const char* label){
  Bytes output(32);auto md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!md||mbedtls_hkdf(md,context_hash.data(),32,secret.data(),32,reinterpret_cast<const unsigned char*>(label),std::strlen(label),output.data(),output.size())!=0){mbedtls_platform_zeroize(output.data(),output.size());return failed();}
  return output;
 }
 Result<Bytes> make_proof(const Bytes& context,PairingRole who){
  if((who!=PairingRole::Initiator&&who!=PairingRole::Responder)||!material(context))return failed();
  auto key=expand(who==PairingRole::Initiator?"Cliptiq pairing proof initiator v1":"Cliptiq pairing proof responder v1");if(!key)return key.error();
  WipeBytes wipe{key.value()};
  Bytes result(32);auto md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  int rc=mbedtls_md_hmac(md,key.value().data(),key.value().size(),context_hash.data(),32,result.data());
  if(rc!=0)return failed();return result;
 }
 Result<Bytes> control_mac(const Bytes& context,PairingRole producer,std::uint64_t sequence,const Bytes& plain){
  if(!own_proof||!verified||!material(context))return failed();
  auto key=expand(producer==PairingRole::Initiator?"Cliptiq pairing control initiator v1":"Cliptiq pairing control responder v1");if(!key)return key.error();
  WipeBytes wipe{key.value()};Bytes data;data.reserve(8+plain.size());
  for(int n=7;n>=0;--n)data.push_back(static_cast<unsigned char>(sequence>>(n*8)));
  data.insert(data.end(),plain.begin(),plain.end());Bytes tag(32);
  if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key.value().data(),key.value().size(),data.data(),data.size(),tag.data())!=0)return failed();
  return tag;
 }
};
PairingCrypto::PairingCrypto(PairingRole role,std::string code){WipeString wipe{code};state_=std::make_unique<State>(role,code);}
PairingCrypto::~PairingCrypto()=default;
Result<Bytes> PairingCrypto::round_one(){auto& s=*state_;if(!s.good||s.sent1)return failed();Bytes b(2048);size_t n=0;
 if(mbedtls_ecjpake_write_round_one(&s.jpake,b.data(),b.size(),&n,mbedtls_ctr_drbg_random,&s.rng)!=0){s.good=false;return failed();}s.sent1=true;b.resize(n);return b;}
Result<void> PairingCrypto::read_round_one(const Bytes& b){auto& s=*state_;if(!s.good||s.read1||b.empty()||b.size()>2048)return failed();
 if(mbedtls_ecjpake_read_round_one(&s.jpake,b.data(),b.size())!=0){s.good=false;return failed();}s.read1=true;return {};}
Result<Bytes> PairingCrypto::round_two(){auto& s=*state_;if(!s.good||!s.sent1||!s.read1||s.sent2)return failed();Bytes b(2048);size_t n=0;
 if(mbedtls_ecjpake_write_round_two(&s.jpake,b.data(),b.size(),&n,mbedtls_ctr_drbg_random,&s.rng)!=0){s.good=false;return failed();}s.sent2=true;b.resize(n);return b;}
Result<void> PairingCrypto::read_round_two(const Bytes& b){auto& s=*state_;if(!s.good||!s.sent1||!s.read1||s.read2||b.empty()||b.size()>2048)return failed();
 if(mbedtls_ecjpake_read_round_two(&s.jpake,b.data(),b.size())!=0){s.good=false;return failed();}s.read2=true;return {};}
Result<Bytes> PairingCrypto::proof(const Bytes& context,PairingRole role){auto& s=*state_;if(role!=s.role)return failed();auto p=s.make_proof(context,role);if(p)s.own_proof=true;return p;}
Result<void> PairingCrypto::verify_proof(const Bytes& context,PairingRole role,const Bytes& proof){auto& s=*state_;
 if(role==s.role||proof.size()!=32)return failed();auto expected=s.make_proof(context,role);if(!expected)return expected.error();
 if(mbedtls_ct_memcmp(expected.value().data(),proof.data(),32)!=0){s.good=false;mbedtls_platform_zeroize(s.secret.data(),s.secret.size());return failed();}
 s.verified=true;return {};
}
Result<Bytes> PairingCrypto::derive(const Bytes& context){auto& s=*state_;if(!s.own_proof||!s.verified||!s.material(context))return failed();return s.expand("Cliptiq pairing PSK v1");}
Result<Bytes> PairingCrypto::seal_control(const Bytes& context,PairingFrame frame){
 auto& s=*state_;if(!control_kind(frame.kind)||s.sent_sequence==std::numeric_limits<std::uint64_t>::max())return failed();
 auto plain=encode_pairing_frame(frame);if(!plain)return plain.error();auto sequence=s.sent_sequence+1;
 auto tag=s.control_mac(context,s.role,sequence,plain.value());if(!tag)return tag.error();
 Bytes payload;payload.reserve(40+frame.payload.size());for(int n=7;n>=0;--n)payload.push_back(static_cast<unsigned char>(sequence>>(n*8)));
 payload.insert(payload.end(),frame.payload.begin(),frame.payload.end());payload.insert(payload.end(),tag.value().begin(),tag.value().end());frame.payload=std::move(payload);
 auto wire=encode_pairing_frame(frame);if(wire)s.sent_sequence=sequence;return wire;
}
Result<PairingFrame> PairingCrypto::open_control(const Bytes& context,const Bytes& wire){
 auto& s=*state_;auto decoded=decode_pairing_frame(wire);if(!decoded)return decoded.error();auto frame=std::move(decoded.value());
 if(!control_kind(frame.kind)||frame.payload.size()<40||s.received_sequence==std::numeric_limits<std::uint64_t>::max())return failed();
 std::uint64_t sequence=0;for(unsigned n=0;n<8;++n)sequence=(sequence<<8)|frame.payload[n];
 if(sequence!=s.received_sequence+1)return failed();Bytes received(frame.payload.end()-32,frame.payload.end());
 frame.payload=Bytes(frame.payload.begin()+8,frame.payload.end()-32);auto plain=encode_pairing_frame(frame);if(!plain)return plain.error();
 auto remote=s.role==PairingRole::Initiator?PairingRole::Responder:PairingRole::Initiator;
 auto expected=s.control_mac(context,remote,sequence,plain.value());if(!expected)return expected.error();
 if(mbedtls_ct_memcmp(expected.value().data(),received.data(),32)!=0){s.good=false;mbedtls_platform_zeroize(s.secret.data(),s.secret.size());return failed();}
 s.received_sequence=sequence;return frame;
}
}

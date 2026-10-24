#include "sha256.hpp"
#include <algorithm>
#include <cstring>
namespace cliptiq::detail {
namespace {
constexpr std::uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
std::uint32_t rotr(std::uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));}
}
void Sha256::compress(){
 std::uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=(std::uint32_t(block_[4*i])<<24)|(std::uint32_t(block_[4*i+1])<<16)|(std::uint32_t(block_[4*i+2])<<8)|block_[4*i+3];
 for(unsigned i=16;i<64;++i){auto x=w[i-15],y=w[i-2];w[i]=w[i-16]+(rotr(x,7)^rotr(x,18)^(x>>3))+w[i-7]+(rotr(y,17)^rotr(y,19)^(y>>10));}
 auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
 for(unsigned i=0;i<64;++i){auto t1=h+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];auto t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}
void Sha256::update(const void* ptr,std::size_t n){auto p=static_cast<const std::uint8_t*>(ptr);bytes_+=n;while(n){auto count=std::min(n,64-filled_);std::memcpy(block_.data()+filled_,p,count);p+=count;n-=count;filled_+=count;if(filled_==64){compress();filled_=0;}}}
void Sha256::integer(std::uint64_t v){std::uint8_t b[8];for(unsigned i=0;i<8;++i)b[7-i]=static_cast<std::uint8_t>(v>>(i*8));update(b,8);}
void Sha256::field(const void* p,std::size_t n){integer(n);update(p,n);}
std::string Sha256::finish(){auto bits=bytes_*8;std::uint8_t pad=0x80;update(&pad,1);pad=0;while(filled_!=56)update(&pad,1);integer(bits);const char* hex="0123456789abcdef";std::string out;out.reserve(64);for(auto v:state_)for(int i=7;i>=0;--i)out+=hex[(v>>(i*4))&15];return out;}
}

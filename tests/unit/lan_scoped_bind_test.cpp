#include "platform/lan/Transport.hpp"
#include <iostream>
using namespace cliptiq;using namespace cliptiq::lan;
int main(){
 TransportConfig cfg;cfg.device_id="scope-test";cfg.bind_address="fe80::c1a0%4294967294";cfg.port=49731;cfg.peers.push_back({"peer","fe80::c1a1%4294967294","remote","auth",49731,Bytes(32,42),true,true});Transport transport;
 auto result=transport.start(cfg,{});if(!result)std::cerr<<result.error().message<<"\n";if(result||result.error().message.find("numeric IPv4")!=std::string::npos){std::cerr<<"Scoped numeric bind was not recognized\n";return 1;}
 cfg.bind_address="fe80::c1a0%not-a-number";result=transport.start(cfg,{});if(!result)std::cerr<<result.error().message<<"\n";if(result||result.error().message.find("numeric IPv4")==std::string::npos)return 2;
#ifndef _WIN32
#ifdef __APPLE__
 cfg.bind_address="fe80::c1a0%lo0";
#else
 cfg.bind_address="fe80::c1a0%lo";
#endif
result=transport.start(cfg,{});if(result||result.error().message.find("numeric IPv4")!=std::string::npos){std::cerr<<"Known named scope was rejected\n";return 3;}
#endif
 std::cout<<"PASS_scope_validation_before_native_bind\n";return 0;
}

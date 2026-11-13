#include "platform/lan/NativeDiscovery.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 DiscoveryDevice d{"peer","Mac","macos","host.local.","192.168.1.3","192.168.1.2",2,1234,49731,0};
 auto txt=encode_discovery_txt(d);CHECK(txt);
 DiscoveryDevice resolved;resolved.host=d.host;resolved.address=d.address;resolved.interface_index=2;resolved.control_port=1234;
 auto parsed=decode_discovery_txt(txt.value(),resolved);CHECK(parsed&&parsed.value().device_id=="peer"&&parsed.value().data_port==49731);
 for(size_t n=1;n<txt.value().size();++n){auto truncated=Bytes(txt.value().begin(),txt.value().begin()+n);CHECK(!decode_discovery_txt(truncated,resolved));}
 auto hostile=txt.value();std::string fake="address=203.0.113.9";hostile.push_back(static_cast<std::uint8_t>(fake.size()));hostile.insert(hostile.end(),fake.begin(),fake.end());auto safe=decode_discovery_txt(hostile,resolved);CHECK(safe&&safe.value().address==resolved.address);
 auto bad=txt.value();bad.insert(bad.end(),{4,'v','=','9','9'});CHECK(!decode_discovery_txt(bad,resolved));
 bad=txt.value();bad[0]=255;CHECK(!decode_discovery_txt(bad,resolved));
 resolved.address="example.com";CHECK(!decode_discovery_txt(txt.value(),resolved));resolved.address="fe80::1";resolved.interface_index=0;CHECK(!decode_discovery_txt(txt.value(),resolved));resolved.interface_index=7;CHECK(decode_discovery_txt(txt.value(),resolved));
 DiscoveryInbox inbox("self");auto generation=inbox.begin();CHECK(inbox.accept(generation,{DiscoveryEvent::Kind::Found,d,{}}));
 auto other=d;other.interface_index=3;other.address="192.168.2.3";CHECK(inbox.accept(generation,{DiscoveryEvent::Kind::Found,other,{}}));CHECK(inbox.drain().size()==2);
 inbox.cancel();CHECK(!inbox.accept(generation,{DiscoveryEvent::Kind::Found,d,{}}));auto next=inbox.begin();CHECK(next!=generation);CHECK(!inbox.accept(generation,{DiscoveryEvent::Kind::Failure,{},"late"}));
 other.device_id="self";CHECK(!inbox.accept(next,{DiscoveryEvent::Kind::Found,other,{}}));
 for(int i=0;i<300;++i)inbox.accept(next,{DiscoveryEvent::Kind::Failure,{},"failure"});CHECK(inbox.drain().size()<=128);
 std::cout<<"PASS_txt_untrusted_endpoint_scope_cancel_generation_and_queue_bounds\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

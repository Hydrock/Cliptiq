#include "NativeDiscovery.hpp"
#include "Text.hpp"
#include <map>
#include <charconv>
namespace cliptiq::lan {
namespace {Error invalid(){return {ErrorCode::InvalidArgument,"Invalid Cliptiq discovery metadata"};}}
Result<Bytes> encode_discovery_txt(const DiscoveryDevice& d){
 if(!valid_device_id(d.device_id)||!valid_display_text(d.name,250)||!valid_display_text(d.platform,32)||!d.data_port)return invalid();
 Bytes out;for(const auto& s:{std::string("v=1"),"id="+d.device_id,"name="+d.name,"os="+d.platform,"dp="+std::to_string(d.data_port)}){
  if(s.size()>255)return invalid();out.push_back(static_cast<std::uint8_t>(s.size()));out.insert(out.end(),s.begin(),s.end());
 }return out;
}
Result<DiscoveryDevice> decode_discovery_txt(const Bytes& bytes,DiscoveryDevice d){
 if(bytes.empty()||bytes.size()>1024||!d.control_port||!valid_display_text(d.host,253)||!valid_numeric_endpoint(d.address,d.interface_index))return invalid();
 std::map<std::string,std::string> values;
 for(size_t at=0;at<bytes.size();){size_t n=bytes[at++];if(!n||n>bytes.size()-at)return invalid();
  std::string s(bytes.begin()+at,bytes.begin()+at+n);at+=n;auto eq=s.find('=');if(eq==std::string::npos||eq==0)return invalid();
  auto k=s.substr(0,eq);if(!valid_display_text(k,32)||!values.emplace(k,s.substr(eq+1)).second)return invalid();
 }
 if(values["v"]!="1"||!valid_device_id(values["id"])||!valid_display_text(values["name"],250)||!valid_display_text(values["os"],32))return invalid();
 unsigned port=0;const auto& p=values["dp"];auto r=std::from_chars(p.data(),p.data()+p.size(),port);
 if(p.empty()||r.ec!=std::errc{}||r.ptr!=p.data()+p.size()||port==0||port>65535)return invalid();
 d.device_id=values["id"];d.name=values["name"];d.platform=values["os"];d.data_port=static_cast<std::uint16_t>(port);return d;
}
std::uint64_t DiscoveryInbox::begin(){std::lock_guard<std::mutex> l(mutex_);events_.clear();active_=true;return ++generation_;}
void DiscoveryInbox::cancel(){std::lock_guard<std::mutex> l(mutex_);active_=false;++generation_;events_.clear();}
bool DiscoveryInbox::accept(std::uint64_t gen,DiscoveryEvent event){
 std::lock_guard<std::mutex> l(mutex_);if(!active_||gen!=generation_||events_.size()>=128)return false;
 if(event.kind!=DiscoveryEvent::Kind::Failure){auto& d=event.device;if(!valid_device_id(d.device_id)||d.device_id==local_id_)return false;
  if(event.kind==DiscoveryEvent::Kind::Found&&(!encode_discovery_txt(d)||!valid_numeric_endpoint(d.address,d.interface_index)||!valid_numeric_endpoint(d.interface_address,d.interface_index)||!d.control_port))return false;
 }else if(event.message.size()>512)return false;
 events_.push_back(std::move(event));return true;
}
std::vector<DiscoveryEvent> DiscoveryInbox::drain(){std::lock_guard<std::mutex> l(mutex_);std::vector<DiscoveryEvent> out;out.swap(events_);return out;}
}

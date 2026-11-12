#include "lan/Resolver.hpp"
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cliptiq::lan;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 std::atomic<bool> stop{false};
 for(const auto* host:{"127.0.0.1","::1","localhost"}){
  auto result=resolve_host(host,49881,stop);require(bool(result),"local hostname did not resolve");
  require(!result.value().empty(),"no resolved addresses");
  for(const auto& a:result.value()){
   require(a.address.ss_family==AF_INET || a.address.ss_family==AF_INET6,"unexpected family");
   const auto port=a.address.ss_family==AF_INET?reinterpret_cast<const sockaddr_in*>(&a.address)->sin_port:reinterpret_cast<const sockaddr_in6*>(&a.address)->sin6_port;
   require(ntohs(port)==49881,"resolved port lost");
  }
 }
 require(!resolve_host("",49881,stop),"empty hostname accepted");
 require(!resolve_host(std::string("local\0host",10),49881,stop),"embedded null accepted");
 require(!resolve_host(std::string(254,'a'),49881,stop),"oversized hostname accepted");
 require(!resolve_host("localhost",0,stop),"zero port accepted");
 auto before=std::chrono::steady_clock::now();
 require(!resolve_host("localhost",49881,stop,std::chrono::milliseconds(0)),"expired timeout accepted");
 stop=true;require(!resolve_host("localhost",49881,stop),"cancelled lookup accepted");
 require(std::chrono::steady_clock::now()-before<std::chrono::milliseconds(100),"pre-cancellation blocked");
 // Exercise callback/cancellation lifetime races using only the OS localhost path.
 for(int i=0;i<100;++i){stop=false;std::thread cancel([&]{stop=true;});auto result=resolve_host("localhost",49881,stop);cancel.join();}
 {
  ResolverJob numeric("127.0.0.1",49881);auto result=numeric.poll();require(result && bool(*result),"numeric async job pending");
  ResolverJob cancelled("localhost",49881);cancelled.cancel();auto cancelledResult=cancelled.poll();require(cancelledResult && !*cancelledResult,"async cancellation lost");
  ResolverJob expired("localhost",49881,std::chrono::milliseconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));auto expiredResult=expired.poll();
  require(expiredResult && !*expiredResult,"async deadline ignored");
  auto start=std::chrono::steady_clock::now();std::vector<std::unique_ptr<ResolverJob>> jobs;
  for(int i=0;i<16;++i)jobs.push_back(std::make_unique<ResolverJob>("localhost",49881));
  for(auto& job:jobs){job->poll();job->cancel();}
  jobs.clear();require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500),"parallel async jobs stalled caller");
 }
 std::cout<<"Numeric IPv4/IPv6, localhost DNS, input validation and cancellation passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "../../platform/Updates.hpp"
#include <iostream>
#include <thread>
#include <chrono>
int main(int argc,char**argv){
  cliptiq::NativeUpdates u;
  if(argc==2&&std::string(argv[1])=="--network-check"){
    auto r=u.check_now(true);if(!r){std::cout<<r.error().message<<'\n';return 1;}
    for(int n=0;n<1600;++n){if(u.poll()){auto message=u.status();std::cout<<message<<'\n';return message.find("No newer stable release")!=std::string::npos||message.find("advertised;")!=std::string::npos?0:1;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    std::cerr<<"metadata check exceeded integration deadline\n";return 1;
  }
  // Test bundles intentionally have no release identity/feed: neither shell,
  // browser, network, installer nor diagnostics picker may run automatically.
#ifdef __APPLE__
  if(u.automatic_available())return 1;
#else
  if(!u.automatic_available())return 1;
#endif
  for(auto seconds:{86400,604800,2592000})u.preferences(false,seconds);
#ifdef __APPLE__
  if(u.check_now(true))return 2;
#endif
  if(u.status().empty())return 3;
  if(cliptiq::local_diagnostics().size()>4096)return 4;
  std::cout<<"unconfigured native route fails closed without side effects\n";
}

#include "../../platform/Updates.hpp"
#include <iostream>
int main(){
  cliptiq::NativeUpdates u;
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

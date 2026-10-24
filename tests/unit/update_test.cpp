#include "cliptiq/update.hpp"
#include "cliptiq/menu.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(false)
struct Provider : UpdateProvider {
  UpdateInfo info{"0.2.0", "linux", "x86_64", "https://github.com/Hydrock/Cliptiq/releases/download/v0.2.0/Cliptiq.tar.gz", "signed", 1024, UpdateRoute::SignedInstaller};
  bool offline=false, timeout=false, trusted=true, cancel=false;
  int fetched=0, verified=0, installs=0;
  Result<UpdateInfo> fetch(UpdateChannel) override {
    ++fetched;
    if (offline || timeout) return Error{ErrorCode::Io, timeout?"timeout":"offline"};
    return info;
  }
  Result<void> authenticate(const UpdateInfo&) override {
    ++verified;
    if (!trusted) return Error{ErrorCode::Corrupt,"signature rejected"};
    return {};
  }
  Result<UpdateOutcome> install(const UpdateInfo&) override {
    ++installs;
    return cancel?UpdateOutcome::Cancelled:UpdateOutcome::Delegated;
  }
};
int main() {try {
  UpdateTarget target{"linux","x86_64",UpdateRoute::SignedInstaller,5*1024*1024};
  Provider p;
  auto good=check_update(p,UpdateChannel::Stable,"0.1.0",target); CHECK(good); CHECK(p.verified==1);
  CHECK(apply_update(p,good.value(),"0.1.0",target)); CHECK(p.verified==2);
  p.cancel=true; CHECK(apply_update(p,good.value(),"0.1.0",target).value()==UpdateOutcome::Cancelled);
  p.trusted=false; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));
  auto installs=p.installs; CHECK(!apply_update(p,good.value(),"0.1.0",target)); CHECK(p.installs==installs); p.trusted=true;
  for (auto v : {"", "0.1", "01.2.0", "0.2.0;evil", "0.2.0-beta", "4294967296.0.0"}) {p.info.version=v; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));}
  p.info.version="0.1.0"; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));
  p.info.version="0.0.9"; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target)); p.info.version="0.2.0";
  p.info.platform="windows"; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target)); p.info.platform="linux";
  p.info.arch="arm64"; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));p.info.arch="x86_64";
  p.info.bytes=target.max_bytes+1; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));p.info.bytes=1024;
  p.info.signing_metadata=""; CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));p.info.signing_metadata="signed";
  for(auto url:{"http://github.com/Hydrock/Cliptiq/x", "https://github.com/evil/x", "https://github.com/Hydrock/Cliptiq.evil/x", "https://user@github.com/Hydrock/Cliptiq/x", "https://github.com/Hydrock/Cliptiq/x\n"}) {p.info.package_url=url;CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));}
  p.info=good.value();p.offline=true;CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));p.offline=false;p.timeout=true;CHECK(!check_update(p,UpdateChannel::Stable,"0.1.0",target));p.timeout=false;
  target.route=UpdateRoute::PackageManager;CHECK(!apply_update(p,good.value(),"0.1.0",target));CHECK(p.installs==installs);
  auto portable=good.value();portable.route=UpdateRoute::Download;target.route=UpdateRoute::Download;
  CHECK(!apply_update(p,portable,"0.1.0",target));CHECK(p.installs==installs);
  CHECK(update_due(true,86400,0,1)); CHECK(!update_due(false,86400,0,999999));
  CHECK(!update_due(true,86400,100,200));CHECK(update_due(true,86400,100,86500));
  CHECK(!update_due(true,123,0,999999));CHECK(!update_due(true,86400,99999,1));
  for(auto seconds:{86400,604800,2592000})CHECK(update_due(true,seconds,1,seconds+1));
  auto store=open_store(":memory:");CHECK(store);auto s=default_settings(CapturePlatform::X11);
  CHECK(edit_settings(s,{{"updates_auto","1"},{"updates_interval","86400"}}));CHECK(!edit_settings(s,{{"updates_interval","12"}}));
  CHECK(save_update_attempt(store.value(),123));CHECK(last_update_attempt(store.value()).value()==123);
  auto loaded=load_settings(store.value());CHECK(loaded);CHECK(save_settings(store.value(),loaded.value()));CHECK(last_update_attempt(store.value()).value()==123);
  auto diagnostic=local_diagnostics();CHECK(diagnostic.find("clipboard")==std::string::npos);CHECK(diagnostic.find("http")==std::string::npos);CHECK(diagnostic.size()<4096);
  auto menu=build_menu(store.value(),MenuKind::Main);CHECK(menu); bool check=false,diag=false;
  for(auto &i:menu.value().items){check|=i.action==MenuAction::CheckUpdates;diag|=i.action==MenuAction::ExportDiagnostics;}
  CHECK(check&&diag);
  CHECK(release_hint("<feed><entry><link rel='alternate' href='https://github.com/Hydrock/Cliptiq/releases/tag/v0.2.0'/></entry></feed>","0.1.0").value()=="0.2.0");
  CHECK(!release_hint(std::string(131073,'x'),"0.1.0"));
  CHECK(!release_hint("<!DOCTYPE x><feed/>","0.1.0"));
  CHECK(!release_hint("<feed><entry><id>https://evil/x/0.2.0</id></entry></feed>","0.1.0"));
  CHECK(!release_hint("<feed><entry><id>https://github.com/Hydrock/Cliptiq/releases/tag/v0.0.1</id></entry></feed>","0.1.0"));
  CHECK(release_hint("<feed><entry><id>tag:github.com,2008:Repository/1/v0.2.0</id><link rel='alternate' href='https://github.com/Hydrock/Cliptiq/releases/tag/v0.2.0'/></entry></feed>","0.1.0"));
  std::cout<<"update policy, authentication boundaries, schedule, settings and redacted diagnostics passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

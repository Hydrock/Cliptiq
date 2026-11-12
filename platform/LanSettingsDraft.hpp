#pragma once
#include "LanSettings.hpp"
#include "cliptiq/locale.hpp"
#include <algorithm>
namespace cliptiq {
// Pure draft: editing never opens sockets or writes credentials/preferences.
struct LanSettingsDraft {
  struct Field { std::string key,label,value; bool check=false,secret=false; };
  lan::Config config;
  std::map<std::string,std::string> keys;
  std::vector<Field> fields() const {
    std::vector<Field> f;
    auto add=[&](std::string k,const char* l,std::string v,bool c=false,bool s=false){f.push_back({k,tr(l),v,c,s});};
    auto flag=[&](std::string k,const char* l,bool b){add(k,l,b?"1":"0",true);};
    add("name","Local device name",config.device_name);
    add("bind","Listen address",config.bind_address);
    add("port","Listen port",std::to_string(config.port));
    flag("paused","Pause LAN sharing",config.paused);
    flag("history","Receive into history only",config.receive_mode==lan::ReceiveMode::HistoryOnly);
    flag("text","Share text",config.text); flag("urls","Share URLs",config.urls); flag("images","Share images",config.images);
    for(const auto& p:config.peers){
      auto prefix=p.id+":";
      add(prefix+"name","Peer name",p.name); add(prefix+"host","Peer address",p.host);add(prefix+"port","Peer port",std::to_string(p.port));
      flag(prefix+"enabled","Peer enabled",p.enabled);flag(prefix+"send","Send to peer",p.send);flag(prefix+"receive","Receive from peer",p.receive);
      auto key=keys.find(p.id);add(prefix+"key","Pair key (blank keeps saved key)",key==keys.end()?"":key->second,false,true);
    }
    return f;
  }
  Result<void> collect(const std::map<std::string,std::string>& values) {
    auto candidate=config; auto next=keys;
    auto get=[&](std::string k){auto i=values.find(k);return i==values.end()?std::string():i->second;};
    auto port=[&](std::string k)->unsigned{auto v=get(k);if(v.empty()||v.size()>5||v.find_first_not_of("0123456789")!=std::string::npos)return 0;return unsigned(std::stoul(v));};
    auto n=port("port"); if(!n||n>65535)return Error{ErrorCode::InvalidArgument,tr("Port must be between 1 and 65535")};
    candidate.device_name=get("name");candidate.bind_address=get("bind");candidate.port=std::uint16_t(n);
    candidate.paused=get("paused")=="1";candidate.receive_mode=get("history")=="1"?lan::ReceiveMode::HistoryOnly:lan::ReceiveMode::ClipboardAndHistory;
    candidate.text=get("text")=="1";candidate.urls=get("urls")=="1";candidate.images=get("images")=="1";
    for(auto& p:candidate.peers){auto k=p.id+":";n=port(k+"port");if(!n||n>65535)return Error{ErrorCode::InvalidArgument,tr("Port must be between 1 and 65535")};p.name=get(k+"name");p.host=get(k+"host");p.port=std::uint16_t(n);p.enabled=get(k+"enabled")=="1";p.send=get(k+"send")=="1";p.receive=get(k+"receive")=="1";auto secret=get(k+"key");if(!secret.empty()){auto fp=lan::key_fingerprint(secret);if(!fp)return fp.error();next[p.id]=secret;}}
    config=std::move(candidate);keys=std::move(next);return {};
  }
  void add(){if(config.peers.size()<16){lan::Peer p;p.id=lan::new_id();p.name=tr("New peer");config.peers.push_back(std::move(p));}}
  void remove(const std::string& id){config.peers.erase(std::remove_if(config.peers.begin(),config.peers.end(),[&](const lan::Peer& p){return p.id==id;}),config.peers.end());keys.erase(id);}
};
}

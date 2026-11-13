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
  // Shared explanations keep the native settings consistent on all platforms.
  std::string section(const Field& f) const {
    if(f.key=="name")return tr("This computer");
    if(f.key=="paused")return tr("Clipboard sharing");
    if(f.key=="text")return tr("Shared formats");
    for(size_t i=0;i<config.peers.size();++i)if(f.key==config.peers[i].id+":name")return tr("Device")+" "+std::to_string(i+1)+" — "+config.peers[i].name;
    return {};
  }
  static std::string explanation(const Field& f) {
    auto at=f.key.find(':');auto key=at==std::string::npos?f.key:"peer:"+f.key.substr(at+1);
    if(key=="name")return tr("A name for this computer, for example Home PC or Laptop. It helps you recognize this device when configuring the other computers.");
    if(key=="bind")return tr("The IP address of THIS computer on your local network or VPN. For example 192.168.1.10. 127.0.0.1 works only on this computer; do not enter the other device address here.");
    if(key=="port")return tr("The TCP port on which this computer accepts connections. Keep 49731 unless it is in use. Enter this same number as the peer port on the other computer.");
    if(key=="paused")return tr("Temporarily stops sending and receiving for all devices without deleting their addresses or keys. Clear this checkbox to resume after saving.");
    if(key=="history")return tr("When checked, received copies appear only in Cliptiq history. When unchecked, they also become the current system clipboard so you can paste with Ctrl+V or Cmd+V.");
    if(key=="text")return tr("Allows sending and receiving plain text. Only new copies are shared; your existing clipboard history is not sent.");
    if(key=="urls")return tr("Allows sending and receiving links recognized by the clipboard as URLs. A link copied as plain text also depends on the text setting.");
    if(key=="images")return tr("Allows sending and receiving images, converted to PNG. Maximum 8 MiB per transfer and 8 million pixels per image. Files and folders are not transferred.");
    if(key=="peer:name")return tr("A friendly name for the OTHER device, for example Work Mac. This name labels copies received from it in your history. It does not have to match its system name.");
    if(key=="peer:host")return tr("The IP address or DNS name of the OTHER computer, reachable from here over LAN or VPN, for example 192.168.1.20 or pc.local.");
    if(key=="peer:port")return tr("Enter the listen port configured on the OTHER computer. Usually 49731. This is its port, not necessarily the listen port of this computer.");
    if(key=="peer:enabled")return tr("Enables this device after saving. Disable it to disconnect only this device while keeping its address and key for later.");
    if(key=="peer:send")return tr("Sends new copies from THIS computer to this device. Configure the matching receive permission on the other computer.");
    if(key=="peer:receive")return tr("Accepts new copies from this device. Where they appear is controlled by Receive into history only above. The other computer must allow sending.");
    if(key=="peer:key")return tr("The same secret key must be entered at both ends of this pair. Generate it on one computer and securely copy it to the other. Leave blank to keep a saved key. Use a different key for each pair.");
    return {};
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

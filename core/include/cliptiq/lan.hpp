#pragma once
#include "history.hpp"
#include "capabilities.hpp"
#include <functional>
namespace cliptiq::lan {
constexpr std::size_t max_event_bytes=8*1024*1024;
enum class ReceiveMode { ClipboardAndHistory, HistoryOnly };
struct Peer { std::string id,name,host,remote_device_id,secret_ref,auth_id; std::uint16_t port=49731; bool enabled=true,send=true,receive=true; };
struct Config { std::string device_id,device_name,bind_address="127.0.0.1"; std::uint16_t port=49731; bool paused=false,text=true,urls=true,images=true; ReceiveMode receive_mode=ReceiveMode::ClipboardAndHistory; std::uint64_t revision=0; std::vector<Peer> peers; };
Result<Config> load_config(Store&);
Result<void> save_config(Store&,const Config&);
Result<void> validate_config(const Config&);
bool active(const Config&);
std::string new_id();
Result<std::string> key_fingerprint(const std::string& key64hex);
struct Event { std::string origin_device_id,event_id; std::uint64_t sequence=0; std::int64_t created_ms=0; std::vector<Representation> representations; };
Result<std::vector<Representation>> portable_snapshot(const ClipboardSnapshot&,const Config&,std::function<Result<Bytes>(const Representation&)> encode_png={});
Result<ClipboardSnapshot> native_snapshot(const Event&,CapturePlatform);
Result<Event> make_event(Store&,std::vector<Representation>,std::int64_t);
Result<Bytes> encode_event(const Event&);
Result<Event> decode_event(const Bytes&);
Result<void> bind_peer_identity(Store&,const std::string& peer_id,const std::string& remote_device_id);
struct Arrival { bool duplicate=false; std::optional<EntryId> entry_id; ReceiveMode receive_mode=ReceiveMode::ClipboardAndHistory; };
Result<Arrival> accept_event(Store&,const std::string& peer_id,const Event&,HistoryPolicy,std::int64_t,CapturePlatform platform=CapturePlatform::Unknown);
Result<bool> is_remote_entry(Store&,const EntryId&);
Result<std::string> entry_origin(Store&,const EntryId&);
}

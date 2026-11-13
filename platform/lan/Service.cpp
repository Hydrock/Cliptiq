#include "Service.hpp"
#include "Transport.hpp"
#include "Secrets.hpp"
#include "Images.hpp"
#include "cliptiq/locale.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
namespace cliptiq::lan {
namespace {
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
Bytes unhex(const std::string& s){Bytes b;auto n=[](char c)->unsigned{return c<='9'?c-'0':(c>='a'?c-'a':c-'A')+10;};for(size_t i=0;i+1<s.size();i+=2)b.push_back(static_cast<unsigned char>((n(s[i])<<4)|n(s[i+1])));return b;}
size_t payload_size(const Event&e){size_t size=0;for(auto&r:e.representations)size+=r.data.size();return size;}
struct Checkpoint {std::mutex mutex;std::condition_variable changed;bool done=false,cancelled=false;std::optional<Result<std::string>> result;};
struct Delivery {Event event;std::string checkpoint;};
struct Runtime {
 std::unique_ptr<Transport> transport;
 std::optional<Store> store;
 std::string database;
 CapturePlatform platform;
 HistoryPolicy history;
 std::mutex mutex;
 bool stopping=false;
 std::deque<std::shared_ptr<Checkpoint>> checkpoints;
 std::deque<Delivery> deliveries;
 size_t delivery_bytes=0;
 std::map<std::string,std::string> statuses;
 std::string pending_capture,baseline_token,last_sent_token;
 Result<Store*> connection(){if(!store){auto opened=open_store(database);if(!opened)return opened.error();store.emplace(std::move(opened.value()));}return &*store;}
 void report(const std::string& peer,const std::string& value){std::lock_guard<std::mutex> lock(mutex);statuses[peer]=value;}
 void flush(){
  if(pending_capture.empty()||!transport->ready_to_publish())return;
  auto id=std::move(pending_capture);pending_capture.clear();
  auto db=connection();if(!db){report("",db.error().message);return;}
  auto origin=is_remote_entry(*db.value(),id);if(!origin||origin.value())return;
  auto snapshot=load_snapshot(*db.value(),id);if(!snapshot)return;
  if(!baseline_token.empty()&&snapshot.value().sequence_token==baseline_token)return;
  if(!snapshot.value().sequence_token.empty()&&snapshot.value().sequence_token==last_sent_token)return;
  auto config=load_config(*db.value());if(!config||!active(config.value()))return;
  auto portable=portable_snapshot(snapshot.value(),config.value(),network_png);if(!portable){report("",portable.error().message);return;}
  auto event=make_event(*db.value(),std::move(portable.value()),now_ms());if(!event){report("",event.error().message);return;}
  auto wire=encode_event(event.value());if(!wire){report("",wire.error().message);return;}
  transport->publish(std::make_shared<const Bytes>(std::move(wire.value())));
  last_sent_token=snapshot.value().sequence_token;
 }

};
Error stopped(){return {ErrorCode::Busy,"LAN sharing stopped"};}
}
struct Service::State {
 Store &ui;std::string database;CapturePlatform platform;ClipboardHooks clipboard;
 Config configuration;HistoryPolicy history;
 std::shared_ptr<Runtime> runtime;
 std::string problem;
 std::deque<std::pair<std::string,std::string>> own_writes;
 State(Store&s,std::string path,CapturePlatform p,ClipboardHooks h):ui(s),database(std::move(path)),platform(p),clipboard(std::move(h)){}
 void stop(){
  auto r=runtime;if(!r)return;
  {std::lock_guard<std::mutex> lock(r->mutex);r->stopping=true;for(auto &request:r->checkpoints){std::lock_guard<std::mutex> guard(request->mutex);request->cancelled=true;request->changed.notify_all();}r->checkpoints.clear();}
  r->transport->stop();runtime.reset();own_writes.clear();
 }
 Result<void> start(){
  problem.clear();if(!active(configuration))return {};
  TransportConfig cfg;cfg.device_id=configuration.device_id;cfg.bind_address=configuration.bind_address;cfg.port=configuration.port;
  for(auto &peer:configuration.peers){if(!peer.enabled||(!peer.send&&!peer.receive))continue;
   auto key=read_secret(peer.secret_ref);if(!key)return key.error();
   auto fingerprint=key_fingerprint(key.value());if(!fingerprint||fingerprint.value()!=peer.auth_id)return Error{ErrorCode::Corrupt,"Stored LAN key does not match paired device"};
   cfg.peers.push_back({peer.id,peer.host,peer.remote_device_id,peer.auth_id,peer.port,unhex(key.value()),peer.send,peer.receive});
   std::fill(key.value().begin(),key.value().end(),'\0');
  }
  if(cfg.peers.empty())return {};
  auto r=std::make_shared<Runtime>();r->database=database;r->platform=platform;r->history=history;r->transport=std::make_unique<Transport>();
  if(clipboard.token){auto baseline=clipboard.token();if(baseline)r->baseline_token=std::move(baseline.value());}
  std::weak_ptr<Runtime> weak=r;TransportCallbacks callbacks;
  callbacks.tick=[weak]{if(auto r=weak.lock())r->flush();};
  callbacks.stopped=[weak]{if(auto r=weak.lock())r->store.reset();};
  callbacks.status=[weak](std::string peer,std::string value){if(auto r=weak.lock())r->report(peer,value);};
  callbacks.identity=[weak](std::string peer,std::string remote)->Result<void>{auto r=weak.lock();if(!r)return stopped();auto db=r->connection();if(!db)return db.error();auto bound=bind_peer_identity(*db.value(),peer,remote);if(!bound)return bound;return finish_authenticated_pair(*db.value(),peer,remote);};
  callbacks.receiving=[weak](std::string)->Result<std::string>{
   auto r=weak.lock();if(!r)return stopped();auto request=std::make_shared<Checkpoint>();
   {std::lock_guard<std::mutex> lock(r->mutex);if(r->stopping)return stopped();if(r->checkpoints.size()>=16)return Error{ErrorCode::Busy,"LAN receive checkpoint queue full"};r->checkpoints.push_back(request);}
   std::unique_lock<std::mutex> guard(request->mutex);
   if(!request->changed.wait_for(guard,std::chrono::seconds(2),[&]{return request->done||request->cancelled;}))return Error{ErrorCode::Busy,"Clipboard checkpoint timed out"};
   if(request->cancelled||!request->result)return stopped();return std::move(*request->result);
  };
  callbacks.received=[weak](std::string peer,Bytes wire,std::string checkpoint)->Result<void>{
   auto r=weak.lock();if(!r)return stopped();auto event=decode_event(wire);if(!event)return event.error();
   for(auto &representation:event.value().representations)if(representation.kind==CanonicalKind::Image){auto checked=network_png(representation);if(!checked)return checked.error();}
   auto db=r->connection();if(!db)return db.error();HistoryPolicy policy;
   {std::lock_guard<std::mutex> lock(r->mutex);if(r->stopping)return stopped();policy=r->history;}
   auto accepted=accept_event(*db.value(),peer,event.value(),policy,now_ms(),r->platform);if(!accepted)return accepted.error();
   if(accepted.value().duplicate||!accepted.value().entry_id)return {};
   if(accepted.value().receive_mode==ReceiveMode::HistoryOnly){r->report(peer,"Received into history");return {};}
   size_t size=payload_size(event.value());
   {std::lock_guard<std::mutex> lock(r->mutex);if(r->stopping)return stopped();
    if(size>max_event_bytes){r->statuses[peer]="Saved in history; clipboard queue is full";return {};}
    while(!r->deliveries.empty()&&(r->deliveries.size()>=64||r->delivery_bytes>max_event_bytes-size)){
      r->delivery_bytes-=payload_size(r->deliveries.front().event);r->deliveries.pop_front();
    }
    r->delivery_bytes+=size;r->deliveries.push_back({std::move(event.value()),std::move(checkpoint)});
   }
   r->report(peer,"Received into history; clipboard pending");return {};
  };
  runtime=r;auto result=r->transport->start(std::move(cfg),std::move(callbacks));
  if(!result){problem=result.error().message;stop();return result;}return {};
 }
};
Service::Service(Store&s,std::string database,CapturePlatform platform,ClipboardHooks hooks):state_(std::make_unique<State>(s,std::move(database),platform,std::move(hooks))){}
Service::~Service(){stop();}
Result<void> Service::initialize(){auto loaded=load_config(state_->ui);if(!loaded)return loaded.error();state_->configuration=std::move(loaded.value());auto started=state_->start();if(!started)state_->problem=started.error().message;return started;}
Result<Config> Service::config(){auto loaded=load_config(state_->ui);if(loaded)state_->configuration=loaded.value();return loaded;}
Result<void> Service::refresh(){state_->stop();return initialize();}
void Service::stop(){state_->stop();}
bool Service::running()const{return state_->runtime&&state_->runtime->transport->active();}
void Service::history_policy(HistoryPolicy value){state_->history=value;if(auto r=state_->runtime){std::lock_guard<std::mutex> lock(r->mutex);r->history=value;}}
std::string Service::status()const{
 if(!state_->problem.empty())return tr(state_->problem);
 auto r=state_->runtime;if(!r)return tr(state_->configuration.paused?"LAN sharing paused":"LAN sharing off");
 std::string text=tr("LAN sharing active");std::lock_guard<std::mutex> lock(r->mutex);
 for(auto &status:r->statuses){auto peer=std::find_if(state_->configuration.peers.begin(),state_->configuration.peers.end(),[&](const Peer&p){return p.id==status.first;});text+="\n"+(peer==state_->configuration.peers.end()?tr("Device"):peer->name)+": "+tr(status.second);}
 return text;
}
Result<void> Service::save(Config draft,std::map<std::string,std::string> keys){
 auto previous=load_config(state_->ui);if(!previous)return previous.error();
 if(previous.value().revision!=draft.revision)return Error{ErrorCode::Busy,"LAN settings changed; reopen network settings"};
 draft.device_id=previous.value().device_id;
 std::vector<std::string> created;auto cleanup=[&]{for(auto &ref:created)erase_secret(ref);};
 for(auto &peer:draft.peers){
  auto old=std::find_if(previous.value().peers.begin(),previous.value().peers.end(),[&](const Peer&p){return p.id==peer.id;});
  if(old!=previous.value().peers.end()){peer.secret_ref=old->secret_ref;peer.auth_id=old->auth_id;peer.remote_device_id=old->remote_device_id;}else{peer.secret_ref.clear();peer.auth_id.clear();peer.remote_device_id.clear();}
  auto key=keys.find(peer.id);
  if(key!=keys.end()&&!key->second.empty()){
   auto fingerprint=key_fingerprint(key->second);if(!fingerprint){cleanup();return fingerprint.error();}
   peer.auth_id=fingerprint.value();peer.secret_ref=draft.device_id+"-"+new_id();peer.remote_device_id.clear();
   auto stored=write_secret(peer.secret_ref,key->second);std::fill(key->second.begin(),key->second.end(),'\0');
   if(!stored){cleanup();return stored;}created.push_back(peer.secret_ref);
  }
  if(peer.secret_ref.empty()||peer.auth_id.empty()){cleanup();return Error{ErrorCode::InvalidArgument,"Every device needs a pairing key"};}
 }
 auto valid=validate_config(draft);if(!valid){cleanup();return valid;}
 state_->stop();auto saved=save_config(state_->ui,draft);
 if(!saved){cleanup();auto persisted=load_config(state_->ui);if(persisted){state_->configuration=std::move(persisted.value());auto ignored=state_->start();(void)ignored;}else state_->problem=persisted.error().message;return saved;}
 auto current=load_config(state_->ui);if(!current){state_->problem=current.error().message;return current.error();}state_->configuration=std::move(current.value());
 for(auto &old:previous.value().peers){auto retained=std::find_if(state_->configuration.peers.begin(),state_->configuration.peers.end(),[&](const Peer&p){return p.secret_ref==old.secret_ref;});if(retained==state_->configuration.peers.end())erase_secret(old.secret_ref);}
 auto started=state_->start();if(!started)state_->problem=started.error().message;
 return {}; // Configuration was saved; start errors remain visible in status.
}
Result<void> Service::copy_secret(const std::string& key){
 if(!key_fingerprint(key))return Error{ErrorCode::InvalidArgument,"Invalid pairing key"};
 Event event{state_->configuration.device_id,new_id(),1,now_ms(),{{0,"portable","text/plain",CanonicalKind::Text,Bytes(key.begin(),key.end())}}};
 auto snapshot=native_snapshot(event,state_->platform);if(!snapshot)return snapshot.error();snapshot.value().flags.transient=Flag::Yes;snapshot.value().flags.concealed=Flag::Yes;
 if(!state_->clipboard.write)return Error{ErrorCode::Unsupported,"Clipboard unavailable"};auto written=state_->clipboard.write(snapshot.value());if(!written)return written.error();return {};
}
void Service::captured(const EntryId& id){
 auto r=state_->runtime;if(!r||!r->transport->active())return;
 std::weak_ptr<Runtime> weak=r;
 auto queued=r->transport->post([weak,id]{if(auto r=weak.lock()){r->pending_capture=id;r->flush();}});
 if(!queued)r->report("",queued.error().message);
}
bool Service::poll(){
 auto r=state_->runtime;if(!r)return false;
 std::deque<std::shared_ptr<Checkpoint>> requests;std::optional<Delivery> delivery;
 {std::lock_guard<std::mutex> lock(r->mutex);requests.swap(r->checkpoints);if(!r->deliveries.empty()){delivery.emplace(std::move(r->deliveries.front()));r->delivery_bytes-=payload_size(delivery->event);r->deliveries.pop_front();}}
 for(auto &request:requests){auto token=state_->clipboard.token?state_->clipboard.token():Result<std::string>(Error{ErrorCode::Unsupported,"Clipboard token unavailable"});std::lock_guard<std::mutex> lock(request->mutex);if(!request->cancelled){request->result.emplace(std::move(token));request->done=true;}request->changed.notify_all();}
 if(delivery){
  auto current=state_->clipboard.token?state_->clipboard.token():Result<std::string>(Error{ErrorCode::Unsupported,"Clipboard unavailable"});
  auto expected=delivery->checkpoint;
  for(size_t hops=0;current && expected!=current.value() && hops<state_->own_writes.size();++hops){
   auto next=std::find_if(state_->own_writes.begin(),state_->own_writes.end(),[&](const auto &entry){return entry.first==expected;});
   if(next==state_->own_writes.end())break;expected=next->second;
  }
  if(!current||current.value()!=expected){r->report("","Saved in history; newer local clipboard kept");return true;}
  auto native=native_snapshot(delivery->event,state_->platform);if(!native){r->report("",native.error().message);return true;}
  auto written=state_->clipboard.compare_write?state_->clipboard.compare_write(native.value(),current.value()):Result<std::string>(Error{ErrorCode::Unsupported,"Conditional clipboard write unavailable"});
  if(written){state_->own_writes.emplace_back(current.value(),written.value());if(state_->own_writes.size()>64)state_->own_writes.pop_front();}
  r->report("",written?"Received into clipboard and history":"Saved in history; clipboard write failed");return true;
 }
 return false;
}
}

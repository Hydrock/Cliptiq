// Service orchestration tests. Deliberately link fake transport/secrets/image boundary
// symbols below instead of their production implementations; TLS has its own tests.
#include "../../platform/lan/Service.hpp"
#include "../../platform/lan/Transport.hpp"
#include "../../platform/lan/Secrets.hpp"
#include "../../platform/lan/Images.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
namespace fake {
std::map<std::string,std::string> secrets;
int reads=0, starts=0, stops=0;
std::atomic<int> published{0};
std::atomic<bool> ready{true};
std::vector<lan::Event> published_events;
std::function<void()> after_secret_write;
std::function<Result<void>(std::function<void()>)> enqueue;
lan::TransportCallbacks callbacks;
std::thread::id worker;
template<class F> auto call(F f) -> std::future<decltype(f())> {
 auto task=std::make_shared<std::packaged_task<decltype(f())()>>(std::move(f));
 auto future=task->get_future();ok(enqueue([task]{(*task)();}));return future;
}
void barrier(){auto f=call([]{return true;});CHECK(f.wait_for(std::chrono::seconds(3))==std::future_status::ready);CHECK(f.get());}
}
namespace cliptiq::lan {
struct Transport::Impl {std::mutex mutex;std::condition_variable changed;std::deque<std::function<void()>> queue;std::thread thread;std::atomic<bool> running{false};bool stopping=false;TransportCallbacks callbacks;};
Transport::Transport():impl_(std::make_unique<Impl>()){}
Transport::~Transport(){stop();}
Result<void> Transport::start(TransportConfig config,TransportCallbacks callbacks){
 CHECK(!config.peers.empty());++fake::starts;impl_->callbacks=std::move(callbacks);fake::callbacks=impl_->callbacks;impl_->running=true;
 fake::enqueue=[this](std::function<void()> f){return post(std::move(f));};
 impl_->thread=std::thread([this]{fake::worker=std::this_thread::get_id();if(impl_->callbacks.started)impl_->callbacks.started();for(;;){std::function<void()> f;{std::unique_lock<std::mutex> l(impl_->mutex);impl_->changed.wait(l,[&]{return impl_->stopping||!impl_->queue.empty();});if(impl_->queue.empty()&&impl_->stopping)break;f=std::move(impl_->queue.front());impl_->queue.pop_front();}f();if(impl_->running&&impl_->callbacks.tick)impl_->callbacks.tick();}if(impl_->callbacks.stopped)impl_->callbacks.stopped();});return {};
}
void Transport::stop(){if(!impl_->running.exchange(false))return;++fake::stops;{std::lock_guard<std::mutex> l(impl_->mutex);impl_->stopping=true;}impl_->changed.notify_all();impl_->thread.join();}
bool Transport::active()const{return impl_->running;}
bool Transport::ready_to_publish()const{return active()&&fake::ready;}
Result<void> Transport::post(std::function<void()> f){std::lock_guard<std::mutex> l(impl_->mutex);if(impl_->stopping)return Error{ErrorCode::Busy,"stopped"};impl_->queue.push_back(std::move(f));impl_->changed.notify_all();return {};}
void Transport::publish(std::shared_ptr<const Bytes> wire){CHECK(std::this_thread::get_id()==fake::worker);CHECK(ready_to_publish());fake::published_events.push_back(take(decode_event(*wire)));++fake::published;}
Result<std::string> read_secret(const std::string& ref){++fake::reads;auto i=fake::secrets.find(ref);if(i==fake::secrets.end())return Error{ErrorCode::NotFound,"missing test secret"};return i->second;}
Result<void> write_secret(const std::string& ref,const std::string& key){fake::secrets[ref]=key;if(fake::after_secret_write)fake::after_secret_write();return {};}
Result<void> erase_secret(const std::string& ref){fake::secrets.erase(ref);return {};}
Result<std::string> generate_key(){return std::string(64,'a');}
Result<Bytes> network_png(const Representation& r){return r.data;}
}
struct Fixture {
 std::filesystem::path directory=std::filesystem::temp_directory_path()/("cliptiq-service-"+lan::new_id());
 std::string path;
 std::optional<Store> db;
 std::unique_ptr<lan::Service> service;
 std::string token="initial";
 std::vector<ClipboardSnapshot> writes;
 bool fail_write=false;
std::function<void()> before_compare;
Result<std::string> write(const ClipboardSnapshot& s){CHECK(std::this_thread::get_id()==ui);if(fail_write)return Error{ErrorCode::Io,"test clipboard write failed"};writes.push_back(s);token="write-"+std::to_string(writes.size());return token;}
 std::thread::id ui=std::this_thread::get_id();
 std::string peer,remote=lan::new_id();
 Fixture(){std::filesystem::create_directories(directory);path=(directory/"history.db").u8string();db.emplace(take(open_store(path)));service=std::make_unique<lan::Service>(*db,path,CapturePlatform::Windows,lan::ClipboardHooks{[this]()->Result<std::string>{CHECK(std::this_thread::get_id()==ui);return token;},[this](const ClipboardSnapshot&s)->Result<std::string>{return write(s);},[this](const ClipboardSnapshot&s,const std::string& expected)->Result<std::string>{CHECK(std::this_thread::get_id()==ui);if(before_compare)before_compare();if(token!=expected)return Error{ErrorCode::Busy,"newer local clipboard kept"};return write(s);}});ok(service->initialize());}
 ~Fixture(){service.reset();db.reset();std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
 void enable(lan::ReceiveMode mode=lan::ReceiveMode::ClipboardAndHistory){auto c=take(service->config());c.receive_mode=mode;lan::Peer p;p.id=lan::new_id();p.name="Desk";p.host="127.0.0.1";peer=p.id;c.peers.push_back(p);ok(service->save(c,{{peer,std::string(64,'a')}}));CHECK(service->running());auto f=fake::call([this]{return fake::callbacks.identity(peer,remote);});ok(f.get());}
 lan::Event event(std::uint64_t seq=1){return {remote,lan::new_id(),seq,1,{{0,"portable","text/plain",CanonicalKind::Text,{'h','i'}}}};}
 std::string checkpoint(){auto f=fake::call([this]{return fake::callbacks.receiving(peer);});auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(f.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){service->poll();CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}return take(f.get());}
 Result<void> receive(const lan::Event&e,std::string checkpoint){auto wire=take(lan::encode_event(e));auto f=fake::call([this,wire,checkpoint]{return fake::callbacks.received(peer,wire,checkpoint);});return f.get();}
 std::size_t count(){return take(list_history(*db)).size();}
};
void latest_capture_waits_for_capacity(){Fixture f;f.enable();fake::ready=false;int sent=fake::published;auto first=f.event(),second=f.event();first.representations[0].data={'f','i','r','s','t'};second.representations[0].data={'l','a','t','e','s','t'};auto firstId=take(ingest(*f.db,take(lan::native_snapshot(first,CapturePlatform::Windows)),{},30));auto secondId=take(ingest(*f.db,take(lan::native_snapshot(second,CapturePlatform::Windows)),{},31));CHECK(firstId&&secondId);f.service->captured(*firstId);f.service->captured(*secondId);fake::barrier();CHECK(fake::published==sent);fake::ready=true;auto tick=fake::call([]{if(fake::callbacks.tick)fake::callbacks.tick();return true;});CHECK(tick.get());fake::barrier();CHECK(fake::published==sent+1);CHECK(fake::published_events.back().representations[0].data==second.representations[0].data);}
void queued_arrivals(){Fixture f;f.enable();auto checkpoint=f.checkpoint();auto first=f.event(1),second=f.event(2);second.representations[0].data={'n','e','w'};ok(f.receive(first,checkpoint));ok(f.receive(second,checkpoint));CHECK(f.count()==2);while(f.service->poll()){}CHECK(!f.writes.empty());auto expected=take(lan::native_snapshot(second,CapturePlatform::Windows));CHECK(f.writes.back().representations[0].data==expected.representations[0].data);}
void compare_write_race(){Fixture f;f.enable();auto checkpoint=f.checkpoint();ok(f.receive(f.event(),checkpoint));f.before_compare=[&]{f.token="local copy between precheck and write";};CHECK(f.service->poll());CHECK(f.writes.empty()&&f.count()==1);CHECK(f.token=="local copy between precheck and write");}
void settings_race(){Fixture f;f.enable();auto draft=take(f.service->config());auto oldref=draft.peers[0].secret_ref;auto oldkey=fake::secrets.at(oldref);auto previousSecrets=fake::secrets.size();std::uint64_t currentRevision=0;fake::after_secret_write=[&]{auto competing=take(open_store(f.path));auto actual=take(lan::load_config(competing));actual.paused=true;ok(lan::save_config(competing,actual));currentRevision=take(lan::load_config(competing)).revision;};auto saved=f.service->save(draft,{{f.peer,std::string(64,'c')}});fake::after_secret_write={};CHECK(!saved);CHECK(!f.service->running());auto actual=take(f.service->config());CHECK(actual.paused&&actual.revision==currentRevision);CHECK(actual.peers[0].secret_ref==oldref);CHECK(fake::secrets.size()==previousSecrets&&fake::secrets.at(oldref)==oldkey);}
void disabled_and_save(){
 int reads=fake::reads,starts=fake::starts;Fixture f;CHECK(fake::reads==reads&&fake::starts==starts&&!f.service->running());
 auto c=take(f.service->config());auto previous=c;lan::Peer p;p.id=lan::new_id();p.name="A";p.host="127.0.0.1";c.peers.push_back(p);auto p2=p;p2.id=lan::new_id();c.peers.push_back(p2);auto secrets=fake::secrets.size();CHECK(!f.service->save(c,{{p.id,std::string(64,'a')}}));CHECK(fake::secrets.size()==secrets);CHECK(take(f.service->config()).revision==previous.revision);CHECK(take(f.service->config()).peers.empty());
 c=previous;c.device_name="new";ok(f.service->save(c,{}));CHECK(!f.service->save(previous,{}));CHECK(take(f.service->config()).device_name=="new");
 ok(f.service->copy_secret(std::string(64,'b')));CHECK(f.writes.size()==1);CHECK(f.writes.back().flags.transient==Flag::Yes&&f.writes.back().flags.concealed==Flag::Yes);CHECK(f.count()==0);CHECK(fake::reads==reads&&fake::starts==starts);
}
void receive_and_no_echo(){Fixture f;f.enable();auto e=f.event();auto token=f.checkpoint();CHECK(f.writes.empty()&&f.count()==0);ok(f.receive(e,token));CHECK(f.writes.empty()&&f.count()==1);CHECK(f.service->poll());CHECK(f.writes.size()==1);auto id=take(list_history(*f.db))[0].id;int sent=fake::published;f.service->captured(id);fake::barrier();CHECK(fake::published==sent);ok(f.receive(e,token));CHECK(!f.service->poll());CHECK(f.writes.size()==1&&f.count()==1);
 auto wrong=f.event(2);wrong.origin_device_id=lan::new_id();CHECK(!f.receive(wrong,f.token));CHECK(f.count()==1&&f.writes.size()==1);
 auto oversized=fake::call([&]{return fake::callbacks.received(f.peer,Bytes(lan::max_event_bytes+1,0),f.token);});CHECK(!oversized.get());CHECK(f.count()==1&&f.writes.size()==1);
 ClipboardSnapshot local;local.representations={{0,"windows","CF_UNICODETEXT",CanonicalKind::Text,{'l','o','c','a','l'}}};auto localId=take(ingest(*f.db,local,{},20));CHECK(localId);f.service->captured(*localId);fake::barrier();CHECK(fake::published==sent+1);
 // No pairing secret is serialized into SQLite (including WAL).
 for(auto const& entry:std::filesystem::directory_iterator(f.directory)){if(!entry.is_regular_file())continue;std::ifstream in(entry.path(),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});CHECK(bytes.find(std::string(64,'a'))==std::string::npos);}
}
void history_only(){Fixture f;f.enable(lan::ReceiveMode::HistoryOnly);auto token=f.checkpoint();ok(f.receive(f.event(),token));CHECK(f.count()==1);CHECK(!f.service->poll());CHECK(f.writes.empty());}
void local_race(){Fixture f;f.enable();auto token=f.checkpoint();f.token="new-local";ok(f.receive(f.event(),token));CHECK(f.service->poll());CHECK(f.writes.empty());CHECK(f.token=="new-local"&&f.count()==1);CHECK(f.service->status().find("newer local clipboard kept")!=std::string::npos);}
void clipboard_write_failure(){Fixture f;f.enable();auto token=f.checkpoint();ok(f.receive(f.event(),token));f.fail_write=true;CHECK(f.service->poll());CHECK(f.count()==1&&f.writes.empty());CHECK(f.service->status().find("clipboard write failed")!=std::string::npos);}
void pause_and_remove(){Fixture f;f.enable();auto c=take(f.service->config());c.paused=true;ok(f.service->save(c,{}));CHECK(!f.service->running());int starts=fake::starts,reads=fake::reads;c=take(f.service->config());c.peers.clear();c.paused=false;ok(f.service->save(c,{}));CHECK(!f.service->running()&&fake::starts==starts&&fake::reads==reads);}
void stop_checkpoint(){Fixture f;f.enable();std::promise<void> entered;auto signal=entered.get_future();auto waiting=fake::call([&]{entered.set_value();return fake::callbacks.receiving(f.peer);});signal.get(); // callback may be just before enqueue; stop must cover either interleaving.
 auto start=std::chrono::steady_clock::now();f.service->stop();CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));CHECK(waiting.wait_for(std::chrono::milliseconds(0))==std::future_status::ready);CHECK(!waiting.get());CHECK(!f.service->running());f.service->stop();}
void initial_snapshot_not_exported(){Fixture f;
 ClipboardSnapshot old;old.sequence_token=f.token;old.representations={{0,"portable","text/plain",CanonicalKind::Text,{'o','l','d'}}};auto oldId=take(ingest(*f.db,old,{},1));CHECK(oldId);
 f.enable();int sent=fake::published.load();f.service->captured(*oldId);fake::barrier();CHECK(fake::published==sent);
 old.sequence_token="fresh";old.representations[0].data={'n','e','w'};auto freshId=take(ingest(*f.db,old,{},2));CHECK(freshId);f.service->captured(*freshId);fake::barrier();CHECK(fake::published==sent+1);
 f.service->captured(*freshId);fake::barrier();CHECK(fake::published==sent+1);
}
int main(){try{initial_snapshot_not_exported();latest_capture_waits_for_capacity();compare_write_race();settings_race();queued_arrivals();disabled_and_save();receive_and_no_echo();history_only();local_race();clipboard_write_failure();pause_and_remove();stop_checkpoint();std::cout<<"PASS LAN service orchestration\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

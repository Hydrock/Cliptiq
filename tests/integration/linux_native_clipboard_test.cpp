#include "Clipboard.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
using namespace cliptiq::linux_backend;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
using Factory=std::function<std::unique_ptr<LinuxClipboardBackend>(ClipboardOptions)>;
struct Source{
 std::atomic<bool> done{false};std::thread thread;std::mutex mutex;
 std::deque<std::function<void(LinuxClipboardBackend&)>> tasks;
 explicit Source(Factory factory){std::promise<void> ready;auto future=ready.get_future();thread=std::thread([&,factory]{auto backend=factory({});ready.set_value();while(!done){std::function<void(LinuxClipboardBackend&)> task;{std::lock_guard<std::mutex> lock(mutex);if(!tasks.empty()){task=std::move(tasks.front());tasks.pop_front();}}if(task)task(*backend);auto result=backend->dispatch(2);if(!result)std::this_thread::sleep_for(std::chrono::milliseconds(1));}});future.get();}
 ~Source(){done=true;thread.join();}
 Result<std::string> put(ClipboardSnapshot s){auto promise=std::make_shared<std::promise<Result<std::string>>>();auto future=promise->get_future();{std::lock_guard<std::mutex> lock(mutex);tasks.push_back([promise,s=std::move(s)](auto& b){promise->set_value(b.write_snapshot(s));});}if(future.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("test source deadline");return future.get();}
};
const Representation* find(const ClipboardSnapshot& s,const char* name){for(const auto& r:s.representations)if(r.native_type==name)return &r;return nullptr;}
int main(int argc,char** argv){if(argc!=2||!getenv("CLIPTIQ_TEST_ISOLATED")||std::string(getenv("CLIPTIQ_TEST_ISOLATED"))!=argv[1]){std::cerr<<"Isolated display launcher required\n";return 2;}
 const bool x=std::string(argv[1])=="x11";const char* platform=x?"x11":"wayland";const char* text=x?"UTF8_STRING":"text/plain;charset=utf-8";
 Factory factory=[&](ClipboardOptions options)->std::unique_ptr<LinuxClipboardBackend>{if(x)return std::make_unique<ClipboardX11>(options);return std::make_unique<ClipboardWayland>(options);};
 auto snapshot=[&](Bytes bytes){ClipboardSnapshot s;s.representations={{0,platform,text,CanonicalKind::Text,std::move(bytes)}};return s;};
 auto wait=[&](LinuxClipboardBackend& b,auto predicate,unsigned timeout=3000){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);while(!predicate()&&std::chrono::steady_clock::now()<end)ok(b.dispatch(5));return predicate();};
 int failures=0;auto test=[&](const char* name,auto f){try{f();std::cout<<"PASS "<<(x?"x11_":"wayland_")<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
 test("native_utf8_binary_rtf_roundtrip",[&]{Source source(factory);auto b=factory({});auto s=snapshot(Bytes{'h',0xc3,0xa9,'\n'});s.representations.push_back({0,platform,"text/rtf",CanonicalKind::Rtf,Bytes{'{',0,1,'}'}});take(source.put(s));auto r=take(b->read_snapshot());CHECK(find(r,text)&&find(r,text)->data==s.representations[0].data);CHECK(find(r,"text/rtf")&&find(r,"text/rtf")->data==s.representations[1].data);});
 test("large_incr_or_fd_roundtrip",[&]{Source source(factory);auto b=factory({});Bytes bytes(512*1024,'x');bytes.back()='y';take(source.put(snapshot(bytes)));auto r=take(b->read_snapshot());CHECK(find(r,text)&&find(r,text)->data==bytes);});
 test("html_disabled_preserves_text",[&]{Source source(factory);auto b=factory({});auto s=snapshot(Bytes{'a'});s.representations.push_back({0,platform,"text/html",CanonicalKind::Html,Bytes{'<','b','>'}});take(source.put(s));auto r=take(b->read_snapshot());CHECK(find(r,text)&&!find(r,"text/html"));});
 test("transient_capture_is_metadata_only",[&]{Source source(factory);auto b=factory({});auto s=snapshot(Bytes{'x'});s.flags.transient=Flag::Yes;take(source.put(s));auto r=take(b->read_snapshot());CHECK(r.representations.empty()&&r.flags.transient==Flag::Yes);});
 test("unknown_source_exclusions_fail_closed",[&]{Source source(factory);ClipboardOptions options;options.policy.excluded_apps={"private.app"};auto b=factory(options);take(source.put(snapshot(Bytes{'x'})));CHECK(take(b->read_snapshot()).representations.empty());});
 test("listener_own_write_stop",[&]{Source source(factory);auto b=factory({});take(source.put(snapshot(Bytes{'a'})));unsigned calls=0;bool error=false;ok(b->start([&](Result<ClipboardSnapshot> r){if(r)++calls;else error=true;}));CHECK(wait(*b,[&]{return calls==1||error;}));CHECK(!error);take(b->write_snapshot(snapshot(Bytes{'b'})));for(int i=0;i<30;++i)ok(b->dispatch(2));CHECK(calls==1);take(source.put(snapshot(Bytes{'c'})));CHECK(wait(*b,[&]{return calls==2||error;}));CHECK(!error);b->stop();take(source.put(snapshot(Bytes{'d'})));for(int i=0;i<10;++i)ok(b->dispatch(2));CHECK(calls==2);});
 return failures?1:0;}

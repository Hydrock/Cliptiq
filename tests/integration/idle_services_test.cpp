#include "../../platform/Media.hpp"
#include "cliptiq/lifecycle.hpp"
#include "sqlite3.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
int main(int argc,char**argv){try{
 if(argc!=2)return 2;
 if(std::string(argv[1])=="media"){
  auto store=take(open_store(":memory:"));
  ClipboardSnapshot s;s.representations={{0,"windows","CF_TEXT",CanonicalKind::Text,{'x'}}};
  take(ingest(store,s,{},1));
  MediaService media(store,CapturePlatform::Windows);ok(media.configure(default_settings(CapturePlatform::Windows)));
  take(media.poll());
  int scans=0;
  sqlite3_trace_v2(store.connection(),SQLITE_TRACE_STMT,[](unsigned,void* ctx,void* statement,void*){
    std::string sql=sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
    if(sql.find("FROM history ORDER BY")!=std::string::npos||sql.find("FROM thumbnails")!=std::string::npos)++*static_cast<int*>(ctx);
    return 0;
  },&scans);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));take(media.poll());
  if(scans)throw std::runtime_error("Unchanged history was rescanned: "+std::to_string(scans));
  // A fresh revision must still be discovered, including after erase.
  s.representations[0].data={'y'};take(ingest(store,s,{},2));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));take(media.poll());
  if(!scans)throw std::runtime_error("Changed history was not rescanned");
  ok(erase_history(store));std::this_thread::sleep_for(std::chrono::milliseconds(300));take(media.poll());
  sqlite3_trace_v2(store.connection(),0,nullptr,nullptr);
 }else if(std::string(argv[1])=="mailbox"){
  auto root=std::filesystem::temp_directory_path()/("cliptiq-idle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  {SessionInstance owner;take(owner.start({(root/"private").u8string(),"idle"},std::nullopt));
   sqlite3* other=nullptr;auto file=(root/"private/idle.sqlite3").u8string();
   if(sqlite3_open(file.c_str(),&other)!=SQLITE_OK)throw std::runtime_error("Cannot open mailbox fixture");
   #ifdef _WIN32
   // Drain startup once, then a quiet mailbox must not touch SQLite at all.
   take(owner.receive());
   sqlite3_exec(other,"BEGIN EXCLUSIVE",nullptr,nullptr,nullptr);
#else
   sqlite3_exec(other,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr);
#endif
   auto result=owner.receive();
   sqlite3_exec(other,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(other);
   if(!result||!result.value().empty())throw std::runtime_error("Quiet mailbox touched SQLite while no launch request was pending");
#ifdef _WIN32
   // Simulate an older sender that commits a valid request without a signal.
   if(sqlite3_open(file.c_str(),&other)!=SQLITE_OK)throw std::runtime_error("Cannot reopen mailbox fixture");
   sqlite3_exec(other,"INSERT INTO mailbox(action) VALUES(1)",nullptr,nullptr,nullptr);sqlite3_close(other);
   std::this_thread::sleep_for(std::chrono::milliseconds(2100));
   auto legacy=take(owner.receive());if(legacy.size()!=1||legacy[0]!=LaunchAction::Main)throw std::runtime_error("Fallback lost unsignaled launch request");
#endif
  }std::filesystem::remove_all(root);
 }else return 2;
 std::cout<<"PASS "<<argv[1]<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

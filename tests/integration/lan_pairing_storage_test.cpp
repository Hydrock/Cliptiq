#include "cliptiq/lan.hpp"
#include "sqlite3.h"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;using namespace cliptiq::lan;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
int main(){try{
 auto store=take(open_store(":memory:"));auto initial=take(load_config(store));
 PendingPair p;p.session_id="session";p.peer_id="peer";p.remote_device_id="remote";p.secret_ref="pairing-session";p.auth_id=std::string(64,'a');p.name="PC";p.host="192.168.1.2";p.bind_address="192.168.1.1";p.port=49731;p.expires_ms=300000;
 CHECK(stage_pair(store,p));CHECK(take(load_config(store)).peers.empty());CHECK(take(pending_pairs(store)).size()==1);CHECK(stage_pair(store,p));
 CHECK(sqlite3_exec(store.connection(),"CREATE TRIGGER fail_pair BEFORE UPDATE ON lan_state BEGIN SELECT RAISE(ABORT,'fixture failure'); END",nullptr,nullptr,nullptr)==SQLITE_OK);
 CHECK(!commit_pair(store,"session"));CHECK(take(load_config(store)).peers.empty());CHECK(take(pending_pairs(store))[0].phase==PendingPair::Phase::Prepared);
 CHECK(sqlite3_exec(store.connection(),"DROP TRIGGER fail_pair",nullptr,nullptr,nullptr)==SQLITE_OK);
 CHECK(commit_pair(store,"session"));CHECK(commit_pair(store,"session"));auto paired=take(load_config(store));CHECK(paired.peers.size()==1&&paired.peers[0].enabled&&paired.peers[0].remote_device_id=="remote"&&paired.bind_address==p.bind_address);
 auto duplicate=p;duplicate.session_id="another";duplicate.peer_id="another";duplicate.secret_ref="pairing-another";CHECK(!stage_pair(store,duplicate));
 CHECK(finish_authenticated_pair(store,"peer","remote"));CHECK(take(pending_pairs(store)).empty());CHECK(abort_pair(store,"session"));CHECK(take(load_config(store)).peers.size()==1);
 p.session_id="second";p.peer_id="second";p.remote_device_id="second";p.secret_ref="pairing-second";p.auth_id=std::string(64,'b');CHECK(stage_pair(store,p));CHECK(commit_pair(store,p.session_id));CHECK(abort_pair(store,p.session_id));CHECK(take(load_config(store)).peers.size()==1);
 std::cout<<"PASS_staging_rollback_idempotent_commit_identity_finish_and_scoped_abort\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

#include "db.hpp"
namespace cliptiq {
struct Store::Impl {sqlite3* db;Limits limits;~Impl(){sqlite3_close_v2(db);}};
Store::Store(sqlite3* db, Limits limits):impl_(new Impl{db,limits}){}
Store::~Store()=default;
Store::Store(Store&&) noexcept=default;
Store& Store::operator=(Store&&) noexcept=default;
sqlite3* Store::connection() const{return impl_->db;}
const Limits& Store::limits() const{return impl_->limits;}
Result<Store> open_store(const std::string& path,Limits limits){return detail::guarded<Store>([&]()->Result<Store>{
 if(path.empty()||path.find('\0')!=std::string::npos)detail::invalid("invalid database path");
 if(!limits.representation_bytes||limits.representation_bytes>32ull*1024*1024||!limits.entry_bytes||limits.entry_bytes>64ull*1024*1024||!limits.history_bytes||limits.history_bytes>256ull*1024*1024||!limits.max_representations||limits.max_representations>4096)detail::invalid("limits exceed hard ceilings or are zero");
 sqlite3* raw=nullptr;int rc=sqlite3_open_v2(path.c_str(),&raw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr);
 std::unique_ptr<sqlite3,decltype(&sqlite3_close_v2)> owner(raw,sqlite3_close_v2);
 if(rc!=SQLITE_OK)throw detail::Failure{detail::db_error(raw,rc)};
 sqlite3_extended_result_codes(raw,1);sqlite3_busy_timeout(raw,1000);sqlite3_limit(raw,SQLITE_LIMIT_LENGTH,65*1024*1024);
 detail::exec(raw,"PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF");
 detail::Statement version(raw,"PRAGMA user_version");version.row();auto v=version.number(0);
 detail::Statement app(raw,"PRAGMA application_id");app.row();auto aid=app.number(0);
 if(v!=0&&v!=1)return Error{ErrorCode::Unsupported,"unsupported database schema version"};
 if(v==1&&aid!=0x43544951)return Error{ErrorCode::Corrupt,"database application identity mismatch"};
 if(v==0){
  detail::Statement tables(raw,"SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");tables.row();
  if(tables.number(0)!=0)return Error{ErrorCode::Unsupported,"refusing to initialize a nonempty foreign database"};
  detail::Transaction tx(raw);
  detail::exec(raw,R"SQL(
CREATE TABLE store_state(id INTEGER PRIMARY KEY CHECK(id=1),data_revision INTEGER NOT NULL DEFAULT 0,cleanup_interval INTEGER NOT NULL DEFAULT 0,cleanup_last INTEGER);
INSERT INTO store_state(id) VALUES(1);
CREATE TABLE history(id TEXT PRIMARY KEY,digest TEXT NOT NULL,created_ms INTEGER NOT NULL,used_ms INTEGER NOT NULL,payload_bytes INTEGER NOT NULL CHECK(payload_bytes>=0),sequence_token TEXT NOT NULL,source_app TEXT,source_confidence INTEGER NOT NULL,transient INTEGER NOT NULL,concealed INTEGER NOT NULL,remote INTEGER NOT NULL,ocr_text TEXT,ocr_state INTEGER);
CREATE INDEX history_digest ON history(digest);
CREATE INDEX history_created ON history(created_ms);
CREATE INDEX history_used ON history(used_ms);
CREATE TABLE representations(entry_id TEXT NOT NULL REFERENCES history(id) ON DELETE CASCADE,ordinal INTEGER NOT NULL,item_index INTEGER NOT NULL,platform TEXT NOT NULL,native_type TEXT NOT NULL,kind INTEGER NOT NULL,data BLOB NOT NULL,text_encoding INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(entry_id,ordinal));
CREATE TABLE history_assets(entry_id TEXT NOT NULL REFERENCES history(id) ON DELETE CASCADE,kind TEXT NOT NULL,data BLOB NOT NULL,PRIMARY KEY(entry_id,kind));
CREATE TABLE snippet_folders(id TEXT PRIMARY KEY,title TEXT NOT NULL,position INTEGER NOT NULL);
CREATE TABLE snippets(id TEXT PRIMARY KEY,folder_id TEXT REFERENCES snippet_folders(id),title TEXT NOT NULL,content TEXT NOT NULL,position INTEGER NOT NULL);
CREATE TABLE settings(key TEXT PRIMARY KEY,value BLOB NOT NULL);
PRAGMA application_id=1129597265;
PRAGMA user_version=1;
)SQL");tx.commit();
 }
 detail::Statement integrity(raw,"PRAGMA quick_check");if(!integrity.row()||integrity.text(0)!="ok")return Error{ErrorCode::Corrupt,"database integrity check failed"};
 detail::Statement state(raw,"SELECT data_revision,cleanup_interval,cleanup_last FROM store_state WHERE id=1");if(!state.row())return Error{ErrorCode::Corrupt,"missing store state"};
 sqlite3_db_config(raw,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);
 Store result(raw,limits);owner.release();return result;
});}
Result<std::uint64_t> data_revision(Store& s){return detail::guarded<std::uint64_t>([&]()->Result<std::uint64_t>{detail::Statement q(s.connection(),"SELECT data_revision FROM store_state WHERE id=1");if(!q.row())return Error{ErrorCode::Corrupt,"missing store state"};return static_cast<std::uint64_t>(q.number(0));});}
Result<void> compact_store(Store& s){return detail::guarded<void>([&]()->Result<void>{detail::exec(s.connection(),"PRAGMA wal_checkpoint(TRUNCATE); VACUUM");return {};});}
}

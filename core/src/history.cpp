#include "cliptiq/history.hpp"
#include "db.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <limits>
#include <random>
namespace cliptiq {
namespace {
using namespace detail;
std::string digest(const ClipboardSnapshot& v){
 Sha256 hash;hash.integer(1);hash.integer(v.representations.size());
 for(const auto& r:v.representations){hash.integer(r.item_index);hash.field(r.platform.data(),r.platform.size());hash.field(r.native_type.data(),r.native_type.size());hash.integer(static_cast<unsigned>(r.kind));hash.integer(static_cast<unsigned>(r.text_encoding));hash.field(r.data.data(),r.data.size());}
 return hash.finish();
}
std::string uuid(){std::random_device random;unsigned char b[16];for(auto& x:b)x=static_cast<unsigned char>(random());b[6]=(b[6]&15)|64;b[8]=(b[8]&63)|128;const char* hex="0123456789abcdef";std::string id;for(int i=0;i<16;++i){if(i==4||i==6||i==8||i==10)id+='-';id+=hex[b[i]>>4];id+=hex[b[i]&15];}return id;}
bool equal(const ClipboardSnapshot& a,const ClipboardSnapshot& b){if(a.representations.size()!=b.representations.size())return false;for(std::size_t i=0;i<a.representations.size();++i){const auto& x=a.representations[i];const auto& y=b.representations[i];if(x.item_index!=y.item_index||x.platform!=y.platform||x.native_type!=y.native_type||x.kind!=y.kind||x.text_encoding!=y.text_encoding||x.data!=y.data)return false;}return true;}
// Unicode White_Space; malformed/non-whitespace bytes are preserved, not discarded.
bool blank(const Bytes& b){std::size_t i=0;while(i<b.size()){std::uint32_t c=b[i++];unsigned more=0;if(c>=0xc2&&c<=0xdf){more=1;c&=31;}else if(c>=0xe0&&c<=0xef){more=2;c&=15;}else if(c>=0xf0&&c<=0xf4){more=3;c&=7;}else if(c>=128)return false;if(i+more>b.size())return false;for(unsigned j=0;j<more;++j){auto d=b[i++];if((d&0xc0)!=0x80)return false;c=(c<<6)|(d&63);}if((more==1&&c<128)||(more==2&&c<2048)||(more==3&&c<65536)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;if(!(c==32||(c>=9&&c<=13)||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000))return false;}return true;}
bool blank_text(const Representation& r){
 if(r.text_encoding==TextEncoding::Utf8)return blank(r.data);
 if(r.data.size()%2)return false;
 for(std::size_t i=0;i<r.data.size();i+=2){
  auto c=r.text_encoding==TextEncoding::Utf16Le?(unsigned(r.data[i])|(unsigned(r.data[i+1])<<8)):((unsigned(r.data[i])<<8)|r.data[i+1]);
  if(i==0&&c==0xfeff)continue;
  if(c==0&&i+2==r.data.size())continue;
  if(!(c==32||(c>=9&&c<=13)||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000))return false;
 }
 return true;
}
bool blank_primary(const ClipboardSnapshot& v){if(v.representations.empty())return true;auto primary=v.representations.front().kind;if(primary!=CanonicalKind::Text&&primary!=CanonicalKind::Rtf)return false;for(const auto& r:v.representations)if(r.item_index==v.representations.front().item_index&&r.kind==CanonicalKind::Text)return blank_text(r);return false;}
std::uint64_t validate(const ClipboardSnapshot& v,const Limits& l){
 if(v.representations.size()>l.max_representations)throw Failure{{ErrorCode::TooLarge,"too many representations"}};
 if(v.sequence_token.size()>4096||(v.source.app_id&&v.source.app_id->size()>4096))invalid("metadata too long");
 if(static_cast<unsigned>(v.source.confidence)>2||static_cast<unsigned>(v.flags.transient)>2||static_cast<unsigned>(v.flags.concealed)>2||static_cast<unsigned>(v.flags.remote)>2)invalid("invalid metadata enum");
 std::uint64_t bytes=0;for(const auto& r:v.representations){if(r.native_type.empty()||r.native_type.size()>4096||r.platform.size()>4096||static_cast<unsigned>(r.kind)>8||static_cast<unsigned>(r.text_encoding)>2)invalid("invalid representation metadata");if(r.data.size()>l.representation_bytes||r.data.size()>l.entry_bytes-bytes)throw Failure{{ErrorCode::TooLarge,"clipboard entry exceeds payload limit"}};bytes+=r.data.size();}
 if(bytes>l.history_bytes)throw Failure{{ErrorCode::TooLarge,"entry exceeds total history quota"}};
 return bytes;
}
ClipboardSnapshot load(sqlite3* db,const EntryId& id,const Limits& limits){
 // Check sizes with SQL length() before copying any metadata/blob into C++.
 {Statement q(db,"SELECT length(CAST(sequence_token AS BLOB)),length(CAST(source_app AS BLOB)) FROM history WHERE id=?");q.bind(1,id);if(!q.row())throw Failure{{ErrorCode::NotFound,"history entry not found"}};if(q.number(0)>4096||q.number(1)>4096)throw Failure{{ErrorCode::TooLarge,"persisted metadata exceeds limit"}};}
 {Statement q(db,"SELECT count(*),coalesce(sum(length(data)),0),coalesce(max(length(data)),0),coalesce(max(length(CAST(platform AS BLOB))),0),coalesce(max(length(CAST(native_type AS BLOB))),0) FROM representations WHERE entry_id=?");q.bind(1,id);q.row();if(q.number(0)>limits.max_representations||static_cast<std::uint64_t>(q.number(1))>limits.entry_bytes||static_cast<std::uint64_t>(q.number(1))>limits.history_bytes||static_cast<std::uint64_t>(q.number(2))>limits.representation_bytes||q.number(3)>4096||q.number(4)>4096)throw Failure{{ErrorCode::TooLarge,"persisted snapshot exceeds limits"}};}

 Statement head(db,"SELECT sequence_token,source_app,source_confidence,transient,concealed,remote FROM history WHERE id=?");head.bind(1,id);if(!head.row())throw Failure{{ErrorCode::NotFound,"history entry not found"}};
 ClipboardSnapshot v;v.sequence_token=head.text(0);if(!head.is_null(1))v.source.app_id=head.text(1);v.source.confidence=static_cast<SourceConfidence>(head.number(2));v.flags={static_cast<Flag>(head.number(3)),static_cast<Flag>(head.number(4)),static_cast<Flag>(head.number(5))};
 Statement q(db,"SELECT item_index,platform,native_type,kind,data,text_encoding FROM representations WHERE entry_id=? ORDER BY ordinal");q.bind(1,id);while(q.row())v.representations.push_back({static_cast<std::uint32_t>(q.number(0)),q.text(1),q.text(2),static_cast<CanonicalKind>(q.number(3)),q.bytes(4),static_cast<TextEncoding>(q.number(5))});validate(v,limits);return v;
}
void erase(sqlite3* db,const EntryId& id){Statement q(db,"DELETE FROM history WHERE id=?");q.bind(1,id);q.done();}
void prune(sqlite3* db,const Limits& l,HistoryPolicy p){
 // Totals are computed from authoritative rows inside the write transaction.
 Statement q(db,p.sort_order==SortOrder::Created?"SELECT id,payload_bytes FROM history ORDER BY created_ms DESC,rowid DESC":"SELECT id,payload_bytes FROM history ORDER BY used_ms DESC,rowid DESC");
 std::vector<std::pair<EntryId,std::uint64_t>> rows;std::uint64_t total=0;while(q.row()){auto n=static_cast<std::uint64_t>(q.number(1));total+=n;rows.emplace_back(q.text(0),n);}
 while(rows.size()>p.max_count||total>l.history_bytes){total-=rows.back().second;erase(db,rows.back().first);rows.pop_back();}
}
}
Result<std::optional<EntryId>> ingest(Store& s,ClipboardSnapshot v,HistoryPolicy p,std::int64_t now,std::optional<std::uint64_t> expected_erase_generation){return detail::guarded<std::optional<EntryId>>([&]()->Result<std::optional<EntryId>>{
 if(!p.max_count||p.max_count>1000||now<0||static_cast<unsigned>(p.sort_order)>1)detail::invalid("invalid history policy or clock");
 auto bytes=validate(v,s.limits());if(blank_primary(v))return std::optional<EntryId>{};
 auto db=s.connection();detail::Transaction tx(db);if(expected_erase_generation&&*expected_erase_generation!=detail::erase_generation(db)){tx.commit();return std::optional<EntryId>{};}auto hash=digest(v);std::optional<EntryId> existing;
 {detail::Statement q(db,"SELECT id FROM history WHERE digest=? ORDER BY used_ms DESC,rowid DESC");q.bind(1,hash);while(q.row()){auto id=q.text(0);if(equal(v,load(db,id,s.limits()))){existing=id;break;}}}
 if(existing&&!p.allow_duplicates){tx.commit();return std::optional<EntryId>{};}
 EntryId id=existing&&p.replace_duplicate?*existing:uuid();
 if(existing&&p.replace_duplicate){
  detail::Statement q(db,"UPDATE history SET used_ms=?,sequence_token=?,source_app=?,source_confidence=?,transient=?,concealed=?,remote=? WHERE id=?");q.bind(1,now);q.bind(2,v.sequence_token);if(v.source.app_id)q.bind(3,*v.source.app_id);else q.null(3);q.bind(4,static_cast<std::int64_t>(v.source.confidence));q.bind(5,static_cast<std::int64_t>(v.flags.transient));q.bind(6,static_cast<std::int64_t>(v.flags.concealed));q.bind(7,static_cast<std::int64_t>(v.flags.remote));q.bind(8,id);q.done();
 }else{
  detail::Statement q(db,"INSERT INTO history(id,digest,created_ms,used_ms,payload_bytes,sequence_token,source_app,source_confidence,transient,concealed,remote) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
  q.bind(1,id);q.bind(2,hash);q.bind(3,now);q.bind(4,now);q.bind(5,static_cast<std::int64_t>(bytes));q.bind(6,v.sequence_token);if(v.source.app_id)q.bind(7,*v.source.app_id);else q.null(7);q.bind(8,static_cast<std::int64_t>(v.source.confidence));q.bind(9,static_cast<std::int64_t>(v.flags.transient));q.bind(10,static_cast<std::int64_t>(v.flags.concealed));q.bind(11,static_cast<std::int64_t>(v.flags.remote));q.done();
  std::int64_t ordinal=0;for(const auto& r:v.representations){detail::Statement part(db,"INSERT INTO representations VALUES(?,?,?,?,?,?,?,?)");part.bind(1,id);part.bind(2,ordinal++);part.bind(3,static_cast<std::int64_t>(r.item_index));part.bind(4,r.platform);part.bind(5,r.native_type);part.bind(6,static_cast<std::int64_t>(r.kind));part.bind(7,r.data);part.bind(8,static_cast<std::int64_t>(r.text_encoding));part.done();}
 }
 prune(db,s.limits(),p);detail::revision(db);bool retained;
 {detail::Statement q(db,"SELECT 1 FROM history WHERE id=?");q.bind(1,id);retained=q.row();}
 tx.commit();return retained?std::optional<EntryId>{id}:std::optional<EntryId>{};
});}
Result<std::vector<HistoryEntry>> list_history(Store& s,SortOrder order,std::size_t limit){return detail::guarded<std::vector<HistoryEntry>>([&]()->Result<std::vector<HistoryEntry>>{
 if(static_cast<unsigned>(order)>1||limit>1000)detail::invalid("invalid list order or limit");
 detail::Statement q(s.connection(),order==SortOrder::Created?"SELECT id,digest,created_ms,used_ms,payload_bytes FROM history ORDER BY created_ms DESC,rowid DESC LIMIT ?":"SELECT id,digest,created_ms,used_ms,payload_bytes FROM history ORDER BY used_ms DESC,rowid DESC LIMIT ?");q.bind(1,static_cast<std::int64_t>(limit));std::vector<HistoryEntry> out;while(q.row())out.push_back({q.text(0),q.text(1),q.number(2),q.number(3),static_cast<std::uint64_t>(q.number(4))});return out;
});}
Result<ClipboardSnapshot> load_snapshot(Store& s,const EntryId& id){return detail::guarded<ClipboardSnapshot>([&]()->Result<ClipboardSnapshot>{auto v=load(s.connection(),id,s.limits());validate(v,s.limits());return v;});}
Result<void> touch_history(Store& s,const EntryId& id,std::int64_t now){return detail::guarded<void>([&]()->Result<void>{if(now<0)detail::invalid("negative clock");auto db=s.connection();detail::Transaction tx(db);detail::Statement q(db,"UPDATE history SET used_ms=? WHERE id=?");q.bind(1,now);q.bind(2,id);q.done();if(!sqlite3_changes(db))return Error{ErrorCode::NotFound,"history entry not found"};detail::revision(db);tx.commit();return {};});}
Result<std::uint64_t> history_erase_generation(Store&s){return detail::guarded<std::uint64_t>([&]()->Result<std::uint64_t>{return detail::erase_generation(s.connection());});}
Result<void> erase_history(Store& s,std::optional<EntryId> id){return detail::guarded<void>([&]()->Result<void>{auto db=s.connection();detail::Transaction tx(db);detail::erased(db);if(id)erase(db,*id);else detail::exec(db,"DELETE FROM history");detail::revision(db);tx.commit();return {};});}
Result<void> tick_cleanup(Store& s,CleanupPolicy p,std::int64_t now){return detail::guarded<void>([&]()->Result<void>{if(p.interval_ms<0||now<0)detail::invalid("invalid cleanup policy or clock");auto db=s.connection();detail::Transaction tx(db);detail::Statement q(db,"SELECT cleanup_interval,cleanup_last FROM store_state WHERE id=1");if(!q.row())return Error{ErrorCode::Corrupt,"missing cleanup state"};auto old=q.number(0),last=q.number(1);bool init=q.is_null(1)||old!=p.interval_ms||now<last;
 if(p.interval_ms&& !init && now-last>=p.interval_ms){detail::erased(db);detail::exec(db,"DELETE FROM history");if(sqlite3_changes(db))detail::revision(db);init=true;}
 if(init||!p.interval_ms){detail::Statement update(db,"UPDATE store_state SET cleanup_interval=?,cleanup_last=? WHERE id=1");update.bind(1,p.interval_ms);if(p.interval_ms)update.bind(2,now);else update.null(2);update.done();}tx.commit();return {};
});}
}

#include "cliptiq/update.hpp"
#include "db.hpp"
#include <array>
#include <charconv>
#include <limits>
#include "tinyxml2.h"
namespace cliptiq {
namespace {
Result<std::array<unsigned,3>> version(const std::string&s) {
  std::array<unsigned,3> v{};size_t start=0;
  for(size_t i=0;i<3;++i) {
    auto end=s.find('.',start);
    if(i==2)end=s.size();
    if(end==std::string::npos||end<=start||end-start>10||(s[start]=='0'&&end-start>1))return Error{ErrorCode::InvalidArgument,"Invalid update version"};
    auto r=std::from_chars(s.data()+start,s.data()+end,v[i]);
    if(r.ec!=std::errc{}||r.ptr!=s.data()+end)return Error{ErrorCode::InvalidArgument,"Invalid update version"};
    start=end+1;
  }
  return v;
}
Result<void> validate(const UpdateInfo&i,const std::string&current,const UpdateTarget&t){
  auto c=version(current),n=version(i.version);
  if(!c||!n)return Error{ErrorCode::InvalidArgument,"Invalid update version"};
  if(n.value()<=c.value())return Error{ErrorCode::InvalidArgument,"Update is not newer"};
  if(i.platform!=t.platform||i.arch!=t.arch||i.route!=t.route)return Error{ErrorCode::InvalidArgument,"Wrong update target or route"};
  if(!i.bytes||i.bytes>t.max_bytes)return Error{ErrorCode::TooLarge,"Update exceeds size budget"};
  if(i.signing_metadata.empty()||i.signing_metadata.size()>8192)return Error{ErrorCode::Corrupt,"Missing signing metadata"};
  const std::string prefix="https://github.com/Hydrock/Cliptiq/releases/download/";
  if(i.package_url.size()>4096||i.package_url.compare(0,prefix.size(),prefix)!=0||i.package_url.size()==prefix.size())return Error{ErrorCode::InvalidArgument,"Unapproved update origin"};
  for(unsigned char c:i.package_url)if(c<=32||c>=127||c=='\\'||c=='#'||c=='?'||c=='%')return Error{ErrorCode::InvalidArgument,"Invalid update URL"};
  return {};
}
}
Result<UpdateInfo> check_update(UpdateProvider&p,UpdateChannel channel,const std::string&current,const UpdateTarget&t){
  auto info=p.fetch(channel);if(!info)return info.error();
  auto v=validate(info.value(),current,t);if(!v)return v.error();
  auto a=p.authenticate(info.value());if(!a)return a.error();
  return info.value();
}
Result<UpdateOutcome> apply_update(UpdateProvider&p,const UpdateInfo&i,const std::string&current,const UpdateTarget&t){
  auto v=validate(i,current,t);if(!v)return v.error();
  // Package managers authenticate repository metadata/packages themselves. Never
  // pass a downloaded payload or write the managed application's executable.
  if(t.route==UpdateRoute::PackageManager)return Error{ErrorCode::Unsupported,"Use the system package manager"};
  if(t.route==UpdateRoute::Download)return Error{ErrorCode::Unsupported,"Portable updates require explicit download/install"};
  auto a=p.authenticate(i);if(!a)return a.error();
  return p.install(i);
}
bool update_due(bool automatic,std::int64_t interval,std::int64_t last,std::int64_t now){
  if(!automatic||now<=0||last<0||now<last||(interval!=86400&&interval!=604800&&interval!=2592000))return false;
  return last==0||now-last>=interval;
}
Result<std::string> release_hint(const std::string&atom,const std::string&current){
  if(atom.size()>131072)return Error{ErrorCode::TooLarge,"Release metadata exceeds limit"};
  if(atom.find("<!")!=std::string::npos||atom.find('\0')!=std::string::npos)return Error{ErrorCode::Corrupt,"Unsupported release metadata"};
  tinyxml2::XMLDocument doc;
  if(doc.Parse(atom.data(),atom.size())!=tinyxml2::XML_SUCCESS)return Error{ErrorCode::Corrupt,"Invalid release metadata"};
  auto root=doc.RootElement();if(!root||std::string(root->Name())!="feed")return Error{ErrorCode::Corrupt,"Invalid release feed"};
  auto c=version(current);if(!c)return c.error();
  std::optional<std::array<unsigned,3>> best;std::string latest;
  unsigned count=0;
  for(auto entry=root->FirstChildElement("entry");entry;entry=entry->NextSiblingElement("entry")){
    if(++count>64)return Error{ErrorCode::TooLarge,"Too many release entries"};
    auto link=entry->FirstChildElement("link");
    const char*href=nullptr;
    for(;link;link=link->NextSiblingElement("link"))if(link->Attribute("rel","alternate")){href=link->Attribute("href");break;}
    if(!href)return Error{ErrorCode::Corrupt,"Missing release link"};
    std::string text=href,prefix="https://github.com/Hydrock/Cliptiq/releases/tag/";
    if(text.compare(0,prefix.size(),prefix)!=0)return Error{ErrorCode::Corrupt,"Wrong release project"};
    auto tag=text.substr(prefix.size());if(!tag.empty()&&tag[0]=='v')tag.erase(0,1);
    auto n=version(tag);if(!n)continue;
    if(n.value()>c.value()&&(!best||n.value()>*best)){best=n.value();latest=tag;}
  }
  if(latest.empty())return Error{ErrorCode::NotFound,"No newer stable release"};
  return latest;
}
Result<std::int64_t> last_update_attempt(Store&s){return detail::guarded<std::int64_t>([&]()->Result<std::int64_t>{
  detail::Statement q(s.connection(),"SELECT value FROM settings WHERE key='update.last_attempt'");
  if(!q.row())return std::int64_t(0);
  auto text=q.text(0);std::int64_t v=0;auto r=std::from_chars(text.data(),text.data()+text.size(),v);
  if(r.ec!=std::errc{}||r.ptr!=text.data()+text.size()||v<0)return Error{ErrorCode::Corrupt,"Invalid update attempt time"};
  return v;
});}
Result<void> save_update_attempt(Store&s,std::int64_t now){return detail::guarded<void>([&]()->Result<void>{
  if(now<=0)return Error{ErrorCode::InvalidArgument,"Invalid update attempt time"};
  detail::Statement q(s.connection(),"INSERT INTO settings(key,value) VALUES('update.last_attempt',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");q.bind(1,std::to_string(now));q.done();return {};
});}
std::string local_diagnostics(){
  return "Cliptiq diagnostics format 1\nversion=0.1.0\nplatform="
#ifdef _WIN32
  "windows"
#elif defined(__APPLE__)
  "macos"
#else
  "linux"
#endif
  "\narch="
#if defined(__aarch64__) || defined(_M_ARM64)
  "arm64"
#else
  "x86_64"
#endif
  "\ntelemetry=disabled\nreport=local-only\n";
}
}

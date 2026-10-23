#include "Transfer.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
namespace cliptiq::linux_backend {
namespace {
const std::vector<NativeType> mime_types={
 {"text/plain;charset=utf-8",CanonicalKind::Text},{"text/plain",CanonicalKind::Text},
 {"text/rtf",CanonicalKind::Rtf},{"application/rtf",CanonicalKind::Rtf},
 {"application/rtfd",CanonicalKind::Rtfd},{"com.apple.flat-rtfd",CanonicalKind::Rtfd},
 {"text/html",CanonicalKind::Html},{"application/pdf",CanonicalKind::Pdf},
 {"text/uri-list",CanonicalKind::Files},{"x-special/gnome-copied-files",CanonicalKind::Files},
 {"text/x-moz-url",CanonicalKind::Url,TextEncoding::Utf16Le},
 {"image/png",CanonicalKind::Image},{"image/tiff",CanonicalKind::Image}};
Error invalid(){return {ErrorCode::Corrupt,"Malformed clipboard text or URI representation"};}
bool utf8(const Bytes& bytes){std::size_t i=0;while(i<bytes.size()){auto c=std::uint32_t(bytes[i++]);unsigned n=0;if(c<128)continue;if(c>=0xc2&&c<=0xdf){n=1;c&=31;}else if(c>=0xe0&&c<=0xef){n=2;c&=15;}else if(c>=0xf0&&c<=0xf4){n=3;c&=7;}else return false;if(i+n>bytes.size())return false;for(unsigned j=0;j<n;++j){auto b=bytes[i++];if((b&192)!=128)return false;c=(c<<6)|(b&63);}if((n==1&&c<128)||(n==2&&c<2048)||(n==3&&c<65536)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;}return true;}
ssize_t safe_write(int fd,const void* data,std::size_t n){sigset_t block,old,pending;sigemptyset(&block);sigaddset(&block,SIGPIPE);if(pthread_sigmask(SIG_BLOCK,&block,&old)!=0){errno=EIO;return -1;}sigpending(&pending);bool existed=sigismember(&pending,SIGPIPE);auto result=write(fd,data,n);int error=errno;if(result<0&&error==EPIPE&&!existed){timespec zero{};sigtimedwait(&block,nullptr,&zero);}pthread_sigmask(SIG_SETMASK,&old,nullptr);errno=error;return result;}
}
const std::vector<NativeType>& native_types(bool x11){static const auto x=[](){auto v=mime_types;v.insert(v.begin(),{{"UTF8_STRING",CanonicalKind::Text},{"STRING",CanonicalKind::Text}});return v;}();return x11?x:mime_types;}
Capabilities native_capabilities(bool x11,bool connected,std::string reason){Capabilities c;c.platform=x11?CapturePlatform::X11:CapturePlatform::Wayland;for(const auto& t:native_types(x11))c.native_types.push_back({x11?"x11":"wayland",t.name,t.kind,t.encoding});c.source_app=x11?Capability{CapabilityStatus::Limited,"X11 owner WM_CLASS is a cooperative hint, not proven authorship"}:Capability{CapabilityStatus::Unavailable,"Wayland data-control does not disclose source application"};if(connected){c.capture={CapabilityStatus::Available,reason.empty()?"Native clipboard protocol; cooperative privacy hints, bounded transfer":reason};c.write={CapabilityStatus::Available,"Selection replay needs live owner event loop; no auto-paste"};}else{c.capture.reason=reason.empty()?"Native clipboard connection unavailable":reason;c.write.reason=c.capture.reason;}return c;}
ClipboardFlags native_flags(const std::vector<ClipboardSnapshot::OfferedType>& offers){ClipboardFlags f;for(const auto& t:offers){if(t.native_type=="org.nspasteboard.TransientType")f.transient=Flag::Yes;if(t.native_type=="org.nspasteboard.ConcealedType")f.concealed=Flag::Yes;}return f;}
Result<void> validate_payload(Representation& r,bool replay){
 if(r.data.size()>Limits{}.representation_bytes)return Error{ErrorCode::TooLarge,"Clipboard representation exceeds hard ceiling"};
 if(r.native_type=="STRING"){
  Bytes converted;for(auto c:r.data){if(c<128)converted.push_back(c);else{converted.push_back(0xc0|(c>>6));converted.push_back(0x80|(c&63));}}if(converted.size()>Limits{}.representation_bytes)return Error{ErrorCode::TooLarge,"Converted text exceeds payload limit"};r.native_type="UTF8_STRING";r.data=std::move(converted);
 }
 if(r.kind==CanonicalKind::Text||r.kind==CanonicalKind::Html||r.kind==CanonicalKind::Files){if(!utf8(r.data))return invalid();}
 if(r.native_type=="text/x-moz-url"){
  if(r.data.size()%2)return invalid();
  for(std::size_t i=0;i<r.data.size();i+=2){auto c=unsigned(r.data[i])|(unsigned(r.data[i+1])<<8);if(c>=0xd800&&c<=0xdbff){if(i+3>=r.data.size())return invalid();auto d=unsigned(r.data[i+2])|(unsigned(r.data[i+3])<<8);if(d<0xdc00||d>0xdfff)return invalid();i+=2;}else if(c>=0xdc00&&c<=0xdfff)return invalid();}
 }
 if(r.native_type=="x-special/gnome-copied-files"){
  std::string value(r.data.begin(),r.data.end());const bool cut=value.compare(0,4,"cut\n")==0,copy=value.compare(0,5,"copy\n")==0;if(!cut&&!copy)return invalid();if(replay&&cut){value.replace(0,4,"copy\n");r.data.assign(value.begin(),value.end());}
 }
 if(r.kind==CanonicalKind::Files){
  std::string value(r.data.begin(),r.data.end());if(r.native_type=="x-special/gnome-copied-files")value.erase(0,value.find('\n')+1);std::size_t start=0;unsigned count=0;
  while(start<value.size()){auto end=value.find('\n',start);if(end==std::string::npos)end=value.size();auto line=value.substr(start,end-start);if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty()&&line.front()!='#'){if(line.find('\0')!=std::string::npos||line.find(':')==std::string::npos||++count>4096)return invalid();}start=end+1;}if(!count)return invalid();
 }
 return {};
}
Result<ClipboardSnapshot> prepare_replay(const ClipboardSnapshot& input,const ClipboardOptions& options,bool x11){
 const auto& l=options.limits;const Limits hard;if(!l.representation_bytes||l.representation_bytes>hard.representation_bytes||!l.entry_bytes||l.entry_bytes>hard.entry_bytes||!l.max_representations||l.max_representations>hard.max_representations||input.representations.empty())return Error{ErrorCode::InvalidArgument,"Invalid replay limits or empty snapshot"};
 if(input.representations.size()>l.max_representations)return Error{ErrorCode::TooLarge,"Too many replay representations"};
 if(static_cast<unsigned>(input.flags.transient)>2||static_cast<unsigned>(input.flags.concealed)>2||static_cast<unsigned>(input.flags.remote)>2)return Error{ErrorCode::InvalidArgument,"Invalid replay flags"};
 ClipboardSnapshot out;out.flags=input.flags;std::uint64_t total=0;
 for(const auto& original:input.representations){auto t=std::find_if(native_types(x11).begin(),native_types(x11).end(),[&](const auto& t){return original.native_type==t.name;});if(original.item_index||original.platform!=(x11?"x11":"wayland")||t==native_types(x11).end()||original.kind!=t->kind||original.text_encoding!=t->encoding)return Error{ErrorCode::Unsupported,"Replay requires recognized native formats at item zero"};if(original.data.size()>l.representation_bytes||original.data.size()>l.entry_bytes-total)return Error{ErrorCode::TooLarge,"Replay exceeds configured byte limits"};auto r=original;auto valid=validate_payload(r,true);if(!valid)return valid.error();if(r.data.size()>l.representation_bytes||r.data.size()>l.entry_bytes-total)return Error{ErrorCode::TooLarge,"Converted replay exceeds limits"};total+=r.data.size();for(const auto& prior:out.representations)if(prior.native_type==r.native_type)return Error{ErrorCode::InvalidArgument,"Duplicate replay format"};out.representations.push_back(std::move(r));}return out;
}
StreamBuffer::StreamBuffer(std::uint64_t n,Clock::time_point d,std::uint64_t g):limit_(n),deadline_(d),generation_(g){if(n>Limits{}.representation_bytes)error_=Error{ErrorCode::InvalidArgument,"Stream budget exceeds hard ceiling"};}
Result<void> StreamBuffer::append(const std::uint8_t* data,std::size_t n,std::uint64_t current){if(error_)return *error_;if(current!=generation_||Clock::now()>=deadline_)error_=Error{ErrorCode::Busy,"Clipboard transfer canceled, changed or timed out"};else if(n>limit_-bytes_.size())error_=Error{ErrorCode::TooLarge,"Clipboard stream exceeds byte budget"};else if(n&&!data)error_=Error{ErrorCode::InvalidArgument,"Invalid stream buffer"};if(error_){bytes_.clear();return *error_;}if(n)bytes_.insert(bytes_.end(),data,data+n);return {};}
Result<Bytes> StreamBuffer::finish(std::uint64_t current){auto valid=append(nullptr,0,current);if(!valid)return valid.error();Bytes result=std::move(bytes_);error_=Error{ErrorCode::InvalidArgument,"Stream already consumed"};return result;}
FdReceiver::FdReceiver(int fd,std::uint64_t n,Clock::time_point d,std::uint64_t g):fd_(fd),buffer_(n,d,g){if(fd_>=0){auto flags=fcntl(fd_,F_GETFL);if(flags<0||fcntl(fd_,F_SETFL,flags|O_NONBLOCK)<0){close(fd_);fd_=-1;}}}
FdReceiver::~FdReceiver(){if(fd_>=0)close(fd_);}
std::optional<Result<Bytes>> FdReceiver::step(std::uint64_t current){
 auto finish=[&](Result<Bytes> result)->std::optional<Result<Bytes>>{if(fd_>=0)close(fd_);fd_=-1;return result;};
 auto valid=buffer_.append(nullptr,0,current);if(!valid)return finish(valid.error());if(fd_<0)return finish(Error{ErrorCode::Io,"Clipboard receive FD unavailable"});
 std::uint8_t data[65536];for(unsigned i=0;i<4;++i){auto n=read(fd_,data,sizeof(data));if(n>0){auto result=buffer_.append(data,static_cast<std::size_t>(n),current);if(!result)return finish(result.error());}else if(n==0)return finish(buffer_.finish(current));else if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return {};else return finish(Error{ErrorCode::Io,"Clipboard FD read failed"});}return {};
}
struct FdSendQueue::Send{int fd;std::shared_ptr<const Bytes> bytes;std::size_t sent=0;Clock::time_point deadline;~Send(){if(fd>=0)close(fd);}};
FdSendQueue::FdSendQueue()=default;FdSendQueue::~FdSendQueue()=default;
Result<void> FdSendQueue::add(int fd,std::shared_ptr<const Bytes> bytes,Clock::time_point deadline){auto deny=[&](ErrorCode code,const char* message)->Result<void>{if(fd>=0)close(fd);return Error{code,message};};std::uint64_t total=0;for(const auto& s:sends_)total+=s->bytes->size();if(fd<0||!bytes)return deny(ErrorCode::InvalidArgument,"Invalid send FD/data");if(sends_.size()>=8||bytes->size()>Limits{}.representation_bytes||total+bytes->size()>128ull*1024*1024)return deny(ErrorCode::TooLarge,"Outgoing clipboard transfer capacity exceeded");auto flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)return deny(ErrorCode::Io,"Cannot configure outgoing clipboard FD");auto send=std::make_unique<Send>();send->fd=fd;send->bytes=std::move(bytes);send->deadline=deadline;sends_.push_back(std::move(send));return {};}
void FdSendQueue::step(){for(auto it=sends_.begin();it!=sends_.end();){auto& s=**it;bool done=Clock::now()>=s.deadline||s.sent==s.bytes->size();for(unsigned i=0;i<4&&!done;++i){auto amount=std::min<std::size_t>(65536,s.bytes->size()-s.sent);auto n=safe_write(s.fd,s.bytes->data()+s.sent,amount);if(n>0){s.sent+=static_cast<std::size_t>(n);done=s.sent==s.bytes->size();}else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))break;else done=true;}if(done)it=sends_.erase(it);else ++it;}}
void FdSendQueue::clear(){sends_.clear();}
std::vector<int> FdSendQueue::fds()const{std::vector<int> result;for(const auto& s:sends_)result.push_back(s->fd);return result;}
}

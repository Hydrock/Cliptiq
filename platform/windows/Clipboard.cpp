#include "Clipboard.hpp"
#include "Formats.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <cstring>
namespace cliptiq::windows {
namespace {
struct Type{const char* name;const wchar_t* registered;UINT standard;CanonicalKind kind;TextEncoding encoding=TextEncoding::Utf8;};
const Type types[]={
 {"CF_UNICODETEXT",nullptr,CF_UNICODETEXT,CanonicalKind::Text,TextEncoding::Utf16Le},
 {"Rich Text Format",L"Rich Text Format",0,CanonicalKind::Rtf},
 {"com.apple.flat-rtfd",L"com.apple.flat-rtfd",0,CanonicalKind::Rtfd},
 {"HTML Format",L"HTML Format",0,CanonicalKind::Html},
 {"Portable Document Format",L"Portable Document Format",0,CanonicalKind::Pdf},
 {"PDF",L"PDF",0,CanonicalKind::Pdf},
 {"CF_HDROP",nullptr,CF_HDROP,CanonicalKind::Files},
 {"Preferred DropEffect",L"Preferred DropEffect",0,CanonicalKind::Files},
 {"UniformResourceLocatorW",L"UniformResourceLocatorW",0,CanonicalKind::Url,TextEncoding::Utf16Le},
 {"PNG",L"PNG",0,CanonicalKind::Image},
 {"CF_DIBV5",nullptr,CF_DIBV5,CanonicalKind::Image},
 {"CF_DIB",nullptr,CF_DIB,CanonicalKind::Image}};
const Type* lookup(const std::string& n){for(const auto& t:types)if(t.name==n)return &t;return nullptr;}
UINT id(const Type& t){return t.standard?t.standard:RegisterClipboardFormatW(t.registered);}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string v(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),v.data(),n,nullptr,nullptr);return v;}
std::string format_name(UINT f){for(const auto& t:types)if(id(t)==f)return t.name;wchar_t n[256]{};auto length=GetClipboardFormatNameW(f,n,256);return length>0?utf8(std::wstring(n,length)):"win32.format."+std::to_string(f);}
SourceInfo owner_source(){DWORD pid=0;auto owner=GetClipboardOwner();if(!owner||!GetWindowThreadProcessId(owner,&pid)||!pid)return {};auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return {};wchar_t path[32768];DWORD length=32768;auto found=QueryFullProcessImageNameW(p,0,path,&length);CloseHandle(p);if(!found)return {};std::wstring lower(length,0);if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,path,static_cast<int>(length),lower.data(),static_cast<int>(length),nullptr,nullptr,0))return {};auto name=utf8(lower);if(name.empty()||name.size()>4096)return {};return {std::move(name),SourceConfidence::BestEffort};}
bool valid_limits(const Limits& l){const Limits h;return l.representation_bytes&&l.representation_bytes<=h.representation_bytes&&l.entry_bytes&&l.entry_bytes<=h.entry_bytes&&l.history_bytes&&l.history_bytes<=h.history_bytes&&l.max_representations&&l.max_representations<=h.max_representations;}
Error wrong_thread(){return {ErrorCode::InvalidArgument,"Windows clipboard operations require their owner thread"};}
struct Open{bool opened=false;~Open(){if(opened)CloseClipboard();}};
struct Memory{HGLOBAL handle=nullptr;~Memory(){if(handle)GlobalFree(handle);}Memory()=default;Memory(Memory&& v)noexcept:handle(v.handle){v.handle=nullptr;}Memory(const Memory&)=delete;};
template<class T,class F>Result<T> guarded(F f){try{return f();}catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"Clipboard allocation failed"};}catch(const std::exception&){return Error{ErrorCode::Internal,"Clipboard operation failed"};}}
Result<Bytes> payload(UINT f,std::uint64_t limit){auto h=GetClipboardData(f);if(!h)return Error{ErrorCode::Io,"Declared clipboard format could not be read"};auto size=GlobalSize(h);if(!size)return Error{ErrorCode::Corrupt,"Clipboard data has no transferable memory"};if(size>limit)return Error{ErrorCode::TooLarge,"Clipboard payload exceeds configured limits"};auto p=static_cast<const std::uint8_t*>(GlobalLock(h));if(!p)return Error{ErrorCode::Io,"Clipboard memory lock failed"};struct Unlock{HGLOBAL h;~Unlock(){GlobalUnlock(h);}} unlock{h};return Bytes(p,p+size);}
constexpr UINT CaptureMessage=WM_APP+51,TaskMessage=WM_APP+52,StopMessage=WM_APP+53,DestroyMessage=WM_APP+54;
}
struct WindowsClipboardBackend::State{
 ClipboardOptions options;HWND window=nullptr;DWORD thread=GetCurrentThreadId();
 std::atomic<bool> active{false};std::atomic<std::uint64_t> generation{0};CaptureCallback callback;
 DWORD observed=0;bool own_pending=false;bool pending=false;unsigned retries=0;std::optional<ErrorCode> last_error;
 std::mutex tasks_mutex;std::deque<std::function<void()>> tasks;
 explicit State(ClipboardOptions o):options(std::move(o)){}
 Capabilities capabilities()const{Capabilities c;c.platform=CapturePlatform::Windows;c.source_app={CapabilityStatus::Limited,"Clipboard owner executable is best effort, not proven authorship; inaccessible owner is Unknown"};for(const auto& t:types)c.native_types.push_back({"windows",t.name,t.kind,t.encoding});if(window){c.capture={CapabilityStatus::Available,"Win32 transferable formats; cooperative privacy hints only; delayed providers may block"};c.write={CapabilityStatus::Available,"Win32 replay, no auto-paste; virtual/OLE-only data unsupported"};}else c.capture.reason="Message window unavailable on this window station";return c;}
 Result<ClipboardSnapshot> read(){return guarded<ClipboardSnapshot>([&]()->Result<ClipboardSnapshot>{
  if(GetCurrentThreadId()!=thread)return wrong_thread();if(!window)return Error{ErrorCode::ReadOnly,"Clipboard message window unavailable"};if(!valid_limits(options.limits))return Error{ErrorCode::InvalidArgument,"Invalid clipboard limits"};
  for(unsigned attempt=0;attempt<3;++attempt){Open open;if(!OpenClipboard(window)){if(attempt<2)Sleep(4);continue;}open.opened=true;
   auto before=GetClipboardSequenceNumber();if(!before)return Error{ErrorCode::ReadOnly,"Clipboard sequence is inaccessible"};auto owner=GetClipboardOwner();
   ClipboardSnapshot out;out.sequence_token="windows:"+std::to_string(before);out.source=options.source_reader?options.source_reader():owner_source();if(out.source.app_id&&!out.source.app_id->empty())out.source.confidence=SourceConfidence::BestEffort;else out.source={};out.flags={Flag::No,Flag::Unknown,Flag::Unknown};
   struct Offer{UINT f;std::string name;const Type* type;};std::vector<Offer> offers;UINT previous=0;SetLastError(ERROR_SUCCESS);
   while(auto f=EnumClipboardFormats(previous)){if(offers.size()>=4096)return Error{ErrorCode::TooLarge,"Too many clipboard formats"};auto name=format_name(f);offers.push_back({f,name,lookup(name)});out.offered_types.push_back({"windows",name});previous=f;SetLastError(ERROR_SUCCESS);}if(GetLastError()!=ERROR_SUCCESS)return Error{ErrorCode::Io,"Clipboard enumeration failed"};
   auto offered=[&](const char* name){return std::any_of(offers.begin(),offers.end(),[&](const auto& o){return o.name==name;});};
   for(const auto& o:offers){if(o.name=="ExcludeClipboardContentFromMonitorProcessing"||o.name=="org.nspasteboard.TransientType")out.flags.transient=Flag::Yes;if(o.name=="org.nspasteboard.ConcealedType")out.flags.concealed=Flag::Yes;}
   if(out.flags.transient!=Flag::Yes)for(const auto& o:offers)if(o.name=="CanIncludeInClipboardHistory"){auto data=payload(o.f,32);DWORD permitted=0;if(data&&data.value().size()>=4)memcpy(&permitted,data.value().data(),4);if(!data||permitted!=1)out.flags.transient=Flag::Yes;}
   for(const auto& o:offers)if(o.type){if(o.name=="Preferred DropEffect"&&!offered("CF_HDROP"))continue;if((o.name=="CF_DIBV5"||o.name=="CF_DIB")&&offered("PNG"))continue;if(o.name=="CF_DIB"&&offered("CF_DIBV5"))continue;if(out.representations.size()>=options.limits.max_representations)return Error{ErrorCode::TooLarge,"Too many clipboard representations"};out.representations.push_back({0,"windows",o.name,o.type->kind,{},o.type->encoding});}
   auto decision=evaluate_capture(out,options.policy,capabilities());out.representations.clear();bool failed=false;Error failure{ErrorCode::Io,"Clipboard read failed"};std::uint64_t total=0;
   if(decision.snapshot)for(auto r:decision.snapshot->representations){auto f=std::find_if(offers.begin(),offers.end(),[&](const auto& o){return o.name==r.native_type;})->f;auto data=payload(f,std::min(options.limits.representation_bytes,options.limits.entry_bytes-total));if(!data){failed=true;failure=data.error();break;}r.data=std::move(data.value());total+=r.data.size();auto valid=validate_native_payload(r);if(!valid){failed=true;failure=valid.error();break;}out.representations.push_back(std::move(r));}
   if(before!=GetClipboardSequenceNumber()||owner!=GetClipboardOwner())continue;if(failed)return failure;return out;
  }return Error{ErrorCode::Busy,"Clipboard busy or changed during three read attempts"};
 });}
 void cleanup(std::uint64_t g){if(g!=generation)return;if(window){RemoveClipboardFormatListener(window);KillTimer(window,1);}pending=false;retries=0;callback={};}
 // Windows can commit its final sequence on CloseClipboard. Confirm it under
 // a second lock only while still our owner; never sample an unlocked rival.
 bool confirm_own(){Open open;if(!OpenClipboard(window))return false;open.opened=true;own_pending=false;if(GetClipboardOwner()!=window)return false;observed=GetClipboardSequenceNumber();return true;}
 void capture(){if(!active||!callback)return;if(own_pending&&confirm_own())return;auto current=GetClipboardSequenceNumber();if(current&&current==observed)return;auto result=read();if(!result&&result.error().code==ErrorCode::Busy&&retries++<3){SetTimer(window,1,20,nullptr);return;}retries=0;KillTimer(window,1);if(result){observed=static_cast<DWORD>(std::stoul(result.value().sequence_token.substr(8)));last_error.reset();}else{if(last_error==result.error().code)return;last_error=result.error().code;}auto notify=callback;if(active&&notify)notify(std::move(result));}
 static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
  auto holder=reinterpret_cast<std::shared_ptr<State>*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){holder=static_cast<std::shared_ptr<State>*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder));(*holder)->window=hwnd;}if(!holder)return DefWindowProcW(hwnd,message,w,l);auto self=*holder;
  try{if(message==WM_CLIPBOARDUPDATE){if(self->active&&!self->pending){self->pending=true;PostMessageW(hwnd,CaptureMessage,static_cast<WPARAM>(self->generation.load()),0);}return 0;}if(message==CaptureMessage){if(w==self->generation){self->pending=false;self->capture();}return 0;}if(message==WM_TIMER&&w==1){KillTimer(hwnd,1);self->capture();return 0;}if(message==StopMessage){self->cleanup(static_cast<std::uint64_t>(w));return 0;}if(message==TaskMessage){std::function<void()> task;{std::lock_guard<std::mutex> lock(self->tasks_mutex);if(!self->tasks.empty()){task=std::move(self->tasks.front());self->tasks.pop_front();}}if(task)task();return 0;}if(message==DestroyMessage){DestroyWindow(hwnd);return 0;}}catch(...){self->last_error=ErrorCode::Internal;}
  if(message==WM_NCDESTROY){self->active=false;self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete holder;}return DefWindowProcW(hwnd,message,w,l);
 }
};
WindowsClipboardBackend::WindowsClipboardBackend(ClipboardOptions o):state_(std::make_shared<State>(std::move(o))){WNDCLASSW wc{};wc.lpfnWndProc=State::procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"Cliptiq.Clipboard.v1";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;auto holder=new std::shared_ptr<State>(state_);if(!CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,holder)){if(!state_->window)delete holder;}}
WindowsClipboardBackend::~WindowsClipboardBackend(){stop();auto s=state_;if(s->window){if(GetCurrentThreadId()==s->thread)DestroyWindow(s->window);else PostMessageW(s->window,DestroyMessage,0,0);}}
Capabilities WindowsClipboardBackend::capabilities()const{return state_->capabilities();}
Result<ClipboardSnapshot> WindowsClipboardBackend::read_snapshot(){return state_->read();}
Result<void> WindowsClipboardBackend::set_capture_policy(CapturePolicy p){if(GetCurrentThreadId()!=state_->thread)return wrong_thread();if(evaluate_capture({},p,capabilities()).reason==CaptureReason::InvalidInput)return Error{ErrorCode::InvalidArgument,"Invalid capture policy"};state_->options.policy=std::move(p);return {};}
Result<void> WindowsClipboardBackend::start(CaptureCallback cb){auto s=state_;if(GetCurrentThreadId()!=s->thread)return wrong_thread();if(!cb||s->active)return Error{ErrorCode::InvalidArgument,"Invalid or active monitor"};if(s->window){RemoveClipboardFormatListener(s->window);KillTimer(s->window,1);}if(!s->window||!AddClipboardFormatListener(s->window))return Error{ErrorCode::ReadOnly,"Clipboard listener unavailable"};++s->generation;s->callback=std::move(cb);s->last_error.reset();s->active=true;s->pending=true;if(!PostMessageW(s->window,CaptureMessage,static_cast<WPARAM>(s->generation.load()),0)){s->active=false;s->cleanup(s->generation);return Error{ErrorCode::Io,"Clipboard dispatcher unavailable"};}return {};}
void WindowsClipboardBackend::stop(){auto s=state_;auto g=++s->generation;s->active=false;if(GetCurrentThreadId()==s->thread)s->cleanup(g);else if(s->window)PostMessageW(s->window,StopMessage,static_cast<WPARAM>(g),0);}
Result<void> WindowsClipboardBackend::dispatch_to_owner(std::function<void()> task){auto s=state_;std::lock_guard<std::mutex> lock(s->tasks_mutex);if(!task||!s->window)return Error{ErrorCode::InvalidArgument,"Invalid owner dispatch"};if(s->tasks.size()>=64)return Error{ErrorCode::Busy,"Owner dispatch queue full"};s->tasks.push_back(std::move(task));if(!PostMessageW(s->window,TaskMessage,0,0)){s->tasks.pop_back();return Error{ErrorCode::Io,"Owner dispatch failed"};}return {};}
Result<std::string> WindowsClipboardBackend::write_snapshot(const ClipboardSnapshot& snapshot){return guarded<std::string>([&]()->Result<std::string>{
 auto s=state_;if(GetCurrentThreadId()!=s->thread)return wrong_thread();if(!s->window||!valid_limits(s->options.limits)||snapshot.representations.empty())return Error{ErrorCode::InvalidArgument,"Invalid clipboard replay"};if(snapshot.representations.size()>s->options.limits.max_representations)return Error{ErrorCode::TooLarge,"Too many replay formats"};
 std::set<UINT> seen;std::vector<std::pair<UINT,Memory>> prepared;std::uint64_t total=0;bool files=false;
 auto prepare=[&](UINT f,const Bytes& bytes)->bool{if(!f)return false;Memory m;m.handle=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,std::max<std::size_t>(1,bytes.size()));if(!m.handle)return false;auto p=GlobalLock(m.handle);if(!p)return false;if(!bytes.empty())memcpy(p,bytes.data(),bytes.size());GlobalUnlock(m.handle);prepared.emplace_back(f,std::move(m));return true;};
 for(auto r:snapshot.representations){auto t=lookup(r.native_type);if(r.platform!="windows"||r.item_index||!t||r.kind!=t->kind||r.text_encoding!=t->encoding)return Error{ErrorCode::Unsupported,"Replay requires transferable Windows formats at item zero"};auto f=id(*t);if(!f||!seen.insert(f).second)return Error{ErrorCode::InvalidArgument,"Duplicate/unavailable replay format"};if(r.data.size()>s->options.limits.representation_bytes||r.data.size()>s->options.limits.entry_bytes-total)return Error{ErrorCode::TooLarge,"Replay exceeds configured limits"};total+=r.data.size();auto valid=validate_native_payload(r);if(!valid)return valid.error();if(r.native_type=="CF_HDROP")files=true;if(r.native_type!="Preferred DropEffect"&&!prepare(f,r.data))return Error{ErrorCode::Io,"Replay memory preparation failed"};}
 if(files&&!prepare(RegisterClipboardFormatW(L"Preferred DropEffect"),Bytes{1,0,0,0}))return Error{ErrorCode::Io,"Replay file effect preparation failed"};if(prepared.empty())return Error{ErrorCode::InvalidArgument,"No replay content"};
 // History replay must never silently remove a producer cloud prohibition.
 if(!prepare(RegisterClipboardFormatW(L"CanUploadToCloudClipboard"),Bytes{0,0,0,0}))return Error{ErrorCode::Io,"Replay cloud prohibition allocation failed"};
 if(static_cast<unsigned>(snapshot.flags.transient)>2||static_cast<unsigned>(snapshot.flags.concealed)>2||static_cast<unsigned>(snapshot.flags.remote)>2)return Error{ErrorCode::InvalidArgument,"Invalid replay flags"};
 if(snapshot.flags.transient==Flag::Yes&&!prepare(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"),Bytes{0}))return Error{ErrorCode::Io,"Replay marker allocation failed"};if(snapshot.flags.concealed==Flag::Yes&&!prepare(RegisterClipboardFormatW(L"org.nspasteboard.ConcealedType"),Bytes{0}))return Error{ErrorCode::Io,"Replay marker allocation failed"};
 Open open;for(unsigned attempt=0;attempt<3;++attempt){if(OpenClipboard(s->window)){open.opened=true;break;}if(attempt<2)Sleep(4);}if(!open.opened)return Error{ErrorCode::Busy,"Clipboard busy; replay did not clear it"};if(!EmptyClipboard())return Error{ErrorCode::Io,"Cannot clear clipboard for replay"};
 for(auto& pair:prepared){if(!SetClipboardData(pair.first,pair.second.handle))return Error{ErrorCode::Io,"Native clipboard rejected replay; clipboard may be partial"};pair.second.handle=nullptr;}
 // Sample while ownership is protected; after unlock another copy may win.
 const auto own_sequence=GetClipboardSequenceNumber();
 if(!CloseClipboard()){open.opened=false;return Error{ErrorCode::Io,"Replay close failed"};}open.opened=false;s->observed=own_sequence;s->own_pending=true;s->confirm_own();s->last_error.reset();return "windows:"+std::to_string(s->observed);
 });}
}

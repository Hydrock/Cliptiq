#import <AppKit/AppKit.h>
#include "Clipboard.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace cliptiq::macos {
namespace {
struct Type { const char* name; CanonicalKind kind; const char* modern; };
constexpr Type types[] = {
 {"public.utf8-plain-text",CanonicalKind::Text,nullptr},
 {"NSStringPboardType",CanonicalKind::Text,"public.utf8-plain-text"},
 {"public.rtf",CanonicalKind::Rtf,nullptr},
 {"NSRTFPboardType",CanonicalKind::Rtf,"public.rtf"},
 {"com.apple.flat-rtfd",CanonicalKind::Rtfd,nullptr},
 {"NSRTFDPboardType",CanonicalKind::Rtfd,"com.apple.flat-rtfd"},
 {"public.html",CanonicalKind::Html,nullptr},
 {"com.adobe.pdf",CanonicalKind::Pdf,nullptr},
 {"NSPDFPboardType",CanonicalKind::Pdf,"com.adobe.pdf"},
 {"public.file-url",CanonicalKind::Files,nullptr},
 {"NSFilenamesPboardType",CanonicalKind::Files,"public.file-url"},
 {"public.url",CanonicalKind::Url,nullptr},
 {"NSURLPboardType",CanonicalKind::Url,"public.url"},
 {"public.png",CanonicalKind::Image,nullptr},
 {"public.tiff",CanonicalKind::Image,nullptr},
 {"NSTIFFPboardType",CanonicalKind::Image,"public.tiff"}
};
NSString* native(const std::string& s) {
 return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
}
std::string utf8(NSString* s) {
 const char* p=s.UTF8String;return p?std::string(p):std::string();
}
const Type* lookup(const std::string& name) {
 for(const auto& t:types)if(name==t.name)return &t;
 return nullptr;
}
bool marker(NSString* type) {
 return [type isEqualToString:@"org.nspasteboard.TransientType"] ||
        [type isEqualToString:@"org.nspasteboard.ConcealedType"] ||
        [type isEqualToString:@"com.apple.is-remote-clipboard"];
}
bool covered(const Type& t,NSArray<NSPasteboardType>* offers) {
 if(t.modern&&[offers containsObject:native(t.modern)])return true;
 return t.kind==CanonicalKind::Image && std::string(t.name)!="public.png" && [offers containsObject:NSPasteboardTypePNG];
}
bool valid_limits(const Limits& l) {
 const Limits hard;
 return l.representation_bytes&&l.representation_bytes<=hard.representation_bytes &&
        l.entry_bytes&&l.entry_bytes<=hard.entry_bytes && l.history_bytes&&l.history_bytes<=hard.history_bytes &&
        l.max_representations&&l.max_representations<=hard.max_representations;
}
template<class T,class F>Result<T> guarded(F f) {
 try {@try {return f();} @catch(NSException*) {return Error{ErrorCode::Io,"Native pasteboard operation failed"};}}
 catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"Clipboard allocation failed"};}
 catch(const std::exception&){return Error{ErrorCode::Internal,"Clipboard operation failed"};}
}
Error thread_error(){return {ErrorCode::InvalidArgument,"macOS clipboard operations require the main thread"};}
struct Permission { bool allowed; Capability capability; };
Permission permission(NSPasteboard* pb,bool user_initiated) {
 if(!pb)return {false,{CapabilityStatus::Unavailable,"Pasteboard is unavailable"}};
 if(@available(macOS 15.4,*)) {
  switch(pb.accessBehavior) {
  case NSPasteboardAccessBehaviorAlwaysDeny:
   return {false,{CapabilityStatus::Unavailable,"Clipboard access denied in System Settings"}};
  case NSPasteboardAccessBehaviorAlwaysAllow:
   return {true,{CapabilityStatus::Available,"Pasteboard access allowed by the system"}};
  case NSPasteboardAccessBehaviorDefault:
   if(![pb.name isEqualToString:NSPasteboardNameGeneral])
    return {true,{CapabilityStatus::Available,"Private named pasteboard access"}};
   [[fallthrough]];
  case NSPasteboardAccessBehaviorAsk:
   return {user_initiated,{CapabilityStatus::Limited,"Clipboard access needs an explicit user permission action; background reads paused"}};
  default:return {false,{CapabilityStatus::Unavailable,"Unknown pasteboard permission state"}};
  }
 }
 return {true,{CapabilityStatus::Available,"Native pasteboard API; cooperative markers do not identify every secret"}};
}
SourceInfo foreground() {
 auto id=NSWorkspace.sharedWorkspace.frontmostApplication.bundleIdentifier;
 auto s=utf8(id);return s.empty()?SourceInfo{}:SourceInfo{s,SourceConfidence::BestEffort};
}
}

struct MacClipboardBackend::State {
 __strong NSPasteboard* board;
 __strong NSTimer* timer=nil;
 ClipboardOptions options;
 CaptureCallback callback;
 std::atomic<bool> active{false};
 NSInteger observed=0;
 NSInteger stable_read=0;
 std::optional<ErrorCode> last_error;
 explicit State(NSPasteboard* b,ClipboardOptions o):board(b),options(std::move(o)){}
 Capabilities capabilities()const {
  Capabilities c;c.platform=CapturePlatform::MacOS;
  c.source_app={CapabilityStatus::Limited,"NSWorkspace foreground bundle ID is best effort, not clipboard authorship"};
  for(const auto& t:types)c.native_types.push_back({"macos",t.name,t.kind,TextEncoding::Utf8});
  if(![NSThread isMainThread]){c.capture.reason="Pasteboard probing requires the main thread";return c;}
  @try {
   c.capture=permission(board,false).capability;
   c.write=board?Capability{CapabilityStatus::Available,"Native pasteboard replay; no auto-paste"}:Capability{};
  }@catch(NSException*){c.capture={CapabilityStatus::Unavailable,"Pasteboard access status unavailable"};}
  return c;
 }
 Result<ClipboardSnapshot> read(bool user_initiated) {
  return guarded<ClipboardSnapshot>([&]()->Result<ClipboardSnapshot>{
   if(![NSThread isMainThread])return thread_error();
   if(!valid_limits(options.limits))return Error{ErrorCode::InvalidArgument,"Invalid clipboard limits"};
   for(unsigned attempt=0;attempt<3;++attempt) {
    auto access=permission(board,user_initiated);
    if(!access.allowed)return Error{ErrorCode::ReadOnly,access.capability.reason};
    const NSInteger before=board.changeCount;
    NSArray<NSPasteboardItem*>* items=board.pasteboardItems ?: @[];
    NSArray<NSPasteboardType>* root=board.types ?: @[];
    if(items.count>options.limits.max_representations||root.count>4096)
     return Error{ErrorCode::TooLarge,"Too many pasteboard items/types"};
    NSMutableOrderedSet<NSPasteboardType>* all=[NSMutableOrderedSet orderedSetWithArray:root];
    for(NSPasteboardItem* item in items) {
     if(item.types.count>4096)return Error{ErrorCode::TooLarge,"Too many item types"};
     [all addObjectsFromArray:item.types];
    }
    if(all.count>4096)return Error{ErrorCode::TooLarge,"Too many advertised types"};
    ClipboardSnapshot out;
    out.sequence_token=utf8(board.name)+":"+std::to_string(before);
    out.source=options.source_reader?options.source_reader():foreground();
    if(out.source.app_id&&!out.source.app_id->empty())out.source.confidence=SourceConfidence::BestEffort;
    else out.source={};
    for(NSString* type in all)out.offered_types.push_back({"macos",utf8(type)});
    out.flags={ [all containsObject:@"org.nspasteboard.TransientType"]?Flag::Yes:Flag::No,
                [all containsObject:@"org.nspasteboard.ConcealedType"]?Flag::Yes:Flag::No,
                [all containsObject:@"com.apple.is-remote-clipboard"]?Flag::Yes:Flag::No };
    struct Candidate { Representation value; __strong NSPasteboardItem* item; __strong NSString* type; };
    std::vector<Candidate> candidates;
    NSMutableSet<NSPasteboardType>* item_types=[NSMutableSet set];
    for(NSUInteger index=0;index<items.count;++index) {
     NSPasteboardItem* item=items[index];
     for(NSString* type in item.types) {
      [item_types addObject:type];
      auto known=lookup(utf8(type));if(!known||marker(type)||covered(*known,item.types))continue;
      candidates.push_back({{static_cast<std::uint32_t>(index),"macos",known->name,known->kind,{}},item,type});
     }
    }
    for(NSString* type in root) {
     if([item_types containsObject:type])continue;
     auto known=lookup(utf8(type));if(!known||marker(type))continue;
     // A synthesized root public.file-url may describe only the first path.
     // Keep the legacy root array so its complete file list is not lost.
     if(std::string(known->name)!="NSFilenamesPboardType"&&covered(*known,root))continue;
     candidates.push_back({{RootPasteboardItem,"macos",known->name,known->kind,{}},nil,type});
    }
    if(candidates.size()>options.limits.max_representations)
     return Error{ErrorCode::TooLarge,"Too many clipboard representations"};
    for(const auto& candidate:candidates)out.representations.push_back(candidate.value);
    auto caps=capabilities();
    if(user_initiated)caps.capture=access.capability;
    const auto decision=evaluate_capture(out,options.policy,caps);
    out.representations.clear();
    if(decision.disposition==CaptureDisposition::Reject) {
     if(before!=board.changeCount)continue;
     stable_read=before;
     return out; // Metadata-only rejection: T03 re-evaluates without any payload.
    }
    std::uint64_t total=0;bool unavailable=false;
    for(const auto& candidate:candidates) {
     const auto selected=std::find_if(decision.snapshot->representations.begin(),decision.snapshot->representations.end(),[&](const auto& r){return r.item_index==candidate.value.item_index&&r.native_type==candidate.value.native_type;});
     if(selected==decision.snapshot->representations.end())continue;
     NSData* d=candidate.item?[candidate.item dataForType:candidate.type]:[board dataForType:candidate.type];
     if(!d){unavailable=true;break;}
     if(d.length>options.limits.representation_bytes||d.length>options.limits.entry_bytes-total)
      return Error{ErrorCode::TooLarge,"Clipboard payload exceeds configured limits"};
     total+=d.length;
     auto value=candidate.value;
     if(d.length){auto p=static_cast<const std::uint8_t*>(d.bytes);value.data.assign(p,p+d.length);}
     out.representations.push_back(std::move(value));
    }
    if(before!=board.changeCount)continue;
    if(unavailable) {
     auto current=permission(board,false);
     if(!current.allowed)return Error{ErrorCode::ReadOnly,current.capability.reason};
     return Error{ErrorCode::Io,"Declared clipboard representation could not be read"};
    }
    stable_read=before;return out;
   }
   return Error{ErrorCode::Busy,"Clipboard changed during each of three read attempts"};
  });
 }
 void poll() {
  @autoreleasepool {
   if(!active.load()||!callback)return;
   NSInteger count;
   @try{count=board.changeCount;}@catch(NSException*){return;}
   if(count==observed)return;
   auto result=read(false);
   if(result){observed=stable_read;last_error.reset();}
   else {if(last_error==result.error().code)return;last_error=result.error().code;}
   auto notify=callback; // Reentrant stop/destruction must not destroy a running function.
   if(active.load()&&notify)notify(std::move(result));
  }
 }
};

MacClipboardBackend::MacClipboardBackend(std::string name,ClipboardOptions options)
 :MacClipboardBackend(name.empty()?NSPasteboard.generalPasteboard:[NSPasteboard pasteboardWithName:native(name)],std::move(options)){}
MacClipboardBackend::MacClipboardBackend(NSPasteboard* board,ClipboardOptions options)
 :state_(std::make_shared<State>(board,std::move(options))){}
MacClipboardBackend::~MacClipboardBackend(){stop();}
Capabilities MacClipboardBackend::capabilities()const{return state_->capabilities();}
Result<ClipboardSnapshot> MacClipboardBackend::read_snapshot(){return state_->read(false);}
Result<ClipboardSnapshot> MacClipboardBackend::read_snapshot(bool user_initiated){return state_->read(user_initiated);}
Result<void> MacClipboardBackend::set_capture_policy(CapturePolicy p) {
 if(![NSThread isMainThread])return thread_error();
 auto decision=evaluate_capture({},p,capabilities());
 if(decision.reason==CaptureReason::InvalidInput)return Error{ErrorCode::InvalidArgument,"Invalid capture policy"};
 state_->options.policy=std::move(p);return {};
}
Result<std::string> MacClipboardBackend::write_snapshot(const ClipboardSnapshot& snapshot) {
 return guarded<std::string>([&]()->Result<std::string>{
  if(![NSThread isMainThread])return thread_error();
  auto& state=*state_;auto board=state.board;const auto& limits=state.options.limits;
  if(!board||!valid_limits(limits)||snapshot.representations.empty())return Error{ErrorCode::InvalidArgument,"Invalid clipboard replay"};
  if(snapshot.representations.size()>limits.max_representations)return Error{ErrorCode::TooLarge,"Too many replay representations"};
  if(static_cast<unsigned>(snapshot.flags.transient)>2||static_cast<unsigned>(snapshot.flags.concealed)>2||static_cast<unsigned>(snapshot.flags.remote)>2)
   return Error{ErrorCode::InvalidArgument,"Invalid replay flags"};
  std::uint64_t total=0;
  std::map<std::uint32_t,NSPasteboardItem*> groups;
  NSMutableArray<NSPasteboardItem*>* objects=[NSMutableArray array];
  std::vector<const Representation*> roots;
  const Representation* legacy_files=nullptr;
  std::set<std::pair<std::uint32_t,std::string>> seen;
  for(const auto& r:snapshot.representations) {
   const auto known=lookup(r.native_type);
   if(r.platform!="macos"||!known||known->kind!=r.kind||r.text_encoding!=TextEncoding::Utf8||!seen.emplace(r.item_index,r.native_type).second)
    return Error{ErrorCode::Unsupported,"Replay requires known, non-duplicate native macOS representations"};
   if(r.data.size()>limits.representation_bytes||r.data.size()>limits.entry_bytes-total)
    return Error{ErrorCode::TooLarge,"Replay exceeds configured limits"};
   total+=r.data.size();
   if(r.native_type=="NSFilenamesPboardType") {
    if(legacy_files)return Error{ErrorCode::InvalidArgument,"Multiple legacy root file arrays cannot be replayed"};
    legacy_files=&r;continue;
   }
   if(r.item_index==RootPasteboardItem){roots.push_back(&r);continue;}
   if(!groups.count(r.item_index)){auto item=[NSPasteboardItem new];groups.emplace(r.item_index,item);[objects addObject:item];}
   if(![groups.at(r.item_index) setData:[NSData dataWithBytes:r.data.data() length:r.data.size()] forType:native(r.native_type)])
    return Error{ErrorCode::Io,"Unable to prepare native replay data"};
  }
  if(objects.count==0&&!roots.empty())[objects addObject:[NSPasteboardItem new]];
  for(const auto* r:roots) {
   auto first=objects.firstObject;auto type=native(r->native_type);
   if([first.types containsObject:type])return Error{ErrorCode::InvalidArgument,"Conflicting root/item replay type"};
   if(![first setData:[NSData dataWithBytes:r->data.data() length:r->data.size()] forType:type])return Error{ErrorCode::Io,"Unable to prepare root replay data"};
  }
  if(objects.count==0&&(snapshot.flags.transient==Flag::Yes||snapshot.flags.concealed==Flag::Yes||snapshot.flags.remote==Flag::Yes))
   [objects addObject:[NSPasteboardItem new]];
  auto first=objects.firstObject;
  for(const auto& flag:std::vector<std::pair<Flag,NSString*>>{{snapshot.flags.transient,@"org.nspasteboard.TransientType"},{snapshot.flags.concealed,@"org.nspasteboard.ConcealedType"},{snapshot.flags.remote,@"com.apple.is-remote-clipboard"}})
   if(flag.first==Flag::Yes&&![first setData:[NSData data] forType:flag.second])return Error{ErrorCode::Io,"Unable to prepare replay metadata"};
  const NSInteger owned=[board clearContents];
  // NSPasteboardItem rejects NSFilenamesPboardType. It must stay on the root,
  // as in the original Clipy compatibility path, before writing modern items.
  if(legacy_files&&![board setData:[NSData dataWithBytes:legacy_files->data.data() length:legacy_files->data.size()] forType:@"NSFilenamesPboardType"])
   return Error{ErrorCode::Io,"Native pasteboard rejected legacy root filenames"};
  if(objects.count&&![board writeObjects:objects])return Error{ErrorCode::Io,"Native pasteboard rejected the replay"};
  const NSInteger final=board.changeCount;
  if(final!=owned)return Error{ErrorCode::Busy,"Clipboard ownership changed during replay"};
  state.observed=final;state.last_error.reset();
  return utf8(board.name)+":"+std::to_string(final);
 });
}
Result<void> MacClipboardBackend::start(CaptureCallback callback) {
 return guarded<void>([&]()->Result<void>{
  if(![NSThread isMainThread])return thread_error();
  if(!callback||state_->active.load())return Error{ErrorCode::InvalidArgument,"Invalid or already running clipboard monitor"};
  if(!state_->board)return Error{ErrorCode::Io,"Pasteboard unavailable"};
  state_->observed=state_->board.changeCount;state_->callback=std::move(callback);state_->active=true;
  std::weak_ptr<State> weak=state_;
  state_->timer=[NSTimer timerWithTimeInterval:0.5 repeats:YES block:^(NSTimer*){auto state=weak.lock();if(state)state->poll();}];
  [[NSRunLoop mainRunLoop] addTimer:state_->timer forMode:NSRunLoopCommonModes];
  return {};
 });
}
void MacClipboardBackend::stop() {
 auto state=state_;state->active=false;
 if([NSThread isMainThread]){[state->timer invalidate];state->timer=nil;state->callback={};}
 else dispatch_async(dispatch_get_main_queue(),^{[state->timer invalidate];state->timer=nil;state->callback={};});
}
}

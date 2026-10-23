#import <AppKit/AppKit.h>
#include "Application.hpp"
#include "cliptiq/history.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
using namespace cliptiq::macos;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); }while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
void ok(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
Bytes bytes(NSData* d){auto p=static_cast<const std::uint8_t*>(d.bytes);return d.length?Bytes(p,p+d.length):Bytes{};}
NSData* data(const Bytes& b){return [NSData dataWithBytes:b.data() length:b.size()];}
Representation text(std::string v="hello",std::uint32_t item=0){return {item,"macos","public.utf8-plain-text",CanonicalKind::Text,Bytes(v.begin(),v.end())};}
struct Board{NSPasteboard* pb=[NSPasteboard pasteboardWithUniqueName];~Board(){[pb releaseGlobally];}};
void put(NSPasteboard* p,NSArray<NSPasteboardItem*>* items){[p clearContents];CHECK([p writeObjects:items]);}
NSPasteboardItem* item(NSString* type,NSData* d){auto i=[NSPasteboardItem new];CHECK([i setData:d forType:type]);return i;}
const Representation* find(const ClipboardSnapshot& s,const std::string& type,std::uint32_t index=0){for(const auto& r:s.representations)if(r.native_type==type&&r.item_index==index)return &r;return nullptr;}
void pump(double seconds){NSDate* until=[NSDate dateWithTimeIntervalSinceNow:seconds];while([until timeIntervalSinceNow]>0)[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];}
template<class F>bool wait(F f,double seconds=2.5){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(static_cast<int>(seconds*1000));while(!f()&&std::chrono::steady_clock::now()<end)pump(0.01);return f();}
NSData* image(bool png){auto rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:2 pixelsHigh:2 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:8 bitsPerPixel:32];memset(rep.bitmapData,127,16);return png?[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}]:rep.TIFFRepresentation;}
// Fault/root-only provider over a real named pasteboard. Not a claim that
// permission dialogs or changing external apps have been exercised.
@interface BoardProvider : NSObject
@property(strong) NSPasteboard* backing;
@property BOOL rootOnly;
@property BOOL moving;
@property NSInteger tick;
@property NSUInteger reads;
@property NSInteger permission;
@end
@implementation BoardProvider
- (instancetype)init{if((self=[super init]))_permission=-1;return self;}
- (NSPasteboardName)name{return _backing.name;}
- (NSArray<NSPasteboardType>*)types{return _backing.types;}
- (NSArray<NSPasteboardItem*>*)pasteboardItems{return _rootOnly?nil:_backing.pasteboardItems;}
- (NSInteger)changeCount{return _moving?++_tick:_backing.changeCount;}
- (NSData*)dataForType:(NSPasteboardType)type{++_reads;return [_backing dataForType:type];}
- (NSPasteboardAccessBehavior)accessBehavior API_AVAILABLE(macos(15.4)){return _permission>=0?static_cast<NSPasteboardAccessBehavior>(_permission):_backing.accessBehavior;}
@end
struct Sink:CaptureSink{
 Store db=take(open_store(":memory:"));int stores=0,thumbs=0,ocrs=0;
 Result<std::optional<EntryId>>store(const ClipboardSnapshot& s)override{++stores;return ingest(db,s,{},1);}
 void thumbnail(const EntryId&,const ClipboardSnapshot&)override{++thumbs;}
 void ocr(const EntryId&,const ClipboardSnapshot&)override{++ocrs;}
};
int main(){@autoreleasepool{
 [NSApplication sharedApplication];int failures=0;
 auto test=[&](const char* name,std::function<void()> f){try{@try{f();std::cout<<"PASS "<<name<<'\n';}@catch(NSException*){++failures;std::cerr<<"FAIL "<<name<<": native exception\n";}}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
 test("multiple_items_and_native_metadata",[]{Board b;auto first=item(NSPasteboardTypeString,[ @"first" dataUsingEncoding:NSUTF8StringEncoding]);auto second=item(NSPasteboardTypeString,[@"second" dataUsingEncoding:NSUTF8StringEncoding]);CHECK([first setData:[NSData data] forType:@"org.nspasteboard.ConcealedType"]);put(b.pb,@[first,second]);MacClipboardBackend backend(b.pb);auto s=take(backend.read_snapshot());CHECK(find(s,"public.utf8-plain-text",0));CHECK(find(s,"public.utf8-plain-text",1));CHECK(s.flags.concealed==Flag::Yes);CHECK(!s.offered_types.empty());});
 test("png_preference_preserves_other_item_tiff",[]{Board b;auto i=item(NSPasteboardTypePNG,image(true));CHECK([i setData:image(false) forType:NSPasteboardTypeTIFF]);put(b.pb,@[i,item(NSPasteboardTypeTIFF,image(false))]);MacClipboardBackend backend(b.pb);auto s=take(backend.read_snapshot());CHECK(find(s,"public.png",0));CHECK(!find(s,"public.tiff",0));CHECK(find(s,"public.tiff",1));});
 test("root_filenames_tiff_fallback",[]{Board b;auto files=[NSPropertyListSerialization dataWithPropertyList:@[@"/tmp/first.txt",@"/tmp/second.txt"] format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];[b.pb declareTypes:@[@"NSFilenamesPboardType",NSPasteboardTypeTIFF] owner:nil];CHECK([b.pb setData:files forType:@"NSFilenamesPboardType"]);CHECK([b.pb setData:image(false) forType:NSPasteboardTypeTIFF]);auto provider=[BoardProvider new];provider.backing=b.pb;provider.rootOnly=YES;MacClipboardBackend backend((NSPasteboard*)provider);auto s=take(backend.read_snapshot());auto f=find(s,"NSFilenamesPboardType",RootPasteboardItem);CHECK(f&&f->data==bytes(files));CHECK(find(s,"public.tiff",RootPasteboardItem));});
 test("stable_token_when_foreground_changes",[]{Board b;put(b.pb,@[item(NSPasteboardTypeString,[@"hello" dataUsingEncoding:NSUTF8StringEncoding])]);auto source=std::make_shared<std::string>("org.first");ClipboardOptions options;options.source_reader=[source]{return SourceInfo{*source,SourceConfidence::Known};};MacClipboardBackend backend(b.pb,options);auto a=take(backend.read_snapshot());*source="org.second";auto c=take(backend.read_snapshot());CHECK(a.sequence_token==c.sequence_token);CHECK(a.source.app_id!=c.source.app_id);CHECK(c.source.confidence==SourceConfidence::BestEffort);CHECK(backend.capabilities().source_app.status==CapabilityStatus::Limited);});
 test("change_during_read_bounded_retry",[]{Board b;put(b.pb,@[item(NSPasteboardTypeString,[@"hello" dataUsingEncoding:NSUTF8StringEncoding])]);auto provider=[BoardProvider new];provider.backing=b.pb;provider.moving=YES;MacClipboardBackend backend((NSPasteboard*)provider);auto r=backend.read_snapshot();CHECK(!r&&r.error().code==ErrorCode::Busy);CHECK(provider.tick<=10);});
 test("permission_denied_no_payload_read",[]{if(@available(macOS 15.4,*)){Board b;auto provider=[BoardProvider new];provider.backing=b.pb;provider.permission=NSPasteboardAccessBehaviorAlwaysDeny;MacClipboardBackend backend((NSPasteboard*)provider);CHECK(backend.capabilities().capture.status==CapabilityStatus::Unavailable);auto r=backend.read_snapshot();CHECK(!r&&r.error().code==ErrorCode::ReadOnly);CHECK(provider.reads==0);}});
 test("privacy_transient_before_payload",[]{Board b;auto i=item(NSPasteboardTypeString,[@"sensitive" dataUsingEncoding:NSUTF8StringEncoding]);[i setData:[NSData data] forType:@"org.nspasteboard.TransientType"];put(b.pb,@[i]);auto provider=[BoardProvider new];provider.backing=b.pb;provider.rootOnly=YES;MacClipboardBackend backend((NSPasteboard*)provider);auto s=take(backend.read_snapshot());Sink sink;auto d=take(dispatch_capture(s,{},backend.capabilities(),sink));CHECK(d.reason==CaptureReason::Transient);CHECK(provider.reads==0);CHECK(sink.stores==0&&sink.thumbs==0&&sink.ocrs==0);});
 test("unknown_source_exclusions_and_marker_prefix",[]{Board b;auto i=item(NSPasteboardTypeString,[@"sensitive" dataUsingEncoding:NSUTF8StringEncoding]);[i setData:[NSData data] forType:@"org.password.manager"];put(b.pb,@[i]);ClipboardOptions options;options.source_reader=[]{return SourceInfo{};};options.policy.excluded_apps={"org.password.manager.extension"};MacClipboardBackend backend(b.pb,options);auto s=take(backend.read_snapshot());Sink sink;auto d=take(dispatch_capture(s,options.policy,backend.capabilities(),sink));CHECK(d.reason==CaptureReason::ExcludedApplication);CHECK(sink.stores==0);});
 test("html_disabled_and_native_roundtrip",[]{Board b;ClipboardOptions options;options.policy.enabled_kinds[3]=true;MacClipboardBackend backend(b.pb,options);ClipboardSnapshot s;s.representations={text("hello"),{0,"macos","public.rtf",CanonicalKind::Rtf,{'{','\\','r','t','f','1',' ','h','i','}'}},{0,"macos","public.html",CanonicalKind::Html,{'<','b','>','x','<','/','b','>'}}};auto token=take(backend.write_snapshot(s));auto r=take(backend.read_snapshot());CHECK(r.sequence_token==token);CHECK(find(r,"public.rtf")->data==s.representations[1].data);CHECK(find(r,"public.html"));ok(backend.set_capture_policy({}));r=take(backend.read_snapshot());CHECK(!find(r,"public.html"));CHECK(find(r,"public.rtf"));});
 test("replay_files_flags_and_item_boundaries",[]{Board b;MacClipboardBackend backend(b.pb);ClipboardSnapshot s;s.representations={{0,"macos","public.file-url",CanonicalKind::Files,bytes([@"file:///tmp/first.txt" dataUsingEncoding:NSUTF8StringEncoding])},{3,"macos","public.file-url",CanonicalKind::Files,bytes([@"file:///tmp/second.txt" dataUsingEncoding:NSUTF8StringEncoding])}};s.flags.concealed=Flag::Yes;take(backend.write_snapshot(s));auto items=b.pb.pasteboardItems;CHECK(items.count==2);CHECK([[items[0] stringForType:NSPasteboardTypeFileURL] isEqualToString:@"file:///tmp/first.txt"]);CHECK([[items[1] stringForType:NSPasteboardTypeFileURL] isEqualToString:@"file:///tmp/second.txt"]);auto r=take(backend.read_snapshot());CHECK(r.flags.concealed==Flag::Yes);CHECK(r.representations.size()>=2);});
 test("oversize_read_and_invalid_write_are_atomic",[]{Board b;put(b.pb,@[item(NSPasteboardTypeString,[@"keep" dataUsingEncoding:NSUTF8StringEncoding])]);ClipboardOptions options;options.limits.representation_bytes=3;MacClipboardBackend backend(b.pb,options);auto r=backend.read_snapshot();CHECK(!r&&r.error().code==ErrorCode::TooLarge);auto count=b.pb.changeCount;ClipboardSnapshot s;s.representations={text("large")};CHECK(!backend.write_snapshot(s));CHECK(b.pb.changeCount==count);CHECK([[b.pb stringForType:NSPasteboardTypeString] isEqualToString:@"keep"]);s.representations={text("ok")};s.representations[0].native_type="unknown.native";CHECK(!backend.write_snapshot(s));CHECK(b.pb.changeCount==count);});
 test("poll_500ms_main_thread_own_write_suppression",[]{Board b;MacClipboardBackend backend(b.pb);int calls=0;bool main=true;ok(backend.start([&](Result<ClipboardSnapshot> r){if(r){++calls;main=main&&[NSThread isMainThread];}}));ClipboardSnapshot s;s.representations={text("own")};take(backend.write_snapshot(s));pump(0.65);CHECK(calls==0);put(b.pb,@[item(NSPasteboardTypeString,[@"external" dataUsingEncoding:NSUTF8StringEncoding])]);CHECK(wait([&]{return calls==1;}));CHECK(main);backend.stop();put(b.pb,@[item(NSPasteboardTypeString,[@"stopped" dataUsingEncoding:NSUTF8StringEncoding])]);pump(0.6);CHECK(calls==1);});
 test("stop_and_destroy_inside_callback",[]{Board b;auto backend=std::make_unique<MacClipboardBackend>(b.pb);int calls=0;ok(backend->start([&](Result<ClipboardSnapshot>){++calls;backend.reset();}));put(b.pb,@[item(NSPasteboardTypeString,[@"external" dataUsingEncoding:NSUTF8StringEncoding])]);CHECK(wait([&]{return calls==1;}));pump(0.6);CHECK(calls==1);});
 test("session_privacy_and_worker_persistence",[]{Board b;NSString* path=[NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID].UUIDString stringByAppendingString:@".db"]];{MacCaptureSession session(path.UTF8String,b.pb.name.UTF8String);bool done=false,main=false;ok(session.start({}, {},[&](Result<CaptureEvent> r){done=bool(r)&&r.value().entry_id.has_value();main=[NSThread isMainThread];}));put(b.pb,@[item(NSPasteboardTypeString,[@"worker" dataUsingEncoding:NSUTF8StringEncoding])]);CHECK(wait([&]{return done;}));CHECK(main);session.stop();}auto s=take(open_store(path.UTF8String));CHECK(take(list_history(s)).size()==1);[[NSFileManager defaultManager] removeItemAtPath:path error:nil];});
 test("session_rejection_creates_no_database",[]{Board b;NSString* path=[NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID].UUIDString stringByAppendingString:@".db"]];MacCaptureSession session(path.UTF8String,b.pb.name.UTF8String);bool rejected=false;CapturePolicy policy;policy.ignore_concealed=true;ok(session.start(policy,{},[&](Result<CaptureEvent> r){rejected=bool(r)&&r.value().decision.reason==CaptureReason::Concealed;}));auto i=item(NSPasteboardTypeString,[@"secret" dataUsingEncoding:NSUTF8StringEncoding]);[i setData:[NSData data] forType:@"org.nspasteboard.ConcealedType"];put(b.pb,@[i]);CHECK(wait([&]{return rejected;}));session.stop();CHECK(![[NSFileManager defaultManager] fileExistsAtPath:path]);});
 return failures?1:0;
}}

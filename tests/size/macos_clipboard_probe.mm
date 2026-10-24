#import <AppKit/AppKit.h>
#include "cliptiq/history.hpp"
#include "cliptiq/policy.hpp"
#ifdef CLIPTIQ_WITH_MACOS_BACKEND
#include "Application.hpp"
#endif
using namespace cliptiq;
namespace {
class Sink:public CaptureSink {
 Store& db_;
public:
 explicit Sink(Store& db):db_(db){}
 Result<std::optional<EntryId>>store(const ClipboardSnapshot& s)override{return ingest(db_,s,{},1);}
 void thumbnail(const EntryId&,const ClipboardSnapshot&)override{}
 void ocr(const EntryId&,const ClipboardSnapshot&)override{}
};
}
int main(){@autoreleasepool{
 [NSApplication sharedApplication];
 NSPasteboard* pb=[NSPasteboard pasteboardWithUniqueName];
 auto opened=open_store(":memory:");if(!opened)return 1;auto& db=opened.value();Sink sink(db);
 ClipboardSnapshot snapshot;snapshot.representations.push_back({0,"macos","public.utf8-plain-text",CanonicalKind::Text,{'h','i'}});
 Capabilities caps;
#ifdef CLIPTIQ_WITH_MACOS_BACKEND
 macos::MacClipboardBackend backend(pb);
 if(!backend.write_snapshot(snapshot))return 2;
 auto read=backend.read_snapshot();if(!read)return 3;snapshot=std::move(read.value());caps=backend.capabilities();
 macos::MacCaptureSession session(":memory:",pb.name.UTF8String);
 bool received=false;
 if(!session.start({}, {},[&](Result<macos::CaptureEvent> e){received=bool(e)&&e.value().entry_id.has_value();}))return 4;
#else
 caps.platform=CapturePlatform::MacOS;caps.capture={CapabilityStatus::Available,"fixture"};
 caps.native_types.push_back({"macos","public.utf8-plain-text",CanonicalKind::Text});
#endif
 auto item=[NSPasteboardItem new];[item setString:@"hi" forType:NSPasteboardTypeString];
 [pb clearContents];if(![pb writeObjects:@[item]])return 5;
#ifdef CLIPTIQ_WITH_MACOS_BACKEND
 NSDate* end=[NSDate dateWithTimeIntervalSinceNow:3];
 while(!received&&end.timeIntervalSinceNow>0)[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
 session.stop();if(!received)return 6;
#else
 NSData* raw=[pb dataForType:NSPasteboardTypeString];if(!raw)return 7;
 const auto* bytes=static_cast<const std::uint8_t*>(raw.bytes);snapshot.representations[0].data.assign(bytes,bytes+raw.length);
#endif
 auto decision=dispatch_capture(snapshot,{},caps,sink);if(!decision||decision.value().disposition!=CaptureDisposition::Accept)return 8;
 if(capture_reason_message(decision.value().reason).empty())return 9;
 auto rows=list_history(db);if(!rows||rows.value().empty())return 10;
 const auto id=rows.value()[0].id;
 if(!load_snapshot(db,id)||!touch_history(db,id,2)||!data_revision(db))return 11;
 if(!tick_cleanup(db,{300000},0)||!erase_history(db)||!compact_store(db))return 12;
 [pb releaseGlobally];return 0;
}}

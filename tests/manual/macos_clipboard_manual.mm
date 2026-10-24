// Explicit opt-in developer helper. Never run by CTest or the footprint probes.
#import <AppKit/AppKit.h>
#include "Application.hpp"
#include <iostream>
using namespace cliptiq;
using namespace cliptiq::macos;
int main(int argc,char** argv){@autoreleasepool{
 if(argc<2){std::cout<<"read | echo | watch [seconds 1..300] [excluded-bundle-id] | write text|rich|image|files\n";return 0;}
 [NSApplication sharedApplication];
 const std::string action=argv[1];
 CapturePolicy policy;policy.enabled_kinds[static_cast<unsigned>(CanonicalKind::Html)]=true;
 ClipboardOptions options;options.policy=policy;
 MacClipboardBackend backend(NSPasteboard.generalPasteboard,options);
 if(action=="read"||action=="echo"){
  auto read=backend.read_snapshot(true);
  if(!read){std::cerr<<read.error().message<<'\n';return 1;}
  auto decision=evaluate_capture(read.value(),policy,backend.capabilities());
  std::cout<<capture_reason_message(decision.reason)<<'\n';
  if(!decision.snapshot)return 2;
  for(const auto& r:decision.snapshot->representations)std::cout<<r.item_index<<' '<<r.native_type<<" bytes="<<r.data.size()<<'\n';
  std::cout<<"Source attribution: best effort/unknown; foreground is not clipboard authorship.\n";
  if(action=="echo"){auto written=backend.write_snapshot(*decision.snapshot);if(!written){std::cerr<<written.error().message<<'\n';return 3;}std::cout<<"Replay complete\n";}
  return 0;
 }
 if(action=="watch"){
  int seconds=20;try{if(argc>2)seconds=std::stoi(argv[2]);}catch(...){return 4;}
  if(seconds<1||seconds>300)return 4;
  if(argc>3)policy.excluded_apps={argv[3]};
  MacCaptureSession session(":memory:");
  auto started=session.start(policy,{},[](Result<CaptureEvent> event){if(!event)std::cout<<"Error: "<<event.error().message<<'\n';else std::cout<<capture_reason_message(event.value().decision.reason)<<" retained="<<event.value().entry_id.has_value()<<'\n';});
  if(!started){std::cerr<<started.error().message<<'\n';return 5;}
  std::cout<<"Watching General pasteboard with RAM-only history. No clipboard content is printed.\n";
  NSDate* end=[NSDate dateWithTimeIntervalSinceNow:seconds];
  while(end.timeIntervalSinceNow>0)[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  session.stop();return 0;
 }
 if(action!="write"||argc!=3)return 6;
 ClipboardSnapshot snapshot;
 auto add=[&](std::uint32_t index,const char* type,CanonicalKind kind,NSData* d){auto p=static_cast<const std::uint8_t*>(d.bytes);snapshot.representations.push_back({index,"macos",type,kind,d.length?Bytes(p,p+d.length):Bytes{}});};
 const std::string kind=argv[2];
 if(kind=="text"||kind=="rich")add(0,"public.utf8-plain-text",CanonicalKind::Text,[@"Cliptiq fixture" dataUsingEncoding:NSUTF8StringEncoding]);
 if(kind=="rich"){
  auto a=[[NSAttributedString alloc] initWithString:@"Cliptiq fixture" attributes:@{NSFontAttributeName:[NSFont boldSystemFontOfSize:16]}];
  add(0,"public.rtf",CanonicalKind::Rtf,[a RTFFromRange:NSMakeRange(0,a.length) documentAttributes:@{}]);
  add(0,"public.html",CanonicalKind::Html,[@"<b>Cliptiq fixture</b>" dataUsingEncoding:NSUTF8StringEncoding]);
 }else if(kind=="image"){
  auto image=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:2 pixelsHigh:2 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:8 bitsPerPixel:32];
  memset(image.bitmapData,127,16);add(0,"public.png",CanonicalKind::Image,[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]);
 }else if(kind=="files"){
  NSString* folder=[NSTemporaryDirectory() stringByAppendingPathComponent:[@"CliptiqManual-" stringByAppendingString:NSUUID.UUID.UUIDString]];
  if(![[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:nil])return 7;
  for(unsigned i=0;i<2;++i){NSString* path=[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"fixture-%u.txt",i]];if(![[@"Cliptiq fixture" dataUsingEncoding:NSUTF8StringEncoding] writeToFile:path atomically:YES])return 7;auto url=[NSURL fileURLWithPath:path];add(i,"public.file-url",CanonicalKind::Files,[url.absoluteString dataUsingEncoding:NSUTF8StringEncoding]);}
  std::cout<<"Generated fixture directory (remove after test): "<<folder.UTF8String<<'\n';
 }else if(kind!="text"&&kind!="rich")return 6;
 auto written=backend.write_snapshot(snapshot);if(!written){std::cerr<<written.error().message<<'\n';return 8;}
 std::cout<<"General clipboard replaced with generated fixture\n";return 0;
}}

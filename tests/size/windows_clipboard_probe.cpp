#include "../fixtures/windows/IsolatedClipboard.hpp"
#include "cliptiq/history.hpp"
#include "cliptiq/policy.hpp"
#ifdef CLIPTIQ_WITH_WINDOWS_BACKEND
#include "Application.hpp"
#endif
using namespace cliptiq;
struct Sink:CaptureSink{Store& db;explicit Sink(Store& s):db(s){}Result<std::optional<EntryId>>store(const ClipboardSnapshot& v)override{return ingest(db,v,{},1);}void thumbnail(const EntryId&,const ClipboardSnapshot&)override{}void ocr(const EntryId&,const ClipboardSnapshot&)override{}};
int main(){try{IsolatedClipboard isolation;
 auto window=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return 1;
 auto opened=open_store(":memory:");if(!opened)return 2;auto& db=opened.value();Sink sink(db);
 ClipboardSnapshot snapshot;snapshot.representations={{0,"windows","CF_UNICODETEXT",CanonicalKind::Text,{'h',0,'i',0,0,0},TextEncoding::Utf16Le}};
 Capabilities caps;
#ifdef CLIPTIQ_WITH_WINDOWS_BACKEND
 windows::WindowsClipboardBackend backend;if(!backend.write_snapshot(snapshot))return 3;auto read=backend.read_snapshot();if(!read)return 4;snapshot=std::move(read.value());caps=backend.capabilities();
 windows::WindowsCaptureSession session(":memory:");bool received=false;
 if(!session.start({}, {},[&](Result<windows::CaptureEvent> event){received=bool(event)&&event.value().entry_id.has_value();}))return 5;
#else
 caps.platform=CapturePlatform::Windows;caps.capture={CapabilityStatus::Available,"fixture"};caps.native_types.push_back({"windows","CF_UNICODETEXT",CanonicalKind::Text,TextEncoding::Utf16Le});
#endif
 if(!OpenClipboard(window)||!EmptyClipboard())return 6;auto memory=GlobalAlloc(GMEM_MOVEABLE,6);if(!memory)return 7;auto bytes=GlobalLock(memory);if(!bytes)return 8;memcpy(bytes,snapshot.representations[0].data.data(),6);GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))return 9;CloseClipboard();
#ifdef CLIPTIQ_WITH_WINDOWS_BACKEND
 auto end=GetTickCount64()+3000;while(!received&&GetTickCount64()<end){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}session.stop();if(!received)return 10;
#else
 if(!OpenClipboard(window))return 11;auto memoryRead=GetClipboardData(CF_UNICODETEXT);auto data=static_cast<const std::uint8_t*>(GlobalLock(memoryRead));if(!data)return 12;snapshot.representations[0].data.assign(data,data+6);GlobalUnlock(memoryRead);CloseClipboard();
#endif
 auto decision=dispatch_capture(snapshot,{},caps,sink);if(!decision||decision.value().disposition!=CaptureDisposition::Accept)return 13;auto rows=list_history(db);if(!rows||rows.value().empty())return 14;auto id=rows.value()[0].id;
 if(!load_snapshot(db,id)||!touch_history(db,id,2)||!data_revision(db)||!tick_cleanup(db,{300000},0)||!erase_history(db)||!compact_store(db))return 15;
 DestroyWindow(window);return 0;
 }catch(...){return 20;}}

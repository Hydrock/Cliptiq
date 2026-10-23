#import <AppKit/AppKit.h>
#include "Application.hpp"
#include "cliptiq/history.hpp"
#include <atomic>
#include <chrono>

namespace cliptiq::macos {
namespace {
struct Worker {
 dispatch_queue_t queue=dispatch_queue_create("app.cliptiq.history",DISPATCH_QUEUE_SERIAL);
 std::string path;
 Limits limits;
 std::optional<Store> store;
 std::atomic<std::uint64_t> generation{0};
 CaptureQueueLimits queue_limits;
 std::atomic<std::size_t> pending_count{0};
 std::atomic<std::uint64_t> pending_bytes{0};
 Worker(std::string p,Limits l,CaptureQueueLimits q):path(std::move(p)),limits(l),queue_limits(q){}
};
std::uint64_t retained_bytes(const ClipboardSnapshot& v) {
 auto bytes=std::uint64_t(sizeof(v)+v.sequence_token.capacity());
 if(v.source.app_id)bytes+=v.source.app_id->capacity();
 bytes+=v.representations.capacity()*sizeof(Representation)+v.offered_types.capacity()*sizeof(ClipboardSnapshot::OfferedType);
 for(const auto& r:v.representations)bytes+=r.data.capacity()+r.platform.capacity()+r.native_type.capacity();
 for(const auto& t:v.offered_types)bytes+=t.platform.capacity()+t.native_type.capacity();
 return bytes; // Conservative capacity estimate; not an OS/process RSS limit.
}
struct PendingCapture {
 std::shared_ptr<Worker> worker;
 std::uint64_t bytes;
 ~PendingCapture(){worker->pending_bytes.fetch_sub(bytes);worker->pending_count.fetch_sub(1);}
};
class HistorySink final:public CaptureSink {
 Worker& worker_;
 HistoryPolicy policy_;
public:
 std::optional<EntryId> retained;
 HistorySink(Worker& w,HistoryPolicy p):worker_(w),policy_(p){}
 Result<std::optional<EntryId>> store(const ClipboardSnapshot& v)override {
  if(!worker_.store){auto opened=open_store(worker_.path,worker_.limits);if(!opened)return opened.error();worker_.store.emplace(std::move(opened.value()));}
  auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto result=ingest(*worker_.store,v,policy_,now);
  if(result)retained=result.value();return result;
 }
 void thumbnail(const EntryId&,const ClipboardSnapshot&)override{} // T12 not linked.
 void ocr(const EntryId&,const ClipboardSnapshot&)override{} // T16 not linked.
};
}
struct MacCaptureSession::Impl {
 MacClipboardBackend backend;
 std::shared_ptr<Worker> worker;
 Impl(std::string path,std::string board,Limits limits,CaptureQueueLimits queue):backend(std::move(board),ClipboardOptions{limits,{},{}}),worker(std::make_shared<Worker>(std::move(path),limits,queue)){}
 bool running=false;
};
MacCaptureSession::MacCaptureSession(std::string path,std::string board,Limits limits,CaptureQueueLimits queue):impl_(new Impl(std::move(path),std::move(board),limits,queue)){}
MacCaptureSession::~MacCaptureSession(){stop();}
MacClipboardBackend& MacCaptureSession::clipboard(){return impl_->backend;}
Result<void> MacCaptureSession::start(CapturePolicy policy,HistoryPolicy history,CaptureEventCallback callback) {
 if(![NSThread isMainThread]||!callback||impl_->running)return Error{ErrorCode::InvalidArgument,"Capture session must start once on the main thread"};
 auto configured=impl_->backend.set_capture_policy(policy);if(!configured)return configured;
 auto worker=impl_->worker;
 if(!worker->queue_limits.max_pending_count||worker->queue_limits.max_pending_count>16||!worker->queue_limits.max_pending_bytes||worker->queue_limits.max_pending_bytes>256ull*1024*1024)
  return Error{ErrorCode::InvalidArgument,"Invalid capture queue limits"};
 const auto generation=++worker->generation;
 std::weak_ptr<Worker> weak=worker;
 auto started=impl_->backend.start([this,weak,generation,policy,history,callback](Result<ClipboardSnapshot> captured){
  auto worker=weak.lock();if(!worker||worker->generation!=generation)return;
  auto caps=impl_->backend.capabilities();
  if(!captured){callback(captured.error());return;}
  if(captured.value().representations.empty()) {
   auto decision=evaluate_capture(captured.value(),policy,caps);
   callback(CaptureEvent{{decision.disposition,decision.reason},std::nullopt});return;
  }
  const auto retained=retained_bytes(captured.value());
  // Single main-thread producer; consumers only decrement. Conservative
  // loads can reject early during a release but cannot over-admit snapshots.
  if(worker->pending_count.load()>=worker->queue_limits.max_pending_count||retained>worker->queue_limits.max_pending_bytes||worker->pending_bytes.load()>worker->queue_limits.max_pending_bytes-retained) {
   callback(Error{ErrorCode::Busy,"Capture queue capacity exceeded; snapshot skipped"});return;
  }
  worker->pending_count.fetch_add(1);worker->pending_bytes.fetch_add(retained);
  auto input=std::make_shared<Result<ClipboardSnapshot>>(std::move(captured));
  dispatch_async(worker->queue,^{@autoreleasepool{
   PendingCapture reservation{worker,retained};
   if(worker->generation!=generation)return;
   auto result=std::make_shared<Result<CaptureEvent>>([&]()->Result<CaptureEvent>{
    if(!*input)return input->error();
    HistorySink sink(*worker,history);
    auto decision=dispatch_capture(input->value(),policy,caps,sink);
    if(!decision)return decision.error();
    // Report a lightweight summary, not an incomplete Accept decision or
    // retained clipboard bytes on the UI queue.
    return CaptureEvent{{decision.value().disposition,decision.value().reason},sink.retained};
   }());
   dispatch_async(dispatch_get_main_queue(),^{if(worker->generation==generation)callback(std::move(*result));});
  }});
 });
 if(!started)return started;
 impl_->running=true;return {};
}
void MacCaptureSession::stop() {
 if(!impl_)return;
 impl_->backend.stop();auto worker=impl_->worker;++worker->generation;
 // Only this serial queue opens/writes/closes Store. Drain in-flight work before
 // returning; no worker ever synchronously dispatches to the main queue.
 dispatch_sync(worker->queue,^{worker->store.reset();});
 impl_->running=false;
}
}

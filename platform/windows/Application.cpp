#include "Application.hpp"
#include "cliptiq/history.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
namespace cliptiq::windows {
namespace {
struct Job{ClipboardSnapshot snapshot;CapturePolicy policy;HistoryPolicy history;Capabilities caps;CaptureEventCallback callback;std::uint64_t generation,bytes;};
struct Worker{
 std::string path;Limits limits;CaptureQueueLimits queue_limits;
 std::mutex mutex;std::condition_variable changed;std::deque<Job> jobs;
 std::size_t pending_count=0;std::uint64_t pending_bytes=0;
 std::atomic<std::uint64_t> generation{0};bool stopping=false;std::thread thread;
 Worker(std::string p,Limits l,CaptureQueueLimits q):path(std::move(p)),limits(l),queue_limits(q){}
};
std::uint64_t retained(const ClipboardSnapshot& v){auto size=std::uint64_t(sizeof(v)+v.sequence_token.capacity());if(v.source.app_id)size+=v.source.app_id->capacity();size+=v.representations.capacity()*sizeof(Representation)+v.offered_types.capacity()*sizeof(ClipboardSnapshot::OfferedType);for(const auto& r:v.representations)size+=r.data.capacity()+r.native_type.capacity()+r.platform.capacity();for(const auto& t:v.offered_types)size+=t.platform.capacity()+t.native_type.capacity();return size;}
struct Sink:CaptureSink{
 Worker& worker;std::optional<Store>& db;const Job& job;std::optional<EntryId> retained_id;
 Sink(Worker& w,std::optional<Store>& s,const Job& j):worker(w),db(s),job(j){}
 Result<std::optional<EntryId>>store(const ClipboardSnapshot& snapshot)override{
  // Only the filtered snapshot reaches WIC. No decode on the UI thread.
  for(const auto& r:snapshot.representations)if(r.kind==CanonicalKind::Image){auto decoded=decode_image(r);if(!decoded)return decoded.error();}
  if(worker.generation!=job.generation)return Error{ErrorCode::Busy,"Capture canceled before persistence"};
  if(!db){auto opened=open_store(worker.path,worker.limits);if(!opened)return opened.error();db.emplace(std::move(opened.value()));}
  const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();auto result=ingest(*db,snapshot,job.history,now);if(result)retained_id=result.value();return result;
 }
 void thumbnail(const EntryId&,const ClipboardSnapshot&)override{} // T12, not implemented here.
 void ocr(const EntryId&,const ClipboardSnapshot&)override{} // T16, capability unavailable.
};
}
struct WindowsCaptureSession::Impl{
 WindowsClipboardBackend backend;std::shared_ptr<Worker> worker;bool running=false;
 Impl(std::string path,ClipboardOptions options,CaptureQueueLimits limits):backend(options),worker(std::make_shared<Worker>(std::move(path),options.limits,limits)){}
};
WindowsCaptureSession::WindowsCaptureSession(std::string path,ClipboardOptions options,CaptureQueueLimits queue):impl_(new Impl(std::move(path),std::move(options),queue)){}
WindowsCaptureSession::~WindowsCaptureSession(){stop();}
WindowsClipboardBackend& WindowsCaptureSession::clipboard(){return impl_->backend;}
Result<void> WindowsCaptureSession::start(CapturePolicy policy,HistoryPolicy history,CaptureEventCallback callback){
 if(!callback||impl_->running)return Error{ErrorCode::InvalidArgument,"Invalid or active capture session"};auto configured=impl_->backend.set_capture_policy(policy);if(!configured)return configured;
 auto worker=impl_->worker;auto& q=worker->queue_limits;
 if(!q.max_pending_count||q.max_pending_count>16||!q.max_pending_bytes||q.max_pending_bytes>256ull*1024*1024)return Error{ErrorCode::InvalidArgument,"Invalid capture queue limits"};
 auto generation=++worker->generation;worker->stopping=false;
 auto backend=&impl_->backend;
 try{worker->thread=std::thread([worker,backend]{
  std::optional<Store> db; // Open, use and destroy only on this thread.
  for(;;){std::optional<Job> job;{std::unique_lock<std::mutex> lock(worker->mutex);worker->changed.wait(lock,[&]{return worker->stopping||!worker->jobs.empty();});if(worker->stopping)break;job.emplace(std::move(worker->jobs.front()));worker->jobs.pop_front();}
   if(worker->generation==job->generation){
    auto result=[&]()->Result<CaptureEvent>{try{Sink sink(*worker,db,*job);auto decision=dispatch_capture(job->snapshot,job->policy,job->caps,sink);if(!decision)return decision.error();return CaptureEvent{decision.value().disposition,decision.value().reason,sink.retained_id};}catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"Capture worker allocation failed"};}catch(const std::exception&){return Error{ErrorCode::Internal,"Capture worker failed"};}}();
    std::weak_ptr<Worker> weak=worker;auto callback=job->callback;auto generation=job->generation;
    // A stopped/destroyed session invalidates every posted completion.
    backend->dispatch_to_owner([weak,generation,callback,result=std::move(result)]()mutable{auto state=weak.lock();if(state&&state->generation==generation)callback(std::move(result));});
   }
   {std::lock_guard<std::mutex> lock(worker->mutex);--worker->pending_count;worker->pending_bytes-=job->bytes;}
  }
 });}catch(const std::exception&){return Error{ErrorCode::Io,"History worker could not start"};}
 std::weak_ptr<Worker> weak=worker;
 auto started=impl_->backend.start([this,weak,generation,policy,history,callback](Result<ClipboardSnapshot> captured){
  auto worker=weak.lock();if(!worker||worker->generation!=generation)return;
  if(!captured){callback(captured.error());return;}auto caps=impl_->backend.capabilities();
  auto decision=evaluate_capture(captured.value(),policy,caps);
  if(decision.disposition==CaptureDisposition::Reject){callback(CaptureEvent{decision.disposition,decision.reason,std::nullopt});return;}
  auto bytes=retained(captured.value());bool full=false;
  {std::lock_guard<std::mutex> lock(worker->mutex);auto q=worker->queue_limits;full=worker->pending_count>=q.max_pending_count||bytes>q.max_pending_bytes||worker->pending_bytes>q.max_pending_bytes-bytes;
   if(!full){worker->jobs.push_back({std::move(captured.value()),policy,history,caps,callback,generation,bytes});++worker->pending_count;worker->pending_bytes+=bytes;}}
  if(full){callback(Error{ErrorCode::Busy,"Capture queue capacity exceeded; snapshot skipped"});return;}worker->changed.notify_one();
 });
 if(!started){stop();return started;}impl_->running=true;return {};
}
void WindowsCaptureSession::stop(){
 if(!impl_)return;impl_->backend.stop();auto worker=impl_->worker;++worker->generation;
 {std::lock_guard<std::mutex> lock(worker->mutex);worker->stopping=true;for(const auto& job:worker->jobs){--worker->pending_count;worker->pending_bytes-=job.bytes;}worker->jobs.clear();}
 worker->changed.notify_all();if(worker->thread.joinable())worker->thread.join();impl_->running=false;
}
}

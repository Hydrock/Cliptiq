#include "Application.hpp"
#include "cliptiq/history.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
namespace cliptiq::linux_backend {
std::unique_ptr<LinuxClipboardBackend> open_backend(ClipboardOptions options) {
  const char *session = getenv("XDG_SESSION_TYPE");
  const bool wayland = (session && std::string(session) == "wayland") ||
                       getenv("WAYLAND_DISPLAY");
  if (wayland)
    return std::make_unique<ClipboardWayland>(std::move(options));
  return std::make_unique<ClipboardX11>(std::move(options));
}
namespace {
struct Configuration {
  CapturePolicy policy;
  HistoryPolicy history;
  Capabilities caps;
  CaptureEventCallback callback;
};
struct Job {
  ClipboardSnapshot snapshot;
  std::shared_ptr<const Configuration> config;
  std::uint64_t generation, bytes;
};
struct Completion {
  Result<CaptureEvent> result;
  std::shared_ptr<const Configuration> config;
  std::uint64_t generation;
};
struct Worker {
  std::string path;
  Limits limits;
  CaptureQueueLimits queue_limits;
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Job> jobs;
  std::deque<Completion> completions;
  std::thread thread;
  bool stopping = false;
  std::size_t pending_count = 0;
  std::uint64_t pending_bytes = 0;
  std::atomic<std::uint64_t> generation{0};
  Worker(std::string p, Limits l, CaptureQueueLimits q)
      : path(std::move(p)), limits(l), queue_limits(q) {}
};
std::uint64_t retained(const ClipboardSnapshot &s) {
  std::uint64_t n =
      sizeof(s) + s.sequence_token.capacity() +
      s.representations.capacity() * sizeof(Representation) +
      s.offered_types.capacity() * sizeof(ClipboardSnapshot::OfferedType);
  if (s.source.app_id)
    n += s.source.app_id->capacity();
  for (const auto &r : s.representations)
    n += r.data.capacity() + r.platform.capacity() + r.native_type.capacity();
  for (const auto &t : s.offered_types)
    n += t.platform.capacity() + t.native_type.capacity();
  return n;
}
struct Sink : CaptureSink {
  Worker &worker;
  const Job &job;
  std::optional<Store> &store_;
  std::optional<EntryId> retained_id;
  Sink(Worker &w, const Job &j, std::optional<Store> &s)
      : worker(w), job(j), store_(s) {}
  Result<std::optional<EntryId>>
  store(const ClipboardSnapshot &snapshot) override {
    if (worker.generation != job.generation)
      return Error{ErrorCode::Busy, "Capture canceled before persistence"};
    if (!store_) {
      auto opened = open_store(worker.path, worker.limits);
      if (!opened)
        return opened.error();
      store_.emplace(std::move(opened.value()));
    }
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    auto result = ingest(*store_, snapshot, job.config->history, now);
    if (result)
      retained_id = result.value();
    return result;
  }
  void thumbnail(const EntryId &, const ClipboardSnapshot &) override {
  } // T12 not implemented.
  void ocr(const EntryId &, const ClipboardSnapshot &) override {
  } // T16 not implemented.
};
} // namespace
struct LinuxCaptureSession::Impl {
  std::unique_ptr<LinuxClipboardBackend> backend;
  std::shared_ptr<Worker> worker;
  std::thread::id owner = std::this_thread::get_id();
  bool running = false;
  Impl(std::unique_ptr<LinuxClipboardBackend> b, std::string path, Limits l,
       CaptureQueueLimits q)
      : backend(std::move(b)),
        worker(std::make_shared<Worker>(std::move(path), l, q)) {}
};
LinuxCaptureSession::LinuxCaptureSession(
    std::unique_ptr<LinuxClipboardBackend> b, std::string path, Limits limits,
    CaptureQueueLimits queue)
    : impl_(new Impl(std::move(b), std::move(path), limits, queue)) {}
LinuxCaptureSession::~LinuxCaptureSession() { stop(); }
LinuxClipboardBackend &LinuxCaptureSession::clipboard() {
  return *impl_->backend;
}
Result<void> LinuxCaptureSession::start(CapturePolicy policy,
                                        HistoryPolicy history,
                                        CaptureEventCallback callback) {
  if (std::this_thread::get_id() != impl_->owner || !impl_->backend ||
      !callback || impl_->running)
    return Error{ErrorCode::InvalidArgument,
                 "Invalid Linux capture session start"};
  auto configured = impl_->backend->set_capture_policy(policy);
  if (!configured)
    return configured;
  auto worker = impl_->worker;
  auto q = worker->queue_limits;
  if (!q.max_pending_count || q.max_pending_count > 16 ||
      !q.max_pending_bytes || q.max_pending_bytes > 256ull * 1024 * 1024)
    return Error{ErrorCode::InvalidArgument, "Invalid capture queue limits"};
  auto config = std::make_shared<const Configuration>(
      Configuration{policy, history, impl_->backend->capabilities(), callback});
  auto generation = ++worker->generation;
  worker->stopping = false;
  try {
    worker->thread = std::thread([worker] {
      std::optional<Store> store;
      for (;;) {
        std::optional<Job> job;
        {
          std::unique_lock<std::mutex> lock(worker->mutex);
          worker->changed.wait(
              lock, [&] { return worker->stopping || !worker->jobs.empty(); });
          if (worker->stopping)
            break;
          job.emplace(std::move(worker->jobs.front()));
          worker->jobs.pop_front();
        }
        if (worker->generation == job->generation) {
          auto result = [&]() -> Result<CaptureEvent> {
            try {
              Sink sink(*worker, *job, store);
              auto decision = dispatch_capture(
                  job->snapshot, job->config->policy, job->config->caps, sink);
              if (!decision)
                return decision.error();
              return CaptureEvent{decision.value().disposition,
                                  decision.value().reason, sink.retained_id};
            } catch (const std::bad_alloc &) {
              return Error{ErrorCode::TooLarge,
                           "Capture worker allocation failed"};
            } catch (const std::exception &) {
              return Error{ErrorCode::Internal, "Capture worker failed"};
            }
          }();
          std::lock_guard<std::mutex> lock(worker->mutex);
          worker->completions.push_back(
              {std::move(result), job->config, job->generation});
        }
        {
          std::lock_guard<std::mutex> lock(worker->mutex);
          --worker->pending_count;
          worker->pending_bytes -= job->bytes;
        }
      }
    });
  } catch (const std::exception &) {
    return Error{ErrorCode::Io, "Cannot start history worker"};
  }
  std::weak_ptr<Worker> weak = worker;
  auto started = impl_->backend->start(
      [weak, generation, config](Result<ClipboardSnapshot> captured) {
        auto worker = weak.lock();
        if (!worker || worker->generation != generation)
          return;
        if (!captured) {
          config->callback(captured.error());
          return;
        }
        auto decision =
            evaluate_capture(captured.value(), config->policy, config->caps);
        if (decision.disposition == CaptureDisposition::Reject) {
          config->callback(CaptureEvent{decision.disposition, decision.reason,
                                        std::nullopt});
          return;
        }
        auto bytes = retained(captured.value());
        bool full = false;
        {
          std::lock_guard<std::mutex> lock(worker->mutex);
          const auto &q = worker->queue_limits;
          full = worker->pending_count >= q.max_pending_count ||
                 bytes > q.max_pending_bytes ||
                 worker->pending_bytes > q.max_pending_bytes - bytes ||
                 worker->completions.size() >= 64;
          if (!full) {
            worker->jobs.push_back(
                {std::move(captured.value()), config, generation, bytes});
            ++worker->pending_count;
            worker->pending_bytes += bytes;
          }
        }
        if (full) {
          config->callback(
              Error{ErrorCode::Busy,
                    "Capture queue capacity exceeded; snapshot skipped"});
          return;
        }
        worker->changed.notify_one();
      });
  if (!started) {
    stop();
    return started;
  }
  impl_->running = true;
  return {};
}
Result<void> LinuxCaptureSession::dispatch(unsigned timeout) {
  if (std::this_thread::get_id() != impl_->owner)
    return Error{ErrorCode::InvalidArgument,
                 "Session dispatch requires owner thread"};
  auto worker = impl_->worker;
  auto result = impl_->backend->dispatch(std::min(timeout, 10u));
  std::deque<Completion> completions;
  {
    std::lock_guard<std::mutex> lock(worker->mutex);
    completions.swap(worker->completions);
  }
  for (auto &item : completions)
    if (worker->generation == item.generation)
      item.config->callback(std::move(item.result));
  return result;
}
void LinuxCaptureSession::stop() {
  if (!impl_)
    return;
  if (impl_->backend)
    impl_->backend->stop();
  auto worker = impl_->worker;
  ++worker->generation;
  {
    std::lock_guard<std::mutex> lock(worker->mutex);
    worker->stopping = true;
    for (const auto &job : worker->jobs) {
      --worker->pending_count;
      worker->pending_bytes -= job.bytes;
    }
    worker->jobs.clear();
    worker->completions.clear();
  }
  worker->changed.notify_all();
  if (worker->thread.joinable())
    worker->thread.join();
  impl_->running = false;
}
} // namespace cliptiq::linux_backend

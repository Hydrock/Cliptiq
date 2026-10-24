#include "Media.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace cliptiq {
namespace {
using Clock = std::chrono::steady_clock;
struct Directory {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~Directory() {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }
#else
  int handle = -1;
  ~Directory() {
    if (handle >= 0)
      close(handle);
  }
#endif
};
Result<std::shared_ptr<Directory>> open_directory(const std::string &path) {
  auto dir = std::make_shared<Directory>();
#ifdef _WIN32
  dir->handle = CreateFileW(
      std::filesystem::u8path(path).c_str(),
      FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  BY_HANDLE_FILE_INFORMATION info{};
  if (dir->handle == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(dir->handle, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    return Error{ErrorCode::Io, "Cannot pin screenshot directory"};
#else
  dir->handle =
      open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (dir->handle < 0)
    return Error{ErrorCode::Io, "Cannot read screenshot directory"};
#endif
  return dir;
}
Result<Bytes> read_file(const std::shared_ptr<Directory> &root,
                        const std::string &name) {
  Bytes bytes;
#ifdef _WIN32
  wchar_t root_path[32768];
  DWORD count = GetFinalPathNameByHandleW(root->handle, root_path, 32768,
                                          FILE_NAME_NORMALIZED);
  if (!count || count >= 32768)
    return Error{ErrorCode::Io, "Pinned screenshot directory unavailable"};
  auto path = std::filesystem::path(std::wstring(root_path, count)) /
              std::filesystem::u8path(name);
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return Error{ErrorCode::Busy, "Screenshot not ready"};
  BY_HANDLE_FILE_INFORMATION a{}, b{};
  bool parent_matches = false;
  wchar_t file_path[32768];
  count =
      GetFinalPathNameByHandleW(file, file_path, 32768, FILE_NAME_NORMALIZED);
  if (count && count < 32768) {
    auto parent =
        open_directory(std::filesystem::path(std::wstring(file_path, count))
                           .parent_path()
                           .u8string());
    if (parent) {
      BY_HANDLE_FILE_INFORMATION expected{}, actual{};
      parent_matches =
          GetFileInformationByHandle(root->handle, &expected) &&
          GetFileInformationByHandle(parent.value()->handle, &actual) &&
          expected.dwVolumeSerialNumber == actual.dwVolumeSerialNumber &&
          expected.nFileIndexHigh == actual.nFileIndexHigh &&
          expected.nFileIndexLow == actual.nFileIndexLow;
    }
  }
  bool success = GetFileInformationByHandle(file, &a) && parent_matches &&
                 !(a.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                 !a.nFileSizeHigh && a.nFileSizeLow &&
                 a.nFileSizeLow <= 32 * 1024 * 1024;
  if (success) {
    bytes.resize(a.nFileSizeLow);
    DWORD got = 0;
    success =
        ReadFile(file, bytes.data(), DWORD(bytes.size()), &got, nullptr) &&
        got == bytes.size() && GetFileInformationByHandle(file, &b) &&
        a.nFileSizeHigh == b.nFileSizeHigh &&
        a.nFileSizeLow == b.nFileSizeLow &&
        CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
  }
  CloseHandle(file);
#else
  int file = openat(root->handle, name.c_str(),
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (file < 0)
    return Error{ErrorCode::Busy, "Screenshot not ready"};
  struct stat a {
  }, b{};
  bool success = fstat(file, &a) == 0 && S_ISREG(a.st_mode) && a.st_size > 0 &&
                 a.st_size <= 32 * 1024 * 1024;
  if (success) {
    bytes.resize(size_t(a.st_size));
    size_t got = 0;
    while (got < bytes.size()) {
      auto n = read(file, bytes.data() + got, bytes.size() - got);
      if (n <= 0)
        break;
      got += size_t(n);
    }
    success =
        got == bytes.size() && fstat(file, &b) == 0 && a.st_size == b.st_size;
#ifdef __APPLE__
    success = success && a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
              a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec;
#else
    success = success && a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
              a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
#endif
  }
  close(file);
#endif
  if (!success)
    return Error{ErrorCode::Busy, "Screenshot changed or exceeds limit"};
  return bytes;
}
std::string version(const Bytes &b) { // Encoded fingerprint only for event
                                      // coalescing, never pixel dedup.
  // Store ingest performs cryptographic content dedup; this bounded event cache
  // keeps exact bytes for the most recent file rather than a collision-prone
  // hash.
  return std::to_string(b.size());
}
} // namespace
struct MediaService::Impl {
  Store &store;
  CapturePlatform platform;
  ScreenshotWatcher watcher;
  struct Job {
    std::uint64_t epoch;
    PreviewTicket ticket;
    Representation rep;
    std::string file;
    std::shared_ptr<Directory> directory;
    std::uint64_t erased = 0;
  };
  struct Done {
    std::uint64_t epoch;
    PreviewTicket ticket;
    std::optional<MenuImage> image;
    std::string fingerprint, file;
    Bytes bytes;
    std::uint64_t erased = 0;
  };
  std::mutex mutex;
  std::condition_variable wake;
  bool quit = false;
  std::uint64_t epoch = 0;
  std::deque<Job> jobs;
  std::deque<Done> done;
  size_t queued = 0;
  std::thread worker;
  CapturePolicy policy;
  HistoryPolicy history;
  PreviewSize size;
  bool previews = true, isolated = true;
  std::atomic<bool> observe{false};
  std::atomic<std::uint64_t> erased{0};
  std::shared_ptr<Directory> directory;
  bool error_reported = false;
  std::string root, watch_error;
  Clock::time_point scanned{};
  std::map<EntryId, MenuImage> ready;
  std::map<EntryId, std::string> attempted;
  std::set<std::string> pending_files;
  std::string last_file;
  Bytes last_bytes;
  explicit Impl(Store &s, CapturePlatform p)
      : store(s), platform(p), worker([this] { run(); }) {}
  void run() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [&] { return quit || !jobs.empty(); });
        if (quit)
          return;
        job = std::move(jobs.front());
        queued -= job.rep.data.size();
        jobs.pop_front();
      }
      Done result;
      result.epoch = job.epoch;
      result.ticket = job.ticket;
      result.file = job.file;
      result.erased = job.erased;
      if (!job.file.empty()) {
        auto deadline = Clock::now() + std::chrono::seconds(10);
        Bytes previous;
        while (Clock::now() < deadline) {
          auto bytes = read_file(job.directory, job.file);
          if (bytes && bytes.value() == previous &&
              validate_screenshot_png(bytes.value())) {
            auto snapshot =
                screenshot_snapshot(platform, bytes.value(), job.file);
            auto decoded = decode_native_image(snapshot.representations[0]);
            if (decoded) {
              auto hash = image_fingerprint(decoded.value());
              auto scaled = fit_preview(decoded.value(), job.ticket.size);
              if (hash && scaled) {
                result.bytes = std::move(bytes.value());
                result.fingerprint = std::move(hash.value());
                result.image = std::move(scaled.value());
              }
            }
            break;
          }
          if (bytes)
            previous = std::move(bytes.value());
          std::unique_lock<std::mutex> lock(mutex);
          if (wake.wait_for(lock, std::chrono::milliseconds(250),
                            [&] { return quit || epoch != job.epoch; }))
            break;
        }
      } else {
        auto decoded = decode_native_image(job.rep);
        if (decoded) {
          auto hash = image_fingerprint(decoded.value());
          auto scaled = fit_preview(decoded.value(), job.ticket.size);
          if (hash && scaled) {
            result.fingerprint = std::move(hash.value());
            result.image = std::move(scaled.value());
          }
        }
      }
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,
                  [&] { return quit || epoch != job.epoch || done.empty(); });
        if (quit)
          return;
        if (epoch == job.epoch)
          done.push_back(std::move(result));
      }
    }
  }
};
Capability MediaService::observation_status() const {
  auto &s = *impl_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.watch_error.empty())
    return {CapabilityStatus::Unavailable, s.watch_error};
  return {s.observe ? CapabilityStatus::Available : CapabilityStatus::Limited,
          s.observe ? "Screenshot directory observation active; source "
                      "application unknown"
                    : "Screenshot observation disabled"};
}
MediaService::MediaService(Store &s, CapturePlatform p)
    : impl_(new Impl(s, p)) {}
MediaService::~MediaService() {
  stop();
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->quit = true;
  }
  impl_->wake.notify_all();
  impl_->worker.join();
}
void MediaService::stop() {
  auto &s = *impl_;
  {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.observe = false;
  }
  s.watcher.stop();
  {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.watch_error.clear();
    s.error_reported = false;
    ++s.epoch;
    s.jobs.clear();
    s.done.clear();
    s.queued = 0;
    s.pending_files.clear();
    s.observe = false;
  }
  s.wake.notify_all();
  s.ready.clear();
  s.attempted.clear();
  s.last_bytes.clear();
  s.last_file.clear();
  s.directory.reset();
}
Result<void> MediaService::configure(const Settings &settings, bool isolated) {
  stop();
  auto &s = *impl_;
  auto generation = history_erase_generation(s.store);
  if (!generation)
    return generation.error();
  s.erased = generation.value();
  auto failure = [&](Error error) -> Result<void> {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.watch_error = error.message;
    return error;
  };
  auto validated = validate_settings(settings);
  if (!validated)
    return failure(validated.error());
  s.policy = settings_capture(settings);
  s.history = settings_history(settings);
  s.size = {unsigned(std::stoul(settings.values.at("preview_width"))),
            unsigned(std::stoul(settings.values.at("preview_height")))};
  s.previews = settings.values.at("image_previews") == "1";
  s.isolated = isolated;
  if (isolated || settings.values.at("screenshots") != "1")
    return {};
  auto root = settings.values.at("screenshot_directory");
  std::error_code ec;
  auto path = std::filesystem::u8path(root);
  if (root.empty() || root.size() > 4096 || !path.is_absolute() ||
      std::filesystem::is_symlink(path, ec) ||
      !std::filesystem::is_directory(path, ec))
    return failure(Error{ErrorCode::InvalidArgument,
                         "Select an accessible absolute screenshot directory"});
  s.root = std::filesystem::canonical(path, ec).u8string();
  if (ec)
    return failure(Error{ErrorCode::Io, "Cannot resolve screenshot directory"});
  auto directory = open_directory(s.root);
  if (!directory) {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.watch_error = directory.error().message;
    return directory.error();
  }
  s.directory = directory.value();
  {
    std::lock_guard<std::mutex> lock(s.mutex);
    s.observe = true;
  }
  auto watched = s.watcher.start(
      s.root, [this](std::string name) { on_screenshot_file(name); });
  if (!watched) {
    stop();
    {
      std::lock_guard<std::mutex> lock(s.mutex);
      s.watch_error = watched.error().message;
    }
    return watched;
  }
  return {};
}
Result<void> MediaService::on_screenshot_file(const std::string &name) {
  auto &s = *impl_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.observe || s.isolated)
    return {};
  if (name.empty()) {
    s.watch_error = "Screenshot directory monitor lost events; disable and "
                    "enable observation to restart";
    s.observe = false;
    return {};
  }
  if (name.empty() || name.size() > 4096 ||
      name.find_first_of("/\\:") != std::string::npos ||
      name.find('\0') != std::string::npos ||
      std::filesystem::u8path(name).extension() != ".png")
    return Error{ErrorCode::InvalidArgument,
                 "Only PNG basenames inside selected directory"};
  if (evaluate_capture(screenshot_snapshot(s.platform, {1}, "preflight"),
                       s.policy, screenshot_capabilities(s.platform))
          .disposition != CaptureDisposition::Accept)
    return {};
  if (s.pending_files.count(name))
    return {};
  if (s.jobs.size() >= 64)
    return Error{ErrorCode::Busy, "Media queue full"};
  s.pending_files.insert(name);
  PreviewTicket file_ticket;
  file_ticket.size = s.size;
  s.jobs.push_back(
      {s.epoch, file_ticket, {}, name, s.directory, s.erased.load()});
  s.wake.notify_all();
  return {};
}
Result<bool> MediaService::poll() {
  auto &s = *impl_;
  auto generation = history_erase_generation(s.store);
  if (!generation)
    return generation.error();
  s.erased = generation.value();
  bool changed = false;
  std::optional<Impl::Done> result;
  {
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.watch_error.empty() && !s.error_reported) {
      s.error_reported = true;
      return Error{ErrorCode::Io, s.watch_error};
    }
    if (!s.done.empty()) {
      result = std::move(s.done.front());
      s.done.pop_front();
    }
  }
  s.wake.notify_all();
  if (result) {
    auto &r = *result;
    std::uint64_t epoch;
    {
      std::lock_guard<std::mutex> lock(s.mutex);
      epoch = s.epoch;
      s.pending_files.erase(r.file);
    }
    if (r.epoch == epoch) {
      if (!r.file.empty() && !r.bytes.empty() && s.observe &&
          r.erased == s.erased.load() &&
          !(s.last_file == r.file && s.last_bytes == r.bytes)) {
        struct Sink : CaptureSink {
          Impl &s;
          std::optional<EntryId> retained;
          std::uint64_t erased;
          explicit Sink(Impl &v, std::uint64_t e) : s(v), erased(e) {}
          Result<std::optional<EntryId>>
          store(const ClipboardSnapshot &snapshot) override {
            auto policy = s.history;
            policy.allow_duplicates = false;
            auto result =
                ingest(s.store, snapshot, policy,
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count(),
                       erased);
            if (result)
              retained = result.value();
            return result;
          }
          void thumbnail(const EntryId &, const ClipboardSnapshot &) override {}
          void ocr(const EntryId &, const ClipboardSnapshot &) override {}
        } sink(s, r.erased);
        auto snapshot = screenshot_snapshot(s.platform, r.bytes,
                                            r.file + ":" + version(r.bytes));
        auto accepted = dispatch_capture(
            snapshot, s.policy, screenshot_capabilities(s.platform), sink);
        if (!accepted)
          return accepted.error();
        if (sink.retained && r.image) {
          auto ticket = request_preview(s.store, *sink.retained, s.size, 1);
          if (!ticket)
            return ticket.error();
          auto stored =
              store_preview_if_present(s.store, ticket.value(), *r.image);
          if (!stored)
            return stored.error();
          auto fingerprint =
              store_image_fingerprint(s.store, ticket.value(), r.fingerprint);
          if (!fingerprint)
            return fingerprint.error();
        }
        changed = accepted.value().disposition == CaptureDisposition::Accept;
        s.last_file = r.file;
        s.last_bytes = std::move(r.bytes);
      } else if (r.image) {
        auto stored = store_preview_if_present(s.store, r.ticket, *r.image);
        if (!stored)
          return stored.error();
        if (stored.value()) {
          auto fingerprint =
              store_image_fingerprint(s.store, r.ticket, r.fingerprint);
          if (!fingerprint)
            return fingerprint.error();
          changed = true;
        }
      }
    }
  }
  if (s.isolated)
    return changed;
  if (!changed && Clock::now() - s.scanned < std::chrono::milliseconds(250))
    return changed;
  s.scanned = Clock::now();
  auto entries = list_history(s.store, SortOrder::LastUsed, 1000);
  if (!entries)
    return entries.error();
  std::set<EntryId> live;
  unsigned scheduled = 0;
  for (const auto &e : entries.value()) {
    live.insert(e.id);
    if (s.attempted[e.id] == e.digest)
      continue;
    if (scheduled >= 4)
      continue;
    auto cached = load_preview(s.store, e.id, s.size);
    if (!cached)
      return cached.error();
    if (cached.value()) {
      size_t used = 0;
      for (const auto &item : s.ready)
        used += item.second.rgba.size();
      if (used + cached.value()->rgba.size() <= media_cache_limit)
        s.ready[e.id] = std::move(*cached.value());
      s.attempted[e.id] = e.digest;
      changed = true;
      continue;
    }
    auto snapshot = load_snapshot(s.store, e.id);
    if (!snapshot)
      continue;
    bool image = false;
    for (auto &r : snapshot.value().representations)
      if (r.kind == CanonicalKind::Image) {
        image = true;
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.jobs.size() >= 64 || s.queued + r.data.size() > 64 * 1024 * 1024)
          break;
        auto ticket = request_preview(s.store, e.id, s.size, 1);
        if (!ticket)
          return ticket.error();
        s.queued += r.data.size();
        s.jobs.push_back(
            {s.epoch, std::move(ticket.value()), std::move(r), {}, {}});
        s.attempted[e.id] = e.digest;
        ++scheduled;
        s.wake.notify_all();
        break;
      }
    if (!image)
      s.attempted[e.id] = e.digest;
  }
  // Publication reload checks existence after completion/dedup. The provider
  // itself performs no SQL or codec work, including while a native popup is
  // open.
  size_t bytes = 0;
  for (auto i = s.ready.begin(); i != s.ready.end();) {
    if (!live.count(i->first))
      i = s.ready.erase(i);
    else {
      bytes += i->second.rgba.size();
      ++i;
    }
  }
  for (const auto &e : entries.value()) {
    if (s.ready.count(e.id))
      continue;
    auto cached = load_preview(s.store, e.id, s.size);
    if (cached && cached.value() &&
        bytes + cached.value()->rgba.size() <= media_cache_limit) {
      bytes += cached.value()->rgba.size();
      s.ready[e.id] = std::move(*cached.value());
      changed = true;
    }
  }
  for (auto i = s.attempted.begin(); i != s.attempted.end();)
    if (!live.count(i->first))
      i = s.attempted.erase(i);
    else
      ++i;
  return changed;
}
std::optional<MenuImage> MediaService::preview(const PreviewRef &r) const {
  auto i = impl_->ready.find(r.entry_id);
  return i == impl_->ready.end() ? std::optional<MenuImage>{} : i->second;
}
} // namespace cliptiq

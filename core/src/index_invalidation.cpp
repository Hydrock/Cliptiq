#include "cliptiq/index.hpp"
#include "index_gate.hpp"
#include <array>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace cliptiq::detail {
namespace {
const std::array<const char *, 8> names{
    {"search.db", "search.db-wal", "search.db-shm", "search.db-journal",
     "build.db", "build.db-wal", "build.db-shm", "build.db-journal"}};
bool regular(const fs::path &p) {
  auto status = fs::symlink_status(p);
  return !fs::is_symlink(status) && fs::is_regular_file(status);
}
void fail(const char *message) { throw Failure{{ErrorCode::Io, message}}; }
std::string marker(const fs::path &dir) {
  auto path = dir / ".cliptiq-owner";
  if (!regular(path) || fs::file_size(path) > 4096)
    fail("Index namespace has no valid owner marker");
  std::ifstream f(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(f)), {});
  if (!f.eof() && f.fail())
    fail("Cannot read index owner marker");
  if (text.compare(0, 17, "Cliptiq index v1\n") != 0)
    fail("Foreign index namespace");
  return text;
}
} // namespace
struct IndexGate::Impl {
  fs::path path;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  OVERLAPPED offset{};
  bool locked = false;
  ~Impl() {
    if (handle != INVALID_HANDLE_VALUE) {
      if (locked)
        UnlockFileEx(handle, 0, 1, 0, &offset);
      CloseHandle(handle);
    }
  }
#else
  int fd = -1;
  bool locked = false;
  ~Impl() {
    if (fd >= 0) {
      if (locked)
        flock(fd, LOCK_UN);
      close(fd);
    }
  }
#endif
};
IndexGate::IndexGate(const std::string &path, const std::string &owner,
                     bool create)
    : impl_(new Impl) {
  auto file = fs::absolute(fs::u8path(path)).lexically_normal();
  if (file.filename() != "search.db" ||
      file.parent_path().filename() != "index")
    invalid("Index must use the owned index/search.db namespace");
  auto parent = fs::weakly_canonical(file.parent_path().parent_path());
  impl_->path = parent / "index/search.db";
  auto gate = parent / ".cliptiq-index-gate";
#ifdef _WIN32
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1,
          &descriptor, nullptr))
    fail("Cannot create index gate ACL");
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
  impl_->handle =
      CreateFileW(gate.c_str(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_ALWAYS,
                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  LocalFree(descriptor);
  if (impl_->handle == INVALID_HANDLE_VALUE)
    fail("Cannot open index gate");
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(impl_->handle, &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      info.nFileSizeHigh || info.nFileSizeLow || info.nNumberOfLinks != 1)
    fail("Invalid index gate file");
  if (!LockFileEx(impl_->handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0,
                  &impl_->offset))
    fail("Cannot lock index gate");
  impl_->locked = true;
#else
  impl_->fd =
      open(gate.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  struct stat info {};
  if (impl_->fd < 0 || fstat(impl_->fd, &info) || !S_ISREG(info.st_mode) ||
      info.st_size || info.st_nlink != 1)
    fail("Invalid index gate file");
  int result;
  do {
    result = flock(impl_->fd, LOCK_EX);
  } while (result < 0 && errno == EINTR);
  if (result < 0)
    fail("Cannot lock index gate");
  impl_->locked = true;
#endif
  auto dir = impl_->path.parent_path();
  auto status = fs::symlink_status(dir);
  if (fs::is_symlink(status) || fs::exists(status) && !fs::is_directory(status))
    fail("Invalid index namespace directory");
  if (!fs::exists(status) && create) {
    if (owner.empty() || owner.size() > 1024)
      invalid("Invalid index owner");
#ifdef _WIN32
    descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1,
            &descriptor, nullptr))
      fail("Cannot create index directory ACL");
    attributes.lpSecurityDescriptor = descriptor;
    BOOL made = CreateDirectoryW(dir.c_str(), &attributes);
    LocalFree(descriptor);
    if (!made)
      fail("Cannot create private index directory");
#else
    if (mkdir(dir.c_str(), 0700))
      fail("Cannot create private index directory");
#endif
    std::ofstream out(dir / ".cliptiq-owner", std::ios::binary);
    out << "Cliptiq index v1\n" << owner;
    out.close();
    if (!out)
      fail("Cannot write index owner");
  }
  if (fs::exists(dir)) {
    auto text = marker(dir);
    if (!owner.empty() && text != "Cliptiq index v1\n" + owner)
      fail("Companion belongs to a different main database");
  }
}
IndexGate::~IndexGate() = default;
fs::path IndexGate::file() const { return impl_->path; }
void IndexGate::clear() {
  auto dir = file().parent_path();
  if (!fs::exists(dir))
    return;
  marker(dir);
  for (auto name : names) {
    auto p = dir / name;
    auto st = fs::symlink_status(p);
    if (fs::exists(st) && !regular(p) || fs::is_symlink(st))
      fail("Invalid companion file; refusing deletion");
  }
  for (auto name : names) {
    auto p = dir / name;
    if (fs::exists(p) && !fs::remove(p))
      fail("Cannot invalidate companion index");
  }
}
std::string companion_path(sqlite3 *db) {
  auto name = sqlite3_db_filename(db, "main");
  if (!name || !*name)
    return {};
  return (fs::absolute(fs::u8path(name)).parent_path() / "index/search.db")
      .u8string();
}
void invalidate_companion(sqlite3 *db) {
  auto path = companion_path(db);
  if (path.empty())
    return;
  auto name = fs::u8path(sqlite3_db_filename(db, "main")).filename().u8string();
  IndexGate gate(path, name);
  gate.clear();
}
} // namespace cliptiq::detail
namespace cliptiq {
Result<std::string> prepare_index_namespace(Store &s) {
  return detail::guarded<std::string>([&]() -> Result<std::string> {
    auto path = detail::companion_path(s.connection());
    if (path.empty())
      return Error{ErrorCode::Unsupported,
                   "Companion index requires an on-disk profile"};
    auto owner = fs::u8path(sqlite3_db_filename(s.connection(), "main"))
                     .filename()
                     .u8string();
    detail::IndexGate gate(path, owner, true);
    return gate.file().u8string();
  });
}
Result<void> invalidate_index(const std::string &path) {
  return detail::guarded<void>([&]() -> Result<void> {
    detail::IndexGate gate(path);
    gate.clear();
    return {};
  });
}
#ifndef CLIPTIQ_OCR_INDEX
Result<void> rebuild_index(Store &, const std::string &, std::uint64_t) {
  return Error{ErrorCode::Unsupported, "FTS is not compiled in Base"};
}
Result<std::vector<IndexHit>> search_index(Store &, const std::string &,
                                           const std::string &, std::size_t) {
  return Error{ErrorCode::Unsupported, "FTS is not compiled in Base"};
}
#endif
} // namespace cliptiq

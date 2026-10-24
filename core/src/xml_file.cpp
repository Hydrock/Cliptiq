#include "cliptiq/xml.hpp"
#include "db.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <random>
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
using namespace detail;
constexpr std::size_t cap = 64 * 1024 * 1024;
std::filesystem::path path_of(const std::string &s) {
  if (s.empty() || s.find('\0') != std::string::npos)
    invalid("Invalid XML file path");
  return std::filesystem::u8path(s);
}
#ifdef _WIN32
struct File {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~File() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};
void io(bool ok, const char *why) {
  if (!ok)
    throw Failure{{ErrorCode::Io, std::string(why) + " (Windows error " +
                                      std::to_string(GetLastError()) + ")"}};
}
#else
struct File {
  int value = -1;
  ~File() {
    if (value >= 0)
      ::close(value);
  }
};
void io(bool ok, const char *why) {
  if (!ok)
    throw Failure{{ErrorCode::Io, std::string(why) + " (errno " +
                                      std::to_string(errno) + ")"}};
}
#endif
} // namespace
Result<Bytes> read_snippet_xml(const std::string &path, std::size_t limit) {
  return detail::guarded<Bytes>([&]() -> Result<Bytes> {
    if (limit > cap)
      invalid("XML file limit exceeds ceiling");
    auto p = path_of(path);
    File f;
#ifdef _WIN32
    f.value = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    io(f.value != INVALID_HANDLE_VALUE, "Cannot open XML file");
    io(GetFileType(f.value) == FILE_TYPE_DISK,
       "XML input must be a regular disk file");
#else
    f.value = ::open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    io(f.value >= 0, "Cannot open XML file");
    struct stat info {};
    io(fstat(f.value, &info) == 0 && S_ISREG(info.st_mode),
       "XML input must be a regular file");
#endif
    Bytes out;
    std::array<unsigned char, 65536> buffer{};
    for (;;) {
#ifdef _WIN32
      DWORD n = 0;
      io(ReadFile(f.value, buffer.data(), DWORD(buffer.size()), &n, nullptr) !=
             0,
         "Cannot read XML file");
#else
      auto n = ::read(f.value, buffer.data(), buffer.size());
      if (n < 0 && errno == EINTR)
        continue;
      io(n >= 0, "Cannot read XML file");
#endif
      if (!n)
        break;
      if (std::size_t(n) > limit - out.size())
        return Error{ErrorCode::TooLarge, "XML file exceeds read budget"};
      out.insert(out.end(), buffer.begin(), buffer.begin() + n);
    }
    return out;
  });
}
Result<void> write_snippet_xml_atomic(const std::string &path,
                                      const Bytes &data) {
  return detail::guarded<void>([&]() -> Result<void> {
    if (data.size() > cap)
      return Error{ErrorCode::TooLarge, "XML output exceeds64MiB"};
    auto target = path_of(path);
    if (target.filename().empty())
      invalid("XML destination requires a file name");
    auto parent = target.parent_path();
    if (parent.empty())
      parent = ".";
    std::random_device random;
    std::filesystem::path temp;
    File f;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
      temp = parent / (".cliptiq-xml-" + std::to_string(random()) + "-" +
                       std::to_string(random()) + ".tmp");
#ifdef _WIN32
      f.value = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
      if (f.value != INVALID_HANDLE_VALUE)
        break;
      if (GetLastError() != ERROR_FILE_EXISTS &&
          GetLastError() != ERROR_ALREADY_EXISTS)
        io(false, "Cannot create temporary XML file");
#else
      f.value =
          ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      if (f.value >= 0)
        break;
      if (errno != EEXIST)
        io(false, "Cannot create temporary XML file");
#endif
    }
#ifdef _WIN32
    io(f.value != INVALID_HANDLE_VALUE, "Cannot allocate temporary XML file");
#else
    io(f.value >= 0, "Cannot allocate temporary XML file");
#endif
    struct Cleanup {
      std::filesystem::path path;
      File *file;
      ~Cleanup() {
#ifdef _WIN32
        if (file->value != INVALID_HANDLE_VALUE) {
          CloseHandle(file->value);
          file->value = INVALID_HANDLE_VALUE;
        }
#else
        if (file->value >= 0) {
          ::close(file->value);
          file->value = -1;
        }
#endif
        std::error_code ec;
        std::filesystem::remove(path, ec);
      }
    } cleanup{temp, &f};
    std::size_t offset = 0;
    while (offset < data.size()) {
      auto length = std::min<std::size_t>(65536, data.size() - offset);
#ifdef _WIN32
      DWORD n = 0;
      io(WriteFile(f.value, data.data() + offset, DWORD(length), &n, nullptr) !=
                 0 &&
             n > 0,
         "Cannot write temporary XML file");
#else
      auto n = ::write(f.value, data.data() + offset, length);
      if (n < 0 && errno == EINTR)
        continue;
      io(n > 0, "Cannot write temporary XML file");
#endif
      offset += std::size_t(n);
    }
#ifdef _WIN32
    io(FlushFileBuffers(f.value) != 0, "Cannot flush temporary XML file");
    io(CloseHandle(f.value) != 0, "Cannot close temporary XML file");
    f.value = INVALID_HANDLE_VALUE;
    io(MoveFileExW(temp.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
       "Cannot atomically replace XML destination");
#else
    io(fsync(f.value) == 0, "Cannot flush temporary XML file");
    auto closing = f.value;
    f.value = -1;
    io(::close(closing) == 0, "Cannot close temporary XML file");
    io(::rename(temp.c_str(), target.c_str()) == 0,
       "Cannot atomically replace XML destination");
    File directory;
    directory.value =
        ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    io(directory.value >= 0 && fsync(directory.value) == 0,
       "XML replaced, directory durability unconfirmed");
#endif
    return {};
  });
}
} // namespace cliptiq

#include "../LifecycleNative.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace cliptiq {
Result<void> secure_instance_directory(const std::string &path) {
  if (mkdir(path.c_str(), 0700) && errno != EEXIST)
    return Error{ErrorCode::Io, "Cannot create private instance directory"};
  struct stat s {};
  if (lstat(path.c_str(), &s) || !S_ISDIR(s.st_mode) || s.st_uid != getuid() ||
      (s.st_mode & 077))
    return Error{
        ErrorCode::Io,
        "Instance directory must be owned by this user with mode 0700"};
  return {};
}
Result<std::shared_ptr<void>> lock_instance(const std::string &path) {
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return Error{ErrorCode::Io, "Cannot open instance lock"};
  struct stat s {};
  if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid != getuid() ||
      s.st_nlink != 1 || (s.st_mode & 077)) {
    close(fd);
    return Error{ErrorCode::Io, "Unsafe instance lock"};
  }
  if (flock(fd, LOCK_EX | LOCK_NB)) {
    int error = errno;
    close(fd);
    if (error == EWOULDBLOCK || error == EAGAIN)
      return std::shared_ptr<void>{};
    return Error{ErrorCode::Io, "Cannot lock user session"};
  }
  return std::shared_ptr<void>(new int(fd), [](void *p) {
    auto fd = static_cast<int *>(p);
    close(*fd);
    delete fd;
  });
}
} // namespace cliptiq

#include "../LifecycleNative.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace cliptiq {
Result<std::string> lifecycle_directory() {
  auto base = getenv("XDG_RUNTIME_DIR");
  return base && *base ? (std::filesystem::u8path(base) / "cliptiq").u8string()
                       : "/tmp/cliptiq-" + std::to_string(getuid());
}
std::string native_session_hint() {
  std::string hint = std::to_string(getuid());
  for (auto key : {"XDG_SESSION_ID", "WAYLAND_DISPLAY", "DISPLAY",
                   "DBUS_SESSION_BUS_ADDRESS"}) {
    auto value = getenv(key);
    if (value && *value)
      return hint + "|" + key + "=" + value;
  }
  return hint;
}
Result<std::string> executable_path() {
  std::string path(32768, '\0');
  auto n = readlink("/proc/self/exe", path.data(), path.size());
  if (n <= 0 || size_t(n) >= path.size())
    return Error{ErrorCode::Io, "Cannot resolve executable"};
  path.resize(size_t(n));
  return path;
}
std::string system_locale() {
  for (auto key : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    auto value = getenv(key);
    if (value && *value)
      return value;
  }
  return "en";
}
namespace {
std::string quote(const std::string &s) {
  std::string shell = "\"";
  for (char c : s) {
    if (c == '%') {
      shell += "%%";
      continue;
    }
    if (c == '"' || c == '\\' || c == '$' || c == '`')
      shell += '\\';
    shell += c;
  }
  shell += '"';
  std::string out;
  for (char c : shell) {
    if (c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}
} // namespace
Result<AutostartStatus> native_autostart(bool enabled,
                                         const AutostartOptions &o) {
  std::filesystem::path directory = std::filesystem::u8path(o.directory);
  if (!o.isolated) {
    auto xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
      directory = std::filesystem::u8path(xdg);
    else {
      auto home = getenv("HOME");
      if (!home)
        return Error{ErrorCode::Io, "HOME unavailable"};
      directory = std::filesystem::u8path(home) / ".config";
    }
    directory /= "autostart";
  }
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec || !directory.is_absolute() ||
      std::filesystem::is_symlink(directory, ec))
    return Error{ErrorCode::Io, "Autostart directory unavailable"};
  auto destination = directory / "io.github.hydrock.cliptiq.desktop";
  const std::string marker = "# Cliptiq owned autostart v1\n";
  if (std::filesystem::exists(destination, ec) ||
      std::filesystem::is_symlink(destination, ec)) {
    if (std::filesystem::is_symlink(destination, ec) ||
        !std::filesystem::is_regular_file(destination, ec) ||
        std::filesystem::file_size(destination, ec) > 65536)
      return Error{ErrorCode::Io, "Unsafe existing autostart entry"};
    std::ifstream file(destination, std::ios::binary);
    std::string line;
    std::getline(file, line);
    if (line + "\n" != marker)
      return Error{ErrorCode::Io,
                   "Refusing to replace an unowned autostart entry"};
  }
  if (!enabled) {
    std::filesystem::remove(destination, ec);
    if (ec)
      return Error{ErrorCode::Io, "Cannot remove autostart entry"};
    return AutostartStatus{false, false, "Autostart disabled"};
  }
  if (o.executable.find('=') != std::string::npos ||
      !std::filesystem::is_regular_file(std::filesystem::u8path(o.executable),
                                        ec) ||
      access(o.executable.c_str(), X_OK))
    return Error{ErrorCode::Io,
                 "Autostart executable is unavailable or unsupported"};
  auto data =
      marker + "[Desktop Entry]\nType=Application\nName=Cliptiq\nExec=" +
      quote(o.executable) + " --background\nTerminal=false\nHidden=false\n";
  auto temporary = (directory / ".cliptiq-autostart-XXXXXX").string();
  int fd = mkstemp(temporary.data());
  if (fd < 0)
    return Error{ErrorCode::Io, "Cannot write autostart entry"};
  size_t offset = 0;
  while (offset < data.size()) {
    auto n = write(fd, data.data() + offset, data.size() - offset);
    if (n <= 0)
      break;
    offset += size_t(n);
  }
  bool success = offset == data.size() && fsync(fd) == 0;
  close(fd);
  if (!success || rename(temporary.c_str(), destination.c_str())) {
    unlink(temporary.c_str());
    return Error{ErrorCode::Io, "Cannot commit autostart entry"};
  }
  return AutostartStatus{
      true, false, "Autostart registered; desktop may delay login launch"};
}
Result<AutostartStatus> native_autostart_status(const AutostartOptions &o) {
  auto directory = std::filesystem::u8path(o.directory);
  if (!o.isolated) {
    auto xdg = getenv("XDG_CONFIG_HOME");
    auto home = getenv("HOME");
    if (xdg && *xdg)
      directory = std::filesystem::u8path(xdg);
    else if (home)
      directory = std::filesystem::u8path(home) / ".config";
    else
      return Error{ErrorCode::Io, "HOME unavailable"};
    directory /= "autostart";
  }
  auto path = directory / "io.github.hydrock.cliptiq.desktop";
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return AutostartStatus{false, false, "Autostart disabled"};
  if (std::filesystem::is_symlink(path, ec) ||
      std::filesystem::file_size(path, ec) > 65536 || ec)
    return Error{ErrorCode::Io, "Unsafe autostart entry"};
  std::ifstream file(path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(file)), {});
  bool registered = data.rfind("# Cliptiq owned autostart v1\n", 0) == 0 &&
                    data.find("Exec=" + quote(o.executable) +
                              " --background\n") != std::string::npos &&
                    data.find("Hidden=true") == std::string::npos;
  return AutostartStatus{
      registered, false,
      registered ? "Autostart registered; desktop may delay login launch"
                 : "Autostart disabled or belongs to another installation"};
}
} // namespace cliptiq

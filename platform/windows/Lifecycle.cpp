#include "../LifecycleNative.hpp"
#define NOMINMAX
#include <aclapi.h>
#include <filesystem>
#include <sddl.h>
#include <vector>
#include <windows.h>
namespace cliptiq {
namespace {
std::wstring wide(const std::string &s) {
  return std::filesystem::u8path(s).wstring();
}
Result<std::vector<BYTE>> token_user() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return Error{ErrorCode::Io, "Cannot identify current user"};
  DWORD n = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &n);
  std::vector<BYTE> data(n);
  bool ok = GetTokenInformation(token, TokenUser, data.data(), n, &n);
  CloseHandle(token);
  if (!ok)
    return Error{ErrorCode::Io, "Cannot read current user SID"};
  return data;
}
} // namespace
Result<void> secure_instance_directory(const std::string &path) {
  auto user = token_user();
  if (!user)
    return user.error();
  auto sid = reinterpret_cast<TOKEN_USER *>(user.value().data())->User.Sid;
  LPWSTR sid_string = nullptr;
  if (!ConvertSidToStringSidW(sid, &sid_string))
    return Error{ErrorCode::Io, "Cannot format user SID"};
  std::wstring sddl = L"O:" + std::wstring(sid_string) + L"D:P(A;OICI;FA;;;" +
                      std::wstring(sid_string) +
                      L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
  LocalFree(sid_string);
  PSECURITY_DESCRIPTOR security = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &security, nullptr))
    return Error{ErrorCode::Io, "Cannot secure instance directory"};
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), security, FALSE};
  auto name = wide(path);
  bool made = CreateDirectoryW(name.c_str(), &attributes);
  auto error = GetLastError();
  LocalFree(security);
  if (!made && error != ERROR_ALREADY_EXISTS)
    return Error{ErrorCode::Io, "Cannot create instance directory"};
  auto attrs = GetFileAttributesW(name.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
      (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
    return Error{ErrorCode::Io, "Unsafe instance directory"};
  PSID owner = nullptr;
  PACL acl = nullptr;
  PSECURITY_DESCRIPTOR sd = nullptr;
  auto rc = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION |
                                      DACL_SECURITY_INFORMATION,
                                  &owner, nullptr, &acl, nullptr, &sd);
  bool ok = rc == ERROR_SUCCESS && owner && EqualSid(owner, sid) && acl;
  BYTE system[SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
  DWORD sn = sizeof(system), an = sizeof(admins);
  CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &sn);
  CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &an);
  if (ok)
    for (DWORD n = 0; n < acl->AceCount; ++n) {
      void *raw = nullptr;
      if (!GetAce(acl, n, &raw)) {
        ok = false;
        break;
      }
      auto header = static_cast<ACE_HEADER *>(raw);
      if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
        auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
        PSID allowed = &ace->SidStart;
        if (!EqualSid(allowed, sid) && !EqualSid(allowed, system) &&
            !EqualSid(allowed, admins))
          ok = false;
      } else if (header->AceType != ACCESS_DENIED_ACE_TYPE)
        ok = false;
    }
  if (sd)
    LocalFree(sd);
  if (!ok)
    return Error{ErrorCode::Io,
                 "Instance directory must be private to the current user"};
  return {};
}
Result<std::shared_ptr<void>> lock_instance(const std::string &path) {
  HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return Error{ErrorCode::Io, "Cannot open instance lock"};
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
      info.nNumberOfLinks != 1) {
    CloseHandle(file);
    return Error{ErrorCode::Io, "Unsafe instance lock"};
  }
  OVERLAPPED overlap{};
  if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                  1, 0, &overlap)) {
    auto error = GetLastError();
    CloseHandle(file);
    if (error == ERROR_LOCK_VIOLATION)
      return std::shared_ptr<void>{};
    return Error{ErrorCode::Io, "Cannot lock user session"};
  }
  return std::shared_ptr<void>(file, [](void *p) { CloseHandle(p); });
}
Result<std::string> lifecycle_directory() {
  auto base = _wgetenv(L"LOCALAPPDATA");
  if (!base)
    return Error{ErrorCode::Io, "LOCALAPPDATA unavailable"};
  auto parent = std::filesystem::path(base) / L"Cliptiq";
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec)
    return Error{ErrorCode::Io, "Cannot create application directory"};
  return (parent / L"instance").u8string();
}
std::string native_session_hint() {
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  return std::to_string(session);
}
Result<std::string> executable_path() {
  std::wstring path(32768, 0);
  auto n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
  if (!n || n >= path.size())
    return Error{ErrorCode::Io, "Cannot resolve executable"};
  path.resize(n);
  return std::filesystem::path(path).u8string();
}
std::string system_locale() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH];
  if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH))
    return "en";
  return std::filesystem::path(name).u8string();
}
Result<AutostartStatus> native_autostart(bool enabled,
                                         const AutostartOptions &o) {
  auto path = wide(o.executable);
  if (path.find(L'"') != std::wstring::npos)
    return Error{ErrorCode::InvalidArgument, "Invalid executable quote"};
  std::wstring command = L"\"" + path + L"\" --background";
  if (command.size() > 260)
    return Error{ErrorCode::TooLarge,
                 "Windows Run command exceeds 260 characters"};
  if (enabled && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    return Error{ErrorCode::Io, "Autostart executable unavailable"};
  std::wstring key = o.isolated
                         ? L"Software\\Cliptiq\\Tests\\Autostart"
                         : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  std::wstring value =
      o.isolated
          ? wide(std::filesystem::u8path(o.directory).filename().u8string())
          : L"Cliptiq";
  HKEY registry = nullptr;
  auto rc = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &registry,
                            nullptr);
  if (rc != ERROR_SUCCESS)
    return Error{ErrorCode::Io, "Cannot access user autostart registry"};
  if (!enabled) {
    DWORD size = 0, type = 0;
    rc = RegQueryValueExW(registry, value.c_str(), nullptr, &type, nullptr,
                          &size);
    if (rc == ERROR_FILE_NOT_FOUND) {
      RegCloseKey(registry);
      return AutostartStatus{false, false, "Autostart disabled"};
    }
    if (rc != ERROR_SUCCESS || type != REG_SZ || size > 65536) {
      RegCloseKey(registry);
      return Error{ErrorCode::Io, "Unexpected autostart registry value"};
    }
    std::vector<wchar_t> stored(size / 2 + 1);
    RegQueryValueExW(registry, value.c_str(), nullptr, &type,
                     reinterpret_cast<BYTE *>(stored.data()), &size);
    if (std::wstring(stored.data()) != command) {
      RegCloseKey(registry);
      return Error{ErrorCode::Io, "Autostart belongs to another installation; "
                                  "remove that entry explicitly"};
    }
    rc = RegDeleteValueW(registry, value.c_str());
  } else
    rc = RegSetValueExW(registry, value.c_str(), 0, REG_SZ,
                        reinterpret_cast<const BYTE *>(command.c_str()),
                        DWORD((command.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(registry);
  if (rc != ERROR_SUCCESS)
    return Error{ErrorCode::Io, "User autostart registration failed"};
  return AutostartStatus{
      enabled, false,
      enabled ? "Autostart registered; Windows may delay login launch"
              : "Autostart disabled"};
}
Result<AutostartStatus> native_autostart_status(const AutostartOptions &o) {
  std::wstring key = o.isolated
                         ? L"Software\\Cliptiq\\Tests\\Autostart"
                         : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  std::wstring value =
      o.isolated
          ? wide(std::filesystem::u8path(o.directory).filename().u8string())
          : L"Cliptiq";
  DWORD size = 0, type = 0;
  auto rc = RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(),
                         RRF_RT_REG_SZ, &type, nullptr, &size);
  if (rc == ERROR_FILE_NOT_FOUND)
    return AutostartStatus{false, false, "Autostart disabled"};
  if (rc != ERROR_SUCCESS || size > 65536)
    return Error{ErrorCode::Io, "Cannot inspect user autostart registry"};
  std::vector<wchar_t> stored(size / 2 + 1);
  rc = RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value.c_str(),
                    RRF_RT_REG_SZ, &type, stored.data(), &size);
  if (rc != ERROR_SUCCESS)
    return Error{ErrorCode::Io, "Cannot inspect user autostart registry"};
  bool enabled = std::wstring(stored.data()) ==
                 L"\"" + wide(o.executable) + L"\" --background";
  return AutostartStatus{
      enabled, false,
      enabled
          ? "Autostart registered; Windows policy may disable or delay launch"
          : "Autostart disabled or belongs to another installation"};
}
} // namespace cliptiq

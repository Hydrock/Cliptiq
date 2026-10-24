#include "../Media.hpp"
#include "Clipboard.hpp"
#define NOMINMAX
#include <atomic>
#include <filesystem>
#include <thread>
#include <windows.h>
namespace cliptiq {
Result<MenuImage> decode_native_image(const Representation &r) {
  auto decoded = windows::decode_image(r);
  if (!decoded)
    return decoded.error();
  auto &i = decoded.value();
  MenuImage out{i.width, i.height, Bytes(size_t(i.width) * i.height * 4)};
  for (unsigned y = 0; y < i.height; ++y)
    for (unsigned x = 0; x < i.width; ++x) {
      auto src = i.bgra.data() + size_t(y) * i.stride + x * 4;
      auto dst = out.rgba.data() + (size_t(y) * i.width + x) * 4;
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
      dst[3] = src[3];
    }
  return out;
}
struct ScreenshotWatcher::Impl {
  HANDLE directory = INVALID_HANDLE_VALUE, event = nullptr, halt = nullptr;
  std::thread thread;
  std::function<void(std::string)> callback;
};
ScreenshotWatcher::ScreenshotWatcher() : impl_(new Impl) {}
ScreenshotWatcher::~ScreenshotWatcher() { stop(); }
void ScreenshotWatcher::stop() {
  auto &s = *impl_;
  if (s.halt)
    SetEvent(s.halt);
  if (s.directory != INVALID_HANDLE_VALUE)
    CancelIoEx(s.directory, nullptr);
  if (s.thread.joinable())
    s.thread.join();
  if (s.directory != INVALID_HANDLE_VALUE)
    CloseHandle(s.directory);
  if (s.event)
    CloseHandle(s.event);
  if (s.halt)
    CloseHandle(s.halt);
  s.directory = INVALID_HANDLE_VALUE;
  s.event = s.halt = nullptr;
  s.callback = {};
}
Result<void> ScreenshotWatcher::start(const std::string &root,
                                      std::function<void(std::string)> cb) {
  stop();
  auto &s = *impl_;
  s.directory =
      CreateFileW(std::filesystem::u8path(root).c_str(), FILE_LIST_DIRECTORY,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (s.directory == INVALID_HANDLE_VALUE)
    return Error{ErrorCode::Io, "Cannot monitor selected screenshot directory"};
  s.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  s.halt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!s.event || !s.halt) {
    stop();
    return Error{ErrorCode::Io, "Cannot create directory notification events"};
  }
  s.callback = std::move(cb);
  s.thread = std::thread([&s] {
    alignas(DWORD) BYTE buffer[16384];
    for (;;) {
      OVERLAPPED ov{};
      ov.hEvent = s.event;
      ResetEvent(s.event);
      if (!ReadDirectoryChangesW(s.directory, buffer, sizeof(buffer), FALSE,
                                 FILE_NOTIFY_CHANGE_FILE_NAME |
                                     FILE_NOTIFY_CHANGE_SIZE |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE,
                                 nullptr, &ov, nullptr))
        break;
      HANDLE events[] = {s.halt, s.event};
      if (WaitForMultipleObjects(2, events, FALSE, INFINITE) !=
          WAIT_OBJECT_0 + 1) {
        CancelIoEx(s.directory, &ov);
        DWORD ignored;
        GetOverlappedResult(s.directory, &ov, &ignored, TRUE);
        break;
      }
      DWORD count = 0;
      if (!GetOverlappedResult(s.directory, &ov, &count, FALSE) || !count)
        break;
      for (size_t p = 0; p + 12 <= count;) {
        auto *n = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(buffer + p);
        if (n->FileNameLength % 2 || n->FileNameLength > count - p - 12)
          break;
        if (n->Action == FILE_ACTION_ADDED ||
            n->Action == FILE_ACTION_MODIFIED ||
            n->Action == FILE_ACTION_RENAMED_NEW_NAME) {
          std::wstring name(n->FileName, n->FileNameLength / 2);
          s.callback(std::filesystem::path(name).u8string());
        }
        if (!n->NextEntryOffset ||
            n->NextEntryOffset < 12 + n->FileNameLength ||
            n->NextEntryOffset > count - p)
          break;
        p += n->NextEntryOffset;
      }
    }
    s.callback("");
  });
  return {};
}
} // namespace cliptiq

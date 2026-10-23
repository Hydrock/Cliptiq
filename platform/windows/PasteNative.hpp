#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "cliptiq/paste.hpp"
#include <windows.h>
namespace cliptiq {
// OS-call boundary, also used to verify decision paths without interactive
// input.
struct NativePasteCalls {
  std::function<HWND()> foreground;
  std::function<bool(HWND)> alive, restore;
  std::function<DWORD(HWND, DWORD *)> window_thread;
  std::function<std::uint64_t(DWORD)> process_birth;
  std::function<DWORD(DWORD)> integrity;
  std::function<SHORT(int)> key_state;
  std::function<UINT(DWORD)> scan;
  std::function<UINT(UINT, INPUT *)> send;
  std::function<std::uint64_t(HWND)> lifetime;
};
} // namespace cliptiq

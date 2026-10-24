#pragma once
#include <functional>
#include <windows.h>
namespace cliptiq {
struct HotkeyTestCalls {
  std::function<bool()> interactive;
  std::function<BOOL(HWND, int, UINT, UINT)> register_key;
  std::function<BOOL(HWND, int)> unregister_key;
};
inline HotkeyTestCalls hotkey_test_calls;
} // namespace cliptiq

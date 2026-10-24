#pragma once
#include "cliptiq/locale.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <map>
#include <windows.h>
namespace cliptiq {
inline const wchar_t *lw(const wchar_t *english) {
  static std::map<std::pair<std::string, std::wstring>, std::wstring> cache;
  auto key = std::make_pair(active_locale(), std::wstring(english));
  auto found = cache.find(key);
  if (found != cache.end())
    return found->second.c_str();
  int n = WideCharToMultiByte(CP_UTF8, 0, english, -1, nullptr, 0, nullptr,
                              nullptr);
  std::string utf8(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, english, -1, utf8.data(), n, nullptr,
                      nullptr);
  if (!utf8.empty())
    utf8.pop_back();
  auto translated = tr(utf8);
  n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated.data(),
                          int(translated.size()), nullptr, 0);
  std::wstring value(n, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated.data(),
                      int(translated.size()), value.data(), n);
  return cache.emplace(std::move(key), std::move(value)).first->second.c_str();
}
} // namespace cliptiq
#elif defined(__OBJC__)
#import <Foundation/Foundation.h>
namespace cliptiq {
inline NSString *loc(NSString *english) {
  return [NSString stringWithUTF8String:lc(english.UTF8String)];
}
} // namespace cliptiq
#endif

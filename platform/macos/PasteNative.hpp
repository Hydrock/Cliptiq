#pragma once
#include "cliptiq/paste.hpp"
#import <ApplicationServices/ApplicationServices.h>
namespace cliptiq {
enum class PasteForeground { Target, Self, Other };
struct NativePasteCalls {
  std::function<bool()> trusted, capture, alive, same_window, activate;
  std::function<PasteForeground()> foreground;
  std::function<CGEventFlags()> held_flags;
  std::function<int()> paste_key;
  std::function<bool(int)> key_down;
  std::function<CGEventRef(CGKeyCode,bool)> create;
  std::function<void(CGEventRef)> post;
};
}

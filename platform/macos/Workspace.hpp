#pragma once
#include "cliptiq/native_menu.hpp"
#import <AppKit/AppKit.h>
namespace cliptiq::macos {
class Workspace {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  Workspace(NSWindow *, const NativeMenuOptions &, std::function<void(MenuCommand)>, MenuPreviewProvider);
  ~Workspace();
  void update(MenuModel);
  void status(const std::string &);
};
}

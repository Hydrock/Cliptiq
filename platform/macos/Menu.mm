#include "cliptiq/native_menu.hpp"
#import <AppKit/AppKit.h>
#include <map>
#include <thread>
@interface CliptiqMenuTarget : NSObject <NSMenuDelegate> {
@public
  std::function<void(NSMenuItem *)> invoke;
  std::function<void()> popup;
  std::function<void(NSMenu *)> will_open, did_close;
}
- (void)selected:(NSMenuItem *)sender;
- (void)open:(id)sender;
@end
@implementation CliptiqMenuTarget
- (void)menuWillOpen:(NSMenu *)menu {
  if (will_open)
    will_open(menu);
}
- (void)menuDidClose:(NSMenu *)menu {
  if (did_close)
    did_close(menu);
}
- (void)selected:(NSMenuItem *)sender {
  if (invoke)
    invoke(sender);
}
- (void)open:(id)sender {
  (void)sender;
  if (popup)
    popup();
}
@end
namespace cliptiq {
namespace {
NSString *string(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"[Invalid text]";
}
NSImage *image(const MenuImage &i) {
  auto rep =
      [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                              pixelsWide:i.width
                                              pixelsHigh:i.height
                                           bitsPerSample:8
                                         samplesPerPixel:4
                                                hasAlpha:YES
                                                isPlanar:NO
                                          colorSpaceName:NSDeviceRGBColorSpace
                                             bytesPerRow:i.width * 4
                                            bitsPerPixel:32];
  if (!rep)
    return nil;
  memcpy(rep.bitmapData, i.rgba.data(), i.rgba.size());
  auto result = [[NSImage alloc] initWithSize:NSMakeSize(i.width, i.height)];
  [result addRepresentation:rep];
  return result;
}
} // namespace
struct NativeMenu::State : std::enable_shared_from_this<State> {
  NativeMenuOptions options;
  std::function<void(MenuCommand)> callback;
  MenuPreviewProvider provider;
  std::thread::id owner = std::this_thread::get_id();
  NSWindow *window = nil;
  NSButton *button = nil;
  NSMenu *menu = nil;
  NSStatusItem *status_item = nil;
  CliptiqMenuTarget *target = nil;
  std::map<unsigned, MenuItem> commands;
  unsigned next = 100;
  std::uint64_t generation = 0;
  MenuModel model;
  InvocationContext invocation;
  std::uint64_t opening = 0;
  bool explicit_tracking = false;
  NativeMenuStatus status;
  State(NativeMenuOptions o, std::function<void(MenuCommand)> c,
        MenuPreviewProvider p)
      : options(o), callback(std::move(c)), provider(std::move(p)) {}
  ~State() {
    if (status_item)
      [NSStatusBar.systemStatusBar removeStatusItem:status_item];
    if (target) {
      target->invoke = {};
      target->popup = {};
      target->will_open = {};
      target->did_close = {};
    }
    [window orderOut:nil];
    [window close];
  }
  bool thread() const {
    return owner == std::this_thread::get_id() && NSThread.isMainThread;
  }
  void invoke(NSMenuItem *sender) {
    auto keep = shared_from_this();
    auto values = (NSArray *)sender.representedObject;
    if (values.count != 2 || [values[0] unsignedLongLongValue] != generation)
      return;
    auto it = commands.find([values[1] unsignedIntValue]);
    if (it == commands.end() || !it->second.enabled ||
        it->second.action == MenuAction::None)
      return;
    auto cb = callback;
    MenuCommand c{it->second.action, it->second.id, model.revision};
    c.invocation = invocation;
    auto held = NSEvent.modifierFlags;
    c.invocation.modifiers = ((held & NSEventModifierFlagControl) ? 1u : 0u) |
                             ((held & NSEventModifierFlagShift) ? 2u : 0u) |
                             ((held & NSEventModifierFlagOption) ? 4u : 0u) |
                             ((held & NSEventModifierFlagCommand) ? 8u : 0u);
    invocation = {};
    if (cb)
      cb(std::move(c));
  }
  NSMenu *build(const std::vector<MenuItem> &items) {
    NSMenu *result = [[NSMenu alloc] initWithTitle:@"Cliptiq"];
    result.autoenablesItems = NO;
    result.delegate = target;
    for (const auto &i : items) {
      if (i.separator) {
        [result addItem:NSMenuItem.separatorItem];
        continue;
      }
      std::string caption =
          (i.number ? std::to_string(*i.number) + ". " : "") + i.label;
      auto item = [[NSMenuItem alloc] initWithTitle:string(caption)
                                             action:@selector(selected:)
                                      keyEquivalent:string(i.shortcut)];
      item.keyEquivalentModifierMask = 0;
      item.target = target;
      item.enabled = i.enabled;
      item.toolTip = i.tooltip.empty() ? nil : string(i.tooltip);
      auto id = next++;
      item.representedObject = @[ @(generation), @(id) ];
      if (i.children.empty())
        commands[id] = i;
      else
        item.submenu = build(i.children);
      auto pixels = menu_preview(i, options, provider);
      if (pixels)
        item.image = image(*pixels);
      else if (options.icons && i.action == MenuAction::SelectHistory)
        item.image =
            [NSImage imageWithSystemSymbolName:i.kind == CanonicalKind::Image
                                                   ? @"photo"
                                                   : @"doc.text"
                      accessibilityDescription:nil];
      [result addItem:item];
    }
    return result;
  }
  void popup(const InvocationContext &ctx = {}) {
    ++opening;
    invocation = ctx;
    if (invocation.target.empty() && options.invocation_provider)
      invocation = options.invocation_provider();
    explicit_tracking = true;
    struct Reset {
      State *s;
      ~Reset() {
        s->invocation = {};
        s->explicit_tracking = false;
      }
    } reset{this};
    auto keep = shared_from_this();
    if (!menu)
      return;
    [window makeKeyAndOrderFront:nil];
    NSPoint p = NSMakePoint(0, NSHeight(window.contentView.bounds));
    NSView *view = button;
    if (ctx.x && ctx.y) {
      p = NSMakePoint(*ctx.x, *ctx.y);
      view = nil;
    }
    [menu
        popUpMenuPositioningItem:menu.numberOfItems ? [menu itemAtIndex:0] : nil
                      atLocation:p
                          inView:view];
  }
  void tray() {
    if (status_item) {
      [NSStatusBar.systemStatusBar removeStatusItem:status_item];
      status_item = nil;
    }
    if (options.tray_style == TrayStyle::Hidden) {
      status.tray_available = false;
      status.tray_reason = "Status item hidden; launcher visible";
      return;
    }
    status_item = [NSStatusBar.systemStatusBar
        statusItemWithLength:NSSquareStatusItemLength];
    NSImage *icon = [NSImage imageWithSystemSymbolName:@"doc.on.clipboard"
                              accessibilityDescription:@"Cliptiq"];
    if (options.tray_style == TrayStyle::Standard)
      [icon setTemplate:YES];
    else {
      icon = [icon
          imageWithSymbolConfiguration:[NSImageSymbolConfiguration
                                           configurationWithPaletteColors:@[
                                             NSColor.whiteColor
                                           ]]];
      [icon setTemplate:NO];
    }
    status_item.button.image = icon;
    status_item.button.toolTip = @"Cliptiq";
    [status_item.button setAccessibilityLabel:@"Cliptiq"];
    status_item.menu = menu;
    status.tray_available = status_item != nil && status_item.visible;
    status.tray_reason = status.tray_available
                             ? "NSStatusItem visible"
                             : "Status item unavailable; launcher visible";
  }
};
NativeMenu::NativeMenu(NativeMenuOptions o, std::function<void(MenuCommand)> c,
                       MenuPreviewProvider p)
    : state_(std::make_shared<State>(o, std::move(c), std::move(p))) {}
NativeMenu::~NativeMenu() = default;
Result<void> NativeMenu::initialize() {
  auto s = state_;
  if (!s->thread() || s->window)
    return Error{ErrorCode::InvalidArgument,
                 "AppKit menu requires main thread"};
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  s->target = [CliptiqMenuTarget new];
  std::weak_ptr<State> weak = s;
  s->target->invoke = [weak](NSMenuItem *item) {
    if (auto s = weak.lock())
      s->invoke(item);
  };
  s->target->will_open = [weak](NSMenu *menu) {
    if (auto s = weak.lock())
      if (menu == s->menu && !s->explicit_tracking) {
        ++s->opening;
        s->invocation = s->options.invocation_provider
                            ? s->options.invocation_provider()
                            : InvocationContext{};
      }
  };
  s->target->did_close = [weak](NSMenu *menu) {
    if (auto s = weak.lock())
      if (menu == s->menu && !s->explicit_tracking) {
        auto opening = s->opening;
        dispatch_async(dispatch_get_main_queue(), ^{
          if (auto state = weak.lock())
            if (state->opening == opening && !state->explicit_tracking)
              state->invocation = {};
        });
      }
  };
  s->target->popup = [weak] {
    if (auto s = weak.lock())
      s->popup();
  };
  s->window =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 300, 110)
                                  styleMask:NSWindowStyleMaskTitled |
                                            NSWindowStyleMaskMiniaturizable
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  s->window.releasedWhenClosed = NO;
  s->window.title = @"Cliptiq";
  s->button = [[NSButton alloc] initWithFrame:NSMakeRect(24, 32, 250, 40)];
  s->button.title = @"Clipboard History";
  s->button.bezelStyle = NSBezelStyleRounded;
  s->button.target = s->target;
  s->button.action = @selector(open:);
  [s->window.contentView addSubview:s->button];
  [s->window makeKeyAndOrderFront:nil];
  s->status.launcher_visible = true;
  auto r = update({});
  s->tray();
  return r;
}
Result<void> NativeMenu::update(MenuModel m) {
  auto valid = validate_menu_model(m);
  if (!valid)
    return valid;
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "AppKit menu not initialized"};
  [s->menu cancelTracking];
  s->model = std::move(m);
  ++s->generation;
  s->commands.clear();
  s->menu = s->build(s->model.items);
  s->status_item.menu = s->menu;
  return {};
}
Result<void> NativeMenu::show(const InvocationContext &c) {
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "AppKit menu not initialized"};
  s->popup(c);
  return {};
}
Result<void> NativeMenu::set_tray_style(TrayStyle style) {
  auto s = state_;
  if (!s->thread() || static_cast<unsigned>(style) > 2)
    return Error{ErrorCode::InvalidArgument, "Invalid tray style"};
  s->options.tray_style = style;
  s->tray();
  return {};
}
Result<void> NativeMenu::process_events(unsigned wait) {
  auto s = state_;
  if (!s->thread())
    return Error{ErrorCode::InvalidArgument,
                 "AppKit events require main thread"};
  @autoreleasepool {
    for (unsigned i = 0; i < 64; ++i) {
      auto event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                      untilDate:[NSDate distantPast]
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
      if (!event)
        break;
      [NSApp sendEvent:event];
    }
    [NSApp updateWindows];
    [[NSRunLoop mainRunLoop]
        runUntilDate:[NSDate dateWithTimeIntervalSinceNow:std::min(wait, 10u) /
                                                          1000.0]];
  }
  return {};
}
NativeMenuStatus NativeMenu::status() const { return state_->status; }
NativeMenuHandles NativeMenu::native_handles() const {
  return {reinterpret_cast<uintptr_t>((__bridge void *)state_->window),
          reinterpret_cast<uintptr_t>((__bridge void *)state_->menu)};
}
} // namespace cliptiq

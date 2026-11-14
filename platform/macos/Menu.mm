#include "../LocaleNative.hpp"
#include "cliptiq/native_menu.hpp"
#include "Workspace.hpp"
#import <AppKit/AppKit.h>
#import <os/log.h>
#include <map>
#include <cstring>
#include <thread>
@interface CliptiqMenuTarget : NSObject <NSMenuDelegate, NSWindowDelegate, NSApplicationDelegate> {
@public
  std::function<void(NSMenuItem *)> invoke;
  std::function<void()> popup, tray_click, close_workspace, reopen;
  std::function<void(NSMenu *)> will_open, did_close;
}
- (void)selected:(NSMenuItem *)sender;
- (void)open:(id)sender;
- (void)trayClick:(id)sender;
@end
@implementation CliptiqMenuTarget
- (BOOL)applicationShouldHandleReopen:(NSApplication *)application hasVisibleWindows:(BOOL)visible {
  (void)application;(void)visible;if(reopen)reopen();return NO;
}
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender; if(close_workspace){close_workspace();return NO;}return YES;}
- (void)trayClick:(id)sender { (void)sender;if(tray_click)tray_click(); }
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
  NSMenu *menu = nil, *tray_menu = nil;
  std::unique_ptr<macos::Workspace> workspace;
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
      target->tray_click = {};
      target->close_workspace = {};
      target->reopen = {};
      if(NSApp.delegate==target)NSApp.delegate=nil;
      target->will_open = {};
      target->did_close = {};
    }
    workspace.reset();window.delegate=nil;
    [window orderOut:nil];
    [window close];
  }
  bool thread() const {
    return owner == std::this_thread::get_id() && NSThread.isMainThread;
  }
  void invoke(NSMenuItem *sender) {
    auto keep = shared_from_this();
    if([sender.representedObject isKindOfClass:NSString.class]) {
      if(sender.tag==1)open_workspace();
      else if(callback)callback({sender.tag==2?MenuAction::OpenSettings:MenuAction::Quit,"",model.revision});
      return;
    }
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
          (i.number ? std::to_string(*i.number) + ". " : "") + i.label + (i.source_device.empty()?"":"  ["+i.source_device+"]");
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
    // Keep State alive through the cleanup guard, even if a command closes UI.
    auto keep = shared_from_this();
    ++opening;
    os_log(OS_LOG_DEFAULT,"Cliptiq popup begin generation=%llu appHidden=%d appActive=%d policy=%ld",(unsigned long long)opening,(int)NSApp.hidden,(int)NSApp.active,(long)NSApp.activationPolicy);
    invocation = ctx;
    if (invocation.target.empty() && options.invocation_provider)
      invocation = options.invocation_provider();
    if(options.transient_popup) {
      ++generation;commands.clear();next=100;
      menu=build(model.items);
    }
    NSMenu *tracking=menu;
    explicit_tracking = true;
    struct Reset {
      State *s;
      NSMenu *tracking;
      bool release;
      ~Reset() {
        s->invocation = {};
        s->explicit_tracking = false;
        if(release && s->menu==tracking) {
          s->status_item.menu=nil;s->menu=nil;
          s->commands.clear();++s->generation;
        }
      }
    } reset{this,tracking,options.transient_popup};
    if (!tracking) return;
    NSPoint p = NSEvent.mouseLocation;
    if (ctx.x && ctx.y) p = NSMakePoint(*ctx.x, *ctx.y);
    // Like Clipy, use screen coordinates and no window/view owner for the
    // accessory-app popup. Inspect its actual surfaces while AppKit tracks it.
    // No window titles, clipboard values or target identifiers enter the log.
    auto surfaceProbe=[NSTimer timerWithTimeInterval:0.05 repeats:NO block:^(NSTimer *){
      unsigned visible=0,active=0;
      for(NSWindow *w in NSApp.windows)if(w.visible && w.level>=NSPopUpMenuWindowLevel){++visible;if(w.onActiveSpace)++active;}
      os_log(OS_LOG_DEFAULT,"Cliptiq popup surface visible=%u activeSpace=%u",visible,active);
    }];
    [[NSRunLoop mainRunLoop] addTimer:surfaceProbe forMode:NSEventTrackingRunLoopMode];
    BOOL selected = [tracking popUpMenuPositioningItem:nil atLocation:p inView:nil];
    [surfaceProbe invalidate];
    os_log(OS_LOG_DEFAULT,"Cliptiq popup end selected=%d",(int)selected);
  }
  void refresh_workspace() {
    if(!workspace)return;
    auto full=options.workspace_model?options.workspace_model():Result<MenuModel>(model);
    if(full)workspace->update(std::move(full.value()));else workspace->status(full.error().message);
  }
  void open_workspace() {
    if(options.workspace_enabled&&!workspace){
      std::weak_ptr<State> weak=shared_from_this();
      workspace=std::make_unique<macos::Workspace>(window,options,[weak](MenuCommand c){if(auto s=weak.lock())if(s->callback)s->callback(std::move(c));},provider);
      refresh_workspace();
    }
    [window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];status.launcher_visible=true;
  }
  void hide_workspace() {
    if(options.workspace_can_close){auto close=options.workspace_can_close();if(!close||!close.value())return;}
    [window orderOut:nil];status.launcher_visible=false;
    std::weak_ptr<State> weak=shared_from_this();
    dispatch_async(dispatch_get_main_queue(), ^{if(auto s=weak.lock())if(!s->window.visible&&s->options.workspace_enabled){s->workspace.reset();s->window.contentView=[NSView new];}});
  }
  void tray_click() {
    if(!options.workspace_enabled){popup();return;}
    if(NSApp.currentEvent.type==NSEventTypeRightMouseUp){
      tray_menu=[[NSMenu alloc] initWithTitle:@"Cliptiq"];
      NSArray *titles=@[@"Открыть Cliptiq",@"Настройки",@"Выйти из Cliptiq"];
      for(NSUInteger i=0;i<titles.count;++i){auto item=[[NSMenuItem alloc] initWithTitle:titles[i] action:@selector(selected:) keyEquivalent:@""];item.target=target;item.tag=i+1;item.representedObject=titles[i];[tray_menu addItem:item];}
      [status_item popUpStatusItemMenu:tray_menu];
    }else open_workspace();
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
    [icon setTemplate:YES];
    status_item.button.image = icon;
    status_item.button.toolTip = @"Cliptiq";
    [status_item.button setAccessibilityLabel:@"Cliptiq"];
    if(options.workspace_enabled||options.transient_popup){status_item.menu=nil;status_item.button.target=target;status_item.button.action=@selector(trayClick:);[status_item.button sendActionOn:NSEventMaskLeftMouseUp|NSEventMaskRightMouseUp];}else status_item.menu=menu;
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
  @autoreleasepool {
  auto s = state_;
  if (!s->thread() || s->window)
    return Error{ErrorCode::InvalidArgument,
                 "AppKit menu requires main thread"};
  [NSApplication sharedApplication];
  // Cocoa text commands need a main-menu key equivalent to reach the current
  // responder. There is no nib/main menu in this accessory application.
  if(!NSApp.mainMenu) {
    auto main=[NSMenu new];
    auto application=[NSMenuItem new];application.submenu=[[NSMenu alloc] initWithTitle:@"Cliptiq"];
    [main addItem:application];
    NSApp.mainMenu=main;
  }
  bool hasEditingMenu=false;
  for(NSMenuItem* item in NSApp.mainMenu.itemArray)
    if([item.identifier isEqualToString:@"cliptiq-edit-menu"]) hasEditingMenu=true;
  if(!hasEditingMenu) {
    auto item=[[NSMenuItem alloc] initWithTitle:loc(@"Edit") action:nil keyEquivalent:@""];
    item.identifier=@"cliptiq-edit-menu";
    auto edit=[[NSMenu alloc] initWithTitle:loc(@"Edit")];
    [edit addItemWithTitle:loc(@"Undo") action:@selector(undo:) keyEquivalent:@"z"];
    auto redo=[edit addItemWithTitle:loc(@"Redo") action:@selector(redo:) keyEquivalent:@"Z"];
    redo.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:loc(@"Cut") action:@selector(cut:) keyEquivalent:@"x"];
    [edit addItemWithTitle:loc(@"Copy") action:@selector(copy:) keyEquivalent:@"c"];
    [edit addItemWithTitle:loc(@"Paste") action:@selector(paste:) keyEquivalent:@"v"];
    [edit addItem:[NSMenuItem separatorItem]];
    [edit addItemWithTitle:loc(@"Select All") action:@selector(selectAll:) keyEquivalent:@"a"];
    item.submenu=edit;[NSApp.mainMenu addItem:item];
  }
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
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
  s->target->reopen=[weak]{if(auto s=weak.lock())s->open_workspace();};
  if(!NSApp.delegate)NSApp.delegate=s->target;
  s->target->tray_click=[weak]{if(auto s=weak.lock())s->tray_click();};
  s->target->close_workspace=[weak]{if(auto s=weak.lock())s->hide_workspace();};
  s->target->popup = [weak] {
    if (auto s = weak.lock())
      s->popup();
  };
  s->window =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, s->options.workspace_enabled?1040:300, s->options.workspace_enabled?680:110)
                                  styleMask:NSWindowStyleMaskTitled |
                                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  s->window.releasedWhenClosed = NO;
  s->window.title = @"Cliptiq";
  s->window.delegate=s->target;
  if(s->options.workspace_enabled)s->window.minSize=NSMakeSize(880,580);
  if(!s->options.workspace_enabled){
  s->button = [[NSButton alloc] initWithFrame:NSMakeRect(24, 32, 250, 40)];
  s->button.title = loc(@"Clipboard History");
  s->button.bezelStyle = NSBezelStyleRounded;
  s->button.target = s->target;
  s->button.action = @selector(open:);
  [s->window.contentView addSubview:s->button];
  }
  auto r = update({});
  s->tray();
  // We pump AppKit manually rather than entering -[NSApplication run].
  // Complete the launch handshake once, as -run normally does, before UI use.
  static dispatch_once_t launched;
  dispatch_once(&launched, ^{[NSApp finishLaunching];});
  if(!s->options.start_hidden||!s->status.tray_available)s->open_workspace();
  return r;
  }
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
  s->next=100;
  s->menu = s->options.transient_popup ? nil : s->build(s->model.items);
  if(!s->options.workspace_enabled)s->status_item.menu = s->menu;
  s->refresh_workspace();
  return {};
}
Result<void> NativeMenu::show(const InvocationContext &c) {
  auto s = state_;
  if (!s->thread() || !s->window)
    return Error{ErrorCode::InvalidArgument, "AppKit menu not initialized"};
  s->popup(c);
  return {};
}
Result<void> NativeMenu::set_options(NativeMenuOptions options) {
  auto s = state_;
  if (!s->thread() || !s->window || unsigned(options.tray_style) > 2 ||
      !options.preview_width || !options.preview_height ||
      options.preview_width > 512 || options.preview_height > 512)
    return Error{ErrorCode::InvalidArgument, "Invalid live menu options"};
  if(s->workspace && s->options.workspace_can_close){
    auto close=s->options.workspace_can_close();if(!close)return close.error();
    if(!close.value())return Error{ErrorCode::Busy,"Snippet draft retained"};
  }
  s->workspace.reset();
  s->options = std::move(options);
  if(s->window.visible&&s->options.workspace_enabled)s->open_workspace();
  auto result = update(s->model);
  if (!result)
    return result;
  return set_tray_style(s->options.tray_style);
}
Result<void> NativeMenu::set_tray_style(TrayStyle style) {
  auto s = state_;
  if (!s->thread() || static_cast<unsigned>(style) > 2)
    return Error{ErrorCode::InvalidArgument, "Invalid tray style"};
  s->options.tray_style = style;
  s->tray();
  if(!s->status.tray_available)s->open_workspace();
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
Result<void> NativeMenu::show_workspace(std::optional<MenuKind> page){auto s=state_;if(!s->thread()||!s->window)return Error{ErrorCode::InvalidArgument,"Workspace requires AppKit owner"};s->open_workspace();if(page&&s->workspace)return s->workspace->select_page(*page);return {};}
void NativeMenu::workspace_status(const std::string &value){auto s=state_;if(s->thread()&&s->workspace)s->workspace->status(value);}
NativeMenuStatus NativeMenu::status() const { return state_->status; }
NativeMenuHandles NativeMenu::native_handles() const {
  return {reinterpret_cast<uintptr_t>((__bridge void *)state_->window),
          reinterpret_cast<uintptr_t>((__bridge void *)state_->menu)};
}
} // namespace cliptiq

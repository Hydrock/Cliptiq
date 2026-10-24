#include "cliptiq/settings.hpp"
#import <AppKit/AppKit.h>
#include <thread>
using namespace cliptiq;
@interface CliptiqShortcutField : NSTextField
@end
@implementation CliptiqShortcutField
@end
@interface CliptiqShortcutEditor : NSTextView
@property(nonatomic, weak) NSTextField *field;
@end
@implementation CliptiqShortcutEditor
- (void)keyDown:(NSEvent *)event {
  auto flags = event.modifierFlags;
  unsigned mods = ((flags & NSEventModifierFlagControl) ? 1u : 0u) |
                  ((flags & NSEventModifierFlagShift) ? 2u : 0u) |
                  ((flags & NSEventModifierFlagOption) ? 4u : 0u) |
                  ((flags & NSEventModifierFlagCommand) ? 8u : 0u);
  if (!mods && event.keyCode == 51) {
    self.string = @"";
    self.field.stringValue = @"";
    return;
  }
  auto characters = event.charactersIgnoringModifiers.uppercaseString;
  std::string key;
  if (characters.length == 1) {
    auto c = [characters characterAtIndex:0];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      key = char(c);
    else if (c >= NSF1FunctionKey && c <= NSF11FunctionKey)
      key = "F" + std::to_string(c - NSF1FunctionKey + 1);
  }
  if (mods && !key.empty()) {
    auto value = format_chord({mods, key});
    if (parse_chord(value)) {
      auto string = [[NSString alloc] initWithBytes:value.data()
                                             length:value.size()
                                           encoding:NSUTF8StringEncoding];
      self.string = string;
      self.field.stringValue = string;
      return;
    }
  }
  [super keyDown:event];
}
@end
@interface CliptiqSettingsActions : NSObject <NSWindowDelegate> {
@public
  std::function<void()> save;
  CliptiqShortcutEditor *editor;
}
- (void)apply:(id)sender;
@end
@implementation CliptiqSettingsActions
- (void)apply:(id)sender {
  (void)sender;
  if (save)
    save();
}
- (id)windowWillReturnFieldEditor:(NSWindow *)window toObject:(id)object {
  (void)window;
  if ([object isKindOfClass:CliptiqShortcutField.class]) {
    if (!editor) {
      editor = [[CliptiqShortcutEditor alloc] initWithFrame:NSZeroRect];
      editor.fieldEditor = YES;
    }
    editor.field = object;
    return editor;
  }
  return nil;
}
@end
namespace cliptiq {
namespace {
NSString *str(const std::string &s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"Invalid UTF-8";
}
} // namespace
struct NativeSettings::State : std::enable_shared_from_this<State> {
  std::thread::id owner = std::this_thread::get_id();
  NSWindow *window = nil;
  NSTextField *message = nil;
  CliptiqSettingsActions *target = nil;
  Settings settings;
  std::function<Result<void>(Settings)> apply;
  std::function<Result<void>(const HotkeyMap &)> authorize;
  struct Control {
    std::string key;
    NSView *view;
    bool boolean, multiline;
  };
  std::vector<Control> controls;
  ~State() {
    if (target)
      target->save = {};
    [window close];
  }
  void save() {
    auto keep = shared_from_this();
    [window makeFirstResponder:nil];
    std::map<std::string, std::string> values;
    for (auto &c : controls) {
      if (c.boolean)
        values[c.key] =
            [(NSButton *)c.view state] == NSControlStateValueOn ? "1" : "0";
      else if (c.multiline)
        values[c.key] = [(NSTextView *)c.view string].UTF8String ?: "";
      else
        values[c.key] = [(NSTextField *)c.view stringValue].UTF8String ?: "";
    }
    auto draft = edit_settings(settings, values);
    if (!draft) {
      message.stringValue = str(draft.error().message);
      return;
    }
    auto result = apply(draft.value());
    if (!result) {
      message.stringValue = str(result.error().message);
      return;
    }
    settings = std::move(draft.value());
    ++settings.revision;
    message.stringValue = @"Saved. Unsupported functions remain disabled.";
  }
};
NativeSettings::NativeSettings() : state_(std::make_shared<State>()) {}
NativeSettings::~NativeSettings() = default;
Result<void>
NativeSettings::show(Settings settings, const Capabilities &caps,
                     std::function<Result<void>(Settings)> callback,
                     std::function<Result<void>(const HotkeyMap &)> authorize) {
  auto s = state_;
  if (s->owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Settings require main thread"};
  auto valid = validate_settings(settings);
  if (!valid)
    return valid;
  [NSApplication sharedApplication];
  if (s->target)
    s->target->save = {};
  [s->window close];
  s->controls.clear();
  s->settings = std::move(settings);
  s->apply = std::move(callback);
  s->authorize = std::move(authorize);
  s->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(120, 120, 900, 700)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  s->window.releasedWhenClosed = NO;
  s->window.title = @"Cliptiq Settings";
  s->target = [CliptiqSettingsActions new];
  s->window.delegate = s->target;
  std::weak_ptr<State> weak = s;
  s->target->save = [weak] {
    if (auto state = weak.lock())
      state->save();
  };
  auto tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(12, 80, 876, 608)];
  tabs.identifier = @"settings-tabs";
  tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [s->window.contentView addSubview:tabs];
  NSArray *names = @[
    @"General", @"Menu", @"Types", @"Excluded", @"Shortcuts", @"Updates",
    @"Beta", @"Donate"
  ];
  NSStackView *stacks[8];
  for (unsigned g = 0; g < 8; ++g) {
    auto item = [[NSTabViewItem alloc] initWithIdentifier:names[g]];
    item.label = names[g];
    auto scroll =
        [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 850, 570)];
    scroll.hasVerticalScroller = YES;
    auto stack = [NSStackView new];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 10;
    stack.edgeInsets = NSEdgeInsetsMake(12, 12, 12, 12);
    stack.frame = NSMakeRect(0, 0, 830, 570);
    scroll.documentView = stack;
    item.view = scroll;
    stacks[g] = stack;
    [tabs addTabViewItem:item];
  }
  for (auto &f : settings_fields(s->settings, caps)) {
    auto row = [NSStackView new];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.spacing = 12;
    auto label = [NSTextField labelWithString:str(f.label)];
    [label.widthAnchor constraintEqualToConstant:430].active = YES;
    [row addArrangedSubview:label];
    NSView *control = nil;
    bool multiline = f.key == "excluded";
    if (f.boolean) {
      auto button = [NSButton checkboxWithTitle:@"" target:nil action:nullptr];
      button.state =
          f.value == "1" ? NSControlStateValueOn : NSControlStateValueOff;
      button.enabled = f.enabled;
      control = button;
    } else if (multiline) {
      auto text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 300, 85)];
      text.string = str(f.value);
      text.editable = f.enabled;
      text.richText = NO;
      control = text;
    } else {
      auto field = f.shortcut ? [CliptiqShortcutField new] : [NSTextField new];
      field.stringValue = str(f.value);
      field.enabled = f.enabled;
      control = field;
    }
    control.identifier = str(f.key);
    control.toolTip = str(f.reason.empty() ? f.label : f.reason);
    [control.widthAnchor constraintEqualToConstant:320].active = YES;
    if (multiline)
      [control.heightAnchor constraintEqualToConstant:85].active = YES;
    [row addArrangedSubview:control];
    [stacks[unsigned(f.group)] addArrangedSubview:row];
    if (!f.reason.empty()) {
      auto reason = [NSTextField wrappingLabelWithString:str(f.reason)];
      [reason.widthAnchor constraintEqualToConstant:780].active = YES;
      [stacks[unsigned(f.group)] addArrangedSubview:reason];
    }
    s->controls.push_back({f.key, control, f.boolean, multiline});
  }
  for (unsigned g = 0; g < 8; ++g) {
    auto size = stacks[g].fittingSize;
    stacks[g].frame =
        NSMakeRect(0, 0, 830, std::max(570.0, double(size.height)));
  }
  auto apply = [NSButton buttonWithTitle:@"Apply"
                                  target:s->target
                                  action:@selector(apply:)];
  apply.identifier = @"apply";
  apply.frame = NSMakeRect(780, 20, 100, 32);
  [s->window.contentView addSubview:apply];
  s->message =
      [NSTextField wrappingLabelWithString:
                       @"Shortcut fields record chords. Backspace clears "
                       @"optional bindings; conflicts retain old settings."];
  s->message.frame = NSMakeRect(16, 12, 740, 52);
  [s->window.contentView addSubview:s->message];
  [s->window makeKeyAndOrderFront:nil];
  return {};
}
Result<void> NativeSettings::process_events() {
  if (state_->owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Settings require main thread"};
  return {};
}
std::uintptr_t NativeSettings::native_handle() const {
  return reinterpret_cast<std::uintptr_t>((__bridge void *)state_->window);
}
bool NativeSettings::confirm_clear() const {
  auto alert = [NSAlert new];
  alert.messageText = @"Clear Cliptiq history?";
  alert.informativeText = @"Snippets and current system clipboard remain.";
  [alert addButtonWithTitle:@"Clear"];
  [alert addButtonWithTitle:@"Cancel"];
  return [alert runModal] == NSAlertFirstButtonReturn;
}
void NativeSettings::status(const std::string &value) {
  if (state_->message)
    state_->message.stringValue = str(value);
}
bool NativeSettings::record_chord(const KeyChord &chord) {
  auto s = state_;
  if (!s->window.isVisible || !s->window.isKeyWindow)
    return false;
  auto responder = s->window.firstResponder;
  NSTextField *field = nil;
  if ([responder isKindOfClass:CliptiqShortcutEditor.class])
    field = [(CliptiqShortcutEditor *)responder field];
  else if ([responder isKindOfClass:CliptiqShortcutField.class])
    field = (NSTextField *)responder;
  if (!field)
    return false;
  auto value = str(format_chord(chord));
  field.stringValue = value;
  if ([responder isKindOfClass:NSTextView.class])
    [(NSTextView *)responder setString:value];
  return true;
}
} // namespace cliptiq

#include "cliptiq/settings.hpp"
#include "../LocaleNative.hpp"
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
  std::function<void()> advanced;
  std::function<void(bool)> preview;
  CliptiqShortcutEditor *editor;
}
- (void)advanced:(id)sender;
- (void)apply:(id)sender;
- (void)preview:(id)sender;
@end
@implementation CliptiqSettingsActions
- (void)advanced:(id)sender { (void)sender;if(advanced)advanced(); }
- (void)preview:(id)sender { if (preview) preview([sender tag] != 0); }
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
using Choices = std::vector<std::pair<std::string, NSString *>>;
Choices choices(const std::string &key) {
  if (key == "sort") return {{"0", loc(@"Copy time")}, {"1", loc(@"Last used")}};
  if (key == "unknown_source") return {{"0", loc(@"Reject when exclusions exist")}, {"1", loc(@"Allow")}, {"2", loc(@"Reject")}};
  if (key.find("modifier") != std::string::npos) return {{"0", loc(@"None")}, {"1", @"Control"}, {"2", @"Shift"}, {"4", @"Option"}, {"8", @"Command"}};
  if (key == "cleanup_seconds") return {{"300", loc(@"5 minutes")}, {"1800", loc(@"30 minutes")}, {"3600", loc(@"1 hour")}, {"14400", loc(@"4 hours")}, {"28800", loc(@"8 hours")}, {"86400", loc(@"1 day")}, {"259200", loc(@"3 days")}, {"604800", loc(@"1 week")}};
  if (key == "updates_interval") return {{"86400", loc(@"Every day")}, {"604800", loc(@"Every week")}, {"2592000", loc(@"Every 30 days")}};
  if (key == "locale") return {{"auto", loc(@"System language")}, {"ru", @"\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}, {"en", @"English"}};
  return {};
}
} // namespace
struct NativeSettings::State : std::enable_shared_from_this<State> {
  std::thread::id owner = std::this_thread::get_id();
  NSWindow *window = nil;
  NSTextField *message = nil;
  CliptiqSettingsActions *target = nil;
  Settings settings;
  std::function<void()> advanced;
  std::function<void(bool, int)> preview;
  NSButton *previewCopy = nil, *previewPaste = nil;
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
    if (target) target->preview = {};
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
      else if ([c.view isKindOfClass:NSPopUpButton.class]) {
        NSString *value = [[(NSPopUpButton *)c.view selectedItem] representedObject];
        values[c.key] = value.UTF8String ?: "";
      }
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
    message.stringValue = loc(@"Saved. Unsupported functions remain disabled.");
  }
};
NativeSettings::NativeSettings() : state_(std::make_shared<State>()) {}
NativeSettings::~NativeSettings() = default;
Result<void>
NativeSettings::show(Settings settings, const Capabilities &caps,
                     std::function<Result<void>(Settings)> callback,
                     std::function<Result<void>(const HotkeyMap &)> authorize) {
  @autoreleasepool {
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
  s->window.title = loc(@"Cliptiq Settings");
  s->window.minSize = NSMakeSize(720, 480);
  s->target = [CliptiqSettingsActions new];
  s->window.delegate = s->target;
  std::weak_ptr<State> weak = s;
  s->target->save = [weak] {
    if (auto state = weak.lock())
      state->save();
  };
  s->target->preview = [weak](bool paste) {
    if (auto state = weak.lock()) {
      [state->window makeFirstResponder:nil];
      for (const auto &c : state->controls) if (c.key == "sound_volume") {
        auto value = [(NSTextField *)c.view stringValue].UTF8String ?: "";
        auto draft = edit_settings(state->settings, {{"sound_volume", value}});
        if (!draft) { state->message.stringValue = str(draft.error().message); return; }
        if (state->preview) state->preview(paste, std::stoi(draft.value().values.at("sound_volume")));
      }
    }
  };
  auto tabs = [[NSTabView alloc] initWithFrame:NSMakeRect(12, 80, 876, 608)];
  tabs.identifier = @"settings-tabs";
  tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [s->window.contentView addSubview:tabs];
  NSArray *names = @[
    loc(@"General"), loc(@"Menu"), loc(@"Types"), loc(@"Excluded"),
    loc(@"Shortcuts"), loc(@"Updates"), loc(@"Beta"), loc(@"About"), loc(@"Sounds")
  ];
  NSStackView *stacks[9];
  for (unsigned g = 0; g < 9; ++g) {
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
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = stack;
    [NSLayoutConstraint activateConstraints:@[
      [stack.leadingAnchor constraintEqualToAnchor:scroll.contentView.leadingAnchor],
      [stack.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor],
      [stack.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]
    ]];
    item.view = scroll;
    stacks[g] = stack;
    [tabs addTabViewItem:item];
  }
  for (auto &f : settings_fields(s->settings, caps)) {
    auto row = [NSStackView new];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.spacing = 12;
    auto label = [NSTextField wrappingLabelWithString:str(f.label)];
    [label.widthAnchor constraintEqualToAnchor:row.widthAnchor multiplier:0.48].active = YES;
    [row addArrangedSubview:label];
    NSView *control = nil;
    bool multiline = f.key == "excluded";
    auto options = choices(f.key);
    bool boolean = f.boolean && options.empty();
    if (boolean) {
      auto button = [NSButton checkboxWithTitle:@"" target:nil action:nullptr];
      button.state =
          f.value == "1" ? NSControlStateValueOn : NSControlStateValueOff;
      button.enabled = f.enabled;
      control = button;
    } else if (!options.empty()) {
      auto popup = [NSPopUpButton new];
      bool matched = false;
      for (const auto &option : options) {
        [popup addItemWithTitle:option.second];
        popup.lastItem.representedObject = str(option.first);
        if (option.first == f.value) { [popup selectItem:popup.lastItem]; matched = true; }
      }
      if (!matched) {
        [popup addItemWithTitle:str(f.value)];
        popup.lastItem.representedObject = str(f.value);
        [popup selectItem:popup.lastItem];
      }
      popup.enabled = f.enabled;
      control = popup;
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
    [control setAccessibilityLabel:str(f.label)];
    control.toolTip = str(f.reason.empty() ? f.label : f.reason);
    [control.widthAnchor constraintEqualToAnchor:row.widthAnchor multiplier:0.48].active = YES;
    if (multiline)
      [control.heightAnchor constraintEqualToConstant:85].active = YES;
    [row addArrangedSubview:control];
    [stacks[unsigned(f.group)] addArrangedSubview:row];
    [row.widthAnchor constraintEqualToAnchor:stacks[unsigned(f.group)].widthAnchor constant:-24].active = YES;
    auto help = [NSTextField wrappingLabelWithString:str(f.description)];
    help.font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    help.textColor = NSColor.secondaryLabelColor;
    [help.widthAnchor constraintEqualToAnchor:row.widthAnchor].active = YES;
    [stacks[unsigned(f.group)] addArrangedSubview:help];
    if (!f.reason.empty()) {
      auto reason = [NSTextField wrappingLabelWithString:str(f.reason)];
      [reason.widthAnchor constraintEqualToAnchor:row.widthAnchor].active = YES;
      [stacks[unsigned(f.group)] addArrangedSubview:reason];
    }
    s->controls.push_back({f.key, control, boolean, multiline});
  }
  auto previews = [NSStackView new];
  previews.orientation = NSUserInterfaceLayoutOrientationVertical;
  for (int n = 0; n < 2; ++n) {
    auto button = [NSButton buttonWithTitle:n ? loc(@"Test paste sound") : loc(@"Test copy sound") target:s->target action:@selector(preview:)];
    button.tag = n;
    button.enabled = bool(s->preview);
    [previews addArrangedSubview:button];
    if (n) s->previewPaste = button; else s->previewCopy = button;
  }
  [stacks[unsigned(SettingsGroup::Sounds)] addArrangedSubview:previews];
  s->target->advanced = [weak] { if(auto s=weak.lock())if(s->advanced)s->advanced(); };
  [stacks[unsigned(SettingsGroup::Beta)] addArrangedSubview:[NSButton buttonWithTitle:loc(@"Advanced LAN settings") target:s->target action:@selector(advanced:)]];
  auto apply = [NSButton buttonWithTitle:loc(@"Apply")
                                  target:s->target
                                  action:@selector(apply:)];
  apply.identifier = @"apply";
  [apply sizeToFit];
  apply.frame = NSMakeRect(884 - apply.frame.size.width, 20, apply.frame.size.width, 32);
  apply.autoresizingMask = NSViewMinXMargin;
  [s->window.contentView addSubview:apply];
  s->message =
      [NSTextField wrappingLabelWithString:loc(@"Shortcut fields record chords. Backspace clears optional bindings; conflicts retain old settings.")];
  s->message.frame = NSMakeRect(16, 12, apply.frame.origin.x - 28, 52);
  s->message.autoresizingMask = NSViewWidthSizable;
  [s->window.contentView addSubview:s->message];
  [s->window makeKeyAndOrderFront:nil];
  return {};
  }
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
  alert.messageText = loc(@"Clear Cliptiq history?");
  alert.informativeText = loc(@"Snippets and current system clipboard remain.");
  [alert addButtonWithTitle:loc(@"Clear")];
  [alert addButtonWithTitle:loc(@"Cancel")];
  return [alert runModal] == NSAlertFirstButtonReturn;
}
void NativeSettings::advanced(std::function<void()> cb) { state_->advanced=std::move(cb); }
void NativeSettings::sound_preview(std::function<void(bool, int)> callback) {
  state_->preview = std::move(callback);
  state_->previewCopy.enabled = bool(state_->preview);
  state_->previewPaste.enabled = bool(state_->preview);
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

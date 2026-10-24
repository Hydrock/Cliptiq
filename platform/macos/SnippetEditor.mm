#include "cliptiq/editor.hpp"
#ifdef CLIPTIQ_EDITOR_XML
#include "../SnippetXml.hpp"
#endif
#import <AppKit/AppKit.h>
#include <algorithm>
#include <thread>
using namespace cliptiq;
@interface CliptiqSnippetNode : NSObject
@property(nonatomic, copy) NSString *itemID, *title;
@property(nonatomic) BOOL folder, enabled;
@property(nonatomic, strong) NSMutableArray<CliptiqSnippetNode *> *children;
@end
@implementation CliptiqSnippetNode
@end
@interface CliptiqSnippetBridge
    : NSObject <NSOutlineViewDataSource, NSOutlineViewDelegate,
                NSTextFieldDelegate, NSTextViewDelegate, NSWindowDelegate> {
@public
  NSArray<CliptiqSnippetNode *> *roots;
  std::function<void(int)> action;
  std::function<void(CliptiqSnippetNode *)> select;
  std::function<void(int, NSString *)> edit;
  std::function<NSPasteboardItem *(CliptiqSnippetNode *)> drag;
  std::function<NSDragOperation(id<NSDraggingInfo>, id, NSInteger, BOOL)> drop;
  std::function<void()> cancel;
  std::function<bool()> can_close;
  NSTextView *shortcutEditor;
}
- (void)addFolder:(id)sender;
- (void)addSnippet:(id)sender;
- (void)remove:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)reload:(id)sender;
- (void)enable:(id)sender;
- (void)bind:(id)sender;
#ifdef CLIPTIQ_EDITOR_XML
- (void)importXML:(id)sender;
- (void)exportXML:(id)sender;
#endif
@end
@interface CliptiqSnippetText : NSTextView
@property(nonatomic, weak) CliptiqSnippetBridge *bridge;
@end
@implementation CliptiqSnippetText
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  if (event.modifierFlags & NSEventModifierFlagCommand) {
    auto key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"z"] || [key isEqualToString:@"y"]) {
      BOOL redo = [key isEqualToString:@"y"] ||
                  (event.modifierFlags & NSEventModifierFlagShift);
      if (redo)
        [self.bridge redo:nil];
      else
        [self.bridge undo:nil];
      return YES;
    }
  }
  return [super performKeyEquivalent:event];
}
@end
@interface CliptiqFolderShortcutField : NSTextField
@end
@implementation CliptiqFolderShortcutField
@end
@interface CliptiqFolderShortcutEditor : NSTextView
@property(nonatomic, weak) NSTextField *field;
@end
@implementation CliptiqFolderShortcutEditor
- (void)keyDown:(NSEvent *)event {
  unsigned mods =
      ((event.modifierFlags & NSEventModifierFlagControl) ? Control : 0) |
      ((event.modifierFlags & NSEventModifierFlagShift) ? Shift : 0) |
      ((event.modifierFlags & NSEventModifierFlagOption) ? Alt : 0) |
      ((event.modifierFlags & NSEventModifierFlagCommand) ? Super : 0);
  if (!mods && event.keyCode == 51) {
    self.string = @"";
    self.field.stringValue = @"";
    return;
  }
  auto text = event.charactersIgnoringModifiers.uppercaseString;
  std::string key;
  if (text.length == 1) {
    auto c = [text characterAtIndex:0];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      key = char(c);
    else if (c >= NSF1FunctionKey && c <= NSF11FunctionKey)
      key = "F" + std::to_string(c - NSF1FunctionKey + 1);
  }
  if (mods && !key.empty()) {
    auto value = format_chord({mods, key});
    if (parse_chord(value)) {
      auto string = [[NSString alloc] initWithUTF8String:value.c_str()];
      self.string = string;
      self.field.stringValue = string;
      return;
    }
  }
  [super keyDown:event];
}
@end
@implementation CliptiqSnippetBridge
- (NSInteger)outlineView:(NSOutlineView *)view numberOfChildrenOfItem:(id)item {
  (void)view;
  return item ? [(CliptiqSnippetNode *)item children].count : roots.count;
}
- (id)outlineView:(NSOutlineView *)view child:(NSInteger)index ofItem:(id)item {
  (void)view;
  return item ? [(CliptiqSnippetNode *)item children][index] : roots[index];
}
- (BOOL)outlineView:(NSOutlineView *)view isItemExpandable:(id)item {
  (void)view;
  return [(CliptiqSnippetNode *)item folder];
}
- (NSView *)outlineView:(NSOutlineView *)view
     viewForTableColumn:(NSTableColumn *)column
                   item:(id)item {
  (void)view;
  (void)column;
  auto node = (CliptiqSnippetNode *)item;
  auto text = [NSTextField
      labelWithString:node.enabled ? node.title
                                   : [@"[disabled] "
                                         stringByAppendingString:node.title]];
  text.lineBreakMode = NSLineBreakByTruncatingTail;
  return text;
}
- (void)outlineViewSelectionDidChange:(NSNotification *)notification {
  auto view = (NSOutlineView *)notification.object;
  auto node = [view itemAtRow:view.selectedRow];
  if (select)
    select(node);
}
- (void)controlTextDidChange:(NSNotification *)notification {
  auto text = (NSTextField *)notification.object;
  if ([text.identifier isEqualToString:@"editor-title"] && edit)
    edit(0, text.stringValue);
}
- (void)textDidChange:(NSNotification *)notification {
  if (edit)
    edit(1, [(NSTextView *)notification.object string]);
}
#ifdef CLIPTIQ_EDITOR_XML
- (void)importXML:(id)sender {
  (void)sender;
  if (action)
    action(513);
}
- (void)exportXML:(id)sender {
  (void)sender;
  if (action)
    action(514);
}
#endif
- (void)addFolder:(id)sender {
  (void)sender;
  if (action)
    action(507);
}
- (void)addSnippet:(id)sender {
  (void)sender;
  if (action)
    action(508);
}
- (void)remove:(id)sender {
  (void)sender;
  if (action)
    action(509);
}
- (void)undo:(id)sender {
  (void)sender;
  if (action)
    action(510);
}
- (void)redo:(id)sender {
  (void)sender;
  if (action)
    action(511);
}
- (void)reload:(id)sender {
  (void)sender;
  if (action)
    action(512);
}
- (void)enable:(id)sender {
  (void)sender;
  if (action)
    action(503);
}
- (void)bind:(id)sender {
  (void)sender;
  if (action)
    action(506);
}
- (BOOL)windowShouldClose:(NSWindow *)window {
  if (can_close && !can_close())
    return NO;
  [window orderOut:nil];
  return NO;
}
- (id)windowWillReturnFieldEditor:(NSWindow *)window toObject:(id)object {
  (void)window;
  if ([object isKindOfClass:CliptiqFolderShortcutField.class]) {
    if (!shortcutEditor) {
      shortcutEditor = [CliptiqFolderShortcutEditor new];
      shortcutEditor.fieldEditor = YES;
    }
    [(CliptiqFolderShortcutEditor *)shortcutEditor setField:object];
    return shortcutEditor;
  }
  return nil;
}
- (id<NSPasteboardWriting>)outlineView:(NSOutlineView *)view
               pasteboardWriterForItem:(id)item {
  (void)view;
  return drag ? drag(item) : nil;
}
- (NSDragOperation)outlineView:(NSOutlineView *)view
                  validateDrop:(id<NSDraggingInfo>)info
                  proposedItem:(id)item
            proposedChildIndex:(NSInteger)index {
  if (!drop || info.draggingSource != view)
    return NSDragOperationNone;
  auto node = (CliptiqSnippetNode *)item;
  if (node && !node.folder)
    return NSDragOperationNone;
  return drop(info, item, index, NO);
}
- (BOOL)outlineView:(NSOutlineView *)view
         acceptDrop:(id<NSDraggingInfo>)info
               item:(id)item
         childIndex:(NSInteger)index {
  if (!drop || info.draggingSource != view)
    return NO;
  return drop(info, item, index, YES) == NSDragOperationMove;
}
- (void)outlineView:(NSOutlineView *)view
    draggingSession:(NSDraggingSession *)session
       endedAtPoint:(NSPoint)point
          operation:(NSDragOperation)operation {
  (void)view;
  (void)session;
  (void)point;
  (void)operation;
  if (cancel)
    cancel();
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
std::string utf8(NSString *s) {
  auto bytes = [s dataUsingEncoding:NSUTF8StringEncoding];
  return bytes
             ? std::string(static_cast<const char *>(bytes.bytes), bytes.length)
             : std::string();
}
} // namespace
struct NativeSnippetEditor::State : std::enable_shared_from_this<State> {
  std::thread::id owner = std::this_thread::get_id();
  NSWindow *window = nil;
  NSOutlineView *tree = nil;
  NSTextField *title = nil, *shortcut = nil, *message = nil;
  CliptiqSnippetText *body = nil;
  NSButton *enabled = nil, *undo = nil, *redo = nil, *addSnippet = nil,
           *bind = nil;
  CliptiqSnippetBridge *bridge = nil;
  std::shared_ptr<SnippetEditorSession> model;
#ifdef CLIPTIQ_EDITOR_XML
  SnippetXmlTransfer xml;
#endif
  bool updating = false;
  std::uint64_t drag = 0;
  std::optional<SnippetItemId> dragged;
  ~State() {
    if (bridge) {
      bridge->action = {};
      bridge->select = {};
      bridge->edit = {};
      bridge->drag = {};
      bridge->drop = {};
      bridge->cancel = {};
      bridge->can_close = {};
    }
    [window close];
  }
  bool transfer_locked() const {
#ifdef CLIPTIQ_EDITOR_XML
    return xml.busy();
#else
    return false;
#endif
  }
  void status(Result<void> r) {
    message.stringValue = r ? str((model->saving() ? std::string("Saving locally...") : "Saved locally. " + model->binding_status()))
                            : str(r.error().message);
  }
#ifdef CLIPTIQ_EDITOR_XML
  void xml_sensitive(bool value) {
    std::function<void(NSView *)> set = [&](NSView *view) {
      if ([view isKindOfClass:[NSControl class]])
        [(NSControl *)view setEnabled:value];
      for (NSView *child in view.subviews)
        set(child);
    };
    set(window.contentView);
    body.editable = value;
  }
  Result<void> start_xml(const std::string &path, bool importing) {
    if (!window || !model)
      return Error{ErrorCode::InvalidArgument, "Open the snippet editor first"};
    auto result = xml.start(*model, path, importing);
    if (result) {
      xml_sensitive(false);
      status(Error{ErrorCode::Busy,
                   importing ? "Importing XML..." : "Exporting XML..."});
    }
    return result;
  }
  bool poll_xml() {
    if (!xml.busy())
      return false;
    auto result = xml.poll(*model);
    if (xml.busy())
      return true;
    xml_sensitive(true);
    if (result) {
      if (xml.importing())
        rebuild();
      else
        detail();
      message.stringValue = str(xml.message());
    } else {
      detail();
      status(result.error());
    }
    return true;
  }
  void xml_picker(bool importing) {
    auto keep = shared_from_this();
    if (xml.busy() || model->has_unsaved()) {
      status(Error{
          ErrorCode::Busy,
          "Save or Reload the draft and finish the current transfer first"});
      return;
    }
    NSSavePanel *panel =
        importing ? [NSOpenPanel openPanel] : [NSSavePanel savePanel];
    panel.allowedFileTypes = @[ @"xml" ];
    panel.allowsOtherFileTypes = NO;
    if (importing) {
      auto open = (NSOpenPanel *)panel;
      open.allowsMultipleSelection = NO;
      open.canChooseDirectories = NO;
    } else
      panel.nameFieldStringValue = @"snippets.xml";
    if ([panel runModal] == NSModalResponseOK && panel.URL)
      status(start_xml(utf8(panel.URL.path), importing));
  }
#endif
  void poll() {
    if(!model || !window) return;
#ifdef CLIPTIQ_EDITOR_XML
    if (poll_xml())
      return;
#endif
    auto result=model->poll_saves();
    if(!result) status(result.error());
    else if(result.value()) {status({});selected_label();}
    history();
  }
  void history() {
    undo.enabled = model->can_undo();
    redo.enabled = model->can_redo();
  }
  void restore_selection() {
    auto selected = model->selected();
    if (!selected) {
      updating=true;[tree deselectAll:nil];updating=false;return;
    }
    auto item = node(*selected);
    auto row = [tree rowForItem:item];
    if (row >= 0) {
      updating = true;
      [tree selectRowIndexes:[NSIndexSet indexSetWithIndex:NSUInteger(row)]
          byExtendingSelection:NO];
      updating = false;
    }
  }
  bool confirm_close() {
#ifdef CLIPTIQ_EDITOR_XML
    if (xml.busy()) {
      status(
          Error{ErrorCode::Busy, "XML transfer pending; wait before closing"});
      return false;
    }
#endif
    if(model && model->saving()) {status(Error{ErrorCode::Busy,"Save pending; wait before closing"});return false;}

    if (!model || !model->has_unsaved())
      return true;
    auto alert = [NSAlert new];
    alert.messageText = @"Discard unsaved draft?";
    alert.informativeText =
        @"Keep editing to correct the error, or discard the local draft.";
    [alert addButtonWithTitle:@"Keep Editing"];
    [alert addButtonWithTitle:@"Discard Draft"];
    if ([alert runModal] != NSAlertSecondButtonReturn)
      return false;
    auto loaded = model->reload();
    status(loaded);
    if (loaded)
      rebuild();
    return bool(loaded);
  }
  void detail() {
    if (transfer_locked())
      return;
    updating = true;
    auto selected = model->selected();
    bool folder = selected && selected->kind == SnippetItemKind::Folder;
    title.enabled = bool(selected);
    title.stringValue = str(model->title());
    enabled.enabled = bool(selected);
    enabled.state =
        model->enabled() ? NSControlStateValueOn : NSControlStateValueOff;
    body.editable = selected && !folder;
    body.string = str(model->content());
    [body.undoManager removeAllActions];
    shortcut.enabled = folder;
    bind.enabled = folder;
    auto chord = model->folder_chord();
    shortcut.stringValue = chord ? str(format_chord(*chord)) : @"";
    addSnippet.enabled = bool(selected);
    history();
    updating = false;
  }
  CliptiqSnippetNode *node(const SnippetItemId &id) {
    for (CliptiqSnippetNode *f in bridge->roots) {
      if (id.kind == SnippetItemKind::Folder &&
          [f.itemID isEqualToString:str(id.value)])
        return f;
      for (CliptiqSnippetNode *s in f.children)
        if (id.kind == SnippetItemKind::Snippet &&
            [s.itemID isEqualToString:str(id.value)])
          return s;
    }
    return nil;
  }
  void selected_label() {
    auto selected = model->selected();
    if (selected) {
      auto item = node(*selected);
      item.title = str(model->title().empty() ? "Untitled" : model->title());
      item.enabled = model->enabled();
      updating = true;
      [tree reloadItem:item];
      updating = false;
    }
    history();
  }
  void rebuild() {
    updating = true;
    auto roots = [NSMutableArray<CliptiqSnippetNode *> new];
    auto parents = [NSMutableDictionary<NSString *, CliptiqSnippetNode *> new];
    for (auto &f : model->folders()) {
      auto n = [CliptiqSnippetNode new];
      n.itemID = str(f.id);
      n.title = str(f.title.empty() ? "Untitled folder" : f.title);
      n.folder = YES;
      n.enabled = f.enabled;
      n.children = [NSMutableArray new];
      [roots addObject:n];
      parents[n.itemID] = n;
    }
    for (auto &s : model->snippets()) {
      auto parent = parents[str(s.folder_id)];
      if (!parent) {
        parent = [CliptiqSnippetNode new];
        parent.itemID = @"";
        parent.title = @"Unfiled legacy items: move to a folder";
        parent.folder = YES;
        parent.children = [NSMutableArray new];
        [roots addObject:parent];
        parents[str(s.folder_id)] = parent;
      }
      auto n = [CliptiqSnippetNode new];
      n.itemID = str(s.id);
      n.title = str(s.title.empty() ? "Untitled snippet" : s.title);
      n.enabled = s.enabled;
      n.children = [NSMutableArray new];
      [parent.children addObject:n];
    }
    bridge->roots = roots;
    [tree reloadData];
    [tree expandItem:nil expandChildren:YES];
    auto selected = model->selected();
    if (selected) {
      auto n = node(*selected);
      auto row = [tree rowForItem:n];
      if (row >= 0) {
        [tree selectRowIndexes:[NSIndexSet indexSetWithIndex:NSUInteger(row)]
            byExtendingSelection:NO];
        [tree scrollRowToVisible:row];
      }
    }
    updating = false;
    detail();
  }
  void perform(int command) {
    if (transfer_locked()) {
      status(Error{ErrorCode::Busy, "XML transfer pending"});
      return;
    }
    auto keep = shared_from_this();
#ifdef CLIPTIQ_EDITOR_XML
    if (command == 513 || command == 514) {
      xml_picker(command == 513);
      return;
    }
#endif
    Result<void> r;
    if (command == 507 || command == 508) {
      auto added = command == 507 ? model->add_folder() : model->add_snippet();
      if (!added)
        r = added.error();
      else {
        rebuild();
        [window makeFirstResponder:command == 507 ? (NSResponder *)title
                                                  : (NSResponder *)body];
      }
    } else if (command == 509) {
      auto alert = [NSAlert new];
      alert.messageText = @"Delete selected item?";
      alert.informativeText = @"A folder also deletes its snippets.";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Delete"];
      bool confirmed = [alert runModal] == NSAlertSecondButtonReturn;
      r = model->erase_selected(confirmed);
      if (r && confirmed)
        rebuild();
    } else if (command == 510 || command == 511) {
      r = command == 510 ? model->undo() : model->redo();
      if (r)
        detail();
    } else if (command == 512) {
      r = model->reload();
      if (r)
        rebuild();
    } else if (command == 503) {
      r = model->enable(enabled.state == NSControlStateValueOn);
      if (r)
        selected_label();
      else
        enabled.state =
            model->enabled() ? NSControlStateValueOn : NSControlStateValueOff;
    } else if (command == 506) {
      auto value = utf8(shortcut.stringValue);
      std::optional<KeyChord> chord;
      if (!value.empty()) {
        auto c = parse_chord(value);
        if (!c) {
          status(c.error());
          return;
        }
        chord = c.value();
      }
      r = model->bind_folder(chord);
    }
    status(r);
  }
};
NativeSnippetEditor::NativeSnippetEditor()
    : state_(std::make_shared<State>()) {}
NativeSnippetEditor::~NativeSnippetEditor() = default;
Result<void>
NativeSnippetEditor::show(std::shared_ptr<SnippetEditorSession> model) {
  auto s = state_;
  if (s->owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Editor requires main thread"};
#ifdef CLIPTIQ_EDITOR_XML
  if (s->xml.busy())
    return Error{ErrorCode::Busy, "XML transfer pending in existing editor"};
#endif
  if (!model)
    return Error{ErrorCode::InvalidArgument, "No editor session"};
  if (s->window && s->model && s->model->has_unsaved())
    return Error{ErrorCode::Busy, "Unsaved draft retained in existing editor"};
  [NSApplication sharedApplication];
  if (s->bridge) {
    s->bridge->action = {};
    s->bridge->select = {};
    s->bridge->edit = {};
    s->bridge->drag = {};
    s->bridge->drop = {};
    s->bridge->cancel = {};
    s->bridge->can_close = {};
  }
  [s->window close];
  s->model = std::move(model);
  auto background=s->model->background_saves();if(!background) return background;
  s->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(100, 100, 900, 650)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  s->window.releasedWhenClosed = NO;
  s->window.title = @"Cliptiq Snippets";
  s->window.minSize = NSMakeSize(700, 400);
  s->bridge = [CliptiqSnippetBridge new];
  s->window.delegate = s->bridge;
  auto root = s->window.contentView;
  auto tools = [[NSStackView alloc] initWithFrame:NSMakeRect(12, 600, 876, 36)];
  tools.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  tools.spacing = 6;
  tools.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [root addSubview:tools];
  auto button = [&](NSString *label, NSString *name, SEL action) {
    auto b = [NSButton buttonWithTitle:label target:s->bridge action:action];
    b.identifier = name;
    [tools addArrangedSubview:b];
    return b;
  };
  button(@"Add Folder", @"editor-add-folder", @selector(addFolder:));
  s->addSnippet =
      button(@"Add Snippet", @"editor-add-snippet", @selector(addSnippet:));
  button(@"Delete…", @"editor-delete", @selector(remove:));
  s->undo = button(@"Undo", @"editor-undo", @selector(undo:));
  s->redo = button(@"Redo", @"editor-redo", @selector(redo:));
  button(@"Reload", @"editor-reload", @selector(reload:));
#ifdef CLIPTIQ_EDITOR_XML
  button(@"Import XML…", @"editor-import", @selector(importXML:));
  button(@"Export XML…", @"editor-export", @selector(exportXML:));
#endif
  auto split = [[NSSplitView alloc] initWithFrame:NSMakeRect(12, 64, 876, 528)];
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:split];
  auto left = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 280, 528)];
  left.hasVerticalScroller = YES;
  s->tree = [[NSOutlineView alloc] initWithFrame:left.bounds];
  s->tree.identifier = @"editor-tree";
  auto column = [[NSTableColumn alloc] initWithIdentifier:@"item"];
  column.width = 260;
  [s->tree addTableColumn:column];
  s->tree.outlineTableColumn = column;
  s->tree.headerView = nil;
  s->tree.delegate = s->bridge;
  s->tree.dataSource = s->bridge;
  left.documentView = s->tree;
  [split addSubview:left];
  auto right = [[NSView alloc] initWithFrame:NSMakeRect(281, 0, 595, 528)];
  [split addSubview:right];
  [split setPosition:280 ofDividerAtIndex:0];
  s->title = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 488, 571, 26)];
  s->title.identifier = @"editor-title";
  s->title.placeholderString = @"Title";
  s->title.accessibilityLabel = @"Title";
  s->title.delegate = s->bridge;
  s->title.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [right addSubview:s->title];
  s->enabled = [NSButton checkboxWithTitle:@"Enabled in menu"
                                    target:s->bridge
                                    action:@selector(enable:)];
  s->enabled.identifier = @"editor-enabled";
  s->enabled.frame = NSMakeRect(12, 454, 571, 24);
  s->enabled.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [right addSubview:s->enabled];
  s->shortcut = [[CliptiqFolderShortcutField alloc]
      initWithFrame:NSMakeRect(12, 418, 441, 26)];
  s->shortcut.identifier = @"editor-shortcut";
  s->shortcut.placeholderString = @"Folder shortcut: press chord, then Bind";
  s->shortcut.accessibilityLabel = @"Folder shortcut";
  s->shortcut.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  [right addSubview:s->shortcut];
  s->bind = [NSButton buttonWithTitle:@"Bind / Clear"
                               target:s->bridge
                               action:@selector(bind:)];
  s->bind.identifier = @"editor-bind";
  s->bind.frame = NSMakeRect(465, 418, 118, 26);
  s->bind.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
  [right addSubview:s->bind];
  auto scroll =
      [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 12, 571, 394)];
  scroll.hasVerticalScroller = YES;
  scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  s->body = [[CliptiqSnippetText alloc] initWithFrame:scroll.bounds];
  s->body.identifier = @"editor-content";
  s->body.bridge = s->bridge;
  s->body.delegate = s->bridge;
  s->body.richText = NO;
  s->body.importsGraphics = NO;
  s->body.allowsUndo = NO;
  s->body.automaticQuoteSubstitutionEnabled = NO;
  s->body.automaticDashSubstitutionEnabled = NO;
  s->body.automaticTextReplacementEnabled = NO;
  s->body.font = [NSFont userFixedPitchFontOfSize:13];
  s->body.verticallyResizable = YES;
  s->body.horizontallyResizable = NO;
  s->body.autoresizingMask = NSViewWidthSizable;
  s->body.textContainer.widthTracksTextView = YES;
  s->body.accessibilityLabel = @"Snippet text";
  scroll.documentView = s->body;
  [right addSubview:scroll];
  s->message = [NSTextField
      wrappingLabelWithString:@"Changes save locally. Undo history resets when "
                              @"you change the selected item."];
  s->message.identifier = @"editor-status";
  s->message.frame = NSMakeRect(12, 12, 876, 44);
  s->message.autoresizingMask = NSViewWidthSizable;
  [root addSubview:s->message];
  std::weak_ptr<State> weak = s;
  s->bridge->can_close = [weak] {
    auto s = weak.lock();
    return !s || s->confirm_close();
  };
  s->bridge->action = [weak](int command) {
    if (auto s = weak.lock())
      s->perform(command);
  };
  s->bridge->select = [weak](CliptiqSnippetNode *n) {
    if (auto s = weak.lock()) {
      if (s->updating)
        return;
      if (s->transfer_locked()) {
        s->restore_selection();
        return;
      }
      std::optional<SnippetItemId> item;
      if (n && n.itemID.length)
        item = SnippetItemId{n.folder ? SnippetItemKind::Folder
                                      : SnippetItemKind::Snippet,
                             utf8(n.itemID)};
      auto r = s->model->select(item);
      s->status(r);
      if (r)
        s->detail();
      else
        s->restore_selection();
    }
  };
  s->bridge->edit = [weak](int field, NSString *value) {
    if (auto s = weak.lock()) {
      if (s->updating || s->transfer_locked())
        return;
      auto r = field ? s->model->queue_content(utf8(value))
                     : s->model->queue_title(utf8(value));
      s->status(r);
      if (r)
        s->selected_label();
    }
  };
  [s->tree registerForDraggedTypes:@[ @"app.cliptiq.snippet-move" ]];
  [s->tree setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
  s->bridge->drag = [weak](CliptiqSnippetNode *n) -> NSPasteboardItem * {
    auto s = weak.lock();
    if (!s || s->transfer_locked() || !n.itemID.length)
      return nil;
    s->dragged = SnippetItemId{n.folder ? SnippetItemKind::Folder
                                        : SnippetItemKind::Snippet,
                               utf8(n.itemID)};
    auto r = s->model->begin_drag(*s->dragged);
    if (!r) {
      s->status(r.error());
      return nil;
    }
    s->drag = r.value();
    auto item = [NSPasteboardItem new];
    [item setString:str(std::to_string(s->drag))
            forType:@"app.cliptiq.snippet-move"];
    return item;
  };
  s->bridge->drop = [weak](id<NSDraggingInfo> info, id target,
                           NSInteger proposed, BOOL commit) -> NSDragOperation {
    auto s = weak.lock();
    if (!s || s->transfer_locked() || !s->drag || !s->dragged ||
        info.draggingSource != s->tree)
      return NSDragOperationNone;
    auto value =
        [info.draggingPasteboard stringForType:@"app.cliptiq.snippet-move"];
    if (![value isEqualToString:str(std::to_string(s->drag))])
      return NSDragOperationNone;
    auto node = (CliptiqSnippetNode *)target;
    FolderId folder;
    std::size_t index = proposed < 0 ? snippet_append : std::size_t(proposed);
    if (s->dragged->kind == SnippetItemKind::Folder) {
      if (node) {
        index = 0;
        for (std::size_t n = 0; n < s->model->folders().size(); ++n)
          if (s->model->folders()[n].id == utf8(node.itemID))
            index = n;
        [s->tree setDropItem:nil dropChildIndex:NSInteger(index)];
      }
      std::size_t source = 0;
      for (std::size_t n = 0; n < s->model->folders().size(); ++n)
        if (s->model->folders()[n].id == s->dragged->value)
          source = n;
      if (index != snippet_append && source < index)
        --index;
    } else {
      if (!node || !node.folder || !node.itemID.length)
        return NSDragOperationNone;
      folder = utf8(node.itemID);
      std::size_t ordinal = 0;
      for (auto &h : s->model->snippets())
        if (h.folder_id == folder) {
          if (h.id == s->dragged->value && index != snippet_append &&
              ordinal < index) {
            --index;
            break;
          }
          ++ordinal;
        }
    }
    if (!commit)
      return NSDragOperationMove;
    auto r = s->model->finish_drag(s->drag, folder, index, true);
    s->drag = 0;
    s->dragged.reset();
    s->status(r);
    if (r)
      s->rebuild();
    return r ? NSDragOperationMove : NSDragOperationNone;
  };
  s->bridge->cancel = [weak] {
    if (auto s = weak.lock()) {
      if (s->drag)
        s->model->finish_drag(s->drag, "", 0, false);
      s->drag = 0;
      s->dragged.reset();
    }
  };
  s->rebuild();
  [s->window makeKeyAndOrderFront:nil];
  return {};
}
Result<void> NativeSnippetEditor::process_events() {
  if (state_->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires main thread"};
  auto keep=state_;keep->poll();
  return {};
}
Result<bool> NativeSnippetEditor::request_close() {
  auto s = state_;
  if (!s->window)
    return true;
  if (!s->confirm_close())
    return false;
  [s->window orderOut:nil];
  return true;
}
std::uintptr_t NativeSnippetEditor::native_handle() const {
  return reinterpret_cast<std::uintptr_t>((__bridge void *)state_->window);
}
bool NativeSnippetEditor::record_chord(const KeyChord &chord) {
  auto s = state_;
  if (s->transfer_locked())
    return false;
  if (!s->window.isVisible || !s->window.isKeyWindow)
    return false;
  auto responder = s->window.firstResponder;
  NSTextField *field = nil;
  if ([responder isKindOfClass:CliptiqFolderShortcutEditor.class])
    field = [(CliptiqFolderShortcutEditor *)responder field];
  else if (responder == s->shortcut)
    field = s->shortcut;
  if (field != s->shortcut || !field.enabled)
    return false;
  field.stringValue = str(format_chord(chord));
  if ([responder isKindOfClass:NSTextView.class])
    [(NSTextView *)responder setString:field.stringValue];
  return true;
}
Result<void> NativeSnippetEditor::import_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (s->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, true);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
Result<void> NativeSnippetEditor::export_xml_path(const std::string &path) {
#ifdef CLIPTIQ_EDITOR_XML
  auto s = state_;
  if (s->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires owner thread"};
  return s->start_xml(path, false);
#else
  (void)path;
  return Error{ErrorCode::Unsupported, "XML disabled in baseline probe"};
#endif
}
bool NativeSnippetEditor::xml_transfer_busy() const {
#ifdef CLIPTIQ_EDITOR_XML
  return state_->xml.busy();
#else
  return false;
#endif
}
} // namespace cliptiq

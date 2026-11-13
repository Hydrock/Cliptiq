#include "../LocaleNative.hpp"
#include "cliptiq/editor.hpp"
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
  bool copy_enabled;
  std::function<void(CliptiqSnippetNode *)> select;
  std::function<void(int, NSString *)> edit;
  std::function<NSPasteboardItem *(CliptiqSnippetNode *)> drag;
  std::function<NSDragOperation(id<NSDraggingInfo>, id, NSInteger, BOOL)> drop;
  std::function<void()> cancel;
  std::function<bool()> can_close;
  NSTextView *shortcutEditor;
  NSOutlineView *outline;
  NSTextField *renaming;
  NSString *originalTitle;
  CliptiqSnippetNode *renamedNode;
  std::function<void(CliptiqSnippetNode *, NSString *)> renameTitle;
}
- (void)rename:(id)sender;
- (void)cancelRename;
- (void)copySnippet:(id)sender;
- (void)addFolder:(id)sender;
- (void)addSnippet:(id)sender;
- (void)remove:(id)sender;
- (void)undo:(id)sender;
- (void)redo:(id)sender;
- (void)reload:(id)sender;
- (void)enable:(id)sender;
- (void)bind:(id)sender;
@end
@interface CliptiqSnippetOutline : NSOutlineView
@property(nonatomic, weak) CliptiqSnippetBridge *bridge;
@end
@implementation CliptiqSnippetOutline
- (void)keyDown:(NSEvent *)event {
  if (event.keyCode == 120) { [self.bridge rename:nil]; return; }
  if(self.bridge && self.bridge->copy_enabled && (event.keyCode==36||event.keyCode==76)){[self.bridge copySnippet:nil];return;}
  [super keyDown:event];
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  if(self.window.firstResponder==self && self.bridge && self.bridge->copy_enabled && (event.modifierFlags&NSEventModifierFlagCommand) && [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"c"]){[self.bridge copySnippet:nil];return YES;}
  return [super performKeyEquivalent:event];
}
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
- (void)copySnippet:(id)sender { (void)sender;if(action)action(515); }
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
  auto cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, column.width, 24)];
  auto text = [NSTextField labelWithString:node.title];
  text.frame = cell.bounds;
  text.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  text.delegate = self;
  text.selectable = YES;
  text.textColor = node.enabled ? NSColor.labelColor : NSColor.secondaryLabelColor;
  text.lineBreakMode = NSLineBreakByTruncatingTail;
  [cell addSubview:text];
  cell.textField = text;
  return cell;
}
- (BOOL)outlineView:(NSOutlineView *)view shouldSelectItem:(id)item {
  (void)item;
  if (renaming) [view.window makeFirstResponder:view];
  return YES;
}
- (void)outlineViewSelectionDidChange:(NSNotification *)notification {
  auto view = (NSOutlineView *)notification.object;
  auto node = [view itemAtRow:view.selectedRow];
  if (select)
    select(node);
}
- (void)rename:(id)sender {
  (void)sender;
  if (renaming || outline.selectedRow < 0) return;
  auto node = (CliptiqSnippetNode *)[outline itemAtRow:outline.selectedRow];
  if (!node.itemID.length) return;
  auto cell = (NSTableCellView *)[outline viewAtColumn:0 row:outline.selectedRow makeIfNecessary:YES];
  renaming = cell.textField;
  originalTitle = node.title;
  renamedNode = node;
  renaming.editable = YES;
  [renaming selectText:nil];
}
- (void)cancelRename {
  if (!renaming) return;
  auto field = renaming;
  auto original = originalTitle;
  // Drop the identity before ending editing, so stale field-editor callbacks
  // cannot write into a rebuilt outline or the newly selected item.
  renaming = nil;
  renamedNode = nil;
  originalTitle = nil;
  auto editor = (NSTextView *)[outline.window fieldEditor:NO forObject:field];
  editor.string = original;
  field.stringValue = original;
  field.editable = NO;
  [outline.window makeFirstResponder:outline];
}
- (void)controlTextDidEndEditing:(NSNotification *)notification {
  auto text = (NSTextField *)notification.object;
  if (text != renaming) return;
  auto value = text.stringValue;
  text.editable = NO;
  renaming = nil;
  if (renameTitle && ![value isEqualToString:originalTitle]) renameTitle(renamedNode, value);
  renamedNode = nil;
  originalTitle = nil;
}
- (BOOL)control:(NSControl *)control textView:(NSTextView *)textView doCommandBySelector:(SEL)selector {
  (void)textView;
  if (control != renaming) return NO;
  if (selector == @selector(cancelOperation:)) {
    textView.string = originalTitle;
    renaming.stringValue = originalTitle;
    [outline.window makeFirstResponder:outline];
    return YES;
  }
  if (selector == @selector(insertNewline:)) {
    [outline.window makeFirstResponder:outline];
    return YES;
  }
  return NO;
}
- (void)textDidChange:(NSNotification *)notification {
  if (edit)
    edit(1, [(NSTextView *)notification.object string]);
}
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
  NSView *panel = nil;
  bool embedded = false;
  NSButton *copyButton = nil;
  std::function<void(const SnippetId &)> copy;
  NSOutlineView *tree = nil;
  NSTextField *shortcut = nil, *message = nil;
  CliptiqSnippetText *body = nil;
  NSButton *enabled = nil, *undo = nil, *redo = nil, *addSnippet = nil,
           *bind = nil;
  CliptiqSnippetBridge *bridge = nil;
  std::shared_ptr<SnippetEditorSession> model;
  std::string window_locale, rendered_structure;
  bool updating = false;
  int pending_command = 0;
  std::uint64_t drag = 0;
  std::optional<SnippetItemId> dragged;
  ~State() {
    if (bridge) {
      bridge->action = {};
      bridge->select = {};
      bridge->edit = {};
      bridge->renameTitle = {};
      bridge->drag = {};
      bridge->drop = {};
      bridge->cancel = {};
      bridge->can_close = {};
    }
    if(embedded)[panel removeFromSuperview];else [window close];
  }
  void status(Result<void> r) {
    message.stringValue =
        r ? str((model->saving()
                     ? std::string(lc("Saving locally..."))
                     : lc("Saved locally. ") + model->binding_status()))
          : str(r.error().message);
  }
  void poll() {
    if(!model || !window) return;
    auto result=model->poll_saves();
    if(!result) { pending_command = 0; status(result.error()); }
    else if(result.value()) {status({});selected_label();}
    history();
    if (pending_command && !model->saving()) {
      auto command = pending_command;
      pending_command = 0;
      if (!model->has_unsaved()) perform(command);
    }
  }
  void history() {
    undo.enabled = model->can_undo();
    redo.enabled = model->can_redo();
    auto selected=model->selected();copyButton.enabled=selected && selected->kind==SnippetItemKind::Snippet;
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
    [window makeFirstResponder:nil];
    if(model && model->saving()) {status(Error{ErrorCode::Busy,"Save pending; wait before closing"});return false;}

    if (!model || !model->has_unsaved())
      return true;
    auto alert = [NSAlert new];
    alert.messageText = loc(@"Discard unsaved draft?");
    alert.informativeText =
        loc(@"Keep editing to correct the error, or discard the local draft.");
    [alert addButtonWithTitle:loc(@"Keep Editing")];
    [alert addButtonWithTitle:loc(@"Discard Draft")];
    if ([alert runModal] != NSAlertSecondButtonReturn)
      return false;
    auto loaded = model->reload();
    status(loaded);
    if (loaded)
      rebuild();
    return bool(loaded);
  }
  void detail() {

    updating = true;
    auto selected = model->selected();
    bool folder = selected && selected->kind == SnippetItemKind::Folder;
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
    if (selected && !bridge->renaming) {
      auto item = node(*selected);
      item.title = str(model->title().empty() ? std::string(lc("Untitled")) : model->title());
      item.enabled = model->enabled();
      updating = true;
      [tree reloadItem:item];
      updating = false;
    }
    history();
  }
  std::string structure() const {
    std::string value;
    auto field=[&](const std::string& text){value+=std::to_string(text.size())+":"+text;};
    for(const auto& folder:model->folders()) {field(folder.id);field(folder.title);field(std::to_string(folder.index));field(folder.enabled?"1":"0");}
    field("snippets");
    for(const auto& snippet:model->snippets()) {field(snippet.id);field(snippet.folder_id);field(snippet.title);field(std::to_string(snippet.index));field(snippet.enabled?"1":"0");}
    return value;
  }
  void rebuild() {
    [bridge cancelRename];
    updating = true;
    auto roots = [NSMutableArray<CliptiqSnippetNode *> new];
    auto parents = [NSMutableDictionary<NSString *, CliptiqSnippetNode *> new];
    for (auto &f : model->folders()) {
      auto n = [CliptiqSnippetNode new];
      n.itemID = str(f.id);
      n.title = str(f.title.empty() ? std::string(lc("Untitled folder")) : f.title);
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
        parent.title = loc(@"Unfiled legacy items: move to a folder");
        parent.folder = YES;
        parent.children = [NSMutableArray new];
        [roots addObject:parent];
        parents[str(s.folder_id)] = parent;
      }
      auto n = [CliptiqSnippetNode new];
      n.itemID = str(s.id);
      n.title = str(s.title.empty() ? std::string(lc("Untitled snippet")) : s.title);
      n.enabled = s.enabled;
      n.children = [NSMutableArray new];
      [parent.children addObject:n];
    }
    rendered_structure=structure();
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

    auto keep = shared_from_this();
    if (bridge->renaming) {
      // Toolbar buttons need not take first-responder status themselves.
      // Commit the field editor while its original node is still selected.
      if (![window makeFirstResponder:tree]) return;
      if (command >= 507 && command <= 512 && model->saving()) {
        // Finish structural actions after asynchronous title persistence.
        pending_command = command;
        status({});
        return;
      }
    }
    Result<void> r;
    if(command==515) {
      if(model->saving()){pending_command=515;status({});return;}
      if(model->has_unsaved()){status(Error{ErrorCode::Busy,"Unsaved draft retained; correct the save error before copying"});return;}
      auto selected=model->selected();
      if(selected && selected->kind==SnippetItemKind::Snippet && copy)copy(selected->value);
      return;
    }
    if (command == 507 || command == 508) {
      auto added = command == 507 ? model->add_folder() : model->add_snippet();
      if (!added)
        r = added.error();
      else {
        rebuild();
        [bridge rename:nil];
      }
    } else if (command == 509) {
      auto alert = [NSAlert new];
      alert.messageText = loc(@"Delete selected item?");
      alert.informativeText = loc(@"A folder also deletes its snippets.");
      [alert addButtonWithTitle:loc(@"Cancel")];
      [alert addButtonWithTitle:loc(@"Delete")];
      bool confirmed = [alert runModal] == NSAlertSecondButtonReturn;
      r = model->erase_selected(confirmed);
      if (r && confirmed)
        rebuild();
    } else if (command == 510 || command == 511) {
      r = command == 510 ? model->undo() : model->redo();
      if (r) {
        detail();
        selected_label();
      }
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
  return show_embedded(std::move(model),0,{});
}
Result<void> NativeSnippetEditor::show_embedded(std::shared_ptr<SnippetEditorSession> model,
    std::uintptr_t parent, std::function<void(const SnippetId &)> copy) {
  @autoreleasepool {
  auto s = state_;
  if (s->owner != std::this_thread::get_id() || !NSThread.isMainThread)
    return Error{ErrorCode::InvalidArgument, "Editor requires main thread"};
  if (!model)
    return Error{ErrorCode::InvalidArgument, "No editor session"};
  if (s->window && s->model && s->model->has_unsaved())
    return Error{ErrorCode::Busy, "Unsaved draft retained in existing editor"};
  [NSApplication sharedApplication];
  // Reuse the outline for an unchanged session/locale. Discarding a populated
  // outline on every opening accumulates AppKit disclosure-button observers.
  auto container=parent?(__bridge NSView *)(void *)parent:nil;
  if(s->panel && s->model==model && s->window_locale==active_locale() && s->embedded==bool(parent)) {
    s->copy=std::move(copy);
    if(parent){s->window=container.window;[container addSubview:s->panel];s->panel.frame=container.bounds;s->panel.hidden=NO;}
    if(s->rendered_structure!=s->structure()) s->rebuild();
    else {s->restore_selection();s->detail();}
    if(!parent)[s->window makeKeyAndOrderFront:nil];
    return {};
  }
  s->window_locale=active_locale();
  if (s->bridge) {
    s->bridge->action = {};
    s->bridge->select = {};
    s->bridge->edit = {};
    s->bridge->renameTitle = {};
    s->bridge->drag = {};
    s->bridge->drop = {};
    s->bridge->cancel = {};
    s->bridge->can_close = {};
  }
  if(s->embedded)[s->panel removeFromSuperview];else [s->window close];
  s->window=nil;s->panel=nil;
  s->model = std::move(model);
  auto background=s->model->background_saves();if(!background) return background;
  s->embedded=bool(parent);s->copy=std::move(copy);s->pending_command=0;
  if(parent)s->window=container.window;
  else s->window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(100, 100, 900, 650)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if(!parent){s->window.releasedWhenClosed = NO;
  s->window.title = loc(@"Cliptiq Snippets");
  s->window.minSize = NSMakeSize(700, 400);}
  s->bridge = [CliptiqSnippetBridge new];s->bridge->copy_enabled=bool(parent);
  if(!parent)s->window.delegate = s->bridge;
  auto root=parent?[[NSView alloc] initWithFrame:NSMakeRect(0,0,900,650)]:s->window.contentView;
  s->panel=root;root.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
  if(parent)[container addSubview:root];
  auto tools = [[NSStackView alloc] initWithFrame:NSMakeRect(12, 600, 876, 36)];
  tools.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  tools.spacing = 6;
  tools.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  auto toolbarScroll = [[NSScrollView alloc] initWithFrame:tools.frame];
  toolbarScroll.autoresizingMask = tools.autoresizingMask;
  toolbarScroll.hasHorizontalScroller = YES;
  toolbarScroll.autohidesScrollers = YES;
  toolbarScroll.drawsBackground = NO;
  tools.autoresizingMask = NSViewNotSizable;
  toolbarScroll.documentView = tools;
  [root addSubview:toolbarScroll];
  auto button = [&](NSString *label, NSString *name, SEL action) {
    auto b = [NSButton buttonWithTitle:label target:s->bridge action:action];
    b.identifier = name;
    [tools addArrangedSubview:b];
    return b;
  };
  button(loc(@"Add Folder"), @"editor-add-folder", @selector(addFolder:));
  s->addSnippet = button(loc(@"Add Snippet"), @"editor-add-snippet",
                         @selector(addSnippet:));
  button(loc(@"Delete…"), @"editor-delete", @selector(remove:));
  s->undo = button(loc(@"Undo"), @"editor-undo", @selector(undo:));
  s->redo = button(loc(@"Redo"), @"editor-redo", @selector(redo:));
  button(loc(@"Rename"), @"editor-rename", @selector(rename:));
  button(loc(@"Reload"), @"editor-reload", @selector(reload:));
  [tools layoutSubtreeIfNeeded];
  tools.frame = NSMakeRect(0, 0, tools.fittingSize.width, 36);
  auto split = [[NSSplitView alloc] initWithFrame:NSMakeRect(12, 64, 876, 528)];
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:split];
  auto left = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 280, 528)];
  left.hasVerticalScroller = YES;
  s->tree = [[CliptiqSnippetOutline alloc] initWithFrame:left.bounds];
  [(CliptiqSnippetOutline *)s->tree setBridge:s->bridge];
  s->bridge->outline = s->tree;
  s->tree.target = s->bridge;
  s->tree.doubleAction = @selector(rename:);
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
  s->enabled = [NSButton checkboxWithTitle:loc(@"Enabled in menu")
                                    target:s->bridge
                                    action:@selector(enable:)];
  s->enabled.identifier = @"editor-enabled";
  s->enabled.frame = NSMakeRect(12, 454, 571, 24);
  s->enabled.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;

  s->shortcut = [[CliptiqFolderShortcutField alloc]
      initWithFrame:NSMakeRect(12, 418, 441, 26)];
  s->shortcut.identifier = @"editor-shortcut";
  s->shortcut.placeholderString =
      loc(@"Folder shortcut: press chord, then Bind");
  s->shortcut.accessibilityLabel = loc(@"Folder shortcut");
  s->shortcut.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;

  s->bind = [NSButton buttonWithTitle:loc(@"Bind / Clear")
                               target:s->bridge
                               action:@selector(bind:)];
  s->bind.identifier = @"editor-bind";
  s->bind.frame = NSMakeRect(465, 418, 118, 26);
  s->bind.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;

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
  s->body.accessibilityLabel = loc(@"Snippet text");
  scroll.documentView = s->body;
  auto inspector = [NSStackView new];
  inspector.orientation = NSUserInterfaceLayoutOrientationVertical;
  inspector.alignment = NSLayoutAttributeLeading;
  inspector.spacing = 10;
  inspector.translatesAutoresizingMaskIntoConstraints = NO;
  [right addSubview:inspector];
  for (NSView *view in @[s->enabled, s->shortcut, s->bind, scroll]) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [inspector addArrangedSubview:view];
    [view.widthAnchor constraintEqualToAnchor:inspector.widthAnchor].active = YES;
  }
  [NSLayoutConstraint activateConstraints:@[
    [inspector.leadingAnchor constraintEqualToAnchor:right.leadingAnchor constant:12],
    [inspector.trailingAnchor constraintEqualToAnchor:right.trailingAnchor constant:-12],
    [inspector.topAnchor constraintEqualToAnchor:right.topAnchor constant:12],
    [inspector.bottomAnchor constraintEqualToAnchor:right.bottomAnchor constant:-12],
    [scroll.heightAnchor constraintGreaterThanOrEqualToConstant:80]
  ]];
  s->copyButton=nil;
  if(parent){
    s->copyButton=[NSButton buttonWithTitle:loc(@"Copy") target:s->bridge action:@selector(copySnippet:)];
    s->copyButton.identifier=@"editor-copy";s->copyButton.translatesAutoresizingMaskIntoConstraints=NO;
    [inspector addArrangedSubview:s->copyButton];
    [s->copyButton.widthAnchor constraintEqualToAnchor:inspector.widthAnchor].active=YES;
  }
  s->message = [NSTextField
      wrappingLabelWithString:loc(@"Changes save locally. Undo history resets when you change the selected item.")];
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
  s->bridge->renameTitle = [weak](CliptiqSnippetNode *node, NSString *value) {
    if (auto state = weak.lock()) {
      auto selected = state->model->selected();
      if (!selected || selected->value != utf8(node.itemID) ||
          (selected->kind == SnippetItemKind::Folder) != bool(node.folder)) return;
      if (![value stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].length) {
        state->status(Error{ErrorCode::InvalidArgument, std::string(lc("Title cannot be empty"))});
        state->selected_label();
        return;
      }
      auto result = state->model->queue_title(utf8(value));
      state->status(result);
      state->selected_label();
    }
  };
  s->bridge->edit = [weak](int field, NSString *value) {
    if (auto s = weak.lock()) {
      if (s->updating)
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
    if (!s || !n.itemID.length)
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
    if (!s || !s->drag || !s->dragged ||
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
  if(parent)root.frame=container.bounds;else [s->window makeKeyAndOrderFront:nil];
  return {};
  }
}
Result<void> NativeSnippetEditor::process_events() {
  if (state_->owner != std::this_thread::get_id())
    return Error{ErrorCode::InvalidArgument, "Editor requires main thread"};
  auto keep=state_;keep->poll();
  return {};
}
Result<bool> NativeSnippetEditor::can_close() {return !state_->window || state_->confirm_close();}
void NativeSnippetEditor::unmount() {
  auto s=state_;if(!s->embedded)return;
  [s->bridge cancelRename];[s->panel removeFromSuperview];s->window=nil;s->copy={};s->pending_command=0;
}
Result<bool> NativeSnippetEditor::request_close() {
  auto s = state_;
  if (!s->window)
    return true;
  if (!s->confirm_close())
    return false;
  if(s->embedded)s->panel.hidden=YES;else [s->window orderOut:nil];
  return true;
}
std::uintptr_t NativeSnippetEditor::native_handle() const {
  return reinterpret_cast<std::uintptr_t>((__bridge void *)state_->window);
}
bool NativeSnippetEditor::record_chord(const KeyChord &chord) {
  auto s = state_;

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
} // namespace cliptiq

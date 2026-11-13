#include "Workspace.hpp"
#include "../LocaleNative.hpp"
#include <algorithm>
#include <cstring>
namespace {
NSString *wsString(const std::string &s) { return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"[Invalid text]"; }
NSTextField *label(NSString *text, CGFloat size) { auto v=[NSTextField labelWithString:text];v.font=[NSFont systemFontOfSize:size];return v; }
NSImage *wsImage(const cliptiq::MenuImage &i) {
 if(!i.width||!i.height||i.width>512||i.height>512||i.rgba.size()!=size_t(i.width)*i.height*4)return nil;
 auto rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:i.width pixelsHigh:i.height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:i.width*4 bitsPerPixel:32];
 if(!rep)return nil;memcpy(rep.bitmapData,i.rgba.data(),i.rgba.size());auto result=[[NSImage alloc] initWithSize:NSMakeSize(i.width,i.height)];[result addRepresentation:rep];return result;
}
}
@interface CliptiqWorkspaceTable : NSTableView
@property(nonatomic,weak) NSButton *actionCopyButton;
@property(nonatomic,weak) NSButton *deleteButton;
@property(nonatomic,weak) NSSearchField *filterField;
@end
@implementation CliptiqWorkspaceTable
- (void)keyDown:(NSEvent *)event {
 if(event.keyCode==36||event.keyCode==76){[self.actionCopyButton performClick:nil];return;}
 if(event.keyCode==51||event.keyCode==117){[self.deleteButton performClick:nil];return;}
 [super keyDown:event];
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
 if(event.modifierFlags & NSEventModifierFlagCommand){
  if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"c"]){[self.actionCopyButton performClick:nil];return YES;}
  if([event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"f"]){[self.window makeFirstResponder:self.filterField];return YES;}
 }
 return [super performKeyEquivalent:event];
}
@end
@interface CliptiqWorkspaceController : NSObject <NSTableViewDataSource,NSTableViewDelegate,NSSearchFieldDelegate> {
@public
 cliptiq::NativeMenuOptions options;
 std::function<void(cliptiq::MenuCommand)> action;
 cliptiq::MenuPreviewProvider provider;
 cliptiq::MenuModel model;
 std::vector<cliptiq::WorkspaceRow> rows;
 std::vector<size_t> visible;
 cliptiq::MenuKind page;
 NSView *root,*snippetPanel;
 NSScrollView *listScroll;
 NSTextField *previewHeading;
 bool editorMounted;
 NSTableView *table;
 NSSearchField *search;
 NSTextField *heading,*summary,*statusLabel;
 NSTextView *preview;
 NSScrollView *textScroll;
 NSImageView *image;
 NSButton *history,*snippets,*copy,*erase,*clear;
 BOOL changing;
}
- (cliptiq::Result<void>)selectPage:(cliptiq::MenuKind)value;
- (void)filter;
- (void)selected;
- (void)clicked:(NSButton *)sender;
@end
@implementation CliptiqWorkspaceController
- (cliptiq::Result<void>)selectPage:(cliptiq::MenuKind)value {
 using namespace cliptiq;
 if(value!=MenuKind::History&&value!=MenuKind::Snippets)return Error{ErrorCode::InvalidArgument,"Invalid workspace page"};
 if(value==MenuKind::Snippets && options.workspace_snippets && !editorMounted){
  __weak CliptiqWorkspaceController *weakSelf=self;
  auto result=options.workspace_snippets(reinterpret_cast<std::uintptr_t>((__bridge void *)snippetPanel),[weakSelf](const std::string& id){
   auto c=weakSelf;if(!c)return;
   auto latest=c->options.workspace_model?c->options.workspace_model():Result<MenuModel>(c->model);
   if(!latest){c->statusLabel.stringValue=wsString(latest.error().message);return;}
   MenuCommand command{MenuAction::SelectSnippet,id,latest.value().revision};command.invocation.copy_only=true;
   auto cb=c->action;if(cb)cb(std::move(command));
  });
  if(!result){statusLabel.stringValue=wsString(result.error().message);return result;}
  editorMounted=true;
 }
 page=value;bool editing=page==MenuKind::Snippets&&editorMounted;
 for(NSView *view in @[summary,search,listScroll,previewHeading,textScroll,image,copy,erase,clear])view.hidden=editing;
 snippetPanel.hidden=!editing;
 history.state=page==MenuKind::History?NSControlStateValueOn:NSControlStateValueOff;
 snippets.state=page==MenuKind::Snippets?NSControlStateValueOn:NSControlStateValueOff;
 if(editing){heading.stringValue=cliptiq::loc(@"Snippets");if(options.workspace_image)options.workspace_image(std::nullopt);image.image=nil;}
 else [self filter];
 return {};
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)view { (void)view;return visible.size(); }
- (NSView *)tableView:(NSTableView *)view viewForTableColumn:(NSTableColumn *)column row:(NSInteger)n {
 (void)view;auto &r=rows[visible[n]];
 NSString *value;
 if([column.identifier isEqualToString:@"content"])value=wsString(r.item.label);
 else if([column.identifier isEqualToString:@"source"])value=r.item.source_device.empty()?cliptiq::loc(@"This device"):wsString(r.item.source_device);
 else if(page==cliptiq::MenuKind::Snippets)value=wsString(r.group);
 else {switch(r.item.kind){case cliptiq::CanonicalKind::Image:value=cliptiq::loc(@"Image");break;case cliptiq::CanonicalKind::Files:value=cliptiq::loc(@"Files");break;case cliptiq::CanonicalKind::Url:value=cliptiq::loc(@"Link");break;case cliptiq::CanonicalKind::Rtf:value=@"RTF";break;case cliptiq::CanonicalKind::Html:value=@"HTML";break;case cliptiq::CanonicalKind::Pdf:value=@"PDF";break;default:value=cliptiq::loc(@"Text");}}
 auto cell=label(value,13);cell.lineBreakMode=NSLineBreakByTruncatingTail;cell.toolTip=wsString(r.item.tooltip);return cell;
}
- (void)tableViewSelectionDidChange:(NSNotification *)note {(void)note;if(!changing)[self selected];}
- (void)controlTextDidChange:(NSNotification *)note {(void)note;[self filter];}
- (void)filter {
 auto n=table.selectedRow;std::string old=(n>=0&&size_t(n)<visible.size())?rows[visible[n]].item.id:"";
 changing=YES;rows=cliptiq::workspace_rows(model,page);visible.clear();NSInteger selected=-1;
 for(size_t i=0;i<rows.size();++i){auto &r=rows[i];auto text=wsString(r.item.label+" "+r.item.tooltip+" "+r.group+" "+r.item.source_device);if(search.stringValue.length&&[text rangeOfString:search.stringValue options:NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch].location==NSNotFound)continue;if(r.item.id==old)selected=visible.size();visible.push_back(i);}
 [table reloadData];if(selected<0&&!visible.empty())selected=0;
 if(selected>=0)[table selectRowIndexes:[NSIndexSet indexSetWithIndex:selected] byExtendingSelection:NO];else [table deselectAll:nil];
 changing=NO;summary.stringValue=[NSString stringWithFormat:cliptiq::loc(@"%lu of %lu entries"),(unsigned long)visible.size(),(unsigned long)rows.size()];heading.stringValue=page==cliptiq::MenuKind::History?cliptiq::loc(@"Clipboard history"):cliptiq::loc(@"Text snippets");
 history.state=page==cliptiq::MenuKind::History?NSControlStateValueOn:NSControlStateValueOff;snippets.state=page==cliptiq::MenuKind::Snippets?NSControlStateValueOn:NSControlStateValueOff;
 clear.enabled=page==cliptiq::MenuKind::History&&!rows.empty();[self selected];
}
- (void)selected {
 auto n=table.selectedRow;auto row=(n>=0&&size_t(n)<visible.size())?&rows[visible[n]]:nullptr;
 copy.enabled=row!=nullptr;erase.enabled=row&&page==cliptiq::MenuKind::History;image.image=nil;image.hidden=YES;textScroll.hidden=NO;
 if(options.workspace_image&&(!row||!row->item.preview))options.workspace_image(std::nullopt);
 if(!row){preview.string=rows.empty()?(page==cliptiq::MenuKind::History?cliptiq::loc(@"History is empty.\n\nCopy text, a link or an image. It will appear here if its type is enabled in settings."):cliptiq::loc(@"Your text snippets will appear here.\n\nOpen the snippet editor to create a folder and your first snippet.")):cliptiq::loc(@"No entries match this filter.");return;}
 cliptiq::MenuCommand command{row->item.action,row->item.id,model.revision};
 auto text=options.workspace_preview?options.workspace_preview(command):cliptiq::Result<std::string>(row->item.tooltip);
 preview.string=wsString(text?(text.value().empty()?cliptiq::tr("This entry has no text preview. Copy preserves its available formats."):text.value()):text.error().message);[preview scrollRangeToVisible:NSMakeRange(0,0)];
 if(row->item.preview&&(options.workspace_image||provider)){auto pixels=options.workspace_image?options.workspace_image(row->item.preview):provider(*row->item.preview);if(pixels){image.image=wsImage(*pixels);if(image.image){image.hidden=NO;textScroll.hidden=YES;}}}
}
- (void)clicked:(NSButton *)sender {
 using namespace cliptiq;
 if(sender.tag==1||sender.tag==2){[self selectPage:sender.tag==1?MenuKind::History:MenuKind::Snippets];return;}
 if(sender.tag==5||sender.tag==6){auto n=table.selectedRow;if(n<0||size_t(n)>=visible.size()||(sender.tag==6&&page!=MenuKind::History))return;
 if(sender.tag==6){auto alert=[NSAlert new];alert.messageText=cliptiq::loc(@"Delete this history entry?");[alert addButtonWithTitle:cliptiq::loc(@"Cancel")];[alert addButtonWithTitle:cliptiq::loc(@"Delete")];if([alert runModal]!=NSAlertSecondButtonReturn)return;}
 auto &row=rows[visible[n]];MenuCommand command{row.item.action,row.item.id,model.revision};command.invocation.copy_only=true;command.invocation.delete_only=sender.tag==6;auto cb=action;if(cb)cb(std::move(command));return;}
 MenuAction a=sender.tag==3?MenuAction::OpenSettings:sender.tag==4?MenuAction::OpenEditor:sender.tag==7?MenuAction::ClearHistory:MenuAction::Quit;auto cb=action;if(cb)cb({a,"",model.revision});
}
@end
namespace cliptiq::macos {
struct Workspace::Impl { CliptiqWorkspaceController *controller; };
Workspace::Workspace(NSWindow *window,const NativeMenuOptions &o,std::function<void(MenuCommand)> cb,MenuPreviewProvider p):impl_(std::make_unique<Impl>()){
 auto c=[CliptiqWorkspaceController new];impl_->controller=c;c->options=o;c->action=std::move(cb);c->provider=std::move(p);c->page=MenuKind::History;
 auto root=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1040,680)];root.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;c->root=root;window.contentView=root;
 auto add=[&](NSView *v,CGFloat x,CGFloat y,CGFloat w,CGFloat h,NSAutoresizingMaskOptions mask){v.frame=NSMakeRect(x,y,w,h);v.autoresizingMask=mask;[root addSubview:v];};
 auto sidebar=[[NSBox alloc] init];sidebar.boxType=NSBoxCustom;sidebar.borderType=NSNoBorder;sidebar.fillColor=[NSColor colorWithCalibratedWhite:0.96 alpha:1];add(sidebar,0,0,190,584,NSViewHeightSizable|NSViewMaxXMargin);
 auto button=[&](NSString *title,int tag,CGFloat x,CGFloat y,CGFloat w){auto b=[NSButton buttonWithTitle:title target:c action:@selector(clicked:)];b.tag=tag;add(b,x,y,w,34,(tag==5||tag==6?NSViewWidthSizable:NSViewMaxXMargin)|(tag<=2?NSViewMinYMargin:NSViewMaxYMargin));return b;};
 add(label(@"Cliptiq",28),24,622,400,40,NSViewMinYMargin);add(label(cliptiq::loc(@"Everything you copied, within reach"),13),25,595,800,24,NSViewMinYMargin);
 c->history=button(cliptiq::loc(@"History"),1,14,516,164);c->snippets=button(cliptiq::loc(@"Snippets"),2,14,468,164);[c->history setButtonType:NSButtonTypePushOnPushOff];[c->snippets setButtonType:NSButtonTypePushOnPushOff];
 button(cliptiq::loc(@"Settings"),3,14,112,164);button(cliptiq::loc(@"Quit Cliptiq"),8,14,28,164);
 c->heading=label(cliptiq::loc(@"Clipboard history"),18);add(c->heading,212,538,410,28,NSViewMinYMargin);c->summary=label(@"",12);add(c->summary,212,511,410,22,NSViewMinYMargin);
 c->search=[[NSSearchField alloc] init];c->search.placeholderString=cliptiq::loc(@"Filter by title and list text");c->search.delegate=c;add(c->search,212,476,410,28,NSViewMinYMargin);
 c->table=[[CliptiqWorkspaceTable alloc] init];c->table.delegate=c;c->table.dataSource=c;c->table.rowHeight=34;c->table.allowsMultipleSelection=NO;c->table.target=c;c->table.doubleAction=@selector(clicked:);
 // A separate tagged target preserves the same copy command on double click.
 c->copy=button(cliptiq::loc(@"Copy"),5,646,86,370);c->table.target=c->copy;c->table.doubleAction=@selector(performClick:);
 auto col=[[NSTableColumn alloc] initWithIdentifier:@"content"];col.title=cliptiq::loc(@"Content");col.width=200;[c->table addTableColumn:col];col=[[NSTableColumn alloc] initWithIdentifier:@"kind"];col.title=cliptiq::loc(@"Type / folder");col.width=110;[c->table addTableColumn:col];col=[[NSTableColumn alloc] initWithIdentifier:@"source"];col.title=cliptiq::loc(@"Source");col.width=100;[c->table addTableColumn:col];
 auto scroll=[[NSScrollView alloc] init];c->listScroll=scroll;scroll.documentView=c->table;scroll.hasVerticalScroller=YES;scroll.hasHorizontalScroller=YES;scroll.borderType=NSBezelBorder;add(scroll,212,152,410,312,NSViewHeightSizable|NSViewMaxXMargin);
 c->previewHeading=label(cliptiq::loc(@"PREVIEW"),12);add(c->previewHeading,646,540,370,24,NSViewWidthSizable|NSViewMinYMargin);
 c->preview=[[NSTextView alloc] initWithFrame:NSMakeRect(0,0,370,370)];c->preview.editable=NO;c->preview.selectable=YES;c->preview.richText=NO;c->preview.font=[NSFont systemFontOfSize:14];c->preview.textContainerInset=NSMakeSize(10,10);c->preview.autoresizingMask=NSViewWidthSizable;c->preview.textContainer.widthTracksTextView=YES;
 c->textScroll=[[NSScrollView alloc] init];c->textScroll.documentView=c->preview;c->textScroll.hasVerticalScroller=YES;add(c->textScroll,646,152,370,378,NSViewHeightSizable|NSViewWidthSizable);
 c->image=[[NSImageView alloc] init];c->image.imageScaling=NSImageScaleProportionallyUpOrDown;add(c->image,646,152,370,378,NSViewHeightSizable|NSViewWidthSizable);
 c->erase=button(cliptiq::loc(@"Delete entry"),6,646,42,370);c->clear=button(cliptiq::loc(@"Clear history…"),7,212,42,410);
 auto keys=(CliptiqWorkspaceTable *)c->table;keys.actionCopyButton=c->copy;keys.deleteButton=c->erase;keys.filterField=c->search;
 c->snippetPanel=[[NSView alloc] init];c->snippetPanel.identifier=@"workspace-snippets-editor";
 add(c->snippetPanel,212,40,804,490,NSViewHeightSizable|NSViewWidthSizable);c->snippetPanel.hidden=YES;
 c->statusLabel=label(wsString(o.workspace_status),12);add(c->statusLabel,212,8,804,24,NSViewWidthSizable|NSViewMaxYMargin);
 c->table.identifier=@"workspace-list";c->search.identifier=@"workspace-filter";c->preview.identifier=@"workspace-preview";c->image.identifier=@"workspace-image";c->statusLabel.identifier=@"workspace-status";
 [c filter];
}
Workspace::~Workspace(){auto c=impl_->controller;if(c->editorMounted&&c->options.workspace_snippets)c->options.workspace_snippets(0,{});c->action={};c->table.delegate=nil;c->table.dataSource=nil;c->search.delegate=nil;if(c->options.workspace_image)c->options.workspace_image(std::nullopt);[c->root removeFromSuperview];}
void Workspace::update(MenuModel model){auto c=impl_->controller;c->model=std::move(model);if(c->page!=MenuKind::Snippets||!c->editorMounted)[c filter];}
Result<void> Workspace::select_page(MenuKind page){return [impl_->controller selectPage:page];}
void Workspace::status(const std::string &s){impl_->controller->statusLabel.stringValue=wsString(s);}
}

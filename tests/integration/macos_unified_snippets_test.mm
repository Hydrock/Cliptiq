#include "cliptiq/editor.hpp"
#include "cliptiq/native_menu.hpp"
#include "cliptiq/locale.hpp"
#import <AppKit/AppKit.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(r.value());}
NSView* find(NSView* view,NSString* id){if([view.identifier isEqualToString:id])return view;for(NSView* child in view.subviews)if(auto v=find(child,id))return v;return nil;}
// Observe responder-chain editing without touching the user's system clipboard.
@interface EditingShortcutProbe : NSTextView
@property(nonatomic,copy) NSString* copied;
@end
@implementation EditingShortcutProbe
- (void)copy:(id)sender {self.copied=[self.string substringWithRange:self.selectedRange];}
- (void)cut:(id)sender {[self copy:sender];[self insertText:@"" replacementRange:self.selectedRange];}
- (void)paste:(id)sender {[self insertText:@"private fixture" replacementRange:self.selectedRange];}
@end
int main(int argc,char** argv){@autoreleasepool{try{
 activate_locale(argc>1&&std::string(argv[1])=="--ru"?"ru":"en");
 auto path=std::filesystem::temp_directory_path()/("cliptiq-unified-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db");
 struct Temp{std::filesystem::path p;~Temp(){std::error_code e;std::filesystem::remove(p,e);}} temp{path};
 auto store=take(open_store(path.u8string()));auto folder=take(create_folder(store,"Folder"));Snippet doc;doc.folder_id=folder;doc.title="Snippet";doc.content="Before";doc.enabled=false;auto id=take(save_snippet(store,doc));
 auto session=std::make_shared<SnippetEditorSession>(store);CHECK(session->reload());CHECK(session->select(SnippetItemId{SnippetItemKind::Snippet,id}));
 NativeSnippetEditor editor;unsigned copies=0,mounts=0;
 NativeMenuOptions options;options.workspace_enabled=true;options.start_hidden=true;
 options.workspace_snippets=[&](std::uintptr_t parent,std::function<void(const std::string&)>){if(!parent){editor.unmount();return Result<void>{};}++mounts;return editor.show_embedded(session,parent,[&](const SnippetId& copied){CHECK(copied==id);CHECK(session->content()=="After");++copies;});};
 options.workspace_can_close=[&]{return editor.can_close();};
 NativeMenu ui(options,[](MenuCommand){});CHECK(ui.initialize());CHECK(ui.show_workspace(MenuKind::Snippets));
 auto window=(__bridge NSWindow*)(void*)ui.native_handles().window;
 auto outline=find(window.contentView,@"editor-tree");auto body=(NSTextView*)find(window.contentView,@"editor-content");CHECK(outline&&body.editable);CHECK(body.window==window);CHECK((__bridge NSWindow*)(void*)editor.native_handle()==window);
 body.string=@"After";[body.delegate textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification object:body]];
 [(NSButton*)find(window.contentView,@"editor-copy") performClick:nil];
 for(int n=0;n<200&&!copies;++n){CHECK(ui.process_events(1));CHECK(editor.process_events());std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(copies==1);
 CHECK(ui.show_workspace(MenuKind::History));CHECK(find(window.contentView,@"workspace-snippets-editor").hidden);
 CHECK(ui.show_workspace(MenuKind::Snippets));CHECK(mounts==1&&find(window.contentView,@"editor-tree")==outline);
 auto commandCopy=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:window.windowNumber context:nil characters:@"c" charactersIgnoringModifiers:@"c" isARepeat:NO keyCode:8];
 CHECK([window makeFirstResponder:body]);
 CHECK([NSApp targetForAction:@selector(copy:)]==body);
 CHECK([NSApp targetForAction:@selector(paste:)]==body);
 auto probe=[[EditingShortcutProbe alloc] initWithFrame:NSMakeRect(0,0,150,50)];probe.string=@"Selected text";[window.contentView addSubview:probe];CHECK([window makeFirstResponder:probe]);
 auto shortcut=[&](NSString* key,unsigned short code,NSEventModifierFlags flags=NSEventModifierFlagCommand){auto event=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags timestamp:0 windowNumber:window.windowNumber context:nil characters:((flags&NSEventModifierFlagShift)?key.uppercaseString:key) charactersIgnoringModifiers:((flags&NSEventModifierFlagShift)?key.uppercaseString:key) isARepeat:NO keyCode:code];[NSApp.mainMenu update];CHECK([NSApp.mainMenu performKeyEquivalent:event]);};
 shortcut(@"a",0);CHECK(probe.selectedRange.length==probe.string.length);
 shortcut(@"c",8);CHECK([probe.copied isEqualToString:@"Selected text"]);
 shortcut(@"v",9);CHECK([probe.string isEqualToString:@"private fixture"]);
 shortcut(@"a",0);shortcut(@"x",7);CHECK([probe.copied isEqualToString:@"private fixture"]&&probe.string.length==0);
 [probe removeFromSuperview];CHECK([window makeFirstResponder:body]);CHECK(copies==1);

 CHECK(![(NSOutlineView*)outline performKeyEquivalent:commandCopy]);CHECK(copies==1);
 CHECK([window makeFirstResponder:outline]);
 CHECK([(NSOutlineView*)outline performKeyEquivalent:commandCopy]);CHECK(copies==2);
 auto enter=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil characters:@"\r" charactersIgnoringModifiers:@"\r" isARepeat:NO keyCode:36];
 [(NSOutlineView*)outline keyDown:enter];CHECK(copies==3);
 CHECK(ui.update({}));CHECK(find(window.contentView,@"editor-tree")==outline);
 CHECK([window makeFirstResponder:body]);
 CHECK(session->can_undo());shortcut(@"z",6);
 CHECK(session->content()=="Before"&&[body.string isEqualToString:@"Before"]);
 auto settleSave=[&]{for(unsigned n=0;n<200&&session->saving();++n){CHECK(editor.process_events());CHECK(ui.process_events(1));std::this_thread::sleep_for(std::chrono::milliseconds(5));}CHECK(!session->saving());};
 settleSave();CHECK(session->can_redo());CHECK(window.firstResponder==body);CHECK([NSApp targetForAction:@selector(redo:)]==body);shortcut(@"z",6,NSEventModifierFlagCommand|NSEventModifierFlagShift);settleSave();CHECK(session->content()=="After");
 // Undo typed text even while its asynchronous save has not completed.
 [body setSelectedRange:NSMakeRange(body.string.length,0)];[body insertText:@" extra" replacementRange:body.selectedRange];
 CHECK([body.string isEqualToString:@"After extra"]&&session->saving());shortcut(@"z",6);settleSave();CHECK(session->content()=="After");
 auto canUndo=session->can_undo();[window performClose:nil];CHECK(!window.visible);CHECK(ui.process_events(1));
 CHECK(ui.show_workspace(MenuKind::Snippets));CHECK(find(window.contentView,@"editor-tree")==outline);CHECK(mounts==2);CHECK(session->can_undo()==canUndo);
 std::cout<<"PASS unified editor, current copy and cached outline across close/reopen\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}}

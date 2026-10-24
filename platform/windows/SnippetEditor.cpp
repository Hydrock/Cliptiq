#include "cliptiq/editor.hpp"
namespace cliptiq {
struct NativeSnippetEditor::State {};
NativeSnippetEditor::NativeSnippetEditor():state_(std::make_shared<State>()){}
NativeSnippetEditor::~NativeSnippetEditor()=default;
Result<void> NativeSnippetEditor::show(std::shared_ptr<SnippetEditorSession>){return Error{ErrorCode::Unsupported,"T10 native editor pending"};}
Result<void> NativeSnippetEditor::process_events(){return {};}
std::uintptr_t NativeSnippetEditor::native_handle()const{return 0;}
bool NativeSnippetEditor::record_chord(const KeyChord&){return false;}
}

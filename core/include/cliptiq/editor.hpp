#pragma once
#include "hotkeys.hpp"
#include "snippets.hpp"
#include <functional>
#include <memory>
namespace cliptiq {
struct SnippetEditorCallbacks {
  std::function<void()> changed;
  std::function<Result<void>(const FolderId &, std::optional<KeyChord>)>
      bind_folder;
  std::function<std::optional<KeyChord>(const FolderId &)> folder_chord;
  std::function<std::string(const FolderId &)> binding_status;
};
class SnippetEditorSession {
  struct State;
  std::unique_ptr<State> state_;

public:
  explicit SnippetEditorSession(Store &, SnippetEditorCallbacks = {});
  ~SnippetEditorSession();
  Result<void> reload();
  const std::vector<Folder> &folders() const;
  const std::vector<SnippetInfo> &snippets() const;
  std::optional<SnippetItemId> selected() const;
  std::string title() const;
  std::string content() const;
  bool enabled() const;
  Result<void> select(std::optional<SnippetItemId>);
  Result<FolderId> add_folder();
  Result<SnippetId> add_snippet();
  Result<void> edit_title(const std::string &);
  Result<void> edit_content(const std::string &);
  Result<void> enable(bool);
  Result<void> erase_selected(bool confirmed);
  bool can_undo() const;
  bool can_redo() const;
  Result<void> undo();
  Result<void> redo();
  Result<std::uint64_t> begin_drag(const SnippetItemId &);
  Result<void> finish_drag(std::uint64_t token, const FolderId &target,
                           std::size_t index, bool committed);
  Result<void> bind_folder(std::optional<KeyChord>);
  std::optional<KeyChord> folder_chord() const;
  std::string binding_status() const;
};
class NativeSnippetEditor {
  struct State;
  std::shared_ptr<State> state_;

public:
  NativeSnippetEditor();
  ~NativeSnippetEditor();
  Result<void> show(std::shared_ptr<SnippetEditorSession>);
  Result<void> process_events();
  std::uintptr_t native_handle() const;
  bool record_chord(const KeyChord &);
};
} // namespace cliptiq

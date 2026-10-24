#pragma once
#include "storage.hpp"
#include <limits>
namespace cliptiq {
constexpr std::size_t snippet_append = std::numeric_limits<std::size_t>::max();
enum class SnippetItemKind { Folder, Snippet };
struct SnippetItemId {
  SnippetItemKind kind;
  std::string value;
};
struct SnippetItemVersion {
  SnippetItemId item;
  std::uint64_t revision;
};
struct Folder {
  FolderId id;
  std::string title;
  std::size_t index = snippet_append;
  bool enabled = true;
  std::uint64_t revision = 0;
};
struct SnippetInfo {
  SnippetId id;
  FolderId folder_id;
  std::string title;
  std::size_t index = snippet_append;
  bool enabled = true;
  std::uint64_t revision = 0;
};
struct Snippet : SnippetInfo {
  std::string content;
};
Result<std::vector<Folder>> list_folders(Store &);
Result<std::vector<SnippetInfo>> list_snippet_headers(Store &);
Result<std::vector<Snippet>> list_snippets(Store &, const FolderId &);
Result<Snippet> get_snippet(Store &, const SnippetId &);
Result<FolderId> create_folder(Store &, const std::string &title);
Result<void> save_folder(Store &, const Folder &);
Result<SnippetId> save_snippet(Store &, const Snippet &);
Result<void> move_snippet(Store &, const SnippetId &, const FolderId &,
                          std::size_t index,
                          std::optional<std::uint64_t> expected_revision = {});
Result<void> reorder_folders(Store &, const std::vector<FolderId> &,
                             std::optional<SnippetItemVersion> expected = {});
Result<void> set_enabled(Store &, const SnippetItemId &, bool);
Result<void> erase_snippet_item(Store &, const SnippetItemId &);
} // namespace cliptiq

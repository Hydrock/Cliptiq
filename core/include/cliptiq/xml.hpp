#pragma once
#include "snippets.hpp"
namespace cliptiq {
struct ImportedSnippet {
  std::string title, content;
};
struct ImportedFolder {
  std::string title;
  std::vector<ImportedSnippet> snippets;
};
struct ImportLimits {
  std::size_t file_bytes = 64 * 1024 * 1024, folders = 1000, snippets = 10000,
              title_bytes = 1024, text_bytes = 1024 * 1024,
              total_text_bytes = 16 * 1024 * 1024;
};
struct ImportSummary {
  std::size_t folders = 0, snippets = 0;
};
Result<std::vector<ImportedFolder>> decode_clipy_xml(const Bytes &,
                                                     ImportLimits = {});
Result<ImportSummary> append_import(Store &,
                                    const std::vector<ImportedFolder> &);
Result<Bytes> encode_clipy_xml(Store &);
Result<Bytes> read_snippet_xml(const std::string &path,
                               std::size_t limit = 64 * 1024 * 1024);
Result<void> write_snippet_xml_atomic(const std::string &path, const Bytes &);
} // namespace cliptiq

#pragma once
#include "storage.hpp"
namespace cliptiq {
struct IndexHit {
  std::string id;
  bool snippet = false;
};
Result<std::string> prepare_index_namespace(Store &);
Result<void> invalidate_index(const std::string &index_path);
Result<void> rebuild_index(Store &, const std::string &index_path,
                           std::uint64_t source_revision);
Result<std::vector<IndexHit>> search_index(Store &,
                                           const std::string &index_path,
                                           const std::string &phrase,
                                           std::size_t limit = 1000);
} // namespace cliptiq

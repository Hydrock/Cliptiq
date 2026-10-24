#pragma once
#include "db.hpp"
#include <filesystem>
namespace cliptiq::detail {
class IndexGate {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  IndexGate(const std::string &path, const std::string &owner = {},
            bool create = false);
  ~IndexGate();
  std::filesystem::path file() const;
  void clear();
};
std::string companion_path(sqlite3 *);
void invalidate_companion(sqlite3 *);
} // namespace cliptiq::detail

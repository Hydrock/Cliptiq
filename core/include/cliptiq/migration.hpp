#pragma once
#include "storage.hpp"
namespace cliptiq {
struct ImportOptions {
  bool apply_settings = false;
};
struct ImportSummary {
  std::size_t history = 0, folders = 0, snippets = 0, settings = 0;
  bool already_imported = false;
};
// Optional offline migration library; never linked into the application.
struct MigrationSetting {
  std::string key;
  Bytes raw;
  std::optional<std::string> apply_value;
};
Result<void>
export_clipy_sqlite(const std::string &source, const std::string &interchange,
                    const std::vector<MigrationSetting> &settings = {});
Result<ImportSummary> import_clipy(Store &, const std::string &interchange,
                                   ImportOptions = {});
} // namespace cliptiq

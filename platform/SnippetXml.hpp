#pragma once
#include "cliptiq/editor.hpp"
#include "cliptiq/xml.hpp"
#include <chrono>
#include <future>
namespace cliptiq {
class SnippetXmlTransfer {
  std::future<Result<ImportSummary>> pending_;
  bool importing_ = false;
  std::string message_;

public:
  bool busy() const { return pending_.valid(); }
  bool importing() const { return importing_; }
  const std::string &message() const { return message_; }
  Result<void> start(SnippetEditorSession &model, const std::string &path,
                     bool importing) {
    if (busy() || model.has_unsaved())
      return Error{
          ErrorCode::Busy,
          "Save or Reload the draft and finish the current transfer first"};
    auto database = model.database_path();
    if (database.empty())
      return Error{ErrorCode::Unsupported,
                   "XML transfer requires a file-backed store"};
    auto limits = model.store_limits();
    importing_ = importing;
    try {
      pending_ = std::async(
          std::launch::async,
          [database, limits, path, importing]() -> Result<ImportSummary> {
            auto opened = open_store(database, limits);
            if (!opened)
              return opened.error();
            auto &store = opened.value();
            if (importing) {
              auto wire = read_snippet_xml(path);
              if (!wire)
                return wire.error();
              auto decoded = decode_clipy_xml(wire.value());
              if (!decoded)
                return decoded.error();
              return append_import(store, decoded.value());
            }
            auto wire = encode_clipy_xml(store);
            if (!wire)
              return wire.error();
            auto written = write_snippet_xml_atomic(path, wire.value());
            if (!written)
              return written.error();
            return ImportSummary{};
          });
      return {};
    } catch (const std::exception &e) {
      return Error{ErrorCode::Internal, e.what()};
    }
  }
  Result<bool> poll(SnippetEditorSession &model) {
    if (!busy() || pending_.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
      return false;
    try {
      auto result = pending_.get();
      if (!result)
        return result.error();
      if (importing_) {
        auto refreshed = model.refresh_external();
        if (!refreshed)
          return Error{refreshed.error().code,
                       "Import committed; refresh failed. Reload the editor, "
                       "do not repeat import: " +
                           refreshed.error().message};
        message_ = "Imported " + std::to_string(result.value().folders) +
                   " folders and " + std::to_string(result.value().snippets) +
                   " snippets.";
      } else
        message_ = "XML exported successfully.";
      return true;
    } catch (const std::exception &e) {
      return Error{ErrorCode::Internal, e.what()};
    }
  }
};
} // namespace cliptiq

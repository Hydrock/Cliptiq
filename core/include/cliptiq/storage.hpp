#pragma once
#include "types.hpp"
#include <memory>
struct sqlite3;
namespace cliptiq {
class Store {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Store(sqlite3*, Limits);
    ~Store();
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    // Internal connection; callers must serialize access to each Store.
    sqlite3* connection() const;
    const Limits& limits() const;
};
Result<Store> open_store(const std::string& path, Limits limits = {});
Result<std::uint64_t> data_revision(Store&);
Result<void> compact_store(Store&); // worker thread only; no secure erase claim
}

#include "Index.hpp"
#include "db.hpp"
namespace cliptiq {
Result<void> IndexService::poll() {
  return detail::guarded<void>([&]() -> Result<void> {
    if (worker_.valid()) {
      if (worker_.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready)
        return {};
      auto result = worker_.get();
      if (result)
        indexed_ = pending_;
      else if (result.error().code != ErrorCode::Busy)
        return result.error();
    }
    auto revision = data_revision(store_);
    if (!revision)
      return revision.error();
    if (revision.value() == indexed_)
      return {};
    auto name = sqlite3_db_filename(store_.connection(), "main");
    if (!name || !*name)
      return Error{ErrorCode::Unsupported,
                   "Index worker requires disk profile"};
    std::string database = name;
    pending_ = revision.value();
    worker_ = std::async(
        std::launch::async, [database, revision = pending_]() -> Result<void> {
          auto store = open_store(database);
          if (!store)
            return store.error();
          auto path = prepare_index_namespace(store.value());
          if (!path)
            return path.error();
          return rebuild_index(store.value(), path.value(), revision);
        });
    return {};
  });
}
} // namespace cliptiq

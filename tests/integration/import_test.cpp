#include "../../core/src/db.hpp"
#include "cliptiq/history.hpp"
#include "cliptiq/migration.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
int main(int argc, char **argv) {
  try {
    auto s = open_store(":memory:");
    if (!s)
      throw std::runtime_error(s.error().message);
    if (argc == 2) {
      auto r = import_clipy(s.value(), argv[1], {});
      if (!r)
        throw std::runtime_error(r.error().message);
      if (r.value().history != 2)
        throw std::runtime_error("two original duplicate rows must survive");
      auto rows = list_history(s.value());
      if (!rows || rows.value().size() != 2)
        throw std::runtime_error("history missing");
      auto again = import_clipy(s.value(), argv[1], {});
      if (!again || !again.value().already_imported)
        throw std::runtime_error("repeat import not idempotent");
      auto removed = erase_history(s.value(), EntryId("h0"));
      if (!removed)
        throw std::runtime_error(removed.error().message);
      detail::Statement retained(s.value().connection(),
                                 "SELECT count(*) FROM migration_originals "
                                 "WHERE id='h0' OR id='legacy.archive.h0'");
      retained.row();
      if (retained.number(0))
        throw std::runtime_error(
            "Deleted clipboard content retained in provenance");
      auto clear = erase_history(s.value());
      if (!clear)
        throw std::runtime_error(clear.error().message);
      detail::Statement leftovers(
          s.value().connection(),
          "SELECT count(*) FROM migration_originals WHERE category "
          "IN('history','asset') OR id LIKE 'legacy.archive.%'");
      leftovers.row();
      if (leftovers.number(0))
        throw std::runtime_error("Clear retained clipboard provenance");
      for (bool periodic : {false, true}) {
        auto fresh = open_store(":memory:");
        if (!fresh)
          throw std::runtime_error(fresh.error().message);
        auto imported = import_clipy(fresh.value(), argv[1], {});
        if (!imported)
          throw std::runtime_error(imported.error().message);
        if (periodic) {
          auto first = tick_cleanup(fresh.value(), {1}, 0);
          auto second = tick_cleanup(fresh.value(), {1}, 2);
          if (!first || !second)
            throw std::runtime_error("cleanup failed");
        } else {
          ClipboardSnapshot snapshot;
          snapshot.representations = {{0, "x11", "UTF8_STRING",
                                       CanonicalKind::Text,
                                       Bytes{'n', 'e', 'w'}}};
          HistoryPolicy policy;
          policy.max_count = 1;
          auto captured =
              ingest(fresh.value(), snapshot, policy, 2000000000000ll);
          if (!captured)
            throw std::runtime_error(captured.error().message);
        }
        detail::Statement remains(fresh.value().connection(),
                                  "SELECT count(*) FROM migration_originals "
                                  "WHERE category IN('history','asset')");
        remains.row();
        if (remains.number(0))
          throw std::runtime_error(
              "Pruning/periodic clear retained history provenance");
      }
      return 0;
    }
    throw std::runtime_error("fixture required");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

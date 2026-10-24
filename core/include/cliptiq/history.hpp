#pragma once
#include "storage.hpp"
namespace cliptiq {
Result<std::optional<EntryId>> ingest(Store&, ClipboardSnapshot, HistoryPolicy, std::int64_t now_ms, std::optional<std::uint64_t> expected_erase_generation = {});
Result<std::uint64_t> history_erase_generation(Store&);
Result<std::vector<HistoryEntry>> list_history(Store&, SortOrder = SortOrder::LastUsed, std::size_t limit = 30);
Result<ClipboardSnapshot> load_snapshot(Store&, const EntryId&);
Result<void> touch_history(Store&, const EntryId&, std::int64_t now_ms);
Result<void> erase_history(Store&, std::optional<EntryId> = std::nullopt);
Result<void> tick_cleanup(Store&, CleanupPolicy, std::int64_t now_ms);
}

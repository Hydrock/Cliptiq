#pragma once
#include "cliptiq/paste.hpp"
#include <stdexcept>
namespace paste_fixture {
using namespace cliptiq;
struct MemoryWriter : ClipboardBackend {
  unsigned writes = 0;
  Capabilities capabilities() const override { return {}; }
  Result<ClipboardSnapshot> read_snapshot() override {
    return Error{ErrorCode::Unsupported, "no read"};
  }
  Result<std::string> write_snapshot(const ClipboardSnapshot &) override {
    ++writes;
    return std::string("own-fixture");
  }
  Result<void> start(CaptureCallback) override { return {}; }
  void stop() override {}
};
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
inline void retained_after_native_denial(PasteBackend &backend,
                                         const TargetToken &token) {
  auto store = take(open_store(":memory:"));
  ClipboardSnapshot snapshot;
  snapshot.representations = {
      {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'s', 'a', 'f', 'e'}}};
  auto id = *take(ingest(store, snapshot, {}, 1));
  MemoryWriter clipboard;
  SelectionIntent intent;
  intent.command = {
      MenuAction::SelectHistory, id, take(data_revision(store)), {token}};
  intent.action = SelectionAction::PasteDelete;
  auto result = take(execute_selection(store, intent, clipboard, backend));
  if (result.status != SelectionStatus::NeedsManualPaste ||
      result.history_deleted || take(list_history(store)).size() != 1 ||
      clipboard.writes != 1)
    throw std::runtime_error(
        "Native denial must retain PasteDelete history and copied payload");
}
} // namespace paste_fixture

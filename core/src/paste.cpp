#include "cliptiq/paste.hpp"
#include "db.hpp"
#include <algorithm>
namespace cliptiq {
Result<SelectionAction> selection_action(unsigned held,
                                         const ModifierBindings &b) {
  unsigned seen = 0;
  for (unsigned bit : {b.plain, b.delete_only, b.paste_delete}) {
    if ((bit & ~15u) || (bit && (bit & (bit - 1))) || (seen & bit))
      return Error{ErrorCode::InvalidArgument, "Conflicting modifier bindings"};
    seen |= bit;
  }
  if (held & ~15u)
    return Error{ErrorCode::InvalidArgument, "Invalid modifier bits"};
  unsigned active = held & seen;
  if (active && (active & (active - 1)))
    return Error{ErrorCode::InvalidArgument, "Ambiguous modifier selection"};
  if (active && active == b.delete_only)
    return SelectionAction::DeleteOnly;
  if (active && active == b.paste_delete)
    return SelectionAction::PasteDelete;
  if (active && active == b.plain)
    return SelectionAction::Plain;
  return SelectionAction::Original;
}
namespace {
ClipboardSnapshot snippet_snapshot(const std::string &text,
                                   CapturePlatform platform) {
  Representation r{0, "", "", CanonicalKind::Text, {}};
  switch (platform) {
  case CapturePlatform::MacOS:
    r.platform = "macos";
    r.native_type = "public.utf8-plain-text";
    break;
  case CapturePlatform::Windows:
    r.platform = "windows";
    r.native_type = "CF_UNICODETEXT";
    r.text_encoding = TextEncoding::Utf16Le;
    break;
  case CapturePlatform::X11:
    r.platform = "x11";
    r.native_type = "UTF8_STRING";
    break;
  case CapturePlatform::Wayland:
    r.platform = "wayland";
    r.native_type = "text/plain;charset=utf-8";
    break;
  default:
    throw detail::Failure{{ErrorCode::Unsupported, "No native text writer"}};
  }
  // Validate UTF-8 before native writers; do not alter snippet
  // whitespace/newlines.
  for (std::size_t i = 0; i < text.size();) {
    unsigned c = static_cast<unsigned char>(text[i++]), count = 0, minimum = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      count = 1;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      count = 2;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      count = 3;
      minimum = 0x10000;
    } else if (c >= 0x80)
      detail::invalid("Invalid UTF-8 snippet");
    if (i + count > text.size())
      detail::invalid("Incomplete UTF-8 snippet");
    for (unsigned n = 0; n < count; ++n) {
      unsigned next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80)
        detail::invalid("Invalid UTF-8 snippet");
      c = (c << 6) | (next & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || !c)
      detail::invalid("Invalid snippet scalar");
    if (platform == CapturePlatform::Windows) {
      auto append = [&](unsigned v) {
        r.data.push_back(static_cast<std::uint8_t>(v));
        r.data.push_back(static_cast<std::uint8_t>(v >> 8));
      };
      if (c > 0xffff) {
        c -= 0x10000;
        append(0xd800 + (c >> 10));
        append(0xdc00 + (c & 1023));
      } else
        append(c);
    }
  }
  if (platform == CapturePlatform::Windows) {
    r.data.push_back(0);
    r.data.push_back(0);
  } else
    r.data.assign(text.begin(), text.end());
  ClipboardSnapshot result;
  result.representations.push_back(std::move(r));
  return result;
}
Result<void> update_history(Store &store, const SelectionIntent &intent,
                            bool remove) {
  return detail::guarded<void>([&]() -> Result<void> {
    detail::Transaction tx(store.connection());
    auto valid = validate_menu_command(store, intent.command);
    if (!valid)
      return valid;
    detail::Statement q(store.connection(),
                        remove ? "DELETE FROM history WHERE id=?"
                               : "UPDATE history SET used_ms=? WHERE id=?");
    if (remove)
      q.bind(1, intent.command.id);
    else {
      q.bind(1, intent.now_ms);
      q.bind(2, intent.command.id);
    }
    q.done();
    detail::revision(store.connection());
    tx.commit();
    return {};
  });
}
} // namespace
Result<SelectionOutcome> execute_selection(Store &store,
                                           const SelectionIntent &intent,
                                           ClipboardBackend &clipboard,
                                           PasteBackend &paste) {
  return detail::guarded<SelectionOutcome>([&]() -> Result<SelectionOutcome> {
    if (static_cast<unsigned>(intent.action) > 3 ||
        static_cast<unsigned>(intent.shortcut) > 1 || intent.now_ms < 0 ||
        (intent.command.action != MenuAction::SelectHistory &&
         intent.command.action != MenuAction::SelectSnippet) ||
        intent.command.id.empty() || intent.command.id.size() > 1024 ||
        intent.command.invocation.target.size() > 4096)
      return Error{ErrorCode::InvalidArgument, "Invalid selection intent"};
    SelectionOutcome outcome;
    bool history = intent.command.action == MenuAction::SelectHistory;
    ClipboardSnapshot snapshot;
    { // Snapshot validation and loading are atomic; never hold DB lock across
      // OS calls.
      detail::Transaction tx(store.connection());
      auto valid = validate_menu_command(store, intent.command);
      if (!valid)
        return valid.error();
      if (!history && (intent.action == SelectionAction::DeleteOnly ||
                       intent.action == SelectionAction::PasteDelete)) {
        outcome.reason = "Destructive modifiers apply only to history";
        return outcome;
      }
      if (history && intent.action == SelectionAction::DeleteOnly) {
        detail::Statement q(store.connection(),
                            "DELETE FROM history WHERE id=?");
        q.bind(1, intent.command.id);
        q.done();
        detail::revision(store.connection());
        tx.commit();
        outcome.history_deleted = true;
        outcome.reason = "Deleted without clipboard write";
        return outcome;
      }
      if (history) {
        auto loaded = load_snapshot(store, intent.command.id);
        if (!loaded)
          return loaded.error();
        snapshot = std::move(loaded.value());
      } else {
        detail::Statement q(store.connection(),
                            "SELECT length(CAST(content AS BLOB)),content FROM "
                            "snippets WHERE id=?");
        q.bind(1, intent.command.id);
        if (!q.row())
          return Error{ErrorCode::NotFound, "Snippet disappeared"};
        if (q.number(0) >
            static_cast<std::int64_t>(store.limits().representation_bytes))
          return Error{ErrorCode::TooLarge, "Snippet exceeds clipboard limit"};
        snapshot =
            snippet_snapshot(q.text(1), clipboard.capabilities().platform);
      }
      tx.commit();
    }
    if (intent.action == SelectionAction::Plain) {
      auto &reps = snapshot.representations;
      reps.erase(std::remove_if(reps.begin(), reps.end(),
                                [](const Representation &r) {
                                  return r.kind != CanonicalKind::Text;
                                }),
                 reps.end());
      if (reps.empty() ||
          std::none_of(reps.begin(), reps.end(), [](const Representation &r) {
            return !r.data.empty();
          })) {
        outcome.reason = "No plain text representation; clipboard unchanged";
        return outcome;
      }
    }
    auto written = clipboard.write_snapshot(snapshot);
    if (!written)
      return written.error();
    outcome.sequence_token = written.value();
    outcome.status = SelectionStatus::Copied;
    bool dispatched = false;
    if (intent.auto_paste) {
      if (intent.command.invocation.target.empty()) {
        outcome.status = SelectionStatus::NeedsManualPaste;
        outcome.reason = "No captured target; paste manually";
      } else {
        auto result =
            paste.dispatch(intent.command.invocation.target, intent.shortcut);
        if (result) {
          outcome.status = SelectionStatus::Dispatched;
          dispatched = true;
          outcome.reason =
              "Shortcut dispatched; application receipt is not confirmed";
        } else {
          outcome.status = SelectionStatus::NeedsManualPaste;
          outcome.reason = result.error().message;
        }
      }
    }
    if (history) {
      bool remove = intent.action == SelectionAction::PasteDelete && dispatched;
      auto updated = update_history(store, intent, remove);
      if (!updated)
        outcome.history_error = updated.error();
      else
        outcome.history_deleted = remove;
    }
    return outcome;
  });
}
} // namespace cliptiq

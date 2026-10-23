#pragma once
#include "clipboard.hpp"
#include "menu.hpp"
#include <memory>
namespace cliptiq {
enum Modifier : unsigned { Control = 1, Shift = 2, Alt = 4, Super = 8 };
enum class SelectionAction { Original, Plain, DeleteOnly, PasteDelete };
enum class Shortcut { Paste, TerminalPaste };
struct ModifierBindings {
  unsigned plain = Control, delete_only = 0, paste_delete = 0;
};
Result<SelectionAction> selection_action(unsigned held,
                                         const ModifierBindings &);
using TargetToken =
    std::string; // Opaque, issued and consumed by a PasteBackend.
class PasteBackend {
public:
  virtual ~PasteBackend() = default;
  virtual Result<void> dispatch(const TargetToken &, Shortcut) = 0;
};
struct SelectionIntent {
  MenuCommand command;
  SelectionAction action = SelectionAction::Original;
  bool auto_paste = true;
  Shortcut shortcut = Shortcut::Paste;
  std::int64_t now_ms = 0;
};
enum class SelectionStatus { Copied, Dispatched, NeedsManualPaste, Cancelled };
struct SelectionOutcome {
  SelectionStatus status = SelectionStatus::Cancelled;
  std::string reason, sequence_token;
  bool history_deleted = false;
  std::optional<Error> history_error;
};
Result<SelectionOutcome> execute_selection(Store &, const SelectionIntent &,
                                           ClipboardBackend &, PasteBackend &);
// Owner-GUI-thread service. Never requests permission or forges Wayland input.
class NativePasteBackend final : public PasteBackend {
  struct State;
  std::unique_ptr<State> state_;

public:
  NativePasteBackend();
  ~NativePasteBackend() override;
  NativePasteBackend(const NativePasteBackend &) = delete;
  NativePasteBackend &operator=(const NativePasteBackend &) = delete;
  TargetToken capture_target();
  Capability capability() const;
  Result<void> dispatch(const TargetToken &, Shortcut) override;
};
} // namespace cliptiq

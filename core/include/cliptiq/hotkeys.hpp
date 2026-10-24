#pragma once
#include "paste.hpp"
#include <map>
namespace cliptiq {
enum class KeyMeaning { Logical, Physical };
struct KeyChord {
  unsigned modifiers = 0;
  std::string key;
  KeyMeaning meaning = KeyMeaning::Logical;
  unsigned physical_code = 0;
};
bool operator==(const KeyChord &, const KeyChord &);
using ActionId = std::string;
using HotkeyMap = std::map<ActionId, std::optional<KeyChord>>;
Result<KeyChord> parse_chord(const std::string &);
std::string format_chord(const KeyChord &);
Result<void> validate_hotkeys(const HotkeyMap &);
struct BindingStatus {
  bool registered = false;
  std::string reason, trigger;
};
class HotkeyBackend {
public:
  virtual ~HotkeyBackend() = default;
  virtual Result<unsigned> acquire(const ActionId &, const KeyChord &) = 0;
  virtual void release(unsigned) = 0;
  virtual Result<void> dispatch() = 0;
  virtual Capability capability() const = 0;
};
class HotkeyRegistry {
  HotkeyBackend &backend_;
  struct Bound {
    KeyChord chord;
    unsigned handle;
  };
  std::map<ActionId, Bound> bound_;

public:
  explicit HotkeyRegistry(HotkeyBackend &);
  ~HotkeyRegistry();
  Result<BindingStatus> bind_hotkey(const ActionId &, std::optional<KeyChord>);
  Result<void> replace(const HotkeyMap &,
                       std::function<Result<void>()> persist);
  void reconcile_folders(const std::map<FolderId, bool> &);
  bool registered(const ActionId &) const;
};
class NativeHotkeys final : public HotkeyBackend {
  struct State;
  std::unique_ptr<State> state_;

public:
  explicit NativeHotkeys(std::function<void(ActionId, InvocationContext)>);
  ~NativeHotkeys() override;
  Result<unsigned> acquire(const ActionId &, const KeyChord &) override;
  void release(unsigned) override;
  Result<void> dispatch() override;
  Capability capability() const override;
  Result<void> configure_portal(const HotkeyMap &);
  std::map<ActionId, BindingStatus> portal_statuses() const;
};
} // namespace cliptiq

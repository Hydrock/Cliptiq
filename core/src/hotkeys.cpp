#include "cliptiq/hotkeys.hpp"
#include <algorithm>
#include <sstream>
#include <vector>
namespace cliptiq {
namespace {
bool action_valid(const ActionId &a) {
  if (a == "main" || a == "history" || a == "snippets" || a == "settings" ||
      a == "editor" || a == "clear")
    return true;
  return a.rfind("folder:", 0) == 0 && a.size() > 7 && a.size() <= 1031 &&
         a.find_first_of("\n\r\0", 0, 3) == std::string::npos;
}
Result<void> chord_valid(const KeyChord &c) {
  bool key =
      (c.key.size() == 1 && ((c.key[0] >= 'A' && c.key[0] <= 'Z') ||
                             (c.key[0] >= '0' && c.key[0] <= '9'))) ||
      (c.key.size() >= 2 && c.key[0] == 'F' && c.key.size() <= 3 &&
       c.key.substr(1).find_first_not_of("0123456789") == std::string::npos &&
       std::stoi(c.key.substr(1)) >= 1 && std::stoi(c.key.substr(1)) <= 11);
  if (!key || !c.modifiers || (c.modifiers & ~15u) ||
      !(c.modifiers & (Control | Alt | Super)) || unsigned(c.meaning) > 1 ||
      c.physical_code > 255 ||
      (c.meaning == KeyMeaning::Logical && c.physical_code) ||
      (c.modifiers == Super && c.key == "V"))
    return Error{ErrorCode::InvalidArgument,
                 "Invalid/reserved shortcut; use Ctrl/Alt/Super and a letter, "
                 "digit or F1-F11"};
  return {};
}
} // namespace
bool operator==(const KeyChord &a, const KeyChord &b) {
  return a.modifiers == b.modifiers && a.meaning == b.meaning &&
         (a.meaning == KeyMeaning::Physical ? a.physical_code == b.physical_code
                                            : a.key == b.key);
}
Result<KeyChord> parse_chord(const std::string &input) {
  if (input.empty() || input.size() > 128)
    return Error{ErrorCode::InvalidArgument, "Invalid shortcut length"};
  std::string text = input;
  KeyChord c;
  if (text[0] == '@') {
    auto colon = text.find(':');
    if (colon == std::string::npos || colon < 2 || colon > 4)
      return Error{ErrorCode::InvalidArgument, "Invalid physical keycode"};
    unsigned code = 0;
    for (std::size_t i = 1; i < colon; ++i) {
      if (text[i] < '0' || text[i] > '9')
        return Error{ErrorCode::InvalidArgument, "Invalid physical keycode"};
      code = code * 10 + unsigned(text[i] - '0');
    }
    if (code > 255)
      return Error{ErrorCode::InvalidArgument, "Physical keycode too large"};
    c.meaning = KeyMeaning::Physical;
    c.physical_code = code;
    text = text.substr(colon + 1);
  }
  if (text.empty() || text.front() == '+' || text.back() == '+' ||
      text.find("++") != std::string::npos)
    return Error{ErrorCode::InvalidArgument, "Empty shortcut component"};
  std::istringstream in(text);
  std::string token;
  while (std::getline(in, token, '+')) {
    unsigned bit = token == "Ctrl"    ? Control
                   : token == "Shift" ? Shift
                   : token == "Alt"   ? Alt
                   : token == "Super" ? Super
                                      : 0;
    if (bit) {
      if (!c.key.empty() || (c.modifiers & bit))
        return Error{ErrorCode::InvalidArgument,
                     "Duplicate/out-of-order shortcut modifier"};
      c.modifiers |= bit;
    } else {
      if (!c.key.empty())
        return Error{ErrorCode::InvalidArgument, "Multiple shortcut keys"};
      c.key = token;
    }
  }
  auto valid = chord_valid(c);
  if (!valid)
    return valid.error();
  return c;
}
std::string format_chord(const KeyChord &c) {
  std::string out;
  for (auto p : {std::pair<unsigned, const char *>{Control, "Ctrl"},
                 {Shift, "Shift"},
                 {Alt, "Alt"},
                 {Super, "Super"}})
    if (c.modifiers & p.first)
      out += std::string(p.second) + "+";
  return (c.meaning == KeyMeaning::Physical
              ? "@" + std::to_string(c.physical_code) + ":"
              : "") +
         out + c.key;
}
Result<void> validate_hotkeys(const HotkeyMap &keys) {
  if (keys.size() > 1006)
    return Error{ErrorCode::TooLarge, "Too many shortcut actions"};
  auto recovery = keys.find("settings");
  if (recovery == keys.end() || !recovery->second)
    return Error{ErrorCode::InvalidArgument,
                 "Settings shortcut cannot be unset"};
  for (auto a = keys.begin(); a != keys.end(); ++a) {
    if (!action_valid(a->first))
      return Error{ErrorCode::InvalidArgument, "Unknown shortcut action"};
    if (!a->second)
      continue;
    auto valid = chord_valid(*a->second);
    if (!valid)
      return valid;
    for (auto b = keys.begin(); b != a; ++b)
      if (b->second && *a->second == *b->second)
        return Error{ErrorCode::Busy, "Shortcut conflicts with another action"};
  }
  return {};
}
HotkeyRegistry::HotkeyRegistry(HotkeyBackend &b) : backend_(b) {}
HotkeyRegistry::~HotkeyRegistry() {
  for (auto &p : bound_)
    backend_.release(p.second.handle);
}
Result<BindingStatus>
HotkeyRegistry::bind_hotkey(const ActionId &action,
                            std::optional<KeyChord> chord) {
  if (!action_valid(action) || (!chord && action == "settings"))
    return Error{ErrorCode::InvalidArgument,
                 "Unknown action or mandatory settings shortcut"};
  auto previous = bound_.find(action);
  if (!chord) {
    if (previous != bound_.end()) {
      backend_.release(previous->second.handle);
      bound_.erase(previous);
    }
    return BindingStatus{false, "Unbound", {}};
  }
  auto valid = chord_valid(*chord);
  if (!valid)
    return valid.error();
  for (auto &p : bound_)
    if (p.first != action && p.second.chord == *chord)
      return Error{ErrorCode::Busy, "Shortcut conflicts with another action"};
  if (previous != bound_.end() && previous->second.chord == *chord)
    return BindingStatus{true, "Already registered", format_chord(*chord)};
  // Allocate the replacement map and result before acquiring any OS resource.
  auto next = bound_;
  next[action] = {*chord, 0};
  BindingStatus status{true, "Registered", format_chord(*chord)};
  auto registered = backend_.acquire(action, *chord);
  if (!registered)
    return registered.error();
  next.at(action).handle = registered.value();
  bound_.swap(next);
  auto old = next.find(action);
  if (old != next.end())
    backend_.release(old->second.handle);
  return status;
}
Result<void> HotkeyRegistry::replace(const HotkeyMap &keys,
                                     std::function<Result<void>()> persist) {
  auto valid = validate_hotkeys(keys);
  if (!valid)
    return valid;
  if (backend_.capability().status == CapabilityStatus::Unavailable &&
      bound_.empty())
    return persist();
  // Prepare all allocations before registration/persistence. Commit is a
  // noexcept swap.
  std::map<ActionId, Bound> next;
  for (const auto &p : keys)
    if (p.second) {
      auto old = bound_.find(p.first);
      next.emplace(
          p.first,
          Bound{*p.second, old != bound_.end() && old->second.chord == *p.second
                               ? old->second.handle
                               : 0});
    }
  struct Pending {
    HotkeyBackend &backend;
    std::vector<unsigned> handles;
    bool committed = false;
    ~Pending() {
      if (!committed)
        for (auto handle : handles)
          backend.release(handle);
    }
  } pending{backend_, {}};
  pending.handles.reserve(next.size());
  for (auto &p : next)
    if (!p.second.handle) {
      auto registered = backend_.acquire(p.first, p.second.chord);
      if (!registered)
        return registered.error();
      pending.handles.push_back(registered.value());
      p.second.handle = registered.value();
    }
  auto saved = persist();
  if (!saved)
    return saved;
  bound_.swap(next);
  pending.committed = true;
  for (const auto &old : next) {
    auto current = bound_.find(old.first);
    if (current == bound_.end() || current->second.handle != old.second.handle)
      backend_.release(old.second.handle);
  }
  return {};
}

void HotkeyRegistry::reconcile_folders(
    const std::map<FolderId, bool> &catalog) {
  backend_.reconcile_folders(catalog);
  for (auto it = bound_.begin(); it != bound_.end();) {
    if (it->first.rfind("folder:", 0) == 0) {
      auto found = catalog.find(it->first.substr(7));
      if (found == catalog.end() || !found->second) {
        backend_.release(it->second.handle);
        it = bound_.erase(it);
        continue;
      }
    }
    ++it;
  }
}
bool HotkeyRegistry::registered(const ActionId &a) const {
  return bound_.count(a) != 0;
}
} // namespace cliptiq

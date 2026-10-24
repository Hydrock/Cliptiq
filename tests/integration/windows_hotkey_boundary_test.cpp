#include "../fixtures/windows/HotkeyCalls.hpp"
#include "../fixtures/windows/IsolatedClipboard.hpp"
#include "cliptiq/hotkeys.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
int main() {
  try {
    IsolatedClipboard station;
    std::map<int, std::pair<UINT, UINT>> held;
    bool block = false;
    hotkey_test_calls.interactive = [] { return true; };
    hotkey_test_calls.register_key = [&](HWND w, int id, UINT mods,
                                         UINT key) -> BOOL {
      CHECK(w && (mods & MOD_NOREPEAT));
      if (block || std::any_of(held.begin(), held.end(), [&](const auto &p) {
            return p.second == std::make_pair(mods, key);
          })) {
        SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);
        return FALSE;
      }
      held[id] = {mods, key};
      return TRUE;
    };
    hotkey_test_calls.unregister_key = [&](HWND, int id) -> BOOL {
      return held.erase(id) ? TRUE : FALSE;
    };
    {
      NativeHotkeys native([](auto, auto) {});
      HotkeyRegistry registry(native);
      CHECK(registry.bind_hotkey("main", take(parse_chord("Ctrl+Alt+Q"))));
      CHECK(held.size() == 1 &&
            held.begin()->second.first ==
                (MOD_NOREPEAT | MOD_CONTROL | MOD_ALT) &&
            held.begin()->second.second == 'Q');
      block = true;
      auto failed = registry.bind_hotkey("main", take(parse_chord("Alt+F8")));
      CHECK(!failed && failed.error().code == ErrorCode::Busy &&
            held.size() == 1 && registry.registered("main"));
      block = false;
      CHECK(registry.bind_hotkey("main", take(parse_chord("Alt+F8"))));
      CHECK(held.size() == 1 && held.begin()->second.second == VK_F8);
      CHECK(!registry.bind_hotkey("settings", {}));
      KeyChord physical{Control, "Q", KeyMeaning::Physical, 16};
      CHECK(!registry.bind_hotkey("main", physical) && held.size() == 1);
    }
    CHECK(held.empty());
    hotkey_test_calls = {};
    std::cout
        << "PASS "
           "native_adapter_RegisterHotKey_boundary_conflict_rollback_modifiers_"
           "and_cleanup; API scripted, no global OS registration\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

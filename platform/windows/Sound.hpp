#pragma once
#include <memory>
#include <vector>

namespace cliptiq::windows {
// Pure PCM generator: does not load an audio library or play anything.
std::vector<unsigned char> sound_wave(bool paste, unsigned volume);

// Owned and called by the application's UI thread. No worker or idle timer.
class NativeSound {
public:
  NativeSound();
  ~NativeSound();
  NativeSound(const NativeSound&) = delete;
  NativeSound& operator=(const NativeSound&) = delete;
  void configure(bool copy, bool paste, unsigned volume);
  void play_copy();
  void play_paste();
  void preview(bool paste, unsigned volume);
private:
  struct State;
  std::unique_ptr<State> state_;
  bool copy_ = false;
  bool paste_ = false;
  unsigned volume_ = 25;
  void play(bool paste, unsigned volume);
};
} // namespace cliptiq::windows

#include "Sound.hpp"
#include "../windows/Sound.hpp"
#import <AppKit/AppKit.h>
#include <algorithm>

namespace cliptiq::macos {
struct NativeSound::State {
  NSSound *copy = nil;
  NSSound *paste = nil;
  unsigned cached_volume = 101;
  void stop() {
    [copy stop];
    [paste stop];
  }
  ~State() {
    @try { stop(); }
    @catch (NSException *exception) { (void)exception; }
  }
};

NativeSound::NativeSound() = default;
NativeSound::~NativeSound() = default;
void NativeSound::configure(bool copy, bool paste, unsigned volume) {
  copy_ = copy;
  paste_ = paste;
  volume_ = std::min(volume, 100u);
}
void NativeSound::play_copy() { if (copy_) play(false, volume_); }
void NativeSound::play_paste() { if (paste_) play(true, volume_); }
void NativeSound::preview(bool paste, unsigned volume) {
  play(paste, std::min(volume, 100u));
}
void NativeSound::play(bool paste, unsigned volume) {
  if (!volume || !NSThread.isMainThread) return;
  try {
    @try {
      if (!state_) state_ = std::make_unique<State>();
      auto& state = *state_;
      if (state.cached_volume != volume) {
        const auto copy_wave = windows::sound_wave(false, volume);
        const auto paste_wave = windows::sound_wave(true, volume);
        auto copy_data = [NSData dataWithBytes:copy_wave.data() length:copy_wave.size()];
        auto paste_data = [NSData dataWithBytes:paste_wave.data() length:paste_wave.size()];
        NSSound *copy_sound = [[NSSound alloc] initWithData:copy_data];
        NSSound *paste_sound = [[NSSound alloc] initWithData:paste_data];
        if (!copy_sound || !paste_sound) return;
        // Prepare replacements before stopping the current playback.
        state.stop();
        state.copy = copy_sound;
        state.paste = paste_sound;
        state.cached_volume = volume;
      }
      state.stop();
      NSSound *sound = paste ? state.paste : state.copy;
      [sound setCurrentTime:0];
      [sound play];
    } @catch (NSException *exception) {
      (void)exception;
      // Playback and Cocoa allocation failures never escape a UI callback.
    }
  } catch (...) {
    // PCM allocation failures are harmless because sound is optional.
  }
}
} // namespace cliptiq::macos

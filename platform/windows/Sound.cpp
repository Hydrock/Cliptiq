#include "Sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace cliptiq::windows {
std::vector<unsigned char> sound_wave(bool paste, unsigned volume) {
  constexpr unsigned rate = 16000;
  const unsigned count = paste ? 1760 : 1280; // 110 / 80 ms
  std::vector<unsigned char> wave(44 + count * 2, 0);
  auto word = [&](unsigned pos, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
      wave[pos + i] = static_cast<unsigned char>(value >> (8 * i));
  };
  auto tag = [&](unsigned pos, const char* s) {
    for (unsigned i = 0; i < 4; ++i) wave[pos + i] = s[i];
  };
  tag(0, "RIFF"); word(4, unsigned(wave.size()) - 8, 4);
  tag(8, "WAVE"); tag(12, "fmt "); word(16, 16, 4);
  word(20, 1, 2); word(22, 1, 2); word(24, rate, 4);
  word(28, rate * 2, 4); word(32, 2, 2); word(34, 16, 2);
  tag(36, "data"); word(40, count * 2, 4);
  constexpr double pi = 3.14159265358979323846;
  const double amplitude = 5500.0 * std::min(volume, 100u) / 100.0;
  for (unsigned i = 0; i < count; ++i) {
    // Sine-squared window gives zero amplitude and slope at both ends.
    const double t = double(i) / rate;
    const double progress = double(i) / (count - 1);
    const double window = std::sin(pi * progress);
    double tone;
    if (paste) {
      // Two soft lower pulses with a quiet valley between them.
      const double pulses = std::sin(2 * pi * progress);
      tone = std::sin(2 * pi * 620 * t) * pulses * pulses;
    } else {
      tone = std::sin(2 * pi * (790 * t + 700 * t * t));
    }
    const auto sample = static_cast<std::int16_t>(amplitude * window * window * tone);
    word(44 + i * 2, static_cast<std::uint16_t>(sample), 2);
  }
  return wave;
}

struct NativeSound::State {
#ifdef _WIN32
  using Play = BOOL (WINAPI*)(LPCWSTR, HMODULE, DWORD);
  HMODULE library = nullptr;
  Play play = nullptr;
  ULONGLONG last = 0;
  bool started = false;
  bool attempted = false;
  unsigned cached_volume = 101;
  std::vector<unsigned char> copy;
  std::vector<unsigned char> paste;
  void stop() { if (play && started) { play(nullptr, nullptr, 0); started = false; } }
  ~State() {
    stop(); // SND_MEMORY storage must survive until playback is stopped.
    if (library) FreeLibrary(library);
  }
#endif
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
void NativeSound::preview(bool paste, unsigned volume) { play(paste, std::min(volume, 100u)); }
void NativeSound::play(bool paste, unsigned volume) {
#ifdef _WIN32
  try {
  if (!volume) return;
  if (!state_) state_ = std::make_unique<State>();
  auto& s = *state_;
  const auto now = GetTickCount64();
  // The longest cue is 110 ms. Drop bursts without cutting off a cue.
  if (s.started && now - s.last < 120) return;
  if (!s.attempted) {
    s.attempted = true;
    s.library = LoadLibraryExW(L"winmm.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (s.library) s.play = reinterpret_cast<State::Play>(GetProcAddress(s.library, "PlaySoundW"));
  }
  if (!s.play) return;
  if (s.cached_volume != volume) {
    // Allocate first so failure leaves any existing playback storage valid.
    auto copy = sound_wave(false, volume);
    auto paste_wave = sound_wave(true, volume);
    s.stop();
    s.copy.swap(copy);
    s.paste.swap(paste_wave);
    s.cached_volume = volume;
  }
  const auto& wave = paste ? s.paste : s.copy;
  if (s.play(reinterpret_cast<LPCWSTR>(wave.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP)) {
    s.started = true;
    s.last = now;
  }
  } catch (...) {
    // Audio is optional: allocation failures must never escape a UI callback.
    // Existing buffers remain valid because replacement is prepared before stop.
  }
#else
  (void)paste;
  (void)volume;
#endif
}
} // namespace cliptiq::windows

#include "platform/windows/Sound.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
static unsigned read(const std::vector<unsigned char>& w, unsigned p, unsigned n) {
  unsigned v = 0;
  for (unsigned i = 0; i < n; ++i) v |= unsigned(w.at(p + i)) << (8 * i);
  return v;
}
int main() {
  try {
    using cliptiq::windows::sound_wave;
    for (bool paste : {false, true}) {
      const auto w = sound_wave(paste, 100);
      CHECK(std::string(w.begin(), w.begin() + 4) == "RIFF");
      CHECK(std::string(w.begin() + 8, w.begin() + 16) == "WAVEfmt ");
      CHECK(std::string(w.begin() + 36, w.begin() + 40) == "data");
      CHECK(read(w, 4, 4) + 8 == w.size());
      CHECK(read(w, 40, 4) + 44 == w.size());
      CHECK(read(w, 20, 2) == 1 && read(w, 22, 2) == 1);
      CHECK(read(w, 24, 4) <= 16000 && read(w, 34, 2) == 16);
      CHECK(read(w, 40, 4) <= read(w, 24, 4) * 2 * 120 / 1000);
      CHECK(read(w, 44, 2) == 0 && read(w, unsigned(w.size()) - 2, 2) == 0);
      int peak = 0;
      for (unsigned p = 44; p < w.size(); p += 2) {
        const unsigned raw = read(w, p, 2);
        const int sample = raw < 32768 ? int(raw) : int(raw) - 65536;
        peak = std::max(peak, sample < 0 ? -sample : sample);
      }
      CHECK(peak > 0 && peak <= 6000);
      const auto silent = sound_wave(paste, 0);
      CHECK(std::all_of(silent.begin() + 44, silent.end(), [](auto b) { return b == 0; }));
      CHECK(sound_wave(paste, 101) == w);
      CHECK(sound_wave(paste, 25) != w);
    }
    CHECK(sound_wave(false, 25) != sound_wave(true, 25));
    std::cout << "PASS bounded gentle PCM WAV cues, silence, volume and distinct events\n";
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}

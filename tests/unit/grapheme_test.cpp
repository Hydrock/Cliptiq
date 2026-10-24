extern "C" {
#include "grapheme.h"
}
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  std::ifstream input(argv[1]);
  if (!input)
    return 2;
  std::string line;
  unsigned cases = 0;
  while (std::getline(input, line)) {
    line = line.substr(0, line.find('#'));
    std::istringstream words(line);
    std::string token, bytes;
    std::vector<size_t> expected, actual{0};
    while (words >> token) {
      if (token == u8"÷")
        expected.push_back(bytes.size());
      else if (token != u8"×") {
        auto c = std::stoul(token, nullptr, 16);
        char b[4];
        bytes.append(b, grapheme_encode_utf8(c, b, 4));
      }
    }
    if (expected.empty())
      continue;
    for (size_t i = 0; i < bytes.size();) {
      auto n = grapheme_next_character_break_utf8(bytes.data() + i,
                                                  bytes.size() - i);
      if (!n)
        return 1;
      i += n;
      actual.push_back(i);
    }
    if (actual != expected) {
      std::cerr << "FAIL Unicode17 grapheme case " << cases << '\n';
      return 1;
    }
    ++cases;
  }
  if (cases < 500)
    return 1;
  std::cout << "PASS Unicode17_grapheme_conformance " << cases << " cases\n";
  return 0;
}

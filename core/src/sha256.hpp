#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace cliptiq::detail {
class Sha256 {
    std::array<std::uint32_t,8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<std::uint8_t,64> block_{};
    std::uint64_t bytes_=0;
    std::size_t filled_=0;
    void compress();
public:
    void update(const void*,std::size_t);
    void integer(std::uint64_t);
    void field(const void*,std::size_t);
    std::string finish();
};
}

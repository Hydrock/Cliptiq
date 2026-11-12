#pragma once
#include "cliptiq/types.hpp"
namespace cliptiq::lan {
Result<std::string> read_secret(const std::string& ref);
Result<void> write_secret(const std::string& ref, const std::string& key64hex);
Result<void> erase_secret(const std::string& ref);
Result<std::string> generate_key();
}

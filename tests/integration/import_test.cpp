#include "cliptiq/history.hpp"
#include "cliptiq/migration.hpp"
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
int main(int argc, char **argv) {
  try {
    auto s = open_store(":memory:");
    if (!s)
      throw std::runtime_error(s.error().message);
    if (argc == 2) {
      auto r = import_clipy(s.value(), argv[1], {});
      if (!r)
        throw std::runtime_error(r.error().message);
      if (r.value().history != 2)
        throw std::runtime_error("two original duplicate rows must survive");
      auto rows = list_history(s.value());
      if (!rows || rows.value().size() != 2)
        throw std::runtime_error("history missing");
      auto again = import_clipy(s.value(), argv[1], {});
      if (!again || !again.value().already_imported)
        throw std::runtime_error("repeat import not idempotent");
      return 0;
    }
    throw std::runtime_error("fixture required");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

#include "cliptiq/locale.hpp"
namespace cliptiq {
namespace {
std::string language = "en";
TranslationLookup lookup = nullptr;
} // namespace
std::string tr(std::string_view s) {
  if (lookup)
    if (auto translated = lookup(s))
      return translated;
  return std::string(s);
}
const char *lc(const char *s) {
  if (lookup)
    if (auto translated = lookup(s))
      return translated;
  return s;
}
std::string active_locale() { return language; }
void install_locale(std::string value, TranslationLookup fn) {
  language = std::move(value);
  lookup = fn;
}
} // namespace cliptiq

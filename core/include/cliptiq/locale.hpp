#pragma once
#include <string>
#include <string_view>
namespace cliptiq {
// Identity unless a catalogue is activated. Only application chrome is passed
// here.
std::string tr(std::string_view english);
const char *lc(const char *english);
std::string activate_locale(std::string_view requested);
std::string active_locale();
using TranslationLookup = const char *(*)(std::string_view);
void install_locale(std::string, TranslationLookup);
} // namespace cliptiq

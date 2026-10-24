#include "core.h"
#include <cstdio>
#include <cstring>
#include <fstream>

// Valid 1x1 PNG; raw bytes preserved, not a placeholder image estimate.
static const unsigned char png[] = {
  137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,
  0,0,0,11,73,68,65,84,120,218,99,100,248,15,0,1,5,1,1,39,24,227,102,0,0,0,0,73,69,78,68,174,66,96,130
};
static int add(int id, const char* format, const char* value) {
    return probe_add(id, format, reinterpret_cast<const unsigned char*>(value), std::strlen(value));
}
int seed_and_test(const char* path, const char* xml_path) {
    if (probe_open(path)) return 10;
    for (int i = 0; i < 30; ++i) {
        char text[100]; std::snprintf(text, sizeof text, "Fixture %02d — Привет, Cliptiq", i);
        if (add(i, "text/plain", text)) return 11;
    }
    if (probe_add(30, "image/png", png, sizeof png) ||
        add(31, "text/html", "<b>Cliptiq &amp; clipboard</b>") || add(31, "text/plain", "Cliptiq & clipboard") ||
        add(32, "text/rtf", "{\\rtf1\\ansi Cliptiq}") || add(32, "text/plain", "Cliptiq")) return 12;
    if (probe_count() != 33 || probe_representations() != 35) return 13;
    unsigned char out[512];
    if (probe_get(30, "image/png", out, sizeof out) != sizeof png || std::memcmp(out, png, sizeof png)) return 14;
    if (probe_get(31, "text/html", out, sizeof out) != 30 || std::memcmp(out, "<b>Cliptiq &amp; clipboard</b>", 30)) return 15;
    if (probe_get(32, "text/rtf", out, sizeof out) != 20 || std::memcmp(out, "{\\rtf1\\ansi Cliptiq}", 20)) return 16;
    if (probe_get(999, "text/plain", out, sizeof out) != -1 || probe_get(30, "image/png", out, 2) != -1) return 17;
    char xml[4096]; int len = probe_xml(xml, sizeof xml);
    if (len <= 0 || !std::strstr(xml, "Hello &amp; welcome") || !std::strstr(xml, "Привет &lt;мир&gt;")) return 18;
    if (probe_xml(xml, 2) != -1) return 19;
    if (xml_path) { std::ofstream file(xml_path, std::ios::binary); file.write(xml, len); if (!file) return 20; }
    return 0;
}

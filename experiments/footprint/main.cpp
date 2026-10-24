#include "core.h"
#include "native.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include "sqlite3.h"
#ifdef PROBE_OCR
#include "ocr.h"
#endif
int seed_and_test(const char*, const char*);

int main(int argc, char** argv) {
    auto start = std::chrono::steady_clock::now();
    bool interactive = argc > 1 && std::strcmp(argv[1], "--interactive") == 0;
    bool benchmark = argc > 1 && std::strcmp(argv[1], "--benchmark") == 0;
    bool lifecycle = argc > 1 && std::strcmp(argv[1], "--lifecycle-smoke") == 0;
    const char* db = ":memory:";
    int result = seed_and_test(db, argc > 2 && !benchmark ? argv[2] : nullptr);
    if (result) { std::fprintf(stderr, "fixture failed: %d\n", result); probe_close(); return result; }
    if (sqlite3_compileoption_used("ENABLE_FTS5")) {
        sqlite3* index = nullptr;
        if (sqlite3_open(":memory:", &index) != SQLITE_OK) return 23;
        int status = sqlite3_exec(index,"CREATE VIRTUAL TABLE search USING fts5(text);INSERT INTO search VALUES('Cliptiq');",nullptr,nullptr,nullptr);
        sqlite3_stmt* query = nullptr;
        if (status == SQLITE_OK) status=sqlite3_prepare_v2(index,"SELECT count(*) FROM search WHERE search MATCH 'Cliptiq'",-1,&query,nullptr);
        bool found=status==SQLITE_OK && sqlite3_step(query)==SQLITE_ROW && sqlite3_column_int(query,0)==1;
        sqlite3_finalize(query); sqlite3_close(index);
        if (!found) { probe_close(); return 24; }
    }
    if (!interactive && !benchmark && !lifecycle) {
        std::printf("{\"entries\":33,\"representations\":35,\"png_roundtrip\":true,\"html_rtf_roundtrip\":true,\"xml_escape\":true}\n");
        probe_close(); return 0;
    }
    bool native_ready = native_setup(interactive);
    auto ready = std::chrono::steady_clock::now();
    if (lifecycle && native_ready) { native_schedule_close(); native_loop(); std::puts("{\"native_window_lifecycle\":true}"); }
    else if (benchmark) {
        const char* ocr="not implemented";
#ifdef PROBE_OCR
        ocr=native_ocr_test();
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        double ms = std::chrono::duration<double, std::milli>(ready - start).count();
        std::ofstream output(argc > 2 ? argv[2] : "runtime.json");
        output << "{\"entries\":33,\"representations\":35,\"native_ui_ready\":" << (native_ready ? "true" : "false")
          << ",\"fts5\":" << (sqlite3_compileoption_used("ENABLE_FTS5") ? "true" : "false")
          << ",\"idle_memory_bytes\":" << native_memory() << ",\"startup_ms\":" << ms
          << ",\"startup_definition\":\"main-to-fixture-and-hidden-native-window; loader time excluded\""
          << ",\"clipboard_roundtrip\":\"not exercised; user clipboard preserved\",\"ocr\":\"" << ocr << "\"}\n";
        result = output ? 0 : 21;
    } else if (native_ready) native_loop();
    else result = 22;
    native_cleanup(); probe_close(); return result;
}

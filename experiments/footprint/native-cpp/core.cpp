#include "../core.h"
#include "sqlite3.h"
#include <cstring>
#include <string>

static sqlite3* db = nullptr;
static int scalar(const char* sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return -1;
    int value = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
    sqlite3_finalize(statement);
    return value;
}
extern "C" int probe_open(const char* path) {
    if (sqlite3_open(path, &db) != SQLITE_OK) return -1;
    return sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS assets(id INTEGER,format TEXT,data BLOB,PRIMARY KEY(id,format));"
        "CREATE TABLE IF NOT EXISTS snippets(title TEXT,content TEXT);"
        "DELETE FROM assets;DELETE FROM snippets;"
        "INSERT INTO snippets VALUES('Greeting','Hello & welcome'),('Unicode','Привет <мир>');", nullptr, nullptr, nullptr);
}
extern "C" int probe_add(int id, const char* format, const unsigned char* data, size_t len) {
    if (len > 32 * 1024 * 1024) return -1;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO assets VALUES(?,?,?)", -1, &statement, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int(statement, 1, id);
    // Same lifetime/SQLITE_STATIC contract as the Rust comparison core.
    sqlite3_bind_text(statement, 2, format, -1, SQLITE_STATIC);
    sqlite3_bind_blob(statement, 3, data, static_cast<int>(len), SQLITE_STATIC);
    int result = sqlite3_step(statement) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(statement);
    return result;
}
extern "C" int probe_count() { return scalar("SELECT count(DISTINCT id) FROM assets"); }
extern "C" int probe_representations() { return scalar("SELECT count(*) FROM assets"); }
extern "C" int probe_get(int id, const char* format, unsigned char* output, size_t capacity) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT data FROM assets WHERE id=? AND format=?", -1, &statement, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int(statement, 1, id);
    sqlite3_bind_text(statement, 2, format, -1, SQLITE_TRANSIENT);
    int len = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        len = sqlite3_column_bytes(statement, 0);
        if (static_cast<size_t>(len) > capacity) len = -1;
        else if (len) std::memcpy(output, sqlite3_column_blob(statement, 0), len);
    }
    sqlite3_finalize(statement);
    return len;
}
static std::string escape(const char* text) {
    std::string out;
    for (; *text; ++text) {
        switch (*text) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break; default: out += *text; }
    }
    return out;
}
extern "C" int probe_xml(char* output, size_t capacity) {
    std::string xml = "<folders><folder><title>Examples</title><snippets>";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT title,content FROM snippets ORDER BY rowid", -1, &statement, nullptr) != SQLITE_OK) return -1;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        xml += "<snippet><title>" + escape(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) + "</title><content>";
        xml += escape(reinterpret_cast<const char*>(sqlite3_column_text(statement, 1))) + "</content></snippet>";
    }
    sqlite3_finalize(statement);
    xml += "</snippets></folder></folders>";
    if (xml.size() + 1 > capacity) return -1;
    std::memcpy(output, xml.c_str(), xml.size() + 1);
    return static_cast<int>(xml.size());
}
extern "C" void probe_close() { if (db) sqlite3_close(db); db = nullptr; }

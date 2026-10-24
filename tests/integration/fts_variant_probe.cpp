#include "cliptiq/history.hpp"
#include "cliptiq/index.hpp"
#include "sqlite3.h"
#include <iostream>
using namespace cliptiq;
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      return 2;
    auto store = take(open_store(argv[1]));
    auto path = take(prepare_index_namespace(store));
    std::string mode = argv[2];
    if (mode == "seed") {
      ClipboardSnapshot v;
      v.sequence_token = "seed";
      v.representations.push_back(
          {0, "x11", "UTF8_STRING", CanonicalKind::Text, {'s', 'e', 'e', 'd'}});
      auto id = *take(ingest(store, v, {}, 1));
      sqlite3_exec(
          store.connection(),
          "UPDATE history SET ocr_text='migration retained',ocr_state=NULL",
          nullptr, nullptr, nullptr);
    } else if (mode == "index") {
      auto rows = take(list_history(store));
      if (rows.size() != 1)
        throw std::runtime_error("history lost across variants");
      auto revision = take(data_revision(store));
      if (!rebuild_index(store, path, revision))
        throw std::runtime_error("rebuild failed");
      if (take(search_index(store, path, "retained")).size() != 1)
        throw std::runtime_error("migration OCR lost");
    } else if (mode == "base") {
      if (take(list_history(store)).size() != 1)
        throw std::runtime_error("Base lost history");
      if (sqlite3_exec(store.connection(),
                       "CREATE VIRTUAL TABLE bad USING fts5(x)", nullptr,
                       nullptr, nullptr) == SQLITE_OK)
        throw std::runtime_error("FTS leaked into main");
    } else if (mode == "clear") {
      if (!erase_history(store))
        throw std::runtime_error("clear failed");
    } else if (mode == "empty") {
      if (!take(list_history(store)).empty())
        throw std::runtime_error("history resurrected");
      if (!rebuild_index(store, path, take(data_revision(store))))
        throw std::runtime_error("empty rebuild failed");
      if (!take(search_index(store, path, "retained")).empty())
        throw std::runtime_error("OCR resurrected");
    } else
      return 2;
    std::cout << mode << ": pass\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

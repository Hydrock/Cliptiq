#include "cliptiq/history.hpp"
#include "cliptiq/index.hpp"
#include "cliptiq/snippets.hpp"
#include "sqlite3.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
template <class T> T take(Result<T> r) {
  if (!r)
    throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
int main() {
  try {
    auto root =
        std::filesystem::temp_directory_path() /
        ("cliptiq-fts-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    auto db = (root / "history.sqlite3").u8string();
    auto store = take(open_store(db));
    CHECK(sqlite3_exec(store.connection(),
                       "CREATE VIRTUAL TABLE forbidden USING fts5(body)",
                       nullptr, nullptr, nullptr) != SQLITE_OK);
    auto path = take(prepare_index_namespace(store));
    std::ofstream(root / "index/unrelated.txt") << "keep";
    std::ofstream(std::filesystem::u8path(path)) << "synthetic index";
    CHECK(erase_history(store));
    CHECK(!std::filesystem::exists(std::filesystem::u8path(path)));
    CHECK(std::filesystem::exists(root / "index/unrelated.txt"));
    CHECK(!invalidate_index((root / "unrelated.txt").u8string()));
    ClipboardSnapshot snapshot;
    snapshot.sequence_token = "text";
    snapshot.representations.push_back({0, "x11", "UTF8_STRING",
                                        CanonicalKind::Text,
                                        Bytes{'h', 'e', 'l', 'l', 'o'}});
    auto id = *take(ingest(store, snapshot, {}, 1));
#ifdef CLIPTIQ_OCR_INDEX
    auto revision = take(data_revision(store));
    {
      auto rebuilt = rebuild_index(store, path, revision);
      if (!rebuilt)
        throw std::runtime_error(rebuilt.error().message);
    }
    CHECK(take(search_index(store, path, "hello")).size() == 1);
    CHECK(sqlite3_exec(
              store.connection(),
              "UPDATE history SET ocr_text='Unicode OCR retained',ocr_state=1",
              nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    CHECK(take(search_index(store, path, "retained")).size() == 1);
    auto folder = take(create_folder(store, "folder"));
    Snippet snippet;
    snippet.folder_id = folder;
    snippet.title = "snippet title";
    snippet.content = "searchable phrase";
    auto snippet_id = take(save_snippet(store, snippet));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    auto matches = take(search_index(store, path, "searchable"));
    CHECK(matches.size() == 1 && matches[0].snippet);
    CHECK(set_enabled(store, {SnippetItemKind::Snippet, snippet_id}, false));
    CHECK(!std::filesystem::exists(std::filesystem::u8path(path)));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    CHECK(take(search_index(store, path, "searchable")).empty());
    CHECK(erase_snippet_item(store, {SnippetItemKind::Folder, folder}));
    auto unicode = snapshot;
    unicode.sequence_token = "utf16";
    unicode.representations[0].data = {'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0};
    unicode.representations[0].text_encoding = TextEncoding::Utf16Le;
    auto utf16_id = *take(ingest(store, unicode, {}, 3));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    CHECK(take(search_index(store, path, "world")).size() == 1);
    CHECK(erase_history(store, utf16_id));
    // An ordinary main write invalidates all companion payload/sidecars.
    CHECK(touch_history(store, id, 2));
    CHECK(!std::filesystem::exists(std::filesystem::u8path(path)));
    CHECK(!rebuild_index(store, path, revision));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    CHECK(erase_history(store, id));
    CHECK(!std::filesystem::exists(std::filesystem::u8path(path)));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
    CHECK(take(search_index(store, path, "hello")).empty());
    std::ofstream(std::filesystem::u8path(path),
                  std::ios::binary | std::ios::trunc)
        << "damaged";
    CHECK(!search_index(store, path, "hello"));
    CHECK(rebuild_index(store, path, take(data_revision(store))));
#else
    CHECK(!rebuild_index(store, path, take(data_revision(store))));
#endif
    std::ofstream(std::filesystem::u8path(path)) << "crash left companion";
    {
      auto reopen = take(open_store(db));
      CHECK(!std::filesystem::exists(std::filesystem::u8path(path)));
      CHECK(take(list_history(reopen)).size() ==
            take(list_history(store)).size());
    }
    // Only owned known files are invalidated; foreign files are retained.
    CHECK(invalidate_index(path));
    CHECK(std::filesystem::exists(root / "index/unrelated.txt"));
    std::cout << "companion index isolation, invalidation, recovery and Base "
                 "compatibility passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

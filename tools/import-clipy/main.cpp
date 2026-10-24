#include "cliptiq/migration.hpp"
#include <iostream>
int run_import(int argc, char **argv) {
  if (argc != 4 && argc != 5) {
    std::cerr
        << "cliptiq-import export SOURCE.sqlite3 NEUTRAL.sqlite3 | import "
           "NEUTRAL.sqlite3 DESTINATION.sqlite3 [--apply-settings]\n";
    return 2;
  }
  std::string command = argv[1];
  if (command == "export" && argc == 4) {
    auto r = cliptiq::export_clipy_sqlite(argv[2], argv[3]);
    if (!r) {
      std::cerr << r.error().message << '\n';
      return 1;
    }
    return 0;
  }
  if (command == "import" &&
      (argc == 4 || std::string(argv[4]) == "--apply-settings")) {
    auto s = cliptiq::open_store(argv[3]);
    if (!s) {
      std::cerr << s.error().message << '\n';
      return 1;
    }
    auto r = cliptiq::import_clipy(s.value(), argv[2], {argc == 5});
    if (!r) {
      std::cerr << r.error().message << '\n';
      return 1;
    }
    std::cout << r.value().history << " history, " << r.value().folders
              << " folders, " << r.value().snippets
              << " snippets; repeated=" << r.value().already_imported << '\n';
    return 0;
  }
  return 2;
}

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
int wmain(int argc,wchar_t**argv){std::vector<std::string> args;for(int i=0;i<argc;++i){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(!n)return 2;std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr);s.pop_back();args.push_back(std::move(s));}std::vector<char*> ptrs;for(auto&s:args)ptrs.push_back(s.data());return run_import(argc,ptrs.data());}
#else
int main(int argc,char**argv){return run_import(argc,argv);}
#endif

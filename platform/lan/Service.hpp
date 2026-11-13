#pragma once
#include "cliptiq/lan.hpp"
#include <map>
#include <memory>
namespace cliptiq::lan {
struct ClipboardHooks {
 std::function<Result<std::string>()> token;
 std::function<Result<std::string>(const ClipboardSnapshot&)> write;
 // Must compare immediately before mutation; native Windows holds OpenClipboard.
 std::function<Result<std::string>(const ClipboardSnapshot&,const std::string&)> compare_write;
};
class Service {
public:
 Service(Store&,std::string database,CapturePlatform,ClipboardHooks);
 ~Service();
 Result<void> initialize();
 Result<Config> config();
 Result<void> refresh();
 Result<void> save(Config,std::map<std::string,std::string> changed_keys);
 Result<void> copy_secret(const std::string&);
 void captured(const EntryId&);
 bool poll();
 void history_policy(HistoryPolicy);
 bool running() const;
 std::string status() const;
 void stop();
private: struct State;std::unique_ptr<State> state_;
};
}

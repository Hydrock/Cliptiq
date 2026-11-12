#pragma once
// Include Winsock before any Windows header.
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
#include "cliptiq/types.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
namespace cliptiq::lan {
struct ResolvedAddress { sockaddr_storage address{}; int length=0; };
// Start, poll and destroy on one thread. poll() never waits for DNS completion;
// numeric hosts complete immediately. A terminal result remains available.
class ResolverJob {
public:
 ResolverJob(const std::string& host, std::uint16_t port,
  std::chrono::milliseconds timeout=std::chrono::milliseconds(3000));
 ~ResolverJob();
 ResolverJob(const ResolverJob&)=delete;
 ResolverJob& operator=(const ResolverJob&)=delete;
 std::optional<Result<std::vector<ResolvedAddress>>> poll();
 void cancel();
private:
 struct State; std::unique_ptr<State> state_;
};
// Blocking convenience wrapper for callers which do not serve active channels.
// No application-owned resolver thread. Cancellation/deadlines bound the caller;
// native resolvers may retain internal system work after cancellation.
Result<std::vector<ResolvedAddress>> resolve_host(const std::string& host,
 std::uint16_t port, const std::atomic<bool>& stopping,
 std::chrono::milliseconds timeout=std::chrono::milliseconds(3000));
}

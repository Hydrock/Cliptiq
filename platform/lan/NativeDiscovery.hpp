#pragma once
#include "Discovery.hpp"
#include <memory>
#include <mutex>
namespace cliptiq::lan {
struct DiscoveryAnnouncement {DiscoveryDevice device;};
struct DiscoveryEvent {
 enum class Kind {Found,Removed,Failure};
 Kind kind=Kind::Failure;DiscoveryDevice device;std::string message;
};
Result<Bytes> encode_discovery_txt(const DiscoveryDevice&);
Result<DiscoveryDevice> decode_discovery_txt(const Bytes&,DiscoveryDevice resolved);
// OS callbacks may outlive cancellation. Generations keep them out of a new search.
class DiscoveryInbox {
 std::mutex mutex_;std::string local_id_;std::uint64_t generation_=0;bool active_=false;
 std::vector<DiscoveryEvent> events_;
public:
 explicit DiscoveryInbox(std::string id):local_id_(std::move(id)){}
 std::uint64_t begin();void cancel();
 bool accept(std::uint64_t,DiscoveryEvent);std::vector<DiscoveryEvent> drain();
};
class NativeDiscovery {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 struct Capability {bool available=false;std::string message;};
 // local_only is an isolated test mode, never used by the application.
 explicit NativeDiscovery(std::string local_id,bool local_only=false);
 ~NativeDiscovery();NativeDiscovery(const NativeDiscovery&)=delete;NativeDiscovery& operator=(const NativeDiscovery&)=delete;
 Capability capability() const;
 Result<void> publish(std::optional<DiscoveryAnnouncement>);
 Result<void> browse(std::int64_t duration_ms=10000);
 void stop_browse();std::vector<DiscoveryEvent> poll();void stop();
};
}

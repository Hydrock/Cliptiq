#pragma once
#include "Clipboard.hpp"
#include <chrono>
namespace cliptiq::linux_backend {
using Clock=std::chrono::steady_clock;
struct NativeType{const char* name;CanonicalKind kind;TextEncoding encoding=TextEncoding::Utf8;};
const std::vector<NativeType>& native_types(bool x11);
Capabilities native_capabilities(bool x11,bool connected,std::string reason={});
ClipboardFlags native_flags(const std::vector<ClipboardSnapshot::OfferedType>&);
Result<void> validate_payload(Representation&,bool replay=false);
Result<ClipboardSnapshot> prepare_replay(const ClipboardSnapshot&,const ClipboardOptions&,bool x11);
class StreamBuffer{
 Bytes bytes_;std::uint64_t limit_;Clock::time_point deadline_;std::uint64_t generation_;std::optional<Error> error_;
public:
 StreamBuffer(std::uint64_t limit,Clock::time_point deadline,std::uint64_t generation);
 Result<void> append(const std::uint8_t*,std::size_t,std::uint64_t current);
 Result<Bytes> finish(std::uint64_t current);
};
class FdReceiver{
 int fd_=-1;StreamBuffer buffer_;
public:
 FdReceiver(int fd,std::uint64_t limit,Clock::time_point deadline,std::uint64_t generation);
 ~FdReceiver();FdReceiver(const FdReceiver&)=delete;
 std::optional<Result<Bytes>> step(std::uint64_t current);
 int fd()const{return fd_;}
};
class FdSendQueue{
 struct Send;std::vector<std::unique_ptr<Send>> sends_;
public:
 FdSendQueue();~FdSendQueue();
 Result<void> add(int fd,std::shared_ptr<const Bytes>,Clock::time_point deadline);
 void step();void clear();std::vector<int> fds()const;
};
}

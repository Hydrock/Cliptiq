#pragma once
#include "PairingCrypto.hpp"
#include <functional>
#include <map>
namespace cliptiq::lan {
Result<Bytes> pairing_random(size_t size);
struct PairingPeer {PairingHello hello;std::string remote_address,local_address;std::uint32_t interface_index=0;};
struct PairingEvent {
 enum class Kind {Incoming,Code,EnterCode,Prepare,Commit,Complete,Failed};
 Kind kind;std::string session_id;PairingPeer peer;std::string code,message;Bytes key;
};
class PairingRateLimiter {
 std::map<std::string,std::vector<std::int64_t>> requests_;std::vector<std::int64_t> global_;
public:bool admit(const std::string& actual_address,std::int64_t now);
};
class PairingSession {
public:
 using Clock=std::function<std::int64_t()>;using Random=std::function<Result<Bytes>(size_t)>;
 PairingSession(PairingRole,PairingHello local,std::string remote_address,std::string local_address,Clock,Random=pairing_random,std::string expected_remote_id={},std::uint32_t interface_index=0);
 ~PairingSession();
 Result<void> start();Result<void> receive(const Bytes&);Result<void> accept();Result<void> enter_code(std::string);
 Result<void> prepared(Result<void>);Result<void> committed(Result<void>);
 void cancel();void tick();bool finished()const;bool awaiting_request()const;
 std::vector<Bytes> take_wire();std::vector<PairingEvent> take_events();
 std::string session_id()const;
private:struct State;std::unique_ptr<State> state_;
};
}

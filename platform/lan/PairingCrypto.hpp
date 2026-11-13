#pragma once
#include "cliptiq/types.hpp"
#include "PairingWire.hpp"
#include <memory>
namespace cliptiq::lan {
enum class PairingRole { Initiator,Responder };
class PairingCrypto {
 struct State;std::unique_ptr<State> state_;
public:
 PairingCrypto(PairingRole,std::string code);
 ~PairingCrypto();
 PairingCrypto(const PairingCrypto&)=delete;
 PairingCrypto& operator=(const PairingCrypto&)=delete;
 Result<Bytes> round_one();Result<void> read_round_one(const Bytes&);
 Result<Bytes> round_two();Result<void> read_round_two(const Bytes&);
 Result<Bytes> derive(const Bytes& context);
 Result<Bytes> proof(const Bytes& context,PairingRole);
 Result<void> verify_proof(const Bytes& context,PairingRole,const Bytes&);
 Result<Bytes> seal_control(const Bytes& context,PairingFrame);
 Result<PairingFrame> open_control(const Bytes& context,const Bytes& frame);
};
}

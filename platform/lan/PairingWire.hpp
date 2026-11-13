#pragma once
#include "cliptiq/types.hpp"
namespace cliptiq::lan {
enum class PairingMessage : std::uint8_t {Request,Accept,RoundOne,RoundTwo,Proof,Prepared,Commit,Committed,Done,Abort};
struct PairingFrame {PairingMessage kind=PairingMessage::Abort;std::string session_id;Bytes payload;};
struct PairingHello {std::string session_id,device_id,name,platform,data_address;std::uint16_t data_port=0;Bytes nonce;};
Result<Bytes> encode_pairing_frame(const PairingFrame&);
Result<PairingFrame> decode_pairing_frame(const Bytes&);
Result<Bytes> encode_pairing_hello(const PairingHello&);
Result<PairingHello> decode_pairing_hello(const Bytes&);
Result<Bytes> pairing_transcript(const PairingHello& initiator,const PairingHello& responder,const Bytes& r1a,const Bytes& r1b,const Bytes& r2a,const Bytes& r2b);
}

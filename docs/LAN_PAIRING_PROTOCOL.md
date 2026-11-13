# Cliptiq local pairing protocol v1

This is a new control protocol, separate from the existing clipboard TLS 1.3
PSK/ECDHE protocol. No clipboard representations, PIN or persistent PSK are sent
as control fields. Discovery is an unauthenticated hint, never an authorization.

## Bounded encoding

All integers are unsigned, big endian. Reject unknown versions/kinds, invalid
UTF-8, control/bidi-control label characters, invalid IDs, oversized fields,
truncation and trailing bytes before acting on a frame.

Frame, maximum **8192 bytes including prefix**:

| Field | Encoding |
|---|---|
| body length | u32, exactly total bytes minus 4 |
| magic/version | 43 54 50 01 (CTP plus version 1) |
| kind | u8 |
| session ID length | u8, 1–128 |
| session ID | ASCII alphanumeric, hyphen or underscore |
| payload length | u16 |
| payload | exactly the declared length |

Kinds in order, values 0–9: Request, Accept, RoundOne, RoundTwo, Proof,
Prepared, Commit, Committed, Done, Abort.

Codec test vector (synthetic payload, not a valid complete Request):

    Request(session="a", payload=01 02)
    00 00 00 0b 43 54 50 01 00 01 61 00 02 01 02

Request and Accept carry a Hello. A Hello is at most 1024 bytes:
magic 43 54 48 01, then five u16-length-prefixed strings in this order:
session ID (128), device ID (128), display name (256), platform (32), numeric
data address (128). Limits in parentheses are bytes. Finish with data port u16
(nonzero) and 32-byte nonzero nonce. IPv6 link-local addresses have no remote
interface-name suffix: the receiving side applies its locally selected scope.
Actual control connection addresses constrain the announced data endpoint.

## Exchange

1. A sends Request/Hello A with a fresh session ID and nonce.
2. B prompts for local approval. Rejection/timeout closes the attempt; no code
   or PSK exists before approval. Approval creates a CSPRNG six-digit code,
   nonce, PAKE responder context, Accept/Hello B and RoundOne B.
3. A enters the code from B. Both sides use the pinned Mbed TLS 3.6.7
   EC J-PAKE implementation, SHA-256 and P-256. A sends RoundOne A and
   RoundTwo A; B processes the rounds and sends RoundTwo B. Round messages
   contain only the library serialization, at most 2048 bytes each.
4. Both produce Proof (32 bytes) over the same canonical transcript and
   verify the opposite role's proof. A wrong code does not produce an
   authenticated key. No permanent PSK is exported until own proof creation
   and successful peer-proof verification.
5. Each stages its new credential and a disabled pending-pair record, then
   sends authenticated Prepared. A initiates Commit after both preparations.
   B commits and replies Committed; A commits and sends Done. These frames
   require authentication and monotonically increasing sequence numbers.
   Pending metadata makes repeated storage completion idempotent.
6. Clipboard exchange starts only through the unchanged authenticated TLS
   data channel when both peers are configured. Successful data-channel
   authentication can also finalize a lost final control acknowledgment.

The session layer owns deadlines, request/guess limits, storage outcomes and
strict allowed state transitions. Dropping or reordering traffic can deny
service, but cannot authorize an unconfirmed pair or export clipboard contents.
Do not treat an unverified Commit/Abort as an authenticated storage instruction.

## Canonical transcript and key separation

Prefix: ASCII "Cliptiq pairing transcript v1" followed by one zero byte.
Then concatenate the following tagged fields. Each has tag u8, length u32 and
the complete field bytes; order is independent of arrival order:

1. encoded Hello A
2. encoded Hello B
3. RoundOne A
4. RoundOne B
5. RoundTwo A
6. RoundTwo B

Both Hello session IDs must match, device IDs differ, and nonces differ.
The transcript is bounded to 32768 bytes. Let H = SHA256(transcript) and S be
the 32-byte library mbedtls_ecjpake_derive_secret result. HKDF below is
HKDF-SHA256(IKM=S, salt=H, info=the exact ASCII label, length=32):

| Purpose | Exact info label |
|---|---|
| persistent PSK | Cliptiq pairing PSK v1 |
| A proof key | Cliptiq pairing proof initiator v1 |
| B proof key | Cliptiq pairing proof responder v1 |
| A control MAC key | Cliptiq pairing control initiator v1 |
| B control MAC key | Cliptiq pairing control responder v1 |

Proof = HMAC-SHA256(role proof key, H). A verifies only the responder proof,
B only the initiator proof, using constant-time comparison. Once the context
has bound a transcript hash it cannot switch to another transcript.

## Authenticated control messages

Only Prepared through Abort use this envelope, and only after verified proofs.
The outer frame keeps its kind and session ID. Its payload is:

    sequence u64 || original payload || tag[32]

The role-specific tag is HMAC-SHA256(control MAC key,
sequence u64 || encode_pairing_frame(original clear frame)). Thus it covers
the kind, session, lengths and body as well as sequence. Sequence numbers
start at 1 separately in each direction; accept only the next expected value.
The 8192-byte total cap still applies after adding the 40-byte envelope.
An initiating party's message cannot be reflected as a responder message.

Control metadata is not encrypted; it contains no code, PSK or clipboard data.
The PAKE protects the low-entropy code against offline password guessing.
The session layer additionally bounds online attempts and expires codes after
120 seconds. Persistent PSKs are stored only in the OS credential store.

## Verification

lan_pairing_wire_test pins the exact frame vector, every truncated prefix,
length/version/kind errors and canonical transcript changes. Crypto tests
exercise a complete exchange, wrong code, changed/replayed transcript, proof
role separation, control tampering/replay/reflection, key-export gating and
the pinned upstream tests/suites/test_suite_ecjpake.data round-one vectors.
No test vector in this document is a real user credential.

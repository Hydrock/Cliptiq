# LAN Discovery and Code Pairing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Find nearby Cliptiq computers with DNS-SD, pair through an expiring code, and retain manual connections under a disclosure control.

**Architecture:** A bounded discovery registry and pairing state machine are shared C++17. Native DNS-SD adapters and a separate nonblocking control listener supply events; UI-thread storage operations persist staged credentials and pairs before the existing TLS transport can use them. Native views consume one controller snapshot and keep existing clipboard handling unchanged.

**Tech Stack:** C++17, AppKit, Win32, DNSService/DnsService APIs, pinned Mbed TLS 3.6.7 EC J-PAKE + HKDF/HMAC SHA-256, SQLite settings, existing system credential adapters.

**Spec:** docs/superpowers/specs/2026-11-13-lan-discovery-pairing-design.md (approved by user).

## Global Constraints

- macOS and Windows share the scenario; Linux manual sharing remains supported and unavailable discovery is explicit.
- Discovery is opt-in and off for old/new profiles until enabled. Scan duration 10 s; code 6 digits, lifetime 120 s, at most 3 attempts per code.
- Discovery does not authorize peers; code/PSK never appears in DNS-SD, clipboard, SQLite, errors or diagnostics.
- No TLS 1.2 downgrade, no homemade PAKE, no IP sweep, no firewall/VPN/route changes.
- Preserve existing device IDs, manual peers, key references, delivery policies, history and fullscreen popup behavior.
- No GUI-thread blocking sockets. No real clipboard or normal-profile credentials in automated tests.
- Keep the requested shared feature/windows-workspace branch. Before installation remove the previous app; keep only /Applications/Cliptiq.app registered and archive staging apps in .noindex.

## Review Focus

1. Repeated/forged announcements and identical names: bounded registry, stable selection, no replacement of trusted identity (Tasks 1, 3).
2. Wrong interface, VPN, IPv6 scope and removed adapters: select a reachable local interface without silently rebinding existing pairs (Tasks 3, 5).
3. Out-of-order/replayed pairing messages and code expiry: no key/pair activation before mutual proof (Tasks 2, 4).
4. Credential-store/SQLite failure and lost final ACK: retry/cancel safely, no active one-sided unauthenticated sharing (Tasks 4, 5).
5. Close/reopen settings during discovery/pairing: cancel ephemeral work, retain only explicitly saved availability, no stale callback touching a destroyed view (Tasks 6–8).

## File and interface map

- platform/lan/Discovery.hpp/.cpp: DTOs, validation, registry and adapter interface.
- platform/lan/DiscoveryMac.mm, DiscoveryWindows.cpp, DiscoveryUnavailable.cpp: native publication/browse/resolve lifecycle.
- platform/lan/PairingCrypto.hpp/.cpp: Mbed TLS PAKE rounds, transcript-bound key derivation and proofs.
- platform/lan/PairingWire.hpp/.cpp and docs/LAN_PAIRING_PROTOCOL.md: bounded frame format and canonical transcript.
- platform/lan/Pairing.hpp/.cpp: event-driven pairing sessions and isolated TCP control worker.
- platform/lan/DiscoveryService.hpp/.cpp: discovery/session orchestration, UI snapshots and persistence hooks.
- platform/lan/Service.hpp/.cpp: existing clipboard service integrates successful pair changes, nothing else.
- core/include/cliptiq/lan.hpp, core/src/lan.cpp: availability preferences and pending-pair metadata in existing settings storage, no secrets.
- platform/LanSettings.hpp, LanSettingsDraft.hpp and native LanSettings views: shared callbacks and native controls.
- platform/MenuHost.cpp: wire the controller into native settings and poll.
- cmake/Lan.cmake, LanTls.cmake, lan-mbedtls-config.h: adapter sources, Dnsapi linkage and ECJPAKE module.
- resources/macos/Info.plist.in, resources/locales/en.json/ru.json: usage description, exact Bonjour service type, copy.

The DTOs below live in namespace cliptiq::lan. Use existing Result<T>, Bytes and error conventions.

### Task 1: Discovery registry and saved opt-in

**Interfaces:**
- DiscoveryDevice { device_id, name, platform, host, address, interface_address: string; interface_index: uint32_t; control_port, data_port: uint16_t; expires_ms: int64_t }.
- DiscoveryPreferences { enabled: bool=false; interface_id: string; revision: uint64_t=0 }.
- load_discovery_preferences(Store&) -> Result<DiscoveryPreferences>; save_discovery_preferences(Store&, const DiscoveryPreferences&) -> Result<void>.
- DiscoveryRegistry(local_id: string); update(DiscoveryDevice, now_ms: int64_t) -> Result<void>; remove(device_id: string, interface_index: uint32_t); expire(now_ms: int64_t); devices() -> vector<DiscoveryDevice>.
- Preserve endpoint alternatives for one device rather than trusting the newest address; expose one selected compatible endpoint per ID to UI. Maximum 64 devices; name 256 UTF-8 bytes; service metadata 1024 bytes; resolve lifetime 30 s unless native removal arrives sooner.

- [x] Write tests/unit/lan_discovery_test.cpp: opt-in false and absent keys, old Config round-trip unchanged, expiry/removal, self rejection, equal names/different IDs, forged ID/invalid UTF-8/control characters, duplicate endpoints, capacity 64.
- [x] Register lan_discovery test in cmake/Lan.cmake; run it and observe missing APIs/failure.
- [x] Implement registry/validation and preferences under settings keys lan.discovery.enabled/interface/revision. Availability changes are explicit immediate actions; opening/editing other fields has no network side effects.
- [x] Run lan_discovery, lan and settings tests; confirm existing profile/peer fixtures unchanged.
- [x] Commit the independently tested registry/preferences change.

### Task 2: PAKE and exact wire protocol

**Interfaces:**
- PairingRole { Initiator, Responder }.
- PairingCrypto(role: PairingRole, code: string); round_one() -> Result<Bytes>; read_round_one(const Bytes&) -> Result<void>; round_two() -> Result<Bytes>; read_round_two(const Bytes&) -> Result<void>; derive(context: Bytes) -> Result<Bytes>; proof(context: Bytes, role: PairingRole) -> Result<Bytes>; verify_proof(context: Bytes, role: PairingRole, proof: Bytes) -> Result<void>.
- PairingHello { session_id, device_id, name, platform, data_address: string; data_port: uint16_t; nonce: Bytes(32) }.
- PairingFrame { kind: enum; session_id: string; payload: Bytes }; encode_pairing_frame/decode_pairing_frame -> Result<Bytes>/Result<PairingFrame>. Maximum payload/frame 8192 bytes, bounded length prefix, no trailing data/unknown version.
- Message kinds: Request, Accept, RoundOne, RoundTwo, Proof, Prepared, Commit, Committed, Done, Abort. Code is never a frame field.
- Transcript order is canonical: initiator hello, responder hello, initiator R1, responder R1, initiator R2, responder R2. Role labels separate both proof HMACs and the final PSK; version, both IDs/nonces and data endpoints are included. No arrival-order hashing.

- [ ] Write tests/unit/lan_pairing_crypto_test.cpp and lan_pairing_wire_test.cpp: matching code derives equal 32-byte keys, wrong code fails proofs, altered/reordered hello/rounds/roles/session fail, replay nonce fails, invalid frame sizes/fields rejected, upstream PAKE vectors exercised.
- [ ] Observe RED before adding APIs/module; document wire and transcript bytes in docs/LAN_PAIRING_PROTOCOL.md with deterministic non-secret test vectors.
- [ ] Enable MBEDTLS_ECJPAKE_C only. Implement wrappers with P-256/SHA-256, secure RNG, constant-time proof compare and secret zeroization; preserve the existing TLS 1.3-only config. Confirm the pinned library supports the selected calls before using them.
- [ ] Run crypto/wire and existing lan_tls_transport tests; test module build on Mac and compile checks on Windows CI when available.
- [ ] Commit the protocol/wrapper with vectors. Stop and report if the pinned library cannot implement it; do not improvise a replacement PAKE.

### Task 3: Native DNS-SD adapters

**Interfaces:**
- DiscoveryAnnouncement { device: DiscoveryDevice; }.
- DiscoveryEvent { kind: Found/Removed/Failure; device: DiscoveryDevice; message: string }.
- NativeDiscovery: publish(optional<DiscoveryAnnouncement>) -> Result<void>; browse(duration_ms: int64_t=10000) -> Result<void>; stop_browse(); poll() -> vector<DiscoveryEvent>; stop(); capability() -> Capability.
- Service type _cliptiq._tcp, domain local.; TXT: v=1, id, name, os, dp (data port). SRV port is control port. Never import a host/endpoint supplied through TXT.

- [ ] Add adapter boundary fixtures for callback-after-cancel, partial resolve, malicious TXT, same name on two interfaces, IPv6 scoped addresses, denied/missing local networking, and publication with zero peers.
- [ ] Run boundary tests RED with fake DNS-SD callbacks; on Mac exercise two isolated announcements/browsers with no clipboard traffic.
- [ ] Implement DNSServiceRegister/Browse/Resolve/GetAddrInfo on Mac; DnsServiceRegister/Browse/Resolve with cancellation lifetime ownership and Dnsapi on Windows 10+. Keep callbacks queued and bounded. Linux unavailable adapter returns a user-readable capability, not a simulated successful search.
- [ ] Verify no background browse after 10 s, publication stays only while opt-in is enabled, resolved endpoints follow the announced interface and no own device appears. Add NSBonjourServices and localized usage description.
- [ ] Commit adapters/tests and record which native platforms were actually run.

### Task 4: Pairing sessions and control listener

**Interfaces:**
- PairingPeer { hello: PairingHello; remote_address, local_address: string }.
- PairingEvent { kind: Incoming/Code/EnterCode/Prepare/Commit/Complete/Failed; session_id: string; peer: PairingPeer; code, message: string; key: Bytes }. Code/key are in-memory only; key only at authenticated Prepare.
- PairingServer: listen(local_address: string, data_port: uint16_t) -> Result<uint16_t> (OS-assigned control port); connect(DiscoveryDevice) -> Result<string>; accept(session_id: string) -> Result<void>; enter_code(session_id: string, code: string) -> Result<void>; prepared(session_id: string, result: Result<void>); committed(session_id: string, result: Result<void>); cancel(session_id: string); poll() -> vector<PairingEvent>; stop().
- Internal pure session class consumes validated frames, commands and injected clock/RNG. Native worker uses bounded nonblocking sockets and produces only queued events.

- [ ] Add tests/unit/lan_pairing_session_test.cpp: explicit approval on B before code, correct/wrong/expired code, cancel, duplicate frames, replay/out-of-order proofs, 120 s expiry, one active receiver code, at most 3 guesses; rate caps keyed by real address and globally, not claimed ID.
- [ ] Observe RED; implement the pure state machine before sockets. Fix rate caps at 3 new requests/source/min and 10 total/min, max 4 pending sockets, max 64 queued events and 64 KiB queued wire/session; no clipboard references in this subsystem.
- [ ] Add loopback tests/integration/lan_pairing_control_test.cpp with fake storage completion. Implement the isolated control worker: bounded frames, partial writes, no GUI waits, 10 s idle socket timeout and full cancellation.
- [ ] Prove simultaneous requests do not create two active pairings; new attempts have new session/code/nonces. Test disconnect before/after Prepare and final ACK loss/retry. PAKE failures expose no reason that discloses the code.
- [ ] Commit session/control implementation and tests.

### Task 5: Staged storage and clipboard-service integration

**Interfaces:**
- PendingPair { session_id, peer_id, remote_device_id, secret_ref, name, host, bind_address: string; port: uint16_t; expires_ms: int64_t; phase: Prepared/Committed } (no key/code).
- stage_pair(Store&, const PendingPair&) -> Result<void>; commit_pair(Store&, session_id: string) -> Result<void>; finish_pair(Store&, session_id: string) -> Result<void>; abort_pair(Store&, session_id: string) -> Result<void>; pending_pairs(Store&) -> Result<vector<PendingPair>>.
- Store pending metadata under lan.pairing.<session_id> settings keys with bounded serialized format. Prepared peers are disabled; commit inserts/enables the actual Config peer transactionally. Existing unrelated peers are not rewritten. Only newly created session credentials may be erased on abort.
- DiscoveryService(Store&, Service&, CapturePlatform): initialize(); snapshot() -> DiscoverySnapshot; available(bool) -> Result<void>; search()/stop_search(); connect(device_id)/accept(session_id)/enter_code(session_id,code)/cancel(session_id); poll() -> bool; close_view(); stop().
- DiscoverySnapshot contains availability/capability/search state, devices, request/code/session state, connected peer summaries and a localized message; no permanent keys.

- [ ] Write tests/integration/lan_discovery_service_test.cpp with fake adapters/clock/secret store: unavailable store, failed SQLite commit, restart with pending metadata, abort cleanup, duplicate same remote ID, secret never in SQLite/log/announcement.
- [ ] Observe RED; implement stage/commit/finish/abort using existing settings and Config transaction helpers. Cap pending records at 4; expiry 5 min for interrupted commits. Add Service refresh/reload hook after a successful pair commit.
- [ ] Wire Prepare -> system secret store -> disabled pending record -> Prepared wire. Initiator sends Commit only after both prepared; responder commits then acknowledges; initiator commits then sends Done. Missing Done can be recovered idempotently; successful authenticated data TLS identity also finalizes pending metadata. Abort before any successful data handshake removes only this session's peer/key; on restart committed pending pairs reconnect to finish before expiry.
- [ ] Implement interface selection from DNS-SD interface/route, preserve explicit working bind settings, require a named-interface choice on conflict. Do not listen on all interfaces as an implicit fallback. Saved opt-in starts publication/control listener without requiring peers; ordinary data service remains existing TLS-only.
- [ ] Run service, storage, loopback and previous LAN tests; commit integration.

### Task 6: Shared UI model and macOS Network screen

**Files:** platform/LanSettings.hpp, LanSettingsDraft.hpp, platform/macos/LanSettings.mm, platform/MenuHost.cpp, resources/locales, tests/integration/macos_lan_settings_test.mm.

**Interfaces:** Add NativeLanSettings::discovery(callbacks) with DiscoveryUiCallbacks { snapshot, available, search, stop_search, connect, accept, enter_code, cancel, close_view }. NativeLanSettings::process_events updates UI only on changed snapshot. Pairing acceptance/code windows belong to the controller lifetime, not raw view pointers.

- [ ] Extend fake native tests RED: one discovery action, result rows/duplicate names, receiver approval/code input, success/error, collapse/expand manual block, old Save/Apply still works, view closed during an outstanding callback.
- [ ] Build the native top-level availability/search/list/connected sections, collapse manual controls by default, keep manual draft in memory across snapshot updates. Do not rebuild all text/key widgets on every poll. Put persistent outcome/error text beside the main actions.
- [ ] Bind controller in MenuHost after required startup access; poll alongside existing Service. Opening settings is read-only; availability toggle persists only its explicit opt-in; closing cancels ephemeral sessions/search, not saved publication.
- [ ] Generate en/ru catalogues; run EN/RU native tests plus fullscreen owner/paste-boundary checks. Do not add implementation jargon to UI copy.
- [ ] Commit macOS UI/controller wiring.

### Task 7: Windows UI and Linux fallback

**Files:** platform/windows/LanSettings.cpp, platform/linux/LanSettings.cpp, tests/integration/lan_settings_test.cpp, native discovery adapter test fixtures, cmake/Lan.cmake.

- [ ] Add Windows native boundary tests RED for the same callbacks/states, repeated snapshot updates, stale pixels while scrolling, disclosure sizing/hit targets, close/reopen and focus in code input.
- [ ] Implement the same flow with Win32 controls, preserving the recent embedded scrolling/layout fixes; use the common shared UI callbacks and validation. Linux shows explicit unavailable discovery with retained manual controls; no dummy device list.
- [ ] Build Windows through the repository CI/toolchain if available. Run Windows native/controller tests on Windows; otherwise mark runtime acceptance open and arrange a real Mac↔Windows user check rather than claim completion from Mac alone.
- [ ] Re-run old manual LAN/native settings coverage to ensure stored peers remain editable and unchanged.
- [ ] Commit Windows UI and fallback adapters.

### Task 8: End-to-end, documentation and installation

- [ ] Add loopback two-instance test: opt-in, discover, approve/code, persist both keys, exchange new text/image, restart, reconnect, changed endpoint, final ACK loss, credential-store failure; isolated temporary profiles and fake/isolated clipboard only.
- [ ] Run full Debug and Release suites, locale generation check, diff check and native bundle validation. Update docs/LAN_CLIPBOARD.md, LAN_VALIDATION.md and MACOS_HANDOFF.md with what was actually tested and known VPN/system-service limitations.
- [ ] Request an independent whole-change code review, fix actionable findings and re-run affected checks. Security review must cover transcript/proof binding, guessing/replay limits, session/storage cleanup and clipboard authorization boundaries.
- [ ] Install verified Mac app only after removing the preceding package/registrations as user requested. Archive staging .app copies outside Spotlight; do not delete history/settings/credentials. Check one installed copy and both required OS permissions.
- [ ] Real Mac↔Windows user acceptance: search without IP, code pairing, both directions of text/images, restart, same-name devices, manual fallback, fullscreen quick menu. Keep any unverified platform/step explicitly open.

## Self-review

Spec covered by Tasks 1–8; the five Review Focus items map to explicit tests above.
Discovery snapshots do not contain permanent keys; pairing key events stay within the
controller and system secret adapter. The data transport API/protocol remains unchanged.
The user has approved implementation; execute natively in this session and use one
independent review at the end. No second product-approval pause is needed for routine
implementation details already bounded by this plan and the approved design.

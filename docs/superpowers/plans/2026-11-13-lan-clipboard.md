# LAN clipboard implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development task by task; review every task and the whole branch.

**Goal:** Deliver approved opt-in peer LAN clipboard sharing with native advanced settings, source labels and clipboard/history receive modes.
**Architecture:** Core owns protocol/config/provenance. One lazy TLS worker owns sockets and its own Store. Native UI/clipboard remain on owner thread. All peer credentials use native secret storage.
**Tech Stack:** C++17, SQLite, native Win32/AppKit/GTK, pinned Mbed TLS 3.6 LTS TLS1.3 PSK (selected after live handshake spike).
**Spec:** docs/superpowers/specs/2026-11-13-lan-clipboard-design.md

## Global constraints

- Device addresses explicit; no cloud/discovery/UPnP. No peer => no sockets/threads/queues/DNS.
- Receive default clipboard+history, configurable history-only. Never synthesize paste on arrival.
- Text/URL/PNG only, no old history export, files or relay. 16 peers, 8MiB event, 16MiB queues; image8Mpixel/dimension8192.
- Reject known concealed/transient; accepted local captures only. Per-event IDs and persistent sequence high-water prevent echoes and ACK replays.
- Secrets never SQLite/logs. Unique 32-byte random PSK per pair; local configured aliases, remote ID pinned after authenticated hello.
- Each core Store is single-thread-owned. Payload/native clipboard UI calls remain UI-thread-owned.
- Do not publish/push. macOS native compilation unavailable here; add native tests and handoff.

## Review focus

1. Remote arrival racing local copy: snapshot native token at receive start, check again on UI delivery; history survives write failure.
2. Delayed UI/stop during receipt: checkpoint requests bounded, cancellation wakes worker before join.
3. Simultaneous channels/DNS/offline: exactly one channel per pair, bounded backoff and no replay backlog.
4. Identical payload from two devices: distinct per-event provenance and no overwriting local origin under duplicate preferences.
5. Configuration/credential errors: do not partially expose listener or persist plaintext; stop promptly on pause/key changes.

## Contracts

Core namespace `cliptiq::lan`, public header `core/include/cliptiq/lan.hpp`:
- `ReceiveMode { ClipboardAndHistory, HistoryOnly }`.
- `Peer { string id,name,host,remote_device_id,secret_ref,auth_id; uint16_t port; bool enabled,send,receive; }`.
- `Config { string device_id,device_name,bind_address; uint16_t port; bool paused,text,urls,images; ReceiveMode receive_mode; uint64_t revision; vector<Peer> peers; }`.
- Default bind127.0.0.1, port49731, pausedfalse, formatsalltrue, defaultreceiveClipboardAndHistory. Effective active only enabledpeer with directions +savedsecretref/authid.
- `load_config(Store&)`, `save_config(Store&,const Config&)`, `validate_config(const Config&)`, `active(const Config&)`, `new_id()`, `key_fingerprint(string64hex)`.
- `Event { string origin_device_id,event_id; uint64_t sequence; int64_t created_ms; vector<Representation> representations; }` canonical types network: text/plain UTF8, text/uri-list UTF8, image/png.
- `portable_snapshot(const ClipboardSnapshot&, const Config&, function<Result<Bytes>(const Representation&)> encode_png)`; native conversion `native_snapshot(const Event&,CapturePlatform)`.
- `make_event(Store&,vector<Representation>,int64_t)`, `encode_event(Event)`, `decode_event(Bytes)`, return Result values.
- `bind_peer_identity(Store&,peer_id,remote_device_id)` pins empty identity or rejects mismatch/self.
- `Arrival { bool duplicate; optional<EntryId> entry_id; ReceiveMode receive_mode; }`; `accept_event(Store&,peer_id,Event,HistoryPolicy,int64_t)` atomic history+origin+seen/highwater, separate remote entries even identical payload; normal local duplicate policy unchanged.
- `is_remote_entry(Store&,EntryId)` and `entry_origin(Store&,EntryId)` stringdisplay. `MenuItem.source_device` string populated by build_menu, UI displays without mutating source_app.
- APIs validate max payload/identifiers and PNG IHDR dimensions; reject unsupported wire formats.

Transport namespace `cliptiq::lan`, `platform/lan/Transport.hpp`:
- `TransportPeer { string id,host,remote_device_id,auth_id; uint16_t port; Bytes key; bool send,receive; }`.
- `TransportConfig { string device_id,bind_address; uint16_t port; vector<TransportPeer> peers; }`.
- `TransportCallbacks { function<Result<void>(string peer,string remote_id)> identity; function<Result<string>(string peer)> receiving; function<Result<void>(string peer,Bytes,string checkpoint)> received; function<void(string peer,string status)> status; }`.
- `class Transport { start(config,callbacks)->Result<void>; stop(); publish(shared_ptr<const Bytes>); active()->bool; }` no thread/resources constructor. Receiving checkpoint callback before full body read, opaque token passed through; ACK only after received persistence succeeds. Disconnect drops unsent backlog; no replay on reconnect. Auth ID derived PSK fingerprint public identity; never logs key/content. TLShello deviceID verifies expected ID; dedup simultaneous channels lexicographically.
- `platform/lan/Secrets.hpp`: `read_secret(ref)->Result<string>`, `write_secret(ref,key64hex)->Result<void>`, `erase_secret(ref)`, `generate_key()->Result<string>` native OS CSPRNG and stores.

Native settings: `platform/LanSettings.hpp`, `NativeLanSettings` in namespacecliptiq:
- `show(lan::Config, function<Result<void>(lan::Config,map<string,string> peer_keys)>, function<Result<string>()> generate, function<Result<void>(string)> copy_secret)->Result<void>`.
- `process_events()->Result<void>`, `status(string)`; no resources untilshow.
- `NativeSettings::advanced(function<void()>)` opensnetworkwindow throughonebuttoninBeta/More. No ordinary-tab expansion.
- UI creates peerid via lan::new_id(), secret_ref and auth_id finalized by controller save callback. Keyfields masked, don't exportplaintext. Copy callback marks transient+concealed using existing clipboard flags.

### Task1 Core and provenance
- [x] Add failing tests protocol, config/migration, maliciousframes/UTF8/PNG, sequence replay, peer pinning, identicalpayloadorigin, deletecleanup.
- [x] Implement corelan sources and migration3, bounded eventwire and canonical/native transformations.
- [x] Run tests; review API and persistence invariants. Do noteditCMake; parent wires sources.

### Task2 TLS transport and native secrets
- [x] Fetch verified pinned official Mbed TLS release, configureminimal TLS1.3 PSK, actualhandshake spike.
- [x] Implement nonblocking singleworker transport, framedhello/data/ack, reconnect, queues, socketcancel, peerpin callbacks.
- [x] Add Windows Credential Manager, macOSKeychain, LinuxSecretService providers; no plaintextfallback.
- [x] Test two real loopbackTLSinstances, wrongkey, malformed/oversize, partialIO, simultaneouschannels, stopoff, newcopy afterreconnect andnooldreplay. No productionLANlistener started without user config.

### Task3 Native UI and source labels
- [x] Add isolated nativeadvancednetworkwindows onWin/Mac/Linux using contracts; maskedkeys/generate/copy, peers CRUD, modes/directions/formats, bind+port, pause,status.
- [x] Add Moreadvancedcallback allplatforms and source labels mainworkspace+quickmenu.
- [x] Test configdraft/cancellation and untranslated/longlabel geometry whereenvironmentpermits.

### Task4 Application controller, codecs and build
- [x] Wire core/TLS/CMake and dependencyfetch/hash/license; keep configureoffline and featureOFF comparison.
- [x] Implement LanService controller ownStoreworker, acceptedcapturefeed outgoing, secretpreparetransaction and reconfigure, UIcheckpointqueue, arrivaldelivery, transientsecretcopy.
- [x] PNG nativeconversion withboundeddecodedsize beforealloc; WindowsWIC/AppKitImageIO/GdkPixbuf withoutnewcodec runtime.
- [x] Wire hoststartup/settings/poll/shutdown and newcapture IDs only; no oldhistory broadcasts.
- [x] Integrationprocess tests withseparateprofiles and fakeUI/clipboard, then Windowsnative test onnamedclipboard whenavailable.

### Task5 Review, resources, handoff
- [x] Run full availableWindowsregressions + newprotocol/TLS/service suites.
- [x] Review security/privacy, eventrace/cancel/restart, disabledresourcecount andboundedmemory.
- [x] Compare release EXE size ON/OFF and background CPU/memory; measure connected Services idle. Document limits and unverified platforms.
- [ ] Physical-network throughput and native cross-OS acceptance: requires target machines; see docs/LAN_VALIDATION.md.
- [x] Update MACOS_HANDOFF and usernetworkguide. Commit implementation onfeature/lan-clipboard only. No push/release.


## Verification ledger

See docs/LAN_VALIDATION.md for exact commands, 48/48 available Windows tests,
resource results and exclusions. Physical native clipboard, cross-OS acceptance,
macOS/Linux builds and real-network throughput remain explicitly pending;
checked tasks mean v1 code and the available Windows verification were delivered,
not release acceptance. Three-device real TLS Service integration uses synthetic
clipboard hooks. Native credentials CRUD passed on Windows. User profile untouched.

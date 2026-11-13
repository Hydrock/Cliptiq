# LAN v1 validation — 2026-11-13

Branch: feature/windows-workspace; Windows 11 build 26200, x64 MSVC 18 Build Tools,
MinSizeRel, static CRT and IPO. The Windows results below predate native Mac validation; see the macOS section
for current build/test evidence. Linux native acceptance remains open. No cross-OS
or physical LAN/VPN test is claimed.

## Checks performed

- Full Windows target build with CLIPTIQ_LAN_SYNC=ON passed.
- Control application build with CLIPTIQ_LAN_SYNC=OFF passed.
- CTest available regression subset: **48/48 passed**, 28.51 seconds.
- Real Windows Credential Manager temporary random-reference CRUD passed with
  CLIPTIQ_TEST_NATIVE_SECRETS=1; test credential was deleted.
- Pinned Mbed TLS archive AND local source verification passed offline.
- Native Russian UI visually checked: advanced entry in More, default disabled,
  field/button labels, scrolling, peer draft and cancellation. Test profile only;
  no actual user peer was configured and no user history was migrated.
- Independent final code review found no new correctness blocker; missing CI
  paths for LAN-only changes were added.

CTest subset command (private writable TEMP avoids inherited fixture ACLs):

```powershell
ctest --test-dir out/windows-lan --output-on-failure --parallel 4 -E '^(editor_native|hotkeys_native_boundary|settings_native|lifecycle_ui_ru|hotkeys_native|paste_native|menu_regression_keyboard|menu_regression_dpi|menu_regression_context|menu_native|windows_clipboard|update_authenticode)$'
```

These 12 exclusions are existing environment limitations, not passing tests:
restricted window-station access (Win32 error 5) and the absent positive signed
Authenticode fixture. LAN-specific native settings test did pass. The legacy
snippet migration fixture was corrected to remove v3 LAN tables when constructing
its synthetic v1 database; actual v1→v3 and v2→v3 migration tests pass.

Coverage includes malformed wire payloads/UTF-8/PNG, bounds, migration/CAS/rollback,
replay/pinning, distinct provenance for identical content, large lossless UTF-16,
real TLS wrong-key/identity, partial I/O and 1MiB payload, ACK after persistence,
queues and shutdown, asynchronous DNS cancellation, latest-only backpressure,
startup-copy exclusion, local-copy races, two consecutive remote arrivals, and
three-device full mesh with unique pair keys. Full Service end-to-end test uses
real TLS/image codecs/history and synthetic native clipboard hooks; this does not
replace actual clipboard acceptance between physical machines.

## Resource comparison

Same 1000 synthetic history entries, hidden windows, capture listener active,
payload retention/OCR/updates off; 5s settling and 30s sampling, sequential runs.
These measurements describe this fixture, not image-heavy user histories.

| Application build | EXE bytes | Private MiB | Working set MiB | CPU % of one core |
|---|---:|---:|---:|---:|
| LAN compiled out | 1,774,592 | 6.590 | 18.406 | 0.260 |
| LAN compiled in, no peers | 2,036,736 | 6.582 | 17.953 | 0.104 |

EXE delta: **262,144 bytes (256 KiB)**. No measurable disabled-mode memory penalty
in this short run; CPU differences are below a useful conclusion from one sample.
Service tests assert no transport is constructed without an active saved peer;
source inspection confirms the lazy runtime owns sockets/DNS/thread/payload queues.
A lack of peers does not mean the rest of Cliptiq has no threads or no memory use.

Reproduce:

```powershell
python experiments/windows-idle/measure.py --build-dir out/windows-lan-off --output out/lan-bench/off.json --entries 1000 --seconds 30 --settle 5 --expect-background --workspace
python experiments/windows-idle/measure.py --build-dir out/windows-lan --output out/lan-bench/on.json --entries 1000 --seconds 30 --settle 5 --expect-background --workspace
python experiments/windows-idle/measure-lan-services.py --build-dir out/windows-lan --output out/lan-bench/active.json
```

A separate connected-idle probe ran **three Services in one test process**, each
with two TLS neighbors, for 30s after text/PNG/URL exchange. Combined process:
4.453 MiB private, 13.371 MiB working set, 0.104% of one core, 205–206 handles.
This is not per-app memory and cannot be compared directly to the table above.
Real-network throughput/latency and active main-application memory still require
measurement on target machines; the functional tests are not throughput benchmarks.

## Before release

Build/run macOS and Linux; exercise Keychain/Secret Service locked/unavailable
states; validate Linux GResolver cancellation completion under stalled DNS;
check cross-OS clipboard and local-copy races (best effort CAS outside Windows),
VPN/DNS/firewall, high-DPI UI, realistic large-image memory and throughput.
Windows build is an unsigned experiment, not a production release.
History migration is schema 3; preserve a pre-migration backup for rollback.


## Windows experimental archive

Packaging staged the native dependency inventory and license files, and generated
`out/t17-windows-x86_64-base/cliptiq-0.1.0-windows-x86_64-base.zip`.
ZIP integrity, embedded EXE hash and extracted executable `--version` passed.
Archive: 1,038,777 bytes; app-owned payload: 2,077,835 bytes.
The existing historical 10%-growth gates **failed** (limits: archive 957,588;
app 1,867,254). Baseline was intentionally not raised. Packaging stopped before
MSIX creation. This ZIP is a usable unsigned experiment, not a passing release
package. Primary added binary cost against the current no-LAN control is 256KiB;
the historical budget also predates current native workspace changes.

## macOS native continuation — 2026-11-13

Baseline `c82e74e` on `feature/windows-workspace`, with the local macOS fixes
recorded in `macos-native-parity-verification.md`. Host: macOS 27.2 (26B123),
arm64, Command Line Tools / AppleClang 21.0.0, CMake 4.4.5, Ninja 1.13.2.
Python 3.15.2 was installed through Homebrew because the pinned TLS fetcher uses
`tarfile.extractall(filter="data")`, unavailable in system Python 3.9.6.
The source pin remains Mbed TLS 3.6.7; neither its version nor hash was changed.

### Link fix and reproduction

The first LAN Debug build compiled the Objective-C++ sources, then failed to link
both application targets: `ld: library 'dns_sd' not found`. The Mac SDK re-exports
DNSService APIs from libSystem's `libsystem_dnssd`. A small DNSServiceGetAddrInfo
C probe linked with the default system libraries. Removed the separate macOS
`dns_sd` link from `cmake/Lan.cmake`; Windows definitions and Linux GTK linking
are unchanged. Debug and MinSizeRel all-target builds then completed.
Independent review found no actionable issue with this incremental change.

Commands (Homebrew binaries first in PATH):

```sh
python3 tools/fetch-lan-deps.py
cmake --preset test -DPython3_EXECUTABLE=/opt/homebrew/bin/python3 -DCLIPTIQ_LAN_SYNC=ON
cmake --build --preset test --parallel 4
ctest --preset test --output-on-failure
cmake --preset release -DPython3_EXECUTABLE=/opt/homebrew/bin/python3 -DCLIPTIQ_LAN_SYNC=ON
cmake --build --preset release --parallel 4
ctest --preset release --output-on-failure
CLIPTIQ_TEST_NATIVE_SECRETS=1 out/core-Darwin-test/lan_secrets_test
python3 tools/fetch-lan-deps.py --verify-only
```

### Evidence and boundaries

- Debug: **53/53 passed**, no exclusions, 52.20 seconds.
- MinSizeRel: **53/53 passed**, no exclusions, 50.96 seconds.
- Native Keychain opt-in CRUD passed using a random temporary reference; the
  test removed its credential. Existing saved peer keys were not used.
- Pinned archive and all extracted build inputs passed offline verification.
- LAN suite includes core migration/wire validation, service behavior, actual
  loopback TLS/image/history exchange, TLS failure cases, DNS IPv4/IPv6/localhost
  and cancellation, plus native secret validation.
- End-to-end tests use synthetic clipboard hooks; they do not prove physical
  Mac↔Windows clipboard exchange. Cross-OS/VPN/firewall, actual AppKit LAN dialog,
  locked-Keychain behavior and production consent prompts remain manual gates.
- Release `.app` file payload is 2,279,011 bytes. `otool -L` lists system libraries
  and frameworks only; Mbed TLS is static. This is an unsigned local build.
- No normal application profile was opened or migrated by this validation.
  Tests use their own temporary databases. No user peer was configured.
- Logs: `docs/support/macos-native/2026-11-13-arm64-lan/`.


## Native Mac Network-tab update and reconnect correction

Synced `feature/windows-workspace` to `7527731`, preserving local permission and
icon work. New AppKit tests exercise the embedded Network tab in English/Russian:
10 tabs with LAN callback present, same parent window, local draft/Cancel, secure
key field, invalid port, repeated Save with returned revision, three-peer layout,
peer removal and parent close/reopen. Tests use callback fixtures, no real peers
or credential writes.

The full run exposed intermittent loss of a first live-peer event during
simultaneous connection replacement. Frames were removed before ACK, or disappeared
when the old socket was erased before the replacement HELLO. Retain outgoing
payloads by peer/publication budget until FIFO ACK; replay from offset zero only
after authenticated HELLO, within the original 10s bound. Expiry closes streams
holding that frame, avoiding a delayed untagged ACK consuming a newer frame.
Publications made with no connected peer remain unretained; stop/config restart
clears retention. No offline disk queue was added.

The deterministic reconnect test failed before peer-level retention, then passed.
It also checks pre-ACK backpressure, ACK release and expiry recovery. Existing
no-offline-replay assertions remain; the immediate Service exchange passed 20
consecutive repeats. Debug full suite: 61/61 (56.66 s). Log bundle:
`docs/support/macos-native/2026-11-13-arm64-startup-network/`.
Physical Mac↔Windows exchange, VPN/firewall and Windows rebuild of this common
transport change remain unverified on this Mac.

## Discovery and code pairing, 2026-11-13

New protocol: native DNS-SD `_cliptiq._tcp.local.`, separate bounded TCP control
listener, pinned Mbed TLS EC J-PAKE, transcript-bound mutual proof, authenticated
sequenced commit messages. See LAN_PAIRING_PROTOCOL.md for wire definitions.
TXT additionally advertises `af=4|6` so a resolved address matches the listener's
address family. No key, code or clipboard content is announced.

Locally verified on macOS: pure hostile metadata/cancellation fixtures, isolated
local-only Bonjour with two announcements, PAKE upstream vectors and wrong-code
checks, real loopback control exchange, concurrent requests, storage rollback,
prepared/committed recovery, cancellation and code expiry, EN/RU AppKit discovery
controls with retained drafts/code input. A two-profile end-to-end test discovers,
pairs by code, saves credentials to a fake store, exchanges new text and PNG over
TLS, and reconnects after restarting the data services. Automated tests never read
normal-profile Keychain entries or mutate the real clipboard.

Windows DNS-SD and Win32 UI have source implementation and native fixtures;
Windows compilation/runtime verification and physical Mac↔Windows acceptance are
still open. No Windows pass is inferred from a Mac run. Linux discovery is explicitly
unsupported, with the existing manual path retained. Existing-peer address changes
remain manual; unauthenticated announcements cannot replace a trusted endpoint.

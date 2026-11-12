# LAN v1 validation — 2026-11-13

Branch: feature/lan-clipboard; Windows 11 build 26200, x64 MSVC 18 Build Tools,
MinSizeRel, static CRT and IPO. Native macOS/Linux build and runtime acceptance
remain open; no cross-OS or physical LAN/VPN test is claimed.

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

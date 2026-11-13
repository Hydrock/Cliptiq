# LAN discovery and code pairing — implementation record

13 November 2026, `feature/windows-workspace`.

Implemented: explicit mDNS/Bonjour search, saved opt-in publication, named interface
selection, user approval on the receiving computer, six-digit EC J-PAKE pairing,
transactional staged peer metadata, independent OS-store credentials, collapsed
manual setup, EN/RU native Mac and Windows controls, and authenticated endpoint
migration during a new search. Existing clipboard TLS protocol remains compatible.

## Verification and handoff

- macOS Debug: **73/73**, 80.25 seconds.
- macOS Release/MinSizeRel: **73/73**, 79.42 seconds.
- Isolated native Bonjour → approval/code → staged credentials → TLS text/PNG →
  restart → changed-port authentication passed. Normal clipboard and Keychain
  were not used by these fixtures.
- Independent whole-change review completed; its important findings were fixed
  and covered by focused regressions and both complete Mac suites.
- Windows: all targets compiled at `8655ddf`; all ten new tests and updated native
  LAN UI test passed. Full suite: 68/71. The three failures match the original
  branch exactly: `settings_native`, `lifecycle_ui_ru`, `lan_tls_transport`.
  [Current job](https://github.com/Hydrock/Cliptiq/actions/runs/43400732188/job/131802630947)
  and [baseline](https://github.com/Hydrock/Cliptiq/actions/runs/43337878366/job/131598575885).
- Mac product code at `5ec433f` installed at `/Applications/Cliptiq.app` after
  removing the preceding package. The later SQLite test dependency fix does not
  change that binary. Profile/history/credentials preserved; staging packages
  archived in `.noindex`; one production Launch Services registration verified.
- Installed process is waiting in the startup permissions window. Accessibility
  was reset for Cliptiq to remove stale ad-hoc identity approval. The user must
  complete the visible permission/Continue flow; no granted state is claimed.
- Physical Mac↔Windows search, code pairing, text/images and fullscreen quick-menu
  acceptance remain for the user with the same new code on both computers.

## Decisions and costs

| Decision | Reason and cost |
|---|---|
| Keep the existing shared branch | Explicit user preference; concurrent local changes would need reconciliation. |
| Share Windows Mbed TLS threading/PSA initialization | PAKE can precede a saved TLS peer; Windows initialization ordering needs native verification (new crypto/control tests passed there). |
| Authenticate post-proof control messages with directional HMAC/sequence | Mutual PAKE proof alone does not protect later commit instructions; adds 40 bytes per frame, no data-protocol change. |
| Limit advertised names to 250 UTF-8 bytes | A DNS TXT item is at most 255 bytes including `name=`; exceptionally long names are shortened only for discovery. |
| Allow 120 seconds for human approval/code entry | Initial/partial/stalled socket I/O still times out after ten seconds; one waiting slot can remain occupied for two minutes. |
| Invalidate a code after one wrong attempt | Stricter than the maximum of three guesses; a typing mistake requires a fresh approval/code. Request limits remain three/source/minute and ten globally/minute. |
| Store the public auth fingerprint in pending metadata | Config needs it to select/check the credential; adds one public 64-hex field, never a PSK/code. |
| Keep Windows runtime acceptance explicitly open until executed | Mac fixtures cannot prove Win32 behavior; CI later compiled and passed the new tests, physical firewall/interoperability still requires a device check. |
| Advertise public `af=4|6` | Prevents selecting an address family different from the single listener; one extra public TXT field, no address/key in TXT. Eligible alternatives are retried. |
| Initially defer trusted endpoint migration (superseded) | Avoided trusting unauthenticated metadata but would require manual DHCP/address repair; independent review identified this as an important gap. |
| Add a no-listener TLS/HELLO probe instead | Saved PSK and pinned device identity authorize endpoint changes; up to five seconds per bounded candidate and one brief authenticated connection. No clipboard callbacks or publication API in the probe. |

## Deferred acceptance

Windows physical layout/firewall prompts and real cross-OS discovery are not
established by CI. Linux intentionally retains manual setup with an explicit
unavailable discovery state. No Windows installer/release publication is claimed.

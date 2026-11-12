# Native desktop parity verification — 2026-11-13

Implemented: inline Windows folder/snippet renaming; AppKit main workspace,
settings and snippet-editor parity; native optional sound; unified quick menu and
separate service status menu; lazy/transient UI lifetime and autorelease drainage.

## Windows evidence

- MinSizeRel MSVC/Ninja build succeeded (`out/windows-audit`, all targets).
- 41 available CTest cases passed with a new private TEMP directory; log:
  `out/windows-audit/macos-parity-windows-tests.log`.
- New native rename test initially failed because no tree edit control existed.
  Real queued Enter then exposed dialog interception; after fixing the event
  route the test passes persistence, cancellation, empty-name rejection and
  removal of the separate title field.
- The seven earlier settings/localization/editor checks also passed.
- New Windows build was launched against the existing user profile. Editor was
  visually inspected with the separate name field removed. User data was not
  renamed for testing; rename persistence tests use their own temporary database.

Twelve existing checks were excluded from this environment-specific run:
`editor_native`, `hotkeys_native_boundary`, `settings_native`, `lifecycle_ui_ru`,
`hotkeys_native`, `paste_native`, `menu_regression_keyboard`,
`menu_regression_dpi`, `menu_regression_context`, `menu_native`,
`windows_clipboard`, `update_authenticode`. Earlier attempts in this host hit
window-station access restrictions and an unavailable positive Authenticode
fixture. This is not a claim that the full Windows suite passed.

## macOS evidence at Windows handoff (historical)

Source reviewed, including weak callback ownership, rename identity and pending
save handling, typed settings values, waveform reuse and autorelease scopes.
`workspace_native` and `editor_inline_rename` are now Apple-only CTest targets;
shared `sound_wave` tests also build on Apple. Existing CI covers Apple Silicon
and Intel using the test and release presets.

No macOS SDK/runtime is available on this Windows development host. None of the
new AppKit code has been compiled, executed, benchmarked or packaged here. The
next native check is `cmake --preset test`, `cmake --build --preset test`, then
`ctest --preset test` on macOS, followed by release build and visual/permission
checks on a real Mac. No new CPU/RSS claims are made for macOS.

## Native Mac validation — 2026-11-13

Checkout: `feature/windows-workspace`, baseline `5712fd7` (includes `07b8930`).
Host: macOS 27.2 (26B123), arm64, Command Line Tools / AppleClang 21.0.0.
Installed CMake 4.4.5 and Ninja 1.13.2 using Homebrew. Pinned SQLite 3.53.4
was fetched with the project's SHA256 verification; system Python is 3.9.6.

### Build and runtime fixes

- Debug compilation reproduced ARC's owned-return getter error for `copyButton`
  and a missing `NSSearchFieldDelegate` declaration. Renamed the weak property
  to `actionCopyButton` and declared the search delegate protocol.
- The sound feedback callback expected the Windows event shape. On macOS it now
  reads `event.decision.disposition`, preserving the accepted/retained-ID gate.
- `settings_native`, `lifecycle_ui_ru` and `lifecycle_bundle` reproduced an AppKit
  exception: constraints activated before views had a common ancestor. Add each
  label/control/help/reason to its stack before activating its width constraint.
- Native settings tests still expected eight tabs. They now expect nine and
  check the new Sounds tab's English/Russian label.
- `history_crash` reproduced a system SQLite WAL read-only-open failure after
  production reopen/checkpoint. Its final SQL read uses `query_only=ON`, allowing
  SQLite to manage WAL/SHM sidecars. Rollback/WAL interruption, integrity and
  content assertions remain enabled; both interruption modes pass.

### Verification

Commands, each completed with exit code 0:

```sh
python3 tools/fetch-footprint-deps.py
cmake --preset test
cmake --build --preset test --parallel 4
ctest --preset test --output-on-failure
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release --output-on-failure
```

- Debug: **47/47 passed**, 26.30 s.
- MinSizeRel: **47/47 passed**, 25.83 s. No tests excluded.
- Native workspace, inline rename, clipboard, paste boundary, settings and bundle
  lifecycle checks all passed. Existing warnings (deprecated AppKit calls,
  duplicate static libraries and C++ style warnings) remain.
- Logs: `docs/support/macos-native/2026-11-13-arm64/`.
- `otool -L` reports only system frameworks and `/usr/lib` dependencies.
- Independent review of this continuation's code/test diff: no actionable findings.
- Ruling: used the clean checkout and branch switch prescribed by the handoff,
  rather than creating another worktree. No user changes existed at the switch.

### Remaining manual gates

Release launched using `/private/tmp/cliptiq-macos-review/history.sqlite3` and
`--demo --show-main --locale ru`. The normal profile was not used for this run.
The existing Debug process was left running and untouched. A later non-demo
background launch forwarded to that existing instance and exited; it is excluded
from resource measurements. Demo mode is necessary to bypass the shared
single-instance scope for the isolated measurement process.

Computer Use inventory sees Cliptiq, but binding either app path repeatedly
returned `timeoutReached`. No screenshot or manual interaction could be obtained.
Therefore visual Windows parity, tray clicks, theme/Retina/multiple displays,
permission prompts, paste into another app and audible sound are **not verified**.
Native automated checks do not replace those gates. Repeated UI open/close
resource comparisons also remain open. This is not release acceptance.

### Resource observations (preliminary, not an idle budget)

MinSizeRel `.app`: 1,891,747 file bytes (1.80 MiB), 1,852 KiB allocated by
`du -sk`. The binary was not distribution-signed/notarized by this continuation.

- Main-window-requested demo process: one snapshot, RSS 110,272 KiB, physical
  footprint approximately 30 MB. A subsequent time-series attempt was interrupted
  before completion and is excluded.
- Separate process with `--demo --background --capture`, same temporary profile:
  five-second warm-up, then seven `ps` samples over 30.061 seconds. RSS ranged
  88,368–146,576 KiB; CPU time grew from 0.22 to 1.92 s (about 5.7% of one core
  over this interval). Final physical footprint approximately 60 MB.
- RSS was still increasing; this is **not** evidence of stable low-resource idle.
  Capture was requested, but consent/capture state and visible windows could not
  be independently inspected. User activity and other desktop apps were not
  controlled. A longer controlled run plus repeated UI cycles is required before
  attributing growth to a leak or stating a memory budget.
- Both measurement processes started by this continuation were terminated after
  measurement. The pre-existing Debug process was not terminated.

Next: restore native UI inspection, complete the handoff's manual checklist, then
repeat background/window/menu cycles with consent state and workload recorded.

### Updated LAN branch validation

After fast-forwarding the shared branch to `c82e74e`, both Debug and MinSizeRel
were rebuilt with `CLIPTIQ_LAN_SYNC=ON`. The only new source fix was removing
macOS `-ldns_sd` (DNSService APIs are re-exported by libSystem). Both full suites
passed **53/53**, with native Keychain CRUD also passing separately. See
[LAN_VALIDATION.md](LAN_VALIDATION.md#macos-native-continuation--2026-11-13)
for commands, logs and scope. The earlier 47-test runs and resource numbers above
are historical evidence from before LAN and do not describe the updated binary.

### Consistent workspace/editor language — 2026-11-13

The reported screenshot exposed hardcoded Russian workspace strings while the
snippet editor correctly used the selected application locale. Workspace chrome,
empty states, type names, count formatting and workspace status messages now use
the shared en/ru catalogue. User history/snippet names and contents are unchanged.
The selected locale is preserved; no user's settings database was changed.
Language changes require a full application restart, as before.

Regression reproduction: the English workspace test failed on its History button;
Russian workspace and both localized editor variants passed. After the fix the
full Debug suite passed **55/55** (51.86 seconds), including `workspace_native_ru`
and `editor_inline_rename_ru`. Catalogues pass `generate-locales.py --check`.
Independent review found no actionable findings. Logs are under
`docs/support/macos-native/2026-11-13-arm64-localization/`.
Full MinSizeRel suite also passed **55/55** (50.98 seconds). The updated
release app is in `out/core-Darwin-release/Cliptiq.app`; restart the running app
to load it, and choose Russian in language settings for a Russian interface.

### Settings button report — 2026-11-13

The running Debug process was launched at 02:02:54, before the corrected Debug
and release binaries linked at 02:23. Its sampled main thread was processing the
old event loop, not running the rebuilt executable. Native Computer Use binding
still timed out, so manual interaction with that old process was unavailable.

Extended the native workspace test to press the actual Settings button, open a
real NativeSettings window, force AppKit layout, pump events, check visibility
and absence of button highlighting, then close and repeat. Both English and
Russian routes pass. A preliminary assertion on NSButton.state was rejected:
an untouched AppKit momentary button also retains state=1 after performClick;
`highlighted` is the relevant pressed visual state. No production change was
needed in this step; the earlier settings constraint fix remains in the build.
Four targeted tests passed in Debug and release; logs are under
`docs/support/macos-native/2026-11-13-arm64-settings-button/`.

The old user's process was left running to avoid losing possible unsaved edits.
The existing schema-2 profile was backed up using SQLite's live backup API to its
own local backups directory and integrity-checked before the schema-3 upgrade.
A full quit and launch of the new release remains required on the user's desktop.

### Quick menu over fullscreen applications — 2026-11-13

The popup was unanchored (`inView:nil`), with no owner configured to join another
application's fullscreen Space. It now uses a temporary transparent nonactivating
NSPanel at the requested pointer position, above normal windows, with
CanJoinAllSpaces, FullScreenAuxiliary and CanJoinAllApplications (macOS 13+).
The panel cannot become key/main, does not activate the app or open the workspace,
and is hidden/closed by the popup cleanup guard on cancellation or selection.
It exists only during menu tracking; original paste target/token are preserved.

`menu_fullscreen_owner` failed before the fix because no owner panel was supplied.
The new AppKit boundary check covers owner visibility/collection behavior, screen
coordinates, no activation/workspace change, three cancel/select openings and
cleanup. It does not simulate actual fullscreen presentation. Physical fullscreen,
multiple displays, menu keyboard navigation and insertion into the original app
remain manual acceptance checks. Independent code review found no actionable
fullscreen implementation issue.

The first Debug suite also exposed an unrelated `lan_end_to_end` mesh readiness
race: a peer's transient "Connected" status was replaced with "Received into
history", so the test waited forever for six simultaneous labels. Only the test
was corrected to wait on durable authenticated HELLO identity bindings. Existing
payload/provenance/no-echo assertions remain. The corrected mesh test passed five
consecutive release repetitions. Network production code was not changed.

Logs: `docs/support/macos-native/2026-11-13-arm64-fullscreen/`.
Apple API reference: <https://developer.apple.com/documentation/appkit/nswindow/collectionbehavior-swift.struct/canjoinallapplications>.

Final verification: debug: 56/56 passed (43.01 s); release: 56/56 passed (41.95 s). No tests excluded.
The release app was rebuilt. Actual fullscreen display is still a manual gate.

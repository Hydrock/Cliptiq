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

## macOS evidence and remaining validation

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

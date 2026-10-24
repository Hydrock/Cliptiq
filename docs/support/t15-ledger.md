# T15 execution ledger

# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md
T15 base b40dd6b18dcbdcb7ec7fba95044dff3fed90f061. Prior T01–T14 committed; manual T04–T14 OPEN. T16 excluded.
Pre-flight: consumes T09 settings/Store and T07 menu; update attempt state must survive settings replacement. User explicitly approved local-only diagnostics replacing Firebase. Existing feature checkout/PR1 reused per continuing user authorization.

Ruling: portable Windows/Linux remain explicit download/install and package-managed apps are never overwritten — avoids an unsafe hidden updater/rollback runtime — cost: unattended portable installation/full automatic-install parity is not provided; native package/installer routes remain required.
Ruling: unsigned repository Atom is availability metadata only, bounded native HTTPS on Windows/Linux; no downloaded payload is ever applied from it — HTTPS is not publisher authenticity — cost: network availability hints are not signed release proof.
Ruling: Sparkle is an optional complete measured macOS lane, own feed/public key required and absent by default — never use upstream Clipy keys/feed or invent production identities — cost: production signed-update acceptance remains OPEN until project signing/feed configuration.
Ruling: accepted local-only diagnostics uses a constant whitelist report without Store access — prevents payload/path/identity leakage by construction — cost: automatic crash collection and remote analytics are intentionally removed, per user approval.
TDD: missing update API RED, policy GREEN; real Atom link regression RED; bounded feed parser GREEN. Native helper wiring was not uniformly test-first; native unconfigured routes and real Authenticode/SDK tests supplement policy tests; not claiming absolute TDD.

Final review: no Critical, one Important actual Sparkle bytes bound; two Minor deferred (Base macOS automatic controls unavailable; invisible diagnostics write error in GUI). One fix pass: pinned source-built downloader,128KiB/5MiB actual bounds, native RED unpatched→GREEN patched; complete closure remeasured. SDK fixture private-key input corrected to the documented32-byte seed (64-byte seed+public is not accepted by2.10.0).
Ruling: source-build pinned Sparkle with a narrow downloader-only bounds patch rather than shipping the unbounded opaque SDK — enforce actual bytes before retaining/writing while retaining upstream verifier/installer — cost: maintaining this small patch and bounded5MiB buffer in optional updater; every SDK upgrade needs new hashes/native tests.
Task 15: complete (commits b40dd6b..9dc9f5c, tests: ctest --preset test --output-on-failure → Total Test time (real) =  33.28 sec)
Final Declined1: Ruling: Production Ed25519 identity/appcast — require project-owned configuration; no invented identity — cost: real feed/key acceptance OPEN.
Final Declined2: Ruling: Developer ID/notarization — leave actual distribution signing to release acceptance — cost: ad-hoc CI is not a distributable signature.
Final Declined3: Ruling: Sparkle install/cancel/relaunch/interrupted recovery/rollback — retain configured native acceptance gate — cost: no production installation PASS.
Final Declined4: Ruling: Sparkle wrong OS/architecture/downgrade end to end — SDK selection retained plus policy tests — cost: configured end-to-end acceptance OPEN.
Final Declined5: Ruling: Actual Sparkle preference persistence — do not overwrite provider preferences on launch; explicit Apply changes them — cost: cross-launch desktop acceptance OPEN.
Final Declined6: Ruling: Sparkle complete closure/relocation evidence — require final exact-source ARM/Intel source-built reports and tests before final claim — cost: neither old prototype nor app executable alone proves closure.
Final Declined7: Ruling: Actual Cliptiq Windows installer/MSIX identity/cancel/data — explicit signed installer/manager route only — cost: no supplied installer; real release acceptance OPEN.
Final Declined8: Ruling: Windows offline revocation — keep native WinVerifyTrust policy; fail closed — cost: production offline acceptance OPEN.
Final Declined9: Ruling: Signed Linux repo update — delegate to user package manager without executable writes — cost: actual signed repository acceptance OPEN.
Final Declined10: Ruling: Unattended portable install/rollback — explicit download/install only per recorded ruling — cost: no automatic portable-install parity.
Final Declined11: Ruling: Abstract UpdateProvider production authentication — trusted-provider interface requires actual byte authentication, not metadata trust — cost: unit provider is not production release evidence.
Final Declined12: Ruling: Desktop proxies/DNS/TLS/shutdown — bounded workers; real own-feed checks pass locally Windows/Linux — cost: all supported-desktop/proxy conditions remain manual OPEN.
Final Declined13: Ruling: Diagnostics picker cancel/replace/filesystem on all OS — native picker and fixed whitelist retained; write-error UI Minor deferred — cost: manual acceptance OPEN; GUI error visibility known limitation.
Final Declined14: Ruling: Clean-system/offline installer/dependency closure — retain distribution gate — cost: hosted development builds are not clean-VM PASS.
Final Declined15: Ruling: Firebase/crash parity — replace by user-approved local-only diagnostics — cost: automatic remote crash/analytics removed intentionally.
Final Declined16: Ruling: Clipboard-owner multi-format races — unchanged T04–T06 implementation and suites retained — cost: manual gates OPEN.
Final Declined17: Ruling: Delete/clear while thumbnail/OCR — unchanged T12; no T16 implementation — cost: manual race acceptance OPEN.
Final Declined18: Ruling: Auto-paste permission/lost target — unchanged T08 fallback and suite — cost: app-to-app manual OPEN.
Final Declined19: Ruling: Existing INCR/image/XML bounds — retain underlying regression suites; updater bounds fixed separately — cost: no independent hostile live-producer requalification.
Final Declined20: Ruling: Linux missing tray/portal/data-control/source identity — retain prior capability contracts — cost: desktop manual OPEN.
Final Declined21: Ruling: Carried T11 Saved during XML worker — defer prior Minor unchanged — cost: generic success/status text remains.
Final Declined22: Ruling: Carried T12 preview-off decode/cache — defer prior Minor unchanged — cost: background work remains.
Final Declined23: Ruling: Carried T13 --background launcher — defer prior Minor unchanged — cost: launcher behavior remains.
Final Declined24: Ruling: Remaining T04–T14 manual gates — keep all OPEN per user instruction — cost: no blanket full-port acceptance.
Final Declined25: Ruling: T16/full-port/release completion — do not implement T16 or publish release — cost: only T15 development increment is in scope.
Final: minor (deferred): Base macOS auto checkbox/interval remain enabled despite unavailable configured Sparkle; stored preference retained, unavailable reason appears in status.
Final: minor (deferred): diagnostics export write errors go only to stderr in ordinary GUI launch.
Final: minor (deferred): carried T11 Saved while XML worker; T12 preview-off decode/cache; T13 --background launcher.
Task 15: complete (commits b40dd6b..3ac9831, tests: ctest --preset test --output-on-failure → Total Test time (real) =  33.92 sec)

Final: fixed Important actual Sparkle download bounds — original pinned native downloader unknown-length appcast RED; source-patched valid/appcast/archive cases GREEN on macOS ARM+Intel; real Ed25519 and full app/framework relocation passed. Exact source3ac983131e1a167908f4c11ab43db3a62bbc20e7 CI40094637399 all11 jobs successful. Linux54/54,Windows44/44,macOS41/41 per hosted suites. Local Debug54/54 and Release54/54; own-feed native Windows/Linux integration passed. All SDK runtime paths match full original T01 framework inventory; no helpers silently removed.

Ruling: exclude only verified generated local __pycache__ bytecode from Git source comparison while retaining its unchanged raw report — it is not a tracked runtime input and all four clean CI source maps are unaffected — cost: local collector includes one generated cache entry; binary hashes and tracked-source audit remain required.
Final: minor (deferred): source-size collector traverses untracked Python cache if present locally; audit explicitly classifies it instead of pretending it is a Git blob.

Final source 3ac983131e1a167908f4c11ab43db3a62bbc20e7; CI4009463739911/11. Manual T04–T14/T15 OPEN; T16 excluded.

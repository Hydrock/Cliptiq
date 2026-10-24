# T14 execution ledger

# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md
Task14 base1eb2b751022376ac3464e5aeea94284edd395b73; approved scope only; T04–T13 manual OPEN.
Pre-flight: T02 Store transactions, T09 settings, T10 IDs/states and T11 XML are consumed. Import is optional separate library/tool, not linked into app.
Ruling: use separate converter + existing SQLite neutral codec — keeps Realm/archives outside main runtime — cost: legacy export requires separate macOS helper.
Progress: portable initial contract RED on missing header/API, then49/49 Linux and19 Windows safe groups plus actual import fixture. Added UTF-8/type guard regressions RED→GREEN and hot-journal spill recovery RED→GREEN. Initial native helper wiring was not uniformly test-first; explicit deviation recorded, not claimed otherwise.
Ruling: source DB/WAL/journal are binary-copied and rehashed before private SQLite recovery — read-only SQLite itself may touch source SHM — cost: extra bounded disk/memory and close-source requirement; live changing source is rejected, not silently sampled.
Ruling: settings are retained raw by default; --apply-settings activates validated portable subset only — avoids automatic OS registration/telemetry or unrepresentable shortcuts — cost: unsupported settings/chords need manual reconciliation.
Ruling: RealmSwift10.54.6/RealmCore14.14.0 are pinned in the optional helper rather than old10.7.2 — modern macOS toolchain compatibility — cost: actual historical Realm file-format acceptance remains manual even though synthetic schema7 conversion is tested.
Task 14: complete (commits 1eb2b75..223ef3f, tests: ctest --preset test --output-on-failure → Total Test time (real) =  29.84 sec)
Task 14: complete (commits 1eb2b75..4e598ed, tests: ctest --preset test --output-on-failure → Total Test time (real) =  32.25 sec)
Task 14: complete (commits 1eb2b75..c965d5c, tests: ctest --preset test --output-on-failure → Total Test time (real) =  33.70 sec)

Final review: Critical ownership retention and Important grouping/portable replay/allocation bounds/scalar coercion fixed; final hosted CI40090941139 is9/9 on c965d5c. Debug52/52, Release52/52, ASan/UBSan52/52 locally. Regression RED/GREEN logs retained; native helper wiring was not uniformly test-first.
Ruling: preserve release Mach-O metadata and exclude dSYM after post-link relocation crash — staged closure now tested with build-tree dylib unavailable — cost: optional Realm lane remains large; full bytes disclosed.
Ruling: portable replay supports explicitly representable formats only — no fabricated file/RTFD conversions — cost: unsupported cross-OS formats/multi-items require original platform; original bytes retained.
Ruling: test fixture retries only explicit Busy and uses controlled lock — matches existing production receive behavior — cost: fixture is not a latency or real desktop acceptance guarantee. Production lifecycle unchanged;10 stress runs and52 Release tests passed.
Declined1 Ruling: capture-owner races remain T04–T06 scope — retain existing automated suites — cost: manual acceptance OPEN.
Declined2 Ruling: background OCR/thumbnail races remain T12/T16 scope; imported deletion ownership fixed — cost: background-worker manual acceptance OPEN, no OCR added.
Declined3 Ruling: lost-target/permission fallback remains T08 scope; boundary/native-copy tests retained — cost: real app-to-app acceptance OPEN.
Declined4 Ruling: INCR/XML retain existing bounded suites, no unrelated rewrite — cost: hostile live-producer manual acceptance OPEN.
Declined5 Ruling: Linux tray/portal/data-control/source identity retain T06/T07/T09 capability contracts — cost: desktop manual acceptance OPEN, no new capability claim.
Declined6 Ruling: modern schema7 fixtures prove conversion, not historic Realm file compatibility — cost: actual historical backups remain manual OPEN.
Declined7 Ruling: exact final source CI40090941139 verified9/9 and four size reports plus both complete macOS migration packages archived — cost: executable/package figures do not prove clean-system installation or installer closure.
Declined8 Ruling: signing/notarization/release delivery remain T15 — cost: development staging only; no official release/update work.
Deferred Minor: T11 generic Saved during locked XML worker; T12 preview-off decode/cache; T13 --background opens launcher. No new Minor; T15 not started. Manual T04–T13 and T14 OPEN.

Runtime c965d5c7251fbd1d715498502162b15c4818a7c4, CI400909411399/9. Manual T04–T13 and T14 OPEN; T15 excluded. Existing draft PR1 updated; no merge/release.

# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md
T16 BASE f92437f4c44d63f40d2f1a6918731b7c91a9a15c. Approved T16 only; T17 excluded. Manual T04–T15 OPEN. Existing feature checkout/PR reused under continuing authorization.
Pre-flight: T03 capture policy, T12 bounded codecs, T14 ordinary OCR columns, T13 shared instance scope reused.
Ruling: one SQLite per build, FTS5 only optional indexed; unregister FTS modules on main connections — avoid duplicate SQLite closure — indexed instructions contain FTS code, Base excludes it.
Ruling: re-evaluate retained images before decode and publication instead of changing each native capture sink — also covers accepted T12 screenshots — bounded extra SQL, depends on retained source metadata completeness.
Ruling: Tesseract LSTM/raw RGBA without extra codecs/OpenMP/curl/archive — T12 already decodes — harmless missing-codec debug helper diagnostics; engine quality remains different.
Ruling: native EN/RU per OS, portable Windows verifies package-identity rejection, positive MSIX remains OPEN — no fabricated identity — actual packaged language availability unproven.
RED/GREEN initial: missing OCR header compile failure; malformed engine UTF8 incorrectly returned success, validated before publication. FTS isolation/invalidation/corruption/revision/variant fixtures pass. Windows path string comparison failed, filesystem identity fixes it. WinRT private C++20 TU supports current MSVC.
Local pre-review: Linux Base57/57; indexed focused4/4 EN/RU; Windows native4/4 and roundtrip; portable WindowsTesseract4/4 and roundtrip. Tesseract static CRT needed CMP0091 NEW in older Leptonica CMake.
Final review: gpt-6-astra read-only8bb6315, docs/support/t16-review.md. Critical1 and Important2/3/4; one fix pass, no re-review.
Final: fixed deletion race — fts_transaction_race RED→GREEN; fixed UI wait — busy mode RED→GREEN; fixed installed model path — ocr_install_layout RED→GREEN; fixed Unavailable exclusion — ocr_queue RED→GREEN. Full post-fix suites pending.
Final: minor (deferred): MenuHost declares OCR Available before actual engine/model probe; engine returns Unsupported correctly.
Ruling: bounded in-memory FTS build, short disk publication gate; main mutation gate spans commit, immediate Busy during publication — prevent deleted payload race without GUI waits — up to128MiB index memory, interactive writes may need retry.
Final: Ruling: declined-to-judge 1 — CI/sizes directly verified before completion — cost if omitted: unproven artifacts.
Final: Ruling: declined-to-judge 2 — positive MSIX OPEN — no identity fixture available — cost: packaged EN/RU unproven.
Final: Ruling: declined-to-judge 3 — macOS fixture CI, desktop cancellation OPEN — cost: OS operation timing unknown.
Final: Ruling: declined-to-judge 4 — clean machines/signing/installers/T17 OPEN — user has no VMs — cost: deployment unproven.
Final: Ruling: declined-to-judge 5 — only synthetic EN/RU quality claims — cost: real-world accuracy unknown.
Final: Ruling: declined-to-judge 6 — structural bounds, no max-load benchmark — cost: peak RSS/throughput unmeasured.
Final: Ruling: declined-to-judge 7 — no hostile same-user concurrent filesystem replacement claim — cost: adversarial races unproven.
Final: Ruling: declined-to-judge 8 — external same-revision index loss repairs on restart/revision; automatic live repair deferred — cost: missing index until restart.
Final: Ruling: declined-to-judge 9 — native OS hang timing untested — cost: shutdown timing unknown.
Final: Ruling: declined-to-judge 10 — prior T01–T15 gates preserved — cost: no fresh full manual acceptance.
Final: Ruling: declined-to-judge 11 — new search/OCR-copy UI excluded by approved scope — cost: backend only.

Post-fix full verification: Linux Base57/57; Linux indexed5/5 + installed EN/RU + process variants; Windows portable5/5 + variants. WAL introduced two old fixture failures: BEGIN trace reentrancy fixed by acquiring nonblocking gate after BEGIN (rollback on Busy); crash fixture now covers explicit rollback and WAL. Foreign rejected DB byte-preservation test RED→GREEN after WAL switch moved behind identity/schema validation. One fix pass remains complete pending final CI.
First CI40099197998:15/16, only WindowsTesseract CRT mismatch; local CMP0091 fix verified portable EN/RU and closure. Final CI to verify all source-platform combinations.
Task 16: complete (commits f92437f..6d0b34a, tests: ctest --test-dir out/ocr-Linux-tesseract -R 'ocr|fts' --output-on-failure → Total Test time (real) =   2.59 sec)

Final verification: CI40100448942 runtime6d0b34a16/16;1306 source hashes verified against181 Git blobs;9reports; EN/RU nativeMac and offlineWin/Linux confirmed. All reviewed Critical/Important fixed, complete post-fix suites green. No merge/release, manuals T04–T16 OPEN, T17 not started.

Final Clipy source comparison: explicitly enable Vision automaticallyDetectsLanguage (same accurate/correction/detection flags as pinned Clipy). Existing EN/RU native fixtures verify engine behavior; no implementation-mirroring test added for this reversible one-property correction. Final runtime CI must be repeated for this commit.
Task 16: complete (commits f92437f..a4deefe, tests: ctest --test-dir out/ocr-Linux-tesseract -R 'ocr|fts' --output-on-failure → Total Test time (real) =   2.45 sec)

Final verification: CI40101383632 runtimea4deefe16/16;1306 source hashes verified against181 Git blobs;9reports; EN/RU nativeMac and offlineWin/Linux confirmed. All reviewed Critical/Important fixed, complete post-fix suites green. No merge/release, manuals T04–T16 OPEN, T17 not started.

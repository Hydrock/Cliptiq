# T02 execution — 2026-10-23

Plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md. BASE 3182cd8752eea2f7d0af82dd5470b4e95d97fdd4.

User accepts C++ with native shells and authorizes T02 implementation and GitHub upload. T03+ are outside this turn.

Ruling: reuse the clean dedicated codex/portability-plan checkout and existing draft PR; no unrelated work is present. A second worktree adds no isolation benefit here. Cost if wrong: unrelated changes could share the review range.

Preflight: T02 representations and policy fields must remain suitable for T03 capture filtering and T04–06 native adapters; storage only receives already accepted snapshots. T07 needs touch/use without altering creation time. T16 requires a persisted data revision and no FTS in the main database. Snippet tables must survive history clearing; snippet editing remains T09.

Ruling: preserve explicit now_ms input as the injected clock; periodic clear uses persisted scheduling state, not an entry TTL. SHA-256 is a small internal implementation to avoid a crypto runtime dependency; equality is still checked on representations. Cost if wrong: SHA implementation maintenance; standard vectors and collision-safe comparison mitigate it.

Status: in progress. Tests precede implementation; evidence will be recorded below.

RED: 17 named history scenarios compiled against not-implemented storage and all failed for the missing implementation. GREEN: all pass against SQLite implementation; additional SHA256/collision/metadata/scheduler/schema/crash cases pass.

Ruling: configurable payload limits may be lowered within spec hard ceilings; metadata strings additionally capped at 4096 bytes and representation count at 4096. These avoid unbounded per-entry metadata independent of payload quota. Cost if wrong: a rare clipboard with huge metadata is rejected explicitly; adapters must enforce stream limits before allocating clipboard input.

Ruling: T02 supplies minimal snippet/settings storage boundaries plus revision/nullable OCR fields, not T09/T16 implementations. No FTS module/index is added. SHA256 input includes version/count/item/platform/type/kind/data with 64-bit big-endian lengths; source/sequence are metadata, not duplicate identity. Cost if wrong: later consumers need an explicit schema migration rather than relying on accidental prototype structure.

Debugging: Windows and WSL CMake caches initially collided; host-specific output directories fix configure reuse. Python SQLite context manager commits but does not close; explicit closing fixes Windows crash-test temporary file cleanup. Local Clang unavailable; local sanitizer run uses GCC, CI separately uses Clang.

Verified locally: Windows MSVC Debug/MinSizeRel, Linux GCC ASan/UBSan, unit history and process-interruption recovery pass. Review and remote CI pending.

Final fresh review (gpt-6-astra): P1 persisted read limits after allocation, P2 UTF-16 whitespace, P2 returned ID already pruned. Regression run reproduced the duplicate read-limit bypass, UTF-16 blank retention and stale return. Fixed size/count SQL preflight before materializing payload (also duplicate candidates), explicit persisted TextEncoding and empty return when accepted entry was evicted. Original reduced-limit load test already failed safely after allocation; its preallocation requirement is verified by loader structure. One fix pass, full suites repeated.

Review minor comments addressed: crash-test description no longer implies production file-permission read-only coverage; unit query_only covers production ReadOnly mapping. Crash fixture now flushes a dirty 200KB update with one-page cache, exits without cleanup, and asserts a nonempty rollback journal before recovery. This strengthens actual hot-journal evidence.

Initial T02 CI 39940659648: all five jobs success (Windows, Linux, macOS arm64/x86_64, Clang ASan/UBSan). Final review-fix run pending.

Task T02: complete (commits 3182cd8..004a0d5; Windows Debug/MinSizeRel CTest, Linux GCC ASan/UBSan and final CI 39941083332 -> PASS). Final CI 5/5 jobs confirms Windows/Linux/macOS both CPU plus Clang sanitizers. 29 unit scenarios and real process interruption test pass in Debug and MinSizeRel. Source-hashed size evidence and CTest logs published in t02-measurements.

Final review findings fixed; no deferred minors. Existing draft PR updated; upload authorized, no merge. T03+ not started. No plan scratch workspace was created, so no cleanup is required. Existing checkout remains for user continuation.

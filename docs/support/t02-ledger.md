# T02 execution — 2026-10-23

Plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md. BASE 3182cd8752eea2f7d0af82dd5470b4e95d97fdd4.

User accepts C++ with native shells and authorizes T02 implementation and GitHub upload. T03+ are outside this turn.

Ruling: reuse the clean dedicated codex/portability-plan checkout and existing draft PR; no unrelated work is present. A second worktree adds no isolation benefit here.

Preflight: T02 representations and policy fields must remain suitable for T03 capture filtering and T04–06 native adapters; storage only receives already accepted snapshots. T07 needs touch/use without altering creation time. T16 requires a persisted data revision and no FTS in the main database. Snippet tables must survive history clearing; snippet editing remains T09.

Ruling: preserve explicit now_ms input as the injected clock; periodic clear uses persisted scheduling state, not an entry TTL. SHA-256 is a small internal implementation to avoid a crypto runtime dependency; equality is still checked on representations.

Status: in progress. Tests precede implementation; evidence will be recorded below.

RED: 17 named history scenarios compiled against not-implemented storage and all failed for the missing implementation. GREEN: all pass against SQLite implementation; additional SHA256/collision/metadata/scheduler/schema/crash cases pass.

Ruling: configurable payload limits may be lowered within spec hard ceilings; metadata strings additionally capped at 4096 bytes and representation count at 4096. These avoid unbounded per-entry metadata independent of payload quota; adapters must enforce stream limits before allocating clipboard input.

Ruling: T02 supplies minimal snippet/settings storage boundaries plus revision/nullable OCR fields, not T09/T16 implementations. No FTS module/index is added. SHA256 input includes version/count/item/platform/type/kind/data with 64-bit big-endian lengths; source/sequence are metadata, not duplicate identity.

Debugging: Windows and WSL CMake caches initially collided; host-specific output directories fix configure reuse. Python SQLite context manager commits but does not close; explicit closing fixes Windows crash-test temporary file cleanup. Local Clang unavailable; local sanitizer run uses GCC, CI separately uses Clang.

Verified locally: Windows MSVC Debug/MinSizeRel, Linux GCC ASan/UBSan, unit history and process-interruption recovery pass. Review and remote CI pending.

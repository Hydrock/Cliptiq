# T03 execution — 2026-10-23

Plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md. BASE 909c1e21a079f4757013ace2914ec045f35730f8.

User authorizes T03 privacy/capability work and upload; T04+ are excluded. Continue the clean dedicated codex/portability-plan checkout and existing draft PR.

Preflight: T02 remains the low-level history store; future capture paths (T04–06/T12) must use the T03 gate before storage/thumbnail/OCR. Capability statuses default unavailable, not inferred from OS names. Source confidence is downgraded when source-app capability is limited/unavailable. No native discovery/backend work belongs to this task.

Ruling: native adapters explicitly register recognized platform/type/kind mappings in Capabilities; clipboard input cannot declare its own type safe. Unknown native formats are dropped even if they claim Text. This avoids a platform decoder/registry dependency in the portable core. Cost if incomplete: an unsupported format is visibly filtered until the adapter registers it.

Ruling: add capture-only advertised type IDs for marker-prefix checks before filtering, without persisting their payloads. Apple remote-file rules apply only to Mac capability context; transient/concealed cooperative markers apply wherever explicitly observed. Cost if adapters omit advertised IDs: marker-based exclusions are incomplete; this contract is required of T04–06 adapters.

Status: in progress. TDD and final independent review required before completion.

RED: 27 initial scenarios compiled against not-implemented policy; 24 failed for missing filtering/dispatch, three rejection/message sanity cases already passed against fail-closed stub. Existing T02 suites remained green. GREEN: all 34 policy scenarios plus history/crash suites pass on Windows Debug/MinSizeRel and Linux GCC ASan/UBSan.

Final fresh review (gpt-6-astra, read-only): no critical/important defects. One nonblocking measurement qualification addressed in core/POLICY.md and results: paired delta includes sink wiring/result recovery and registry setup, not policy code in isolation. No deferred minors.

Local paired Windows size: baseline 844800 bytes, with policy dispatch 856576 (11776 added), ZIP delta 4632 bytes. This is a dirty-worktree exploratory result; final source-hashed CI evidence takes precedence. No new third-party runtime/library dependency was added.

Task T03: complete (commits 909c1e2..6824d00; Windows Debug/MinSizeRel, Linux GCC ASan/UBSan/Release and final CI 39943798856 -> PASS). 34 policy cases + 29 history cases + crash suite pass; final CI 5/5 (Windows/Linux/Mac both CPU, Clang sanitizers). Source-hashed paired sizes, compiler identities and raw logs are published in t03-measurements.

Final review clean, no deferred minors. Results uploaded to existing feature branch/draft PR; no merge/release. T04+ not started.

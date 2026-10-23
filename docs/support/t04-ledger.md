# T04 execution — 2026-10-23

Plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md. BASE a303dc9c427b1e8a8705c7043a7be2f3595c0442.

User authorizes macOS clipboard backend, privacy integration, size checks and upload. T05+ excluded. Reuse clean dedicated codex/portability-plan checkout/draft PR.

Preflight: backend callbacks run on main dispatcher; application-owned serial queue alone touches Store. All captured content must pass T03 before persistence/derivatives. Source identity is NSWorkspace foreground best effort, never clipboard authorship. Native type registry includes all supported kinds independent of toggles. Original offers/markers survive until privacy evaluation.

Ruling: use Objective-C++/ARC + system AppKit, with no new third-party runtime. macOS named pasteboards isolate automated tests from general clipboard; real NSPasteboard operations run only in macOS CI. Provider subclasses are limited to deterministic fault/root-only conditions and are labelled as such.

Ruling: accessBehavior (macOS 15.4+) is checked before background reads. Ask/default General access pauses capture with a reason instead of silently triggering a repeated prompt; explicit user-initiated reads can request the system prompt. Deny remains unavailable. Real consent UI/app-to-app manual verification cannot be substituted with provider injection or hosted CI.

Ruling: bound NSData length before C++ copy and enforce representation/entry limits, but NSPasteboard dataForType is an eager OS API. No claim of bounding provider/server allocations or preempting a stuck native provider. Root-only representations use reserved item_index UINT32_MAX; normal item indices are preserved.

Status: in progress. Native RED/green evidence and fresh final review pending. Manual app/permission scenarios remain explicit acceptance items until actually exercised.

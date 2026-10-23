# T05 execution — 2026-10-23

BASE afea4cbf397fc7c49f4d1b71a119696fc5ed04c7. Plan docs/superpowers/plans/2026-10-23-cliptiq-port.md; spec alongside it. User authorizes Windows backend, privacy integration, size measurements and upload to existing branch/draft PR. T04 manual remains OPEN. T06+ excluded.

Ruling: reuse clean dedicated codex/portability-plan checkout, as previous tasks; no unrelated user edits. Prior approved C++/Win32 design is binding; no renewed architecture approval needed. Apply using-git-worktrees to verify this isolation, not create another checkout.

Pre-flight T02/T03/T04 -> T05: same native byte snapshot, trusted native registry, capability reasons, UI callbacks, worker-owned Store and mandatory privacy gate. Windows single clipboard object has formats, not macOS arbitrary multiple items; CF_HDROP groups paths at item 0. Encoding Utf16Le on Unicode/URL; CF_HTML preserves UTF-8 byte offsets. Never persist GDI/OLE handles.

Ruling: tests switch to a newly created noninteractive window station/desktop BEFORE any clipboard/window calls, abort if isolation fails. Windows has one clipboard per window station. No automated test/size probe may read or overwrite the user's WinSta0 clipboard. Real application smoke is a separate opt-in manual gate.

Ruling: known Windows history exclusion hints are fail-closed; ExcludeClipboardContentFromMonitorProcessing and CanIncludeInClipboardHistory=0 map to transient. Cooperative concealed markers supported, absence not proof of no secret. Owner executable path normalized invariant lowercase is BestEffort, missing/inaccessible owner remains Unknown; no claim of proven clipboard authorship or elevation bypass.

Ruling: WIC decode runs after privacy gate on the storage worker, with dimension/pixel/payload bounds. Store retains original transferable format bytes. CF_HDROP replay requests COPY, never repeats historic CUT. Virtual files/OLE-only objects and unsupported DIB layouts are explicit unsupported formats, not serialized process pointers. No T12 thumbnail/OCR implementation.

Status: tests/stubs first; RED, implementation, native CI, review and size evidence pending.
# Native RED confirmed: isolated Windows CI39967338865 / f4f3269, 15/18 scenarios failed on Not implemented/expected missing behavior; 3 fail-closed sanity assertions already passed. Local test station creation is denied (Win32 error5), including unrestricted exec; this is not backend evidence. Pure format test locally RED on Not implemented, then GREEN after parser/WIC implementation. Native station isolation succeeds in hosted Windows CI.

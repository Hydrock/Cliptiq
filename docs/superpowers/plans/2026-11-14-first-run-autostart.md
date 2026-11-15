# First-run autostart

Implement in feature/windows-workspace. New normal desktop profiles receive a
native setup dialog with Launch at login checked. Continue registers through the
existing transactional Autostart::apply and saves settings; Quit changes nothing.
A persisted t09.login row is the completion marker, including false. Existing
profiles retain their current value, and updates/settings saves keep that row.
No schema migration or changed global defaults. Smoke/demo, explicit database and
background invocations never display setup. macOS uses the existing SMAppService
registration and approval status; no silent approval bypass.

1. Reproduce missing persisted-choice detection in settings tests.
2. Add read-only login_configured metadata and native Windows/macOS setup dialogs.
3. Integrate before capture and preserve registration rollback on storage failure.
4. Add EN/RU strings, regenerate catalogues, build and run targeted/full checks.
5. Verify Windows checkbox/cancel without changing the real user's autostart;
   document that macOS compilation/login approval requires a Mac.

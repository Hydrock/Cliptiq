# T13 implementation ledger

Base: `1b3357a95193315d69f33ebeddcec5cfab7a2c96`.
Approved T13 covers S08–09,12–13. T14 is excluded. Manual T04–T12 remain OPEN.
Dedicated clean feature checkout codex/portability-plan is reused, as in T02–T12.
Linux baseline:43/43 CTest groups.

Pre-flight: settings login/support placeholders consume new native lifecycle APIs;
CLI show routes exist but currently launch independent hosts. Menu launcher remains
the recovery surface when tray/hotkeys are unavailable. Localization must translate
application chrome only, never clipboard history or user snippet text.

Ruling: use an OS file lock and a bounded SQLite mailbox in a private per-user/session
directory — SQLite is already bundled and accepts only five fixed open actions;
cost: GUI polling, rather than immediate OS message delivery, uses a100ms poll interval (plus bounded SQLite busy wait).

Ruling: macOS login registration requires a proper app bundle — SMAppService status
must distinguish approval from enabled; cost: unbundled probes cannot establish login.

Progress: core lifecycle RED on missing routes, then GREEN; startup apply RED on
missing transaction, then GREEN with native rollback; foreign mailbox RED on
incorrect overwrite, then GREEN with app-ID/version/size guards. Real subprocess
forwarding and kill/restart tests pass on Linux and Windows. Linux48/48 CTest
and Windows19/19 selected safe groups passed before final accessibility layout changes.
Local Windows native GUI tests cannot create an isolated Window Station (error5);
those tests stay in hosted CI/manual acceptance rather than being weakened.

Localization: shared generated132-key EN/RU schema, seven partial upstream locales,
42 extra matching XIB/lproj strings, pinned source inventory/hashes and MIT/BSD
attribution. RU native Settings smoke on isolated X11 and Wayland passes. User
clipboard text equal to a chrome key remains byte-identical and untranslated.
New windows apply the selected language at next launch; technical diagnostics
without a resource entry explicitly retain English fallback.

Packaging: native Mac app bundle identity for SMAppService, Linux desktop actions,
Windows Common Controls/asInvoker manifest, staging install rules. No real user
login registration is invoked by smoke/CI; isolated native API tests cover registry
and desktop-file creation/removal. Mac login registration/approval/signing remains
manual. Existing preview-off and XML picker deferred minors are unchanged.

Ready verification: Linux48/48, Windows19/19 safe groups and production GUI PE
subsystem check GREEN. Empty initialization mailbox recovery RED→GREEN; foreign
SQLite databases remain rejected. Partial-locale fallback now resolves the English
catalogue value, not merely the key (RED→GREEN). Legacy local-only clear/folder CLI
routes remain usable by the primary, but cannot be forwarded as IPC actions.

GUI layout: GTK labels wrap and name controls through ATK, AppKit uses wrapping
labels/accessibility names, Win32 measures label heights using the actual font.
Real DPI/screen-reader and logout/login behavior remain manual. No broad sanitizer
suppressions or new bundled runtime have been added. Source generators are checked
against committed EN/RU schema; all source snapshots and size probes are pending CI.

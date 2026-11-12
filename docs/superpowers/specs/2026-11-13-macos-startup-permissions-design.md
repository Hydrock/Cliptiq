# macOS startup permissions

The user approved requiring clipboard capture and automatic paste before normal
application functionality. LAN permission is conditional on enabling LAN.

Use a native foreground AppKit setup window before capture, hotkeys, LAN service,
updates and main UI initialization. Show independent status and request buttons
for clipboard and automatic paste, plus Refresh, Continue and Quit. Continue is
disabled until both checks pass and is rechecked on click. Closing quits startup.
With all access already granted, start normally without a setup window. Smoke and
explicit demo test modes bypass the interactive gate; no background process may
silently grant OS access.

On macOS 15.4+, clipboard capture requires NSPasteboard AlwaysAllow; Ask/default
are insufficient for unattended history capture. Before that OS, native clipboard
access has no equivalent permission. Automatic paste requires both AX trust and
post-event access. Request buttons invoke public OS permission guidance and open
System Settings; user must grant access. Poll only permission metadata, never
clipboard payloads. No screen/camera/microphone or unconditional LAN requests.

Chosen approach: startup setup instead of a hidden post-selection modal (reported
behind Chrome). Keep the later modal only as a revocation fallback. Use native
AppKit, existing locales, injectable permission calls, no new dependencies.

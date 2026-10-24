# T15 manual acceptance — OPEN

- [ ] macOS Intel/ARM with actual project Ed25519 appcast, Developer ID/notarized releases: successful update/relaunch, invalid/missing signature, tamper, downgrade, wrong OS/architecture, oversized payload, cancellation, interrupted download and rollback/recovery. No production key/feed is supplied by CI.
- [ ] macOS provider preferences: prior manual/automatic/skip state survives launch; explicit Settings Apply changes schedule; provider last-check is reflected; automatic install remains disallowed. Base lane states unavailable automatic checking.
- [ ] Windows actual signed Cliptiq installer/MSIX manager: publisher identity, tamper, cancellation, restart/data continuity, offline revocation policy and standard-user behavior. The signed Microsoft CRT fixture validates the native verifier, not a Cliptiq release.
- [ ] Linux actual signed deb/rpm repository update via package manager; managed application never self-overwrites. Tar and Windows portable require explicit user installation and do not promise unattended auto-install/rollback.
- [ ] Native metadata networking: real proxies/TLS backend availability, offline/DNS/timeout behavior, privacy and update UI responsiveness; no network by default or in smoke. GitHub metadata is not a verified package.
- [ ] Native diagnostics picker on all OS: cancel/new file/confirmed replacement/write failure; report contains only the published whitelist and no clipboard/snippets/user paths/identifiers. No upload occurs.
- [ ] Clean-system/offline dependency and full signed installer closure measurements remain unverified by hosted development CI.

Manual T04–T14 stays OPEN. T16 has not started.

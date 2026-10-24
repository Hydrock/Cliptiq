# T15 update delivery contract

There is no published Cliptiq release, installer, production appcast/key or signed package repository supplied by this work. Development builds fail closed for installation. Do not substitute Clipy's feed/key, a sample key, a certificate found on this computer, or plain HTTPS for publisher authentication. T17 distribution/signing remains separate.

| Lane | Check | Installation/authenticity | Main-runtime cost |
|---|---|---|---|
| macOS Base | Explicit project release page | Explicit download; no embedded updater | Small native facade, system AppKit |
| macOS Sparkle | Own configured appcast, provider schedule | Sparkle Ed25519 verification before extraction, Developer ID/notarization at distribution, user confirmation | Complete optional framework/XPC/installer resources measured, not silently downloaded |
| Windows | Fixed project Atom metadata, opt-in schedule | Explicit signed installer/MSIX manager; no portable self-replacement | OS WinHTTP; optional separate Authenticode verifier measured |
| Linux deb/rpm | Fixed project Atom metadata, opt-in schedule | User's package manager and authenticated configured repository; app never writes package files | Existing GIO/TLS backend; no bundled OpenSSL |
| Linux tar / Windows portable | Fixed project metadata | Explicit download/install; unattended portable install is deliberately unavailable | No rollback helper/runtime hidden from size totals |

The metadata worker is background-only. Windows has5s transport-stage timeouts and15s body deadline with128KiB maximum. Linux uses verified GIO TLS with a20s cancellation deadline,5s socket timeout and maximum16KiB HTTP header +128KiB body; unavailable TLS backend/offline/redirect/error response fails explicitly. Neither worker accepts a redirected release download, executes a shell or supplies arbitrary URLs. Remote release metadata cannot initiate installation.

Core `check_update(provider,channel,current,target)` validates version, platform/architecture/route, fixed release download origin, positive bounded size and required signing metadata before calling a **trusted application provider** to authenticate actual bytes. `apply_update` validates again and re-authenticates before delegation. These policy tests use a controllable provider; this is not evidence that production signing keys or a real installer exist. Package-manager and Download routes always reject self-application. Default budget5MiB concerns the update payload, not the total offline install closure. Sparkle's delegate also rejects an advertised payload exceeding5MiB or a non-project download URL; its source-built SDK enforces actual decoded byte limits before appending (128KiB appcast,5MiB archive), with an ephemeral uncached session and20s resource timeout. Only a complete bounded archive reaches private staging/disk; signature/install behavior remains upstream. This adds at most the bounded archive buffer to the optional process, not an unbounded in-memory appcast. CMake rejects an unpatched framework identity.

Settings preserve automatic-check preference and86400/604800/2592000-second intervals. Last-attempt state uses a separate Store key and survives normal settings writes. Sparkle owns actual provider schedule/last-check; no second polling scheduler forces its network cycle. A failed/offline attempt is not reported as a successful check or installation. Smoke and unconfigured-route tests never open a browser, network connection or installer.

## macOS configured build

`CLIPTIQ_SPARKLE_FRAMEWORK` points to the source-built Sparkle2.10.0 framework with the pinned Cliptiq byte-bounds patch, produced by `tests/size/update_sparkle.py`. `CLIPTIQ_UPDATE_FEED` must point to an HTTPS XML appcast below `https://github.com/Hydrock/Cliptiq/releases/download/`; `CLIPTIQ_UPDATE_PUBLIC_KEY` is the project's own base64 Ed25519 public key. Neither is provisioned by this task. Private signing keys never belong in Git or chat. Include all framework helpers and the Sparkle license; do not claim the no-XPC prototype is the full lane.

The measurement uses the actual Cliptiq app, architecture-thins Mach-O slices, retains all runtime helpers/localizations, signs ad hoc for CI and checks relocation with the build-tree framework unavailable. Ad-hoc signing is **not** Developer ID/notarization or a production update acceptance test.

## Windows verification utility

`cliptiq_verify_update --verify FILE SHA256_SIGNER_CERT` invokes WinVerifyTrust, requires its successful trust result and an exact SHA256 signer-certificate pin. It rejects unsigned/tampered files, wrong publisher, malformed pin, reparse-point/directory inputs and files over5MiB. It keeps a non-write/non-delete-shared source handle during verification. It never executes or installs the inspected file. The caller must obtain the expected publisher identity from trusted project configuration, not the download itself. Do not build portable auto-install around this check alone: a staged private file, final-byte binding, rollback, user approval and a signed delivery route would all be required.

Primary references: [Sparkle setup/security](https://sparkle-project.org/documentation/), [publishing signed updates](https://sparkle-project.org/documentation/publishing/), [WinVerifyTrust](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust).

The original2.10.0 binary SDK has only an advertised-length policy and is not an acceptable production provider for Cliptiq bounds. CI first demonstrates an oversized unknown-length appcast succeeding against unpatched source (RED), then compiles the same native downloader with the patch and proves valid/oversized appcast and oversized archive handling (GREEN). The transport alone is synthetic NSURLProtocol; no public network/installer is involved. Both the framework and downloader XPC target compile that patched source. SDK CLI Ed25519 fixtures use public RFC8032 test seeds, never a Keychain/production identity.

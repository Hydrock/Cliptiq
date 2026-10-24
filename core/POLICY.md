# Capture privacy and capabilities (T03)

`evaluate_capture(snapshot, policy, capabilities)` is a pure gate. It returns a
structured disposition/reason and an optional **filtered** snapshot. Rejections
have no snapshot. `capture_reason_message` returns fixed nonsensitive strings;
presentation/localization can use the reason enum without logging content or
excluded application IDs.

`dispatch_capture` is the shared capture-side entry for future clipboard and
screenshot adapters. It invokes the injected sink's storage operation only on
accept, and thumbnail/OCR only after storage returns a retained ID. Storage
errors propagate; blank/duplicate/pruned no-op IDs skip derivatives. Image OCR
also requires an explicit request (default off) and available/limited OCR
capability. These hooks schedule future worker work; they do not implement
decoders, thumbnailing or OCR in T03. Sink callbacks must handle their own
processing failures and must not throw; no callback runs on policy rejection.
The T02 `ingest` function remains a low-level repository API, not a capture gate.

## Trusted inputs and backend obligations

Capabilities has independent capture/write/hotkeys/auto-paste/source-app/tray/
pointer-placement/OCR records, each Available/Limited/Unavailable plus a reason.
Every default is Unavailable/Not probed. No OS name alone implies support.
`CapturePlatform` provides the context for Apple-only remote clipboard rules;
T03 does not probe any native OS or start T04.

Native adapters explicitly register supported platform/native-type/kind/encoding
tuples in `Capabilities.native_types`. This is trusted backend configuration,
not data derived by accepting a clipboard type's claimed class. Registration
must include all supported formats even if a kind is currently disabled.
Unregistered formats, mismatched classes/encoding, and arbitrary Opaque class
are dropped. PDF/RTFD preserve opaque native bytes under their known PDF/RTFD
classes; a generic unknown Opaque type is never a supported ninth content kind.
No UTI/MIME inference library or format decoder is linked into policy.

Adapters supply complete ordered advertised native type IDs in
`snapshot.offered_types`, **before** enabled-kind/type filtering, including
cooperative metadata markers and root offers. If omitted, representations serve
as the original offer order. Marker checks inspect both lists. Adapters must
not omit privacy markers when loading payloads. Offered IDs/marker payloads are
never included in the accepted snapshot; only known concealed/remote/transient
flags and allowlisted source metadata survive. T02 intentionally does not
persist the capture-only offered list or add it to content digest.

App IDs must use the same stable backend-normalized spelling as exclusion
settings. Core compares complete IDs exactly; it does not guess platform-specific
case folding, executable aliases or ownership from a foreground window.

## Rules and defaults

- Transient Yes or `org.nspasteboard.TransientType`: always reject.
- Concealed Yes or `org.nspasteboard.ConcealedType`: reject only when the
  concealed exclusion is enabled (default off). Otherwise preserve its flag.
- Apple remote flag or `com.apple.is-remote-clipboard` in Mac context: reject
  all when configured (default off); always reject primary file-reference data;
  otherwise strip secondary Files representations. Primary is the first original
  non-metadata offer, before kind filtering. An unsupported earlier offer does
  not promote a secondary file. Apple policy is not applied to Windows/Linux
  cloud/remote hints as if they were Universal Clipboard.
- Exact excluded app identity or `excluded_id` starting with an advertised type
  marker: reject. Prefix direction matches Clipy (`excluded_id.starts_with(type)`),
  not the reverse. Markers are checked even when their content class is disabled
  or unknown. Empty exclusion IDs are invalid configuration.
- Source-app Unavailable or missing/Unknown identity: sanitize to Unknown and
  remove any untrusted ID. Limited source-app capability downgrades Known to
  BestEffort; an existing BestEffort source is never promoted. Known/BestEffort
  identities still match exclusions. Acceptance with uncertain source carries
  AcceptedLimitedSource, not a claim that the source is safe.
- Default unknown-source mode rejects Unknown only with a nonempty exclusion
  list; explicit AllowUnknown permits it with the limitation visible, while
  RejectAlways disallows it. A marker exclusion still wins in every mode. On a
  Wayland backend without source identification, configuring exclusions can
  therefore stop all capture; it is not silently bypassed.
- Eight content classes can be toggled independently. All default on except
  HTML. Only registered/enabled representations are copied; native bytes,
  encoding, order and original item indices remain unchanged. No allowed format
  remaining means rejection. No existing history is deleted by policy changes.

Unknown cooperative flags remain Unknown, even when an ignore option is on.
This is marker-based filtering, not universal detection of passwords or remote
clipboard content. Platforms must expose those limits through capability
reasons/UI. Capture Limited may permit a specific backend/session; it never
enables unrelated capabilities. Malformed enums/IDs and oversized metadata or
payloads fail closed before copying a filtered snapshot (T02 hard ceilings).

## Verification and footprint

`ctest --preset test -R policy --output-on-failure` runs the policy suite. It
uses real decisions and real SQLite behind a counting sink to verify persistence
and absence of storage/thumbnail/OCR effects on rejection. No user clipboard is
read or changed. Screenshot coverage supplies an image snapshot to the **same**
gate; no screenshot watcher is implemented yet.

`core_probe` and `core_policy_probe` use the same commit/compiler/MinSizeRel/link
flags. The latter adds policy dispatch, trusted registry/capability setup, sink
wiring and result recovery. The delta measures this linked integration workload,
not policy machine code alone. Neither executable is a product build; native
backend conformance, OS permissions and full distribution size remain later work.

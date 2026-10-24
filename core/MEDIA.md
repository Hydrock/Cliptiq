# Media contract (T12)

`media.hpp` owns preview persistence, aspect-fit, PNG preflight, screenshot snapshot
metadata and pixel fingerprints. `platform/Media.hpp` owns one worker, native
decoders, directory monitors, and GUI-thread publication. Native menu providers
only copy ready RGBA buffers; they perform no SQL, file reads or image decoding.

## Preview completion and limits

`request_preview(Store&, EntryId, PreviewSize, generation)` returns a typed ticket:
ID, current representation digest, dimensions and a monotonically increasing
generation. Dimensions are 1..512, default100×32, with aspect preservation and no
upscaling. `store_preview_if_present` validates every ticket field and existence in
one transaction. A deleted/cleared/pruned entry cannot be recreated by completion.
Changing dimensions creates a new generation; an old-sized result cannot overwrite it.

Existing `history_assets` holds request metadata, preview data and a full-pixel
SHA256 signature; foreign-key deletion cascades remove all of them. Disk preview
data and ready RAM cache each cap16MiB. Queue≤64 jobs and64MiB encoded preview
payloads; one worker and one completed result. Encoded images/files≤32MiB;
dimensions≤40,000,000 pixels before full decode allocation. Pixel normalization can
temporarily retain the system decoder's full buffer and the RGBA copy; these
ceilings do not establish a160MiB RSS bound. Allocation/error behavior of the OS
codec remains part of the platform dependency.

Native codecs: ImageIO/CoreGraphics on macOS; existing WIC PNG/DIB decoder on
Windows; GdkPixbuf loader selected by the offered MIME type on Linux. No new
bundled codec, web runtime or image SDK. Existing menu hex-color swatches remain.

## Screenshot ingestion and privacy

Observation defaults OFF. `screenshots=1` requires an explicitly selected absolute
`screenshot_directory`. Only immediate `.png` children are accepted, never arbitrary
paths or an initial scan of existing images. Selected directories are pinned with
native handles. POSIX reads use openat/no-follow; Windows resolves the pinned
directory handle and verifies the opened file's final parent identity. Final-file
symlinks/reparse points, non-regular files, path traversal and oversized files fail
closed. A renamed/replaced directory cannot redirect imports to its replacement.

Windows uses bounded nonrecursive ReadDirectoryChangesW; Linux uses GFileMonitor;
macOS uses NSMetadataQuery with `kMDItemIsScreenCapture=1`, scoped to the selected
folder and filtered to immediate children. File event names coalesce. Complete
identical file bytes must be observed twice250ms apart; changing/incomplete files
retry for up to10s. Framing/CRC checks and successful native PNG decoding precede
ingestion. This reads screenshot files, not screen recording/capture APIs.

Privacy policy preflight runs before enqueue/read/decode; actual `dispatch_capture`
runs again before persistence. Screenshot app identity is Unknown, source_app
capability Limited. App exclusions in the default unknown-source mode reject file
imports. Explicit AllowUnknown can permit them; RejectAlways and image-off reject.
Filesystem events cannot establish concealed/transient/remote clipboard flags or
the foreground application's identity. No OCR pipeline is introduced.

Changing configuration/off stops native callbacks, changes the service epoch, drops
pending work and prevents old results from publishing. Decode itself may finish,
but cannot commit an obsolete result. One growing file can occupy the worker for
up to10s; stop/configuration wakes stability waits. Clear, individual deletion and
periodic cleanup advance internal `settings['t12.erase_generation']` transactionally.
Pending screenshot jobs carry this generation; ingest rechecks it inside its
immediate transaction. This also covers clearing an empty history. Events arriving
around clear may be conservatively dropped until the GUI refreshes its generation.
Older binaries do not maintain this metadata: concurrent older/newer writers are
unsupported. This is additive namespaced metadata, not a schema-version migration.

Dedup uses dimensions and full normalized RGBA pixels, not compressed bytes or a
lossy thumbnail. Only screenshot-origin duplicates are removed; clipboard history
duplicates retain the configured policy. Exact encoded duplicates are suppressed
before screenshot ingest can replace clipboard metadata. Preview/pixel completion
still requires a retained ID. Workers never write SQLite; each Store is serialized.

Monitor errors remain in `observation_status()` as Unavailable, produce a disabled
menu notice and persist in Settings status on reopen. Windows overflow/failure and
Linux root-loss/unmount stop accepting events until the user restarts observation.
Normal stop is not a monitor failure. Full-volume overflow recovery does not scan
arbitrary files. macOS relies on Spotlight indexing; query startup alone does not
prove live screenshot delivery.

Known deferred Minor: preview-off hides menu images but currently still performs
background decoding/cache generation. It is not yet a resource-saving switch.

## Primary references

- [Pinned Screeen source](https://github.com/Clipy/Screeen/tree/f8f4bfa6cb38cb69ec1ee104fb5ad949a92b2ae3): examined before selecting macOS detection. Clipy uses added metadata items and NSImage; Screeen's default configured-location/Desktop discovery is deliberately replaced by explicit folder selection here. Exact automatic Mac parity is not claimed.
- [ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw): asynchronous completion and overflow semantics.
- [GdkPixbufLoader](https://docs.gtk.org/gdk-pixbuf/class.PixbufLoader.html): size-prepared precedes allocation and close finalizes incremental input.

[Manual acceptance](../tests/manual/media.md) remains OPEN.

# Windows clipboard backend (T05)

C++17/Win32, static CRT, system User32/Shell32/COM/WIC. No third-party GUI,
web runtime, bundled image codec or OCR engine. Common ClipboardBackend interface
matches T04; this is a library, not the finished menu application.

## Capture and replay

Create/start/read/write/policy configuration on the owning UI thread with a
pumped Win32 message loop. A message-only window registers
AddClipboardFormatListener; WM_CLIPBOARDUPDATE is coalesced. Initial contents are
also examined. Sequence and owner are checked before/after reads, including
delayed rendering. Open/consistency retry is capped at three attempts (4 ms
between busy opens); listener retries Busy via three 20 ms timers before reporting
it. Delayed OS providers can still block inside GetClipboardData: retries do not
preempt native calls. No polling loop or claimed capture of every intermediate
version. Own replay's final sequence is suppressed.

Supported transferable formats at item_index 0:

| Format | Representation |
|---|---|
| CF_UNICODETEXT | UTF-16LE, NUL terminated, surrogate validation, CRLF preserved |
| Rich Text Format | Native RTF bytes |
| HTML Format | Entire CF_HTML including UTF-8 byte offsets, validated ranges/encoding |
| Portable Document Format / PDF | Registered PDF bytes, app interoperability must be tested |
| com.apple.flat-rtfd | Opaque RTFD bytes, no Windows application support promised |
| CF_HDROP | Wide DROPFILES list, multiple paths; references, not copied file contents |
| Preferred DropEffect | File metadata; replay always requests COPY, never historic CUT |
| UniformResourceLocatorW | UTF-16LE URL bytes, no navigation |
| PNG / CF_DIBV5 / CF_DIB | PNG preferred, then DIBV5, then DIB; original native bytes retained |

Windows formats describe one logical clipboard object; multiple file paths are a
CF_HDROP group, not arbitrary macOS items. Unknown registered/private types and
virtual FileGroupDescriptor/FileContents/OLE-only objects are not transferable
history representations. CF_BITMAP/GDI/metafile/palette handles are never
serialized as bytes; OS-provided DIB conversions may be available. ANSI text uses
Windows' synthesized Unicode when offered. ANSI DROPFILES, palette/RLE/nonstandard
DIB masks and embedded/linked DIB profiles are explicitly unsupported.

GlobalSize is bounded before GlobalLock/deep copy (32 MiB representation,
64 MiB entry defaults, configurable downwards). Formats/count/metadata are bounded.
UTF-16 terminators, DROPFILES structure, DIB dimensions/stride and CF_HTML offsets
are validated. Global memory is unlocked before closing; borrowed handles are
never freed. Replay validates/prepares transferable GMEM_MOVEABLE buffers before
EmptyClipboard and transfers ownership only after successful SetClipboardData.
Native failure after clear can leave partial data: replay is not transactional.

## Privacy and attribution

The full ordered native offer list survives until T03. ExcludeClipboardContentFromMonitorProcessing
presence denies capture before content reads. CanIncludeInClipboardHistory other
than a valid DWORD 1 is conservatively transient; malformed/unreadable hints deny.
CanUploadToCloud alone is not interpreted as a local history prohibition or Apple
Universal Clipboard. org.nspasteboard transient/concealed markers are honoured if
cooperatively published on Windows. No universal password detector is claimed;
concealed and remote absence remain Unknown.

Metadata placeholders pass evaluate_capture before content is requested. Disabled
HTML/unknown formats are never requested; rejected content cannot create a DB.
Known marker prefixes, app exclusions and Unknown-source default rules use T03.
Owner PID -> QueryFullProcessImageName path -> invariant lowercase UTF-8 is only
BestEffort, never Known. Missing/exited/inaccessible owner is Unknown, without
substituting an unrelated foreground app or elevating privileges. Application
exclusions must use the same normalized full executable path. Background capture
is not proof of authorship or permission to bypass UIPI/secure desktops.

WindowsCaptureSession owns a serial worker. Only it opens/uses/closes Store. The
filtered dispatch_capture snapshot is passed to a sink which decodes images with
system WIC on the worker before persistence. Pixel limit 40 million (at most
160 MB BGRA output), native input limits still apply; WIC/provider internal memory
is not a total RSS guarantee. No image decode, thumbnail or OCR occurs on the UI
thread. Thumbnail/OCR hooks remain unimplemented, OCR capability Unavailable.

Queue limits default to two pending/in-flight snapshots and 128 MiB conservative
retained input estimate, configurable within hard ceilings. Overflow reports Busy
and skips the new copy. stop invalidates queued/completion generations and joins
in-flight I/O before closing the store. It may wait for SQLite/WIC; it is not an
I/O cancellation guarantee. UI reports contain only decision/reason/retained ID.
Callbacks must not throw and may stop/destroy their main-thread owner. Off-thread
backend stop is deferred with generation guards; destruction is normally on the
owner thread, otherwise that thread must keep pumping deferred cleanup. Session
start/stop/destruction and policy changes are owner-thread operations.

The host remains responsible for private DB directory ACLs, single-instance
ownership, settings UI, hotkeys, auto-paste, launch-at-login and distribution.

## Tests

Automated native tests/size hosts create a unique noninteractive window station
and desktop BEFORE any clipboard access, aborting on isolation failure. They do
not access WinSta0 or user clipboard content. Delayed rendering spies use real
WM_RENDERFORMAT; sequence-change/source callbacks are explicitly fault injection.
Local executor denies station creation (error5); this is a recorded limitation,
not a pass or permission to test on the user's clipboard. Native evidence comes
from Windows hosted CI; parser/WIC/core tests also run locally.

See [manual handoff](../../tests/manual/windows.md),
[results](../../docs/support/t05-results.md), [fixture attribution](../../tests/fixtures/windows/README.md).

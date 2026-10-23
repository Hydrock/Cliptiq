# Windows T05 fixtures

Fixtures are independently generated in windows_clipboard_test.cpp: UTF-16LE
Cyrillic/emoji/CRLF, manually counted CF_HTML byte ranges with a multibyte UTF-8
character, minimal RTF/PDF/RTFD blobs, DROPFILES with two synthetic Unicode paths,
DIBV5 alpha and DIB24 bottom-up stride. PNG is a generated 1x1 RGBA sample encoded
with zlib/PNG chunks; no third-party images are redistributed. No real user files
or clipboard content are needed. Paths test structure, not file existence.

Real Win32 GMEM_MOVEABLE, clipboard listener, delayed WM_RENDERFORMAT providers,
WIC decoding and SQLite sink are used. Source callbacks that mutate a format are
explicit deterministic sequence-race injection, not an external app race claim.
IsolatedClipboard.hpp belongs only to test/probe hosts; it switches to a new
noninteractive window station/desktop and aborts if unsuccessful. No fallback to
WinSta0. Native station tests run in Windows CI; manual apps are separate.

Expected outcomes derive from the Clipy feature matrix and Microsoft public
clipboard contracts. No Clipy binary fixtures/assets/code have been copied here.

Review regressions include binary RTF with PNG \binN bytes, explicit cloud-upload
prohibition and a deterministic unlock race. The race uses a test-only import
interceptor for CloseClipboard: it calls the real API, then publishes a second
owner through real Win32 calls before replay resumes. No production hook, no
user clipboard access, no elevation/consent bypass.

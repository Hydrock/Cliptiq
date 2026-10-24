# T05 Windows manual acceptance

Status: **NOT EXECUTED**. Hosted CI exercises real Win32 APIs on a noninteractive
isolated window station. It does not establish Notepad/browser/Office/Explorer
interoperability, secure/elevated desktop behavior or stock clean-system install.
Local executor cannot create the isolated station. Do not remove isolation or
substitute the user's existing clipboard to turn an automated test green.

On a Windows test machine build Release with pinned SQLite/CMake, run CTest.
Use only generated nonsensitive data for the following app checks. Record Windows
product/build, CPU, compiler, commit, app versions, observed native formats,
command/steps and exact observed results. No personal clipboard contents in logs.

- [ ] Notepad: Cyrillic/emoji, CRLF, empty/whitespace and multiple lines; capture,
  history replay and paste back preserve Unicode and line breaks.
- [ ] Browser -> Word/LibreOffice: formatted text and UTF-8 HTML fragments; repeat
  with HTML off, verify RTF/plain alternatives and no disabled HTML replay.
- [ ] Explorer: copy two temporary files with spaces/Unicode; replay/paste into
  another owned test folder. Cut -> replay must COPY, not delete original files.
  Missing files are references, not a promise of retained contents.
- [ ] Image editor: PNG alpha, DIBV5 alpha, bottom-up/top-down DIB24 with padding;
  paste/replay preserve orientation and supported transparency. Oversized/malformed
  fixtures report bounds/errors without persistence; no UI freeze from decoding.
- [ ] A PDF-capable clipboard producer/consumer: verify the actual registered
  format name and roundtrip. Raw registered blob tests alone do not prove app support.
- [ ] Virtual files from mail/archive/OLE-only producers are reported unsupported
  unless a real transferable alternative exists; no persisted handle/pointer values.
- [ ] Clipboard busy/delayed owner/owner exit: bounded open retry, consistency,
  explicit capability/error reason. Native delayed rendering can still block.
- [ ] App exclusions use normalized lowercase full owner executable path; missing
  or inaccessible owner remains Unknown. Check cooperative deny/concealed hints.
- [ ] Listener start/stop, sleep/resume, rapid copies, blocked storage and restart:
  no own-write history loops, UI completion stays on owner thread, Busy/overflow visible.
- [ ] Unelevated app with elevated producer/secure desktop: no automatic elevation,
  no bypass of Windows isolation/UIPI, accurate limits instead of a protection claim.

T04 macOS manual gate remains open independently. T06 is not part of this task.

Official API sources: [Win32 clipboard operations](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations),
[format listeners](https://learn.microsoft.com/en-us/windows/win32/dataxchg/using-the-clipboard),
[CF_HTML](https://learn.microsoft.com/en-us/windows/win32/dataxchg/html-clipboard-format),
[window station clipboard isolation](https://learn.microsoft.com/en-us/windows/win32/winstation/window-stations),
[clipboard formats/privacy hints](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats).

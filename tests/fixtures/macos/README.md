# T04 fixtures and attribution

Generated locally in tests with AppKit/Foundation: a 2x2 RGBA PNG/TIFF,
NSAttributedString RTFD with an NSFileWrapper PNG attachment, NSView-generated PDF,
UTF-8 text, hand-written minimal RTF/HTML, file URLs and a serialized two-path
NSFilenamesPboardType array. No user clipboard, real secrets or external files
are read by automated tests. Empty metadata-marker data are synthetic.

Behavioral cases are recreated from Clipy develop at
`71b0e82623d5c73b4b717d81233eb78db8bbe258`:

- ClipyTests/Models/PasteboardContentTests.swift: root filenames/TIFF fallback,
  multiple items and metadata roundtrip.
- Clipy/Sources/Models/PasteboardAvailableType.swift: modern/legacy mapping,
  PNG preference, transient/concealed/remote policy.
- Clipy/Sources/Extensions/NSPasteboard+Deprecated.swift: legacy type identifiers.
- Clipy/Sources/Services/ClipService.swift: 500 ms changeCount polling, initial
  existing-content capture from cached count zero.

Source references: [Clipy](https://github.com/clipy/clipy/tree/71b0e82623d5c73b4b717d81233eb78db8bbe258).
Tests and backend are independently implemented; fixture bytes are generated,
not redistributed upstream binary assets. Byte-fidelity assertions compare with
the bytes actually supplied by NSPasteboard, which may normalize plist encoding.
File arrays are preserved in the legacy root compatibility representation.

BoardProvider and ItemProvider wrap real named pasteboards/items to inject
root-only access, changing counters, permission states and payload-read counters.
These conditions are labelled injection, not claims about OS permission prompts,
external app behavior or actual external ownership races. Standalone timer,
multi-item replay and worker persistence tests use real NSPasteboard APIs.

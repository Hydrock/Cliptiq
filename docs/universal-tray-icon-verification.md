# Universal tray icon — 2026-11-12

One two-tone clipboard glyph replaces Windows standard/reversed rendering.
Light paper and dark outline remain visible on light and dark backgrounds.
The bounded RGBA generator covers 16–64 pixels with supersampling; Windows uses
the DPI-specific small-icon metric, premultiplied BGRA and initialized AND mask.
Linux uses the same glyph in DBus ARGB order. AppKit always uses its native
clipboard template image, automatically adapting to the menu-bar appearance.

Removed tray style from settings descriptors and Windows choice/caption code.
Legacy stored tray keys are ignored on load and removed on next save, so a former
Hidden preference does not suppress the new icon. Internal Hidden CLI/test mode
remains for isolated fixtures; normal product startup always uses the one icon.

Tests observed RED then GREEN for missing generator and retired setting. Image
checks cover 16/20/24/32 pixel dimensions, transparent padding and opaque light/dark
pixels. Migration test loads old Hidden and saves without the retired key.
A visual sheet generated from the exact core raster was checked on light/dark
backgrounds; output out/windows-audit/tray-icon-sheet.png. Regenerate with
menu_preview_test --tray-sheet <output.ppm> and convert PPM for viewing.

Full Windows build succeeded; final available CTest 39/39 passed. The update test
was adjusted to the user's previous menu-scope correction: service commands are
absent from the quick popup but still valid application commands. Twelve existing
WindowStation/Authenticode checks remain excluded. Source review found no important
issues. macOS and Linux changes are source-only, not built in this Windows session.

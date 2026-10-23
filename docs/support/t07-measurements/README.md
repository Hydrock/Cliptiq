# T07 reproducible evidence

Source `19b93758382ae8570a377597f536e2cceaa41678`, final [CI39997567519](https://github.com/Hydrock/Cliptiq/actions/runs/39997567519), 7/7 successful jobs. Documentation added later does not change measured code. `ci/*/t02-size.json` retains the old filename and earlier probe sections; **the `menu` section is T07**. `full_product_bytes` and offline dependencies remain null. Every reported source hash was checked against this tree. Local Linux release binary SHA256 matches CI for all three variants.

`ci/` contains unmodified UTF-8 artifact text with line endings normalized to LF, including Debug/MinSizeRel CTest and sanitizer LastTest logs. `ci-compilers.json` preserves compiler IDs and successful test counts from job logs. Linux17, macOS9 and Windows12 CTest groups. Unicode17 GraphemeBreakTest:766 cases. These are groups, not counts of individual assertions.

`local/` preserves Linux GCC debug/sanitizer runs, the separate system ELF closure measurement, local MSVC build/portable tests, initial RED and final-review RED evidence. RED logs are intentionally failing historical tests and must not be mistaken for final CI. `review-red.json` links the deliberately failing native regression CI source0e3432a; final fixes are19b9375. GCC local sanitizer supplements CI Clang sanitizer.

`screenshots/` were regenerated from final native code in owned Xvfb with synthetic fixture text only: Adwaita1x, Adwaita:dark with GDK_SCALE=2, HighContrast with GDK_DPI_SCALE=1.5 (font scaling, not physical monitor150%). They were visually inspected for selection, literal ampersand/underscore, emoji, orange swatch and disabled Clear. They prove neither full screen-reader support nor real desktop integration. GTK synthetic popup reports no trigger-event warnings; owned bus teardown can print disconnect. Hosted Linux may lack AT-SPI service; accessibility remains a manual gate.

Windows native keyboard/DPI/context fixtures replace only the modal TrackPopupMenuEx display boundary in a test-only library; native buttons, messages, focus and bitmap handles are real. AppKit context regression substitutes only the modal NSMenu boundary. Production/size hosts use actual APIs. Linux independent SNI watcher/client exercises wire layout/events/re-registration on a private bus. No user clipboard data is stored here.

`sha256.txt` covers all files in this directory except itself. No executable binaries or credentials are published. See [results](../t07-results.md) and [manual checklist](../../../tests/manual/menu.md).

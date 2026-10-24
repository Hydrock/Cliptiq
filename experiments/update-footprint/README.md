# T01 updater footprint spike

Run `python measure.py` on Windows/macOS. On macOS first fetch baseline SQLite with `python tools/fetch-footprint-deps.py`; CMake/Ninja/rustc are required by the existing fixture CMake project, although only C++ target is built. Run `python3 linux-payload.py` on Linux after the existing C++ GTK3 Base build.

Outputs are isolated under `out/update-footprint/`. Evidence and scope limits: `docs/support/update-footprint/README.md`. No actual installer is executed and no real update feed is contacted. The Windows CLI stages HTTPS payloads and validates embedded Authenticode plus signer certificate pin; it deliberately has no install operation. The macOS probe initializes Sparkle with automatic starting disabled, embeds full or optional no-XPC framework, and signs ad hoc for a lifecycle/size test. Developer ID/notarization and actual signed installs remain untested.

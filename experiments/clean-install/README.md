# T01 clean-install probes

Run Base builders first. Linux: `python experiments/clean-install/run_linux.py --lane gtk3` (also gtk4, ocr). Optional GTK3 package test: add `--deb out/update-footprint/linux/cliptiq-t01_0.0.1_amd64.deb` after running the Linux update-payload builder. Docker must be running with Linux containers. Outputs go to unique `out/clean-install/<lane>/runs/<id>` directories and latest.json; only this run's container is removed.

The exact pinned Ubuntu image is pulled, dependencies downloaded, bridge network detached, then installation and tests run offline. Runtime filesystem delta is measured before adding Xvfb harness. Existing OS base is not counted as a new dependency. DEB installation/removal is optional and recorded explicitly. No desktop session is claimed.

Windows/macOS: `python experiments/clean-install/clean_profile.py`; see clean-install.yml for reproducible setup. Fresh HOME/PATH, copied archive, fixture/lifecycle, Windows import allowlist and macOS ad-hoc signature checks. These runners have developer tools: clean_vm_verified remains false. Never describe profile isolation as a clean OS VM.

Published evidence: [clean-install](../../docs/support/clean-install/README.md).

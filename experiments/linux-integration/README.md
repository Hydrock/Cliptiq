# T01 Linux integration smoke probes

Throwaway capability measurement, not T02 or production adapters. Run on Linux as a non-root user; all clipboard/key operations target an isolated Xvfb or headless Sway server. The D-Bus session is private. No connection to the user's display, clipboard, or session bus is used by the probes.

Ubuntu build/test prerequisites (the package manager may install further dependencies):

```sh
sudo apt-get install gcc pkg-config libx11-dev libxtst-dev xvfb xauth dbus python3-dbus python3-gi sway wl-clipboard wayland-utils
bash experiments/linux-integration/run.sh
```

From PowerShell on the measured machine:

```powershell
wsl -d Ubuntu -- bash -lc 'cd "/mnt/c/Users/SmiteVils/Documents/ChatGPT/ClipBoard Project" && bash experiments/linux-integration/run.sh'
```

`run.sh` accepts an optional **existing** output directory relative to `experiments/linux-integration/`. Default evidence is `docs/support/linux-integration/`. Temporary executables, Xvfb, Sway, clipboard owners and D-Bus services are stopped after each run. Runtime warnings from the private portal are saved in `dbus.log`; Sway logs are saved in `sway.log`.

The runner fails on a compile error, clipboard mismatch, missing data-control advertisement, missing key event, mock registration/menu failure, or process deadline. The assertions are the measurement smoke tests; they are not product contract tests.

* `x11.c`: two independent Xlib connections; actual CLIPBOARD owner, SelectionRequest/SelectionNotify and exact UTF-8 readback. A root-window passive Ctrl+Shift+F12 grab receives XTest input while another client's mapped window has focus. XTest is test input only. Ctrl+Alt+F12 was deliberately avoided because the X server can reserve Ctrl+Alt+function keys for VT switching.
* `dbus_probe.py`: separate mock watcher and item services; wire calls to RegisterStatusNotifierItem, Properties.Get(Menu), dbusmenu.GetLayout/Event and Activate. This verifies message plumbing against our mock, not conformance to every host implementation. The same private session attempts bounded activation of an installed portal, saves actual introspection XML and reports GlobalShortcuts/Clipboard advertisement. A missing interface is a measured unavailable capability, not a passing hotkey test.
* Sway headless/pixman: actual `wayland-info` registry; requires `zwlr_data_control_manager_v1`; independent `wl-copy`/`wl-paste` processes compare UTF-8 bytes and clear the selection before shutdown. No physical input devices or desktop shell.

Read [measurement scope](../../docs/support/linux-integration/README.md) before drawing support or distribution conclusions.

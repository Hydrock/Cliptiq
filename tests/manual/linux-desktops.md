# T06 Linux desktop acceptance — OPEN

Automated backend evidence does not close interactive desktop acceptance. No clean desktop VM is available; CI runners/WSL developer images do not substitute for clean installation. T04/T05 manual gates remain OPEN. T07 has not started.

| Environment | Tested version | Capture/write | Source | Portal | Tray/popup/launcher | Status |
|---|---|---|---|---|---|---|
| X11 isolated Xvfb | 21.1.12 local Ubuntu 24.04 | TARGETS/normal/INCR, owner death, timeout, generation, replay | WM_CLASS BestEffort or Unknown | unused | T07+ | backend automated only |
| Sway headless, native Wayland | 1.9, wlroots 0.17.1 local | wlr data-control roundtrip, monitor, persistence | Unknown | unused | T07+ | backend automated only |
| ext + wlr wire peer | test fixture, not desktop | ext preferred; FD faults/no protocol/seat tests | Unknown | unused | absent | protocol evidence only |
| GNOME Shell | 46.0 private headless | no ext/wlr data-control advertised; native background backend unavailable (registry inference) | Unknown | unused | T07+ | LIMITED; desktop OPEN |
| KDE Plasma/KWin | unavailable locally | unverified | unverified | unused | T07+ | OPEN |
| Hyprland | unavailable locally | unverified | unverified | unused | T07+ | OPEN |

For each real target record distribution/compositor/package versions, X11/Wayland, advertised globals/seat, capture and replay against an independent browser/editor/file manager/image app, rich formats, metadata privacy, exclusion/Unknown behavior, 32 MiB rejection, owner/compositor exit, restart, retained database integrity, clean installation and dynamic library availability. Test process exit: X11/Wayland selection ownership is not durable storage; no clipboard-manager persistence promised. Check shortcuts, snippets, visible launcher, popup route and tray separately only when their owning stages exist. Without a tested integration pack do not label a restricted compositor a supported history desktop. Portal denial is never bypassed.

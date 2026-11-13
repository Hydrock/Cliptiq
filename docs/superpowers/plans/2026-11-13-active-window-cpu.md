# Active Windows window CPU fix

Goal: eliminate the active-window busy loop without delaying clipboard/hotkey input or breaking native dialog keyboard behavior.

Evidence: active real window reached 12–15% of one CPU; phase instrumentation showed ~1000 outer loops/second with almost no removable input. Native stack samples put active work in UIAutomationCore/Oleacc via PeekMessage; MsgWait returned for nonqueued/system requests. Toggling cosmetic row metadata or removing a duplicate pump did not solve it. A real cross-thread SendMessage regression (no API mocking) fails on the previous pump and passes on the blocking pump.

Architecture: GetMessage handles nonqueued calls while staying blocked, with a private window timer retaining the existing <=100ms controller deadline. Both dispatch paths share pretranslation and dialog routing. Editor rename Enter/Escape must pass through its owner before IsDialogMessage. No changes to LAN protocol, history or user preferences. Windows-only fix; shared options/headers have Windows guards.

- [x] Reproduce and capture controller counters/native module stacks.
- [x] Add failing regression for nonqueued requests, bounded queue backlog and posted-message wake.
- [x] Implement bounded blocking message receive and shared dispatcher.
- [x] Preserve editor pretranslation, Enter, filter Tab, text selection and foreign-dialog routing.
- [x] Independent review of production diff (no P1/P2 findings).
- [x] Complete available test suite; 61/61 passed, 12 documented environment exclusions.
- [ ] Install without diagnostic instrumentation, preserving profile and old binary.
- [ ] Measure real foreground/controls and background with focus metadata; save raw counters and report.

# T12 desktop acceptance — OPEN

No clean Windows/macOS VMs are available. Hosted CI, isolated Linux compositors and
temporary-directory tests do not establish a clean install or live desktop behavior.
All existing T04–T11 manual gates remain OPEN. T13 is not started.

- [ ] macOS ARM and Intel: copy PNG/TIFF images in Preview/Safari, check previews
  in tray/history/folder menus, DPI/color/alpha,100×32 fit and dimensions1/512.
- [ ] Windows: copy PNG/DIB from Paint/Snipping Tool/browser, check native previews,
  high DPI, alpha, clear/delete while decode is pending, locked clipboard behavior.
- [ ] Linux X11/Wayland: test GNOME/KDE desktop menus/tray/launcher, screenshots from
  native tools, previews with/without tray/data-control, MIME codec availability.
- [ ] Confirm default OFF; explicitly select a screenshot folder and enable it.
  Existing files and PNGs outside that folder must not import. Test nested folders,
  partial writes, temporary-file rename, repeated notifications and Unicode names.
- [ ] macOS: use actual system screenshots with Spotlight indexing enabled, custom
  screenshot location, and disabled indexing. Confirm exact capability limitations;
  query start/stop in CI does not satisfy live metadata delivery acceptance.
- [ ] Deny folder access using real platform permissions (including macOS privacy
  prompts and Windows ACLs). Inspect persistent error in menu/Settings, then restore
  permissions and restart observation. Test removed/unmounted folder and overflow.
- [ ] Replace the selected pathname with a new folder/junction/symlink during file
  stability/decode. No content from the replacement/outside folder may enter history.
- [ ] Configure app exclusions: default Unknown-with-exclusions rejects screenshots;
  explicit AllowUnknown permits unknown source, RejectAlways and image-off reject.
  Do not infer source application or concealed flags from the filename/front app.
- [ ] Clear/delete while a screenshot file is pending. It must not recreate content;
  confirm a new independent event after the clear can be captured. Disable/re-enable
  and change dimensions/policy while worker work is pending.
- [ ] Copy a screenshot and observe the same file in both arrival orders, including
  PNG re-encoding. Ordinary clipboard duplicates must preserve their configured policy.
- [ ] Reopen menus/settings after watcher failure: failure remains visible until
  restart; observation must not appear active when unavailable.

Automated coverage and remaining limitations are described in
[T12 results](../../docs/support/t12-results.md) and [media contract](../../core/MEDIA.md).

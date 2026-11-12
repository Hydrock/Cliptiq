# Shared quick menu and native UI consistency — 2026-11-12

User requested a single popup with History and Snippets branches and familiar
interaction across Windows and macOS. This supersedes the previous separate-menu
choice. Preserve native rendering and the current Windows workspace design.

- Common core Main projection owns both localized branches; native adapters render
  the same tree. Default order: History, Snippets. Existing snippets_above preference
  can reverse these branches. Empty branches are disabled, not omitted.
- Existing history grouping, snippet folder order, filtering, IDs, limits and
  paste-target validation remain unchanged. Existing global service actions remain.
- Standard main/history/snippets hotkeys open the unified model, retaining saved
  bindings as aliases. Settings identifies one Quick menu plus alternate bindings.
  Specific folder shortcuts keep their direct-folder purpose.
- Mouse pointer anchors the explicit popup; no workspace activation. Selection
  pastes into the prior target when automatic paste is enabled. Cancel changes no
  clipboard content. OS work-area adjustment remains native.
- Windows workspace keeps the existing sidebar/list/detail layout. macOS still
  needs a full workspace port; this change only shares the quick-menu structure
  and updates the AppKit explicit-popup location/activation behavior.

Cross-platform UI contract: use the same navigation names, ordering, action meanings,
empty states and settings groups. Use platform system typography/controls, focus
indicators and shortcuts. Do not introduce a web runtime or Raven solely for pixel
matching. Native menu metrics may differ. Future Mac workspace should use the same
sidebar, list/filter and preview/action regions as Windows, with native scaling.

Verification: root structure and locale tests observed failing before implementation
and passing afterwards; complete Windows available suite and packaging recorded
in the implementation report. AppKit source change requires build and manual
validation on a Mac; no claim of full macOS UI parity is made.

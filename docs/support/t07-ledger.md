# T07 execution

BASE953de7e7762af85764b494995fa3a39f33de2669. User authorizes model/native popup/tray implementation, history integration, footprint checks and upload to existing draft PR. T04-T06 manual gates remain OPEN; T08 excluded. Reuse clean dedicated codex/portability-plan branch and existing approved design/plan; no fresh architecture approval needed.

Pre-flight T02->T07: Store/list_history/load_snapshot exist; avoid loading every full64MiB payload on UI. Need bounded read-only projection in common menu builder, one read transaction, stable IDs and data_revision. T10 snippets editor/enabled flags do not exist yet, but T02 snippet tables do; T07 renders existing persisted ordered folders/snippets, without adding editor/import/T10 CRUD.

Ruling: native adapters emit immutable commands carrying ID/revision; no auto-paste, modifier action or clipboard write in T07 (T08 owner). A native review host displays history/snippets and clear/status, preserving visible launcher when tray is hidden/missing. Host is a review harness, not finished shipping app. Icons/previews reference persisted assets; decode/image production belongs to T12. Native selection must never execute destructive or selection callbacks on stale models.

Ruling: small pinned Unicode17 libgrapheme3 character segmentation only, not full ICU; linked size measured. Existing defaults/history grouping follow upstream audit. No platform repeats SQL/grouping.

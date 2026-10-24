# SDD ledger — plan: docs/superpowers/plans/2026-10-23-cliptiq-port.md
T17 BASE 8b8d7df2bddc2c6aeae8ff40d1af02147eea8d15. Continue existing checkout/feature branch and draft PR under user authorization. Manual T04–T16 OPEN; no production release/merge.
Pre-flight: T01 size-report measures external OCR separately; T16 app-owned OCR already contains models, so T17 records included models without double counting. T13 lifecycle scope shared, T15 signature/update checks fail closed; no production identities supplied. T16 optional pack source/CI retained.
Ruling: skill task-start parser expects Task17, actual plan uses T17 — preserve exact brief and BASE manually — no scope change; helper completion tooling needs same adaptation.
Ruling: enforce provisional 10MiB/5MiB Base ceilings and10% growth as engineering gates, not newly user-approved budgets; separate optional lane ceilings based on measured closure — prevents hidden growth — initial gates may require an explicit measured adjustment.
Ruling: build unsigned/ad-hoc CI candidates; production signing/notarization/install/clean-machine gates remain OPEN — no identities/VMs available and user explicitly keeps manual acceptance open — not a production-release claim.

Final review: fresh gpt-6-astra, range8b8d7df..fe95fdb; no Critical, two Important, no Minor. One fix pass.
Final: fixed absolute/CWD-dependent ELF/Mach-O staging loader paths — test_absolute_and_cwd_dependent_loader_routes_are_rejected RED→GREEN; unsupported 32-bit/big-endian/FAT64 native magic cannot hide as data — test_unsupported_native_magic_cannot_hide_as_data RED→GREEN; tools25 tests green (3 Windows symlink skips), Linux release-suite62/62.
Final: Ruling: manual T04–T16 gates remain OPEN — explicit user instruction; automated contracts do not establish desktop acceptance — cost if wrong: no production acceptance proof.
Final: Ruling: unavailable clean VMs — retain null system delta and real install gate OPEN; CI is not substitute — cost: installation failures on consumer systems remain possible.
Final: Ruling: unsigned/ad-hoc candidates — useful test artifacts only, no production promotion — cost: OS trust/install acceptance not demonstrated.
Final: Ruling: production identity/notarization — no supplied credentials, keep gates OPEN — cost: no public trusted distribution.
Final: Ruling: Windows/Linux ARM support deferred — no measured native lanes, validator rejects unmeasured size targets — cost: those architectures are not supported candidates.
Final: Ruling: automated-contract feature scope — explicit adaptations and existing evidence, no claim of manual parity — cost: real desktop differences may remain.
Final: Ruling: native package execution — reviewer did not execute all OS packages; actual CI runs required before completion — cost: target packaging errors must still be resolved.
Ruling: project license not selected — package dependency notices without assigning project copyright terms — cost if wrong: production licensing remains unresolved.
Ruling: hosted toolchain mutable — pin Actions/dependency content and record actual toolchain; hermetic reproducibility not claimed — cost: future SDK/image changes may change sizes or behavior.
Ruling: MinSizeRel IPO enabled after measured Linux -5.95% binary reduction, supported compiler required — cost: longer links and compiler-specific size changes checked by CI.

Final: minor (deferred): intermittent existing X11 fixture atom query and T01 macOS lifecycle timeout; original logs retained, unchanged assertions, one explicit CI retry. Root cause not established; future CI may need a retry.

Task 17: complete (automatic scope; commits8b8d7df..5380462; native CI40105477768 attempt2 success16/16; release suites macOS49/49x2 Windows52/52 Linux62/62; tools25; coverage212 rows;8 distribution budgets/inventories PASS;1322 source digests verified). Real clean-machine/manual/production gates explicitly OPEN. Existing branch/draft PR updated under user authorization, no merge/release.

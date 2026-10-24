# T16 final fresh review

Reviewer: gpt-6-astra, read-only, one pass. Range f92437f..8bb6315.
No second reviewer or re-review. Earlier T01–T15 accepted separately.

- Critical1: index gate released before main COMMIT allowed stale sensitive
  companion recreation. Reviewer reproduced0main/1companion row after clear.
- Important2: installed Linux models prefix/tessdata did not match runtime
  prefix/bin/tessdata.
- Important3: exclusive gate during whole rebuild could stall UI writes.
- Important4: retained Unavailable entries were never retried after pack repair.
- Minor5 (deferred): MenuHost reports OCR Available before probing native/model
  availability. Actual engine still returns Unsupported and privacy applies.

One fix pass, each defect reproduced RED before fix:
`fts_transaction_race` (clear/rebuild boundary), its busy mode (UI write wait),
`ocr_install_layout.py` (missing installed model), `ocr_queue` (Unavailable).
Fixes: in-memory bounded FTS work, read snapshot released before publication;
main mutation gate through COMMIT/ROLLBACK; nonblocking main acquisition;
WAL main to avoid read snapshot blocking UI commits; known companion backup
and rename under publication gate; Linux install models adjacent to binary;
Unavailable entries retry on restart and at60s bounded retry epochs.
Completed worker output remains queued across transient publication Busy.
Native Win32 static CRT build uses CMP0091 NEW (Leptonica older CMake policy).

## Declined to judge — executor dispositions

1. Final CI/sizes pending: executor must verify completed jobs and evidence
   before final claim; no release status inferred from source review.
2. Positive MSIX recognition: stays OPEN; only package identity rejection
   verified. Cost: actual packaged availability remains unproven.
3. macOS runtime/cancellation: native hosted CI runs fixture recognition;
   desktop cancellation responsiveness OPEN. Cost: OS operation timing unknown.
4. Clean consumer offline closure/signing/installers/support matrix: T17/manual
   OPEN, CI dependency report is not clean-machine proof. Cost: deployment gaps.
5. General quality/detection/correction/beyond EN-RU: only synthetic EN/RU
   evidence, documented Cliptig misrecognition. Cost: real-world accuracy unknown.
6. Maximum-workload latency/memory: structural bounds, no maximum-load benchmark;
   nonblocking gate tested. Cost: peak RSS/throughput not measured.
7. Hostile concurrent replacement of directories/markers/models: static regular/
   symlink/hash checks and cooperating-process locks; no claim of hostile same-
   user filesystem race resistance. Cost: adversarial replacement not proven.
8. External unchanged-revision index loss during running session: startup/revision
   rebuild and query errors exist; automatic repair without revision change
   deferred, no current search UI. Cost: stale/missing companion until restart.
9. Native shutdown hangs: cooperative cancellation and bounded Tesseract30s,
   OS Vision hang not injected. Cost: shutdown timing for OS hang unknown.
10. Prior T01–T15: previous reviews/manual gates preserved. Cost: no fresh full
    manual retest of earlier platform behavior.
11. New search/OCR-copy UI: excluded by approved T16. Cost: no new user search
    interface; this task supplies backend only.

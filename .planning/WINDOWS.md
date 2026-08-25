---
schema_version: 1
open_count: 4
waived_count: 0
fixed_count: 5
total_count: 9
last_updated: 2026-08-25T11:07:14.000Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 01 | unmet-truth | src/main_basestation.cpp |  | G-01-4 runtime half: browser-stays-on-page behavior of the new delegated AJAX submit handler awaits operator confirmation at the 01-09 hardware bench session (code level closed by 01-07, commit 754f834) | fixed | G-01-4 runtime truth confirmed at the 01-09 bench session — operator triggered captures from the dashboard with no raw-JSON navigation | 2026-08-22T23:35:05.295Z | 2026-08-23T01:49:47.144Z |
| 2 | 01 | unmet-truth | src/image_rx_manager.cpp | 199 | G-01-5: thumbnail push-burst residual loss - thumbnail arrives corrupt with Incomplete badge while the full completes (01-09 bench, iteration 5); heal-phase serial discriminator + pacing round pending (01-UAT.md G-01-5) | fixed | FIXED by 01-11 (f265556 + ride-alongs 91bee03/c67e1a5; G-01-6 half by 678d4f1), operator-verified on the bench 2026-08-24 across three post-fix sessions: phantom FAILED 0/0/0 vs ~41/capture baseline (31/0/90 benign 'AUX-low missed after complete write - treated as sent' lines); IO-39 flood ZERO; thumbnails correctly sized all sessions; with capture spacing 4/4 kinds COMPLETE (image 4 thumb 8/8 + full 52/52, image 5 thumb 9/9 + full 53/53, tail 49..52 recovered pass 2); gallery grows across reboots (index 2-3-4-5, IDs 1..5 sequential, IMG_00002_*..IMG_00005_* appended — no overwrite). Residual under unspaced captures is a different mechanism, routed as G-01-7 (entry 3) | 2026-08-23T01:49:29.101Z | 2026-08-23T22:09:23.432Z |
| 3 | 01 | unmet-truth | src/image_tx_manager.cpp |  | G-01-7: concurrent-transfer starvation - a capture triggered while the previous image's transfer/heal is pending evicts/starves it (balloon2.log:656 supersede-evict line; session-2 image-2-thumb 6/8 + image-3-full 15/52 INCOMPLETE vs session-3 spaced 4/4 COMPLETE). Lever: serialize transfers / never evict a pending heal; interim mitigation: space captures (01-UAT.md G-01-7) | open | RESCOPED (bench session #5, 2026-08-25, balloon5.log/base5.log): the session-4 failure modes are ELIMINATED — zero INCOMPLETE verdicts in either log, zero stored-bytes CRC mismatches, zero BUSY deferrals, zero mid-service evictions (balloon5.log:813-814/:1062/:1560/:6440 all idle-targeted + one TTL :4258), all thumbs correctly sized 6-7 chunks, serialization hold fired 4x with heals completing first (base5.log:66/:377/:2382/:3356), beacons held cadence (484 accepted, worst gap 2 consecutive). Series A STILL FAILS on two narrower axes: (a) third unspaced trigger seq=6 lost all 4 transmissions under the concurrent chunk storm and timed out honestly (base5.log:55/:73/:81/:92, :126 'timeout after 3 retries'); (b) image 15's full silently lost to manifest AIR LOSS (balloon5.log:274 sent with TX success / never received — the TX-gated re-announce cannot see it; entry 5 defect C). Next levers: command air-priority/pacing during window service + receipt-driven manifest recovery (01-UAT.md G-01-7) | 2026-08-23T22:09:24.459Z |  |
| 4 | 01 | unmet-truth | src/camera_manager.cpp | 398 | G-01-8: SET_RESOLUTION buffer-realloc defect - value 9 (VGA) floods cam_hal FB-OVF (4379 lines) and fails all captures until reboot; setFrameSize changes sensor framesize only, never reallocates the boot-allocated PSRAM buffers. Fix class: NACK above allocated buffer or re-init camera (01-UAT.md G-01-8) | fixed | FIXED by 01-12 (f51bad6 allocatedFrameSize bound + re-init-with-recovery), operator-verified bench session 4 (2026-08-24): CIF/VGA/SVGA each executed via 'Camera: framesize growth requires re-init' (balloon4.log:1791/:2784/:3588) with ZERO FB-OVF at every settings step (baseline 4,379; the session's only 4 FB-OVF lines are series-A thumbnail events), every capture SUCCESS (IDs 6-11), camera never died; VGA full COMPLETE 97/97 + correctly-sized thumb (base4.log:1581/:1938). Unexercised clauses (QVGA restore, reboot boot-resolution, SC-3 pairs) ride the next bench round under Test 3 | 2026-08-23T22:09:25.640Z | 2026-08-24T13:16:00.000Z |
| 5 | 01 | unmet-truth | src/camera_manager.cpp | 361 | G-01-9 defect A/B/C complex (01-12 bench session 4): full-sized thumbnails passing the QQVGA dimension guard (payload-vs-metadata mismatch, 4 of 6 captures, 'Thumbnail created, size: 7157 bytes' balloon4.log:355); all-chunks-received fulls failing stored-bytes CRC (base4.log:854/:3267 — balloon-source-side corruption, not air); lost FULL manifest silently drops a full (image 8, balloon4.log:722 sent / never received). Named levers: payload-check in the thumb guard, kind-tagged window-chunk logging to discriminate, bounded full-manifest re-announce (01-UAT.md G-01-9) | open | Bench session #5 (2026-08-25, balloon5.log/base5.log): defect A FIXED (01-13 CR-03 drain + payload bound — all 7 logged thumbs genuine QQVGA 1176-1340 B / 6-7 chunks, balloon5.log:198/:242/:699/:1029/:1525/:5014/:6399; drained-stale-frame discriminator at every logged capture :196/:240/:697/:1027/:1523/:5012/:6397); defect B FIXED (01-13 kind-exact routing — zero stored-bytes CRC mismatches including CIF 31-chunk base5.log:776 and SVGA 80/82-chunk :1243/:2770; kind-stamped window chunks live balloon5.log:289). Defect C REMAINS OPEN, precisely bounded: the 01-14 re-announce is TX-verdict-gated and was never exercised (zero TX failures) — image 15's FULL manifest air-lost after successful TX (balloon5.log:274, no receipt at base, no drop log) silently dropped the full. Lever: receipt-driven recovery (base-side nudge on thumb-COMPLETE-without-FULL-manifest, or balloon-side periodic bounded re-announce while head-and-idle) — 01-UAT.md G-01-9 | 2026-08-24T13:16:00.000Z |  |
| 6 | 01 | deviation | src/command_sender.cpp |  | CommandSender retry-bound overrun - 13x 'Retrying command seq=N (attempt 4/3)' in base4.log (e.g. :115, :290, :372, :2330): one retry fires beyond the documented D-05/D-07 3-attempt bound before the terminal guard stops it; late retries re-armed finalized kind-0 windows post-terminal (16x 'chunk for finalized image 11 kind 0 ignored', base4.log:3286-3310) | fixed | FIXED by 01-15 (45c8b80 retry-ordinal label), operator-verified bench session #5 (2026-08-25, base5.log): zero beyond-bound labels — all 52 'Retrying command seq=N (retry K/3)' lines read K<=3; the one exhausted command terminalized honestly at exactly 3 retries (base5.log:126 'Command seq=6 timeout after 3 retries'); post-terminal traffic is now bounded straggler re-service from in-bound retries, correctly ignored by the terminal guard (25 'chunk for finalized ... ignored' lines session-wide, worst cluster 14x image 17 kind 1 at :779-807 after a lost ACK on seq 28 whose retry 1/3 re-served the window; the 01-15 cancel-on-finalize ends it, :778 'Cancelled command seq=28') — benign by design, no overrun-driven re-arm remains | 2026-08-24T13:16:00.000Z | 2026-08-25T13:45:00.000Z |
| 7 | 01 | deviation | .planning/phases/01-command-protocol-control/01-15-SUMMARY.md |  | 01-15 deviation (positive): heal-site defer log 'heal deferred - window request seq ...' added beyond the named artifacts for 01-16 bench discrimination at session-4's failure site; shares the deferSkipLoggedSeq latch, pinned 'stall deferred' grep still == 1 | fixed | RESOLVED at the 01-16 bench (session #5, 2026-08-25, base5.log): the heal-site defer log fired as designed exactly once — :283 'ImageRx: heal deferred - window request seq 12 still in flight for image 15 kind 0' (image 15's thumb then healed to COMPLETE 7/7 at :310); the related 'stall deferred' line fired 16x, all benign, every affected transfer COMPLETE. Discrimination purpose served, no false positives | 2026-08-24T04:31:30.154Z | 2026-08-25T13:45:00.000Z |
| 8 | 01 | unmet-truth | src/command_handler.cpp | 139 | CR-04 blanket camera-ready gate in CommandHandler::executeCommand refuses EVERY command when camera->isReady() is false (isReady is just 'initialized'), so a low-battery camera-down window (main_balloon.cpp:864 enableCamera(false)) blocks IMAGE_WINDOW_REQUEST and GET_STATUS while ImageTx PSRAM buffers and the E32 radio stay operational — announced fulls become unretrievable exactly when an operator must recover them before power loss, and the base status poll fails while the balloon still beacons; found by review round #8 (01-REVIEW.md d546ace), confirmed at source by re-verification #7 | open | Routing only (no flip — bench closure is 01-20): fix scoped to the 8 camera-touching handlers, landing in plan 01-19 this round; flip at the 01-20 bench | 2026-08-25T11:07:14.000Z |  |
| 9 | 01 | unmet-truth | src/image_tx_manager.cpp | 601 | WR-08 push paths advance chunk cursors on failed transmit (pushThumbChunk nextThumbChunk++ and serviceWindowChunk windowNextIndex++ unconditional), and a failed tail chunk can mark an entry SERVED whose final bytes never left the balloon; unexercised at session 5 (zero TX failures); found by review round #8, confirmed at source by re-verification #7 | open | Routing only (no flip — bench closure is 01-20): success-gated advance + SERVED with a same-index bound (IMG_CHUNK_TX_RETRY_MAX), landing in plan 01-17 Task 1 this round; flip at the 01-20 bench | 2026-08-25T11:07:14.000Z |  |

````json
[
  {
    "id": 1,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/main_basestation.cpp",
    "line": null,
    "description": "G-01-4 runtime half: browser-stays-on-page behavior of the new delegated AJAX submit handler awaits operator confirmation at the 01-09 hardware bench session (code level closed by 01-07, commit 754f834)",
    "status": "fixed",
    "reason": "G-01-4 runtime truth confirmed at the 01-09 bench session — operator triggered captures from the dashboard with no raw-JSON navigation",
    "recorded_at": "2026-08-22T23:35:05.295Z",
    "resolved_at": "2026-08-23T01:49:47.144Z"
  },
  {
    "id": 2,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/image_rx_manager.cpp",
    "line": 199,
    "description": "G-01-5: thumbnail push-burst residual loss - thumbnail arrives corrupt with Incomplete badge while the full completes (01-09 bench, iteration 5); heal-phase serial discriminator + pacing round pending (01-UAT.md G-01-5)",
    "status": "fixed",
    "reason": "FIXED by 01-11 (f265556 + ride-alongs 91bee03/c67e1a5; G-01-6 half by 678d4f1), operator-verified on the bench 2026-08-24 across three post-fix sessions: phantom FAILED 0/0/0 vs ~41/capture baseline (31/0/90 benign 'AUX-low missed after complete write - treated as sent' lines); IO-39 flood ZERO; thumbnails correctly sized all sessions; with capture spacing 4/4 kinds COMPLETE (image 4 thumb 8/8 + full 52/52, image 5 thumb 9/9 + full 53/53, tail 49..52 recovered pass 2); gallery grows across reboots (index 2-3-4-5, IDs 1..5 sequential, IMG_00002_*..IMG_00005_* appended — no overwrite). Residual under unspaced captures is a different mechanism, routed as G-01-7 (entry 3)",
    "recorded_at": "2026-08-23T01:49:29.101Z",
    "resolved_at": "2026-08-23T22:09:23.432Z"
  },
  {
    "id": 3,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/image_tx_manager.cpp",
    "line": null,
    "description": "G-01-7: concurrent-transfer starvation - a capture triggered while the previous image's transfer/heal is pending evicts/starves it (balloon2.log:656 supersede-evict line; session-2 image-2-thumb 6/8 + image-3-full 15/52 INCOMPLETE vs session-3 spaced 4/4 COMPLETE). Lever: serialize transfers / never evict a pending heal; interim mitigation: space captures (01-UAT.md G-01-7)",
    "status": "open",
    "reason": "RESCOPED (bench session #5, 2026-08-25, balloon5.log/base5.log): the session-4 failure modes are ELIMINATED — zero INCOMPLETE verdicts in either log, zero stored-bytes CRC mismatches, zero BUSY deferrals, zero mid-service evictions (balloon5.log:813-814/:1062/:1560/:6440 all idle-targeted + one TTL :4258), all thumbs correctly sized 6-7 chunks, serialization hold fired 4x with heals completing first (base5.log:66/:377/:2382/:3356), beacons held cadence (484 accepted, worst gap 2 consecutive). Series A STILL FAILS on two narrower axes: (a) third unspaced trigger seq=6 lost all 4 transmissions under the concurrent chunk storm and timed out honestly (base5.log:55/:73/:81/:92, :126 'timeout after 3 retries'); (b) image 15's full silently lost to manifest AIR LOSS (balloon5.log:274 sent with TX success / never received — the TX-gated re-announce cannot see it; entry 5 defect C). Next levers: command air-priority/pacing during window service + receipt-driven manifest recovery (01-UAT.md G-01-7)",
    "recorded_at": "2026-08-23T22:09:24.459Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/camera_manager.cpp",
    "line": 398,
    "description": "G-01-8: SET_RESOLUTION buffer-realloc defect - value 9 (VGA) floods cam_hal FB-OVF (4379 lines) and fails all captures until reboot; setFrameSize changes sensor framesize only, never reallocates the boot-allocated PSRAM buffers. Fix class: NACK above allocated buffer or re-init camera (01-UAT.md G-01-8)",
    "status": "fixed",
    "reason": "FIXED by 01-12 (f51bad6 allocatedFrameSize bound + re-init-with-recovery), operator-verified bench session 4 (2026-08-24): CIF/VGA/SVGA each executed via 'Camera: framesize growth requires re-init' (balloon4.log:1791/:2784/:3588) with ZERO FB-OVF at every settings step (baseline 4,379; the session's only 4 FB-OVF lines are series-A thumbnail events), every capture SUCCESS (IDs 6-11), camera never died; VGA full COMPLETE 97/97 + correctly-sized thumb (base4.log:1581/:1938). Unexercised clauses (QVGA restore, reboot boot-resolution, SC-3 pairs) ride the next bench round under Test 3",
    "recorded_at": "2026-08-23T22:09:25.640Z",
    "resolved_at": "2026-08-24T13:16:00.000Z"
  },
  {
    "id": 5,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/camera_manager.cpp",
    "line": 361,
    "description": "G-01-9 defect A/B/C complex (01-12 bench session 4): full-sized thumbnails passing the QQVGA dimension guard (payload-vs-metadata mismatch, 4 of 6 captures, 'Thumbnail created, size: 7157 bytes' balloon4.log:355); all-chunks-received fulls failing stored-bytes CRC (base4.log:854/:3267 — balloon-source-side corruption, not air); lost FULL manifest silently drops a full (image 8, balloon4.log:722 sent / never received). Named levers: payload-check in the thumb guard, kind-tagged window-chunk logging to discriminate, bounded full-manifest re-announce (01-UAT.md G-01-9)",
    "status": "open",
    "reason": "Bench session #5 (2026-08-25, balloon5.log/base5.log): defect A FIXED (01-13 CR-03 drain + payload bound — all 7 logged thumbs genuine QQVGA 1176-1340 B / 6-7 chunks, balloon5.log:198/:242/:699/:1029/:1525/:5014/:6399; drained-stale-frame discriminator at every logged capture :196/:240/:697/:1027/:1523/:5012/:6397); defect B FIXED (01-13 kind-exact routing — zero stored-bytes CRC mismatches including CIF 31-chunk base5.log:776 and SVGA 80/82-chunk :1243/:2770; kind-stamped window chunks live balloon5.log:289). Defect C REMAINS OPEN, precisely bounded: the 01-14 re-announce is TX-verdict-gated and was never exercised (zero TX failures) — image 15's FULL manifest air-lost after successful TX (balloon5.log:274, no receipt at base, no drop log) silently dropped the full. Lever: receipt-driven recovery (base-side nudge on thumb-COMPLETE-without-FULL-manifest, or balloon-side periodic bounded re-announce while head-and-idle) — 01-UAT.md G-01-9",
    "recorded_at": "2026-08-24T13:16:00.000Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "01",
    "file": "src/command_sender.cpp",
    "line": null,
    "description": "CommandSender retry-bound overrun - 13x 'Retrying command seq=N (attempt 4/3)' in base4.log (e.g. :115, :290, :372, :2330): one retry fires beyond the documented D-05/D-07 3-attempt bound before the terminal guard stops it; late retries re-armed finalized kind-0 windows post-terminal (16x 'chunk for finalized image 11 kind 0 ignored', base4.log:3286-3310)",
    "status": "fixed",
    "reason": "FIXED by 01-15 (45c8b80 retry-ordinal label), operator-verified bench session #5 (2026-08-25, base5.log): zero beyond-bound labels — all 52 'Retrying command seq=N (retry K/3)' lines read K<=3; the one exhausted command terminalized honestly at exactly 3 retries (base5.log:126 'Command seq=6 timeout after 3 retries'); post-terminal traffic is now bounded straggler re-service from in-bound retries, correctly ignored by the terminal guard (25 'chunk for finalized ... ignored' lines session-wide, worst cluster 14x image 17 kind 1 at :779-807 after a lost ACK on seq 28 whose retry 1/3 re-served the window; the 01-15 cancel-on-finalize ends it, :778 'Cancelled command seq=28') — benign by design, no overrun-driven re-arm remains",
    "recorded_at": "2026-08-24T13:16:00.000Z",
    "resolved_at": "2026-08-25T13:45:00.000Z"
  },
  {
    "id": 7,
    "kind": "deviation",
    "phase": "01",
    "file": ".planning/phases/01-command-protocol-control/01-15-SUMMARY.md",
    "line": null,
    "description": "01-15 deviation (positive): heal-site defer log 'heal deferred - window request seq ...' added beyond the named artifacts for 01-16 bench discrimination at session-4's failure site; shares the deferSkipLoggedSeq latch, pinned 'stall deferred' grep still == 1",
    "status": "fixed",
    "reason": "RESOLVED at the 01-16 bench (session #5, 2026-08-25, base5.log): the heal-site defer log fired as designed exactly once — :283 'ImageRx: heal deferred - window request seq 12 still in flight for image 15 kind 0' (image 15's thumb then healed to COMPLETE 7/7 at :310); the related 'stall deferred' line fired 16x, all benign, every affected transfer COMPLETE. Discrimination purpose served, no false positives",
    "recorded_at": "2026-08-24T04:31:30.154Z",
    "resolved_at": "2026-08-25T13:45:00.000Z"
  },
  {
    "id": 8,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/command_handler.cpp",
    "line": 139,
    "description": "CR-04 blanket camera-ready gate in CommandHandler::executeCommand refuses EVERY command when camera->isReady() is false (isReady is just 'initialized'), so a low-battery camera-down window (main_balloon.cpp:864 enableCamera(false)) blocks IMAGE_WINDOW_REQUEST and GET_STATUS while ImageTx PSRAM buffers and the E32 radio stay operational — announced fulls become unretrievable exactly when an operator must recover them before power loss, and the base status poll fails while the balloon still beacons; found by review round #8 (01-REVIEW.md d546ace), confirmed at source by re-verification #7",
    "status": "open",
    "reason": "Routing only (no flip — bench closure is 01-20): fix scoped to the 8 camera-touching handlers, landing in plan 01-19 this round; flip at the 01-20 bench",
    "recorded_at": "2026-08-25T11:07:14.000Z",
    "resolved_at": null
  },
  {
    "id": 9,
    "kind": "unmet-truth",
    "phase": "01",
    "file": "src/image_tx_manager.cpp",
    "line": 601,
    "description": "WR-08 push paths advance chunk cursors on failed transmit (pushThumbChunk nextThumbChunk++ and serviceWindowChunk windowNextIndex++ unconditional), and a failed tail chunk can mark an entry SERVED whose final bytes never left the balloon; unexercised at session 5 (zero TX failures); found by review round #8, confirmed at source by re-verification #7",
    "status": "open",
    "reason": "Routing only (no flip — bench closure is 01-20): success-gated advance + SERVED with a same-index bound (IMG_CHUNK_TX_RETRY_MAX), landing in plan 01-17 Task 1 this round; flip at the 01-20 bench",
    "recorded_at": "2026-08-25T11:07:14.000Z",
    "resolved_at": null
  }
]
````

---
schema_version: 1
open_count: 3
waived_count: 0
fixed_count: 3
total_count: 6
last_updated: 2026-08-24T13:16:00.000Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 01 | unmet-truth | src/main_basestation.cpp |  | G-01-4 runtime half: browser-stays-on-page behavior of the new delegated AJAX submit handler awaits operator confirmation at the 01-09 hardware bench session (code level closed by 01-07, commit 754f834) | fixed | G-01-4 runtime truth confirmed at the 01-09 bench session — operator triggered captures from the dashboard with no raw-JSON navigation | 2026-08-22T23:35:05.295Z | 2026-08-23T01:49:47.144Z |
| 2 | 01 | unmet-truth | src/image_rx_manager.cpp | 199 | G-01-5: thumbnail push-burst residual loss - thumbnail arrives corrupt with Incomplete badge while the full completes (01-09 bench, iteration 5); heal-phase serial discriminator + pacing round pending (01-UAT.md G-01-5) | fixed | FIXED by 01-11 (f265556 + ride-alongs 91bee03/c67e1a5; G-01-6 half by 678d4f1), operator-verified on the bench 2026-08-24 across three post-fix sessions: phantom FAILED 0/0/0 vs ~41/capture baseline (31/0/90 benign 'AUX-low missed after complete write - treated as sent' lines); IO-39 flood ZERO; thumbnails correctly sized all sessions; with capture spacing 4/4 kinds COMPLETE (image 4 thumb 8/8 + full 52/52, image 5 thumb 9/9 + full 53/53, tail 49..52 recovered pass 2); gallery grows across reboots (index 2-3-4-5, IDs 1..5 sequential, IMG_00002_*..IMG_00005_* appended — no overwrite). Residual under unspaced captures is a different mechanism, routed as G-01-7 (entry 3) | 2026-08-23T01:49:29.101Z | 2026-08-23T22:09:23.432Z |
| 3 | 01 | unmet-truth | src/image_tx_manager.cpp |  | G-01-7: concurrent-transfer starvation - a capture triggered while the previous image's transfer/heal is pending evicts/starves it (balloon2.log:656 supersede-evict line; session-2 image-2-thumb 6/8 + image-3-full 15/52 INCOMPLETE vs session-3 spaced 4/4 COMPLETE). Lever: serialize transfers / never evict a pending heal; interim mitigation: space captures (01-UAT.md G-01-7) | open | 01-12 levers (1064480) are IN and functioning on the bench (session 4: hold fired 3x base4.log:521/:1127/:2336; zero mid-service evictions — all 5 supersede-evictions targeted non-mid-service entries; heals serialize ahead of fulls) but the series-A acceptance FAILED: image 7 thumb INCOMPLETE 35/36 (base4.log:634) with its heal window requests NACK-deferred by the one-at-a-time BUSY guard while image 8's window was mid-service (balloon4.log:1154/:1157) until the D-24 passes exhausted; image 7 full 36/36-received but stored-bytes CRC mismatch (base4.log:854); image 8 full silently never transferred (FULL manifest sent balloon4.log:722, never received). RESCOPED residual: defer-aware pass accounting + full-manifest re-announce + the G-01-9 thumb-sizing fix (entry 5) — the gap stays open until unspaced captures complete without INCOMPLETE verdicts (01-UAT.md G-01-7) | 2026-08-23T22:09:24.459Z |  |
| 4 | 01 | unmet-truth | src/camera_manager.cpp | 398 | G-01-8: SET_RESOLUTION buffer-realloc defect - value 9 (VGA) floods cam_hal FB-OVF (4379 lines) and fails all captures until reboot; setFrameSize changes sensor framesize only, never reallocates the boot-allocated PSRAM buffers. Fix class: NACK above allocated buffer or re-init camera (01-UAT.md G-01-8) | fixed | FIXED by 01-12 (f51bad6 allocatedFrameSize bound + re-init-with-recovery), operator-verified bench session 4 (2026-08-24): CIF/VGA/SVGA each executed via 'Camera: framesize growth requires re-init' (balloon4.log:1791/:2784/:3588) with ZERO FB-OVF at every settings step (baseline 4,379; the session's only 4 FB-OVF lines are series-A thumbnail events), every capture SUCCESS (IDs 6-11), camera never died; VGA full COMPLETE 97/97 + correctly-sized thumb (base4.log:1581/:1938). Unexercised clauses (QVGA restore, reboot boot-resolution, SC-3 pairs) ride the next bench round under Test 3 | 2026-08-23T22:09:25.640Z | 2026-08-24T13:16:00.000Z |
| 5 | 01 | unmet-truth | src/camera_manager.cpp | 361 | G-01-9 defect A/B/C complex (01-12 bench session 4): full-sized thumbnails passing the QQVGA dimension guard (payload-vs-metadata mismatch, 4 of 6 captures, 'Thumbnail created, size: 7157 bytes' balloon4.log:355); all-chunks-received fulls failing stored-bytes CRC (base4.log:854/:3267 — balloon-source-side corruption, not air); lost FULL manifest silently drops a full (image 8, balloon4.log:722 sent / never received). Named levers: payload-check in the thumb guard, kind-tagged window-chunk logging to discriminate, bounded full-manifest re-announce (01-UAT.md G-01-9) | open |  | 2026-08-24T13:16:00.000Z |  |
| 6 | 01 | deviation | src/command_sender.cpp |  | CommandSender retry-bound overrun - 13x 'Retrying command seq=N (attempt 4/3)' in base4.log (e.g. :115, :290, :372, :2330): one retry fires beyond the documented D-05/D-07 3-attempt bound before the terminal guard stops it; late retries re-armed finalized kind-0 windows post-terminal (16x 'chunk for finalized image 11 kind 0 ignored', base4.log:3286-3310) | open |  | 2026-08-24T13:16:00.000Z |  |

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
    "reason": "01-12 levers (1064480) are IN and functioning on the bench (session 4: hold fired 3x base4.log:521/:1127/:2336; zero mid-service evictions — all 5 supersede-evictions targeted non-mid-service entries; heals serialize ahead of fulls) but the series-A acceptance FAILED: image 7 thumb INCOMPLETE 35/36 (base4.log:634) with its heal window requests NACK-deferred by the one-at-a-time BUSY guard while image 8's window was mid-service (balloon4.log:1154/:1157) until the D-24 passes exhausted; image 7 full 36/36-received but stored-bytes CRC mismatch (base4.log:854); image 8 full silently never transferred (FULL manifest sent balloon4.log:722, never received). RESCOPED residual: defer-aware pass accounting + full-manifest re-announce + the G-01-9 thumb-sizing fix (entry 5) — the gap stays open until unspaced captures complete without INCOMPLETE verdicts (01-UAT.md G-01-7)",
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
    "reason": "",
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
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-24T13:16:00.000Z",
    "resolved_at": null
  }
]
````

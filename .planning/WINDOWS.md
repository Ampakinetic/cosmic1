---
schema_version: 1
open_count: 2
waived_count: 0
fixed_count: 2
total_count: 4
last_updated: 2026-08-23T22:09:25.640Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 01 | unmet-truth | src/main_basestation.cpp |  | G-01-4 runtime half: browser-stays-on-page behavior of the new delegated AJAX submit handler awaits operator confirmation at the 01-09 hardware bench session (code level closed by 01-07, commit 754f834) | fixed | G-01-4 runtime truth confirmed at the 01-09 bench session — operator triggered captures from the dashboard with no raw-JSON navigation | 2026-08-22T23:35:05.295Z | 2026-08-23T01:49:47.144Z |
| 2 | 01 | unmet-truth | src/image_rx_manager.cpp | 199 | G-01-5: thumbnail push-burst residual loss - thumbnail arrives corrupt with Incomplete badge while the full completes (01-09 bench, iteration 5); heal-phase serial discriminator + pacing round pending (01-UAT.md G-01-5) | fixed | FIXED by 01-11 (f265556 + ride-alongs 91bee03/c67e1a5; G-01-6 half by 678d4f1), operator-verified on the bench 2026-08-24 across three post-fix sessions: phantom FAILED 0/0/0 vs ~41/capture baseline (31/0/90 benign 'AUX-low missed after complete write - treated as sent' lines); IO-39 flood ZERO; thumbnails correctly sized all sessions; with capture spacing 4/4 kinds COMPLETE (image 4 thumb 8/8 + full 52/52, image 5 thumb 9/9 + full 53/53, tail 49..52 recovered pass 2); gallery grows across reboots (index 2-3-4-5, IDs 1..5 sequential, IMG_00002_*..IMG_00005_* appended — no overwrite). Residual under unspaced captures is a different mechanism, routed as G-01-7 (entry 3) | 2026-08-23T01:49:29.101Z | 2026-08-23T22:09:23.432Z |
| 3 | 01 | unmet-truth | src/image_tx_manager.cpp |  | G-01-7: concurrent-transfer starvation - a capture triggered while the previous image's transfer/heal is pending evicts/starves it (balloon2.log:656 supersede-evict line; session-2 image-2-thumb 6/8 + image-3-full 15/52 INCOMPLETE vs session-3 spaced 4/4 COMPLETE). Lever: serialize transfers / never evict a pending heal; interim mitigation: space captures (01-UAT.md G-01-7) | open |  | 2026-08-23T22:09:24.459Z |  |
| 4 | 01 | unmet-truth | src/camera_manager.cpp | 398 | G-01-8: SET_RESOLUTION buffer-realloc defect - value 9 (VGA) floods cam_hal FB-OVF (4379 lines) and fails all captures until reboot; setFrameSize changes sensor framesize only, never reallocates the boot-allocated PSRAM buffers. Fix class: NACK above allocated buffer or re-init camera (01-UAT.md G-01-8) | open |  | 2026-08-23T22:09:25.640Z |  |

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
    "reason": "",
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
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-23T22:09:25.640Z",
    "resolved_at": null
  }
]
````

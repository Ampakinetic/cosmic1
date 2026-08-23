---
schema_version: 1
open_count: 1
waived_count: 0
fixed_count: 1
total_count: 2
last_updated: 2026-08-23T21:30:00Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 01 | unmet-truth | src/main_basestation.cpp |  | G-01-4 runtime half: browser-stays-on-page behavior of the new delegated AJAX submit handler awaits operator confirmation at the 01-09 hardware bench session (code level closed by 01-07, commit 754f834) | fixed | G-01-4 runtime truth confirmed at the 01-09 bench session — operator triggered captures from the dashboard with no raw-JSON navigation | 2026-08-22T23:35:05.295Z | 2026-08-23T01:49:47.144Z |
| 2 | 01 | unmet-truth | src/image_rx_manager.cpp | 199 | G-01-5: thumbnail push-burst residual loss - thumbnail arrives corrupt with Incomplete badge while the full completes (01-09 bench, iteration 5); heal-phase serial discriminator + pacing round pending (01-UAT.md G-01-5) | open | 01-10 discriminator round complete: mechanism named — E32 AUX handshake phantom transmit-failure (balloon TX accounting defect, e32_lora.cpp:223 write-then-:227-wait; v3: 37 sent/41 FAILED booked while base received 72/72 chunks, 4/4 kinds COMPLETE), NOT air loss (real loss 1-2 END-MARKER-MISS chunks/16, self-healed; CRC FAIL 0); B5 close-range RX-overload NOT reproduced — downgraded; spurious cmd=30 window requests (4-5/capture) driven by the false loss signal; reliability series at separation deferred to 01-11 Task 3 (operator-approved). Remediation + bench re-verification in 01-11 | 2026-08-23T01:49:29.101Z |  |

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
    "status": "open",
    "reason": "01-10 discriminator round complete: mechanism named — E32 AUX handshake phantom transmit-failure (balloon TX accounting defect, e32_lora.cpp:223 write-then-:227-wait; v3: 37 sent/41 FAILED booked while base received 72/72 chunks, 4/4 kinds COMPLETE), NOT air loss (real loss 1-2 END-MARKER-MISS chunks/16, self-healed; CRC FAIL 0); B5 close-range RX-overload NOT reproduced — downgraded; spurious cmd=30 window requests (4-5/capture) driven by the false loss signal; reliability series at separation deferred to 01-11 Task 3 (operator-approved). Remediation + bench re-verification in 01-11",
    "recorded_at": "2026-08-23T01:49:29.101Z",
    "resolved_at": null
  }
]
````

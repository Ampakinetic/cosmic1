---
status: complete
phase: 01-command-protocol-control
source: [01-VERIFICATION.md]
started: 2026-08-18T11:14:05Z
updated: 2026-08-22T22:39:32Z
---

## Current Test
[testing complete]

## Tests

### 1. End-to-end command round trip over real radios
expected: ACK within 2 s; queue row updates to terminal state; counters advance
result: pass
note: re-test after debug fix 1223f46 (balloon boot abort — three stacked causes); original failure logged in Gaps as G-01-1

### 2. Retry/TIMEOUT on degraded link including the duplicate-ACK edge
expected: With the link degraded (range/antenna removed), a retried command shows
retry progress and reaches TIMEOUT/FAILED per the locked vocabulary; the pending
count returns to 0 and stays there — including after a late duplicate ACK arrives
for a command already ACKED by an earlier retry (the 01-06 terminal-state guard is
what makes this safe; prior verifier noted this test is only meaningful now)
result: pass

### 3. Camera settings on the physical sensor
expected: Adjusting camera settings from the web UI produces visible changes in
captured images (brightness/contrast/saturation/exposure/WB/quality/frame size);
the "CIF 400x296" option succeeds (post-01-06 relabel — previously always NACK'd)
result: issue
reported: "There are other problems with the camera, the Storage section says 'Unavailable' but there is a 4Gb FAT32 formatted SD in the base station module. All the picture transmissions are failing with timeouts."
severity: blocker

### 4. Auto-capture cadence + disable
expected: AUTO_CAPTURE_ENABLE captures at exactly the commanded interval (try a
value above 30 s to prove no legacy interleave); after an ACKed AUTO_CAPTURE_DISABLE
zero automatic captures occur; image IDs form one sequence across manual + interval
captures (GET_STATUS lastImageId truthful)
result: issue
reported: "When I press the Auto Capture Enable, or the 'Trigger Camera Capture' button, I end up looking at the JSON result string and have to go back to see the admin page."
severity: major

### 5. Prohibition review — no fabricated state on the wire or UI
expected: Judgment-tier prohibition from plan 01-06: the operator is never shown
protocol or camera state that does not reflect reality (no ACK for unexecuted
actions, no green link LED while failed, GET_STATUS fields match actual balloon
state)
result: pass

## Summary

total: 5
passed: 3
issues: 2
pending: 0
skipped: 0
blocked: 0

## Gaps

- gap_id: G-01-1
  truth: "ACK within 2 s; queue row updates to terminal state; counters advance"
  status: resolved
  reason: "User reported: There are currently problems with the balloon node not sending data nor using it's OLED screen"
  severity: blocker
  test: 1
  resolved_by: "1223f46 (debug session balloon-no-data-oled-blank — three stacked causes)"
  resolved_at: 2026-08-22
  verified_by: "UAT re-test pass on hardware"
  artifacts: []
  missing: []

- gap_id: G-01-3
  truth: "Adjusting camera settings from the web UI produces visible changes in captured images; CIF 400x296 option succeeds; base Storage section reflects the mounted 4GB FAT32 SD; picture transmissions complete without timing out"
  status: failed
  reason: "User reported: There are other problems with the camera, the Storage section says 'Unavailable' but there is a 4Gb FAT32 formatted SD in the base station module. All the picture transmissions are failing with timeouts."
  severity: blocker
  test: 3
  artifacts: []  # Filled by diagnosis
  missing: []    # Filled by diagnosis

- gap_id: G-01-4
  truth: "Pressing Auto Capture Enable or Trigger Camera Capture submits in-page (AJAX) and the operator stays on the admin page with the result reflected in the queue/status UI"
  status: failed
  reason: "User reported: When I press the Auto Capture Enable, or the 'Trigger Camera Capture' button, I end up looking at the JSON result string and have to go back to see the admin page."
  severity: major
  test: 4
  artifacts: []  # Filled by diagnosis
  missing: []    # Filled by diagnosis

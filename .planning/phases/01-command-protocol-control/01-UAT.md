---
status: partial
phase: 01-command-protocol-control
source: [01-VERIFICATION.md]
started: 2026-08-18T11:14:05Z
updated: 2026-08-22T10:42:00Z
---

## Current Test

[testing paused — 4 items outstanding; balloon link fault under investigation]

## Tests

### 1. End-to-end command round trip over real radios
expected: ACK within 2 s; queue row updates to terminal state; counters advance
result: issue
reported: "There are currently problems with the balloon node not sending data nor using it's OLED screen"
severity: blocker

### 2. Retry/TIMEOUT on degraded link including the duplicate-ACK edge
expected: With the link degraded (range/antenna removed), a retried command shows
retry progress and reaches TIMEOUT/FAILED per the locked vocabulary; the pending
count returns to 0 and stays there — including after a late duplicate ACK arrives
for a command already ACKED by an earlier retry (the 01-06 terminal-state guard is
what makes this safe; prior verifier noted this test is only meaningful now)
result: [pending]

### 3. Camera settings on the physical sensor
expected: Adjusting camera settings from the web UI produces visible changes in
captured images (brightness/contrast/saturation/exposure/WB/quality/frame size);
the "CIF 400x296" option succeeds (post-01-06 relabel — previously always NACK'd)
result: [pending]

### 4. Auto-capture cadence + disable
expected: AUTO_CAPTURE_ENABLE captures at exactly the commanded interval (try a
value above 30 s to prove no legacy interleave); after an ACKed AUTO_CAPTURE_DISABLE
zero automatic captures occur; image IDs form one sequence across manual + interval
captures (GET_STATUS lastImageId truthful)
result: [pending]

### 5. Prohibition review — no fabricated state on the wire or UI
expected: Judgment-tier prohibition from plan 01-06: the operator is never shown
protocol or camera state that does not reflect reality (no ACK for unexecuted
actions, no green link LED while failed, GET_STATUS fields match actual balloon
state)
result: [pending]

## Summary

total: 5
passed: 0
issues: 1
pending: 4
skipped: 0
blocked: 0

## Gaps

- gap_id: G-01-1
  truth: "ACK within 2 s; queue row updates to terminal state; counters advance"
  status: failed
  reason: "User reported: There are currently problems with the balloon node not sending data nor using it's OLED screen"
  severity: blocker
  test: 1
  artifacts: []  # Filled by diagnosis
  missing: []    # Filled by diagnosis

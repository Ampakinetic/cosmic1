---
status: partial
phase: 01-command-protocol-control
source: [01-VERIFICATION.md]
started: 2026-08-18T11:14:05Z
updated: 2026-08-19T00:00:00Z
---

## Current Test

[testing paused — 5 items outstanding]

## Tests

### 1. End-to-end command round trip over real radios
expected: ACK within 2 s; queue row updates to terminal state; counters advance
result: blocked
blocked_by: physical-device
reason: "I don't have the hardware back until tomorrow."

### 2. Retry/TIMEOUT on degraded link including the duplicate-ACK edge
expected: With the link degraded (range/antenna removed), a retried command shows
retry progress and reaches TIMEOUT/FAILED per the locked vocabulary; the pending
count returns to 0 and stays there — including after a late duplicate ACK arrives
for a command already ACKED by an earlier retry (the 01-06 terminal-state guard is
what makes this safe; prior verifier noted this test is only meaningful now)
result: blocked
blocked_by: physical-device
reason: "I don't have the hardware back until tomorrow."

### 3. Camera settings on the physical sensor
expected: Adjusting camera settings from the web UI produces visible changes in
captured images (brightness/contrast/saturation/exposure/WB/quality/frame size);
the "CIF 400x296" option succeeds (post-01-06 relabel — previously always NACK'd)
result: blocked
blocked_by: physical-device
reason: "I don't have the hardware back until tomorrow."

### 4. Auto-capture cadence + disable
expected: AUTO_CAPTURE_ENABLE captures at exactly the commanded interval (try a
value above 30 s to prove no legacy interleave); after an ACKed AUTO_CAPTURE_DISABLE
zero automatic captures occur; image IDs form one sequence across manual + interval
captures (GET_STATUS lastImageId truthful)
result: blocked
blocked_by: physical-device
reason: "I don't have the hardware back until tomorrow."

### 5. Prohibition review — no fabricated state on the wire or UI
expected: Judgment-tier prohibition from plan 01-06: the operator is never shown
protocol or camera state that does not reflect reality (no ACK for unexecuted
actions, no green link LED while failed, GET_STATUS fields match actual balloon
state)
result: blocked
blocked_by: physical-device
reason: "I don't have the hardware back until tomorrow."

## Summary

total: 5
passed: 0
issues: 0
pending: 0
skipped: 0
blocked: 5

## Gaps

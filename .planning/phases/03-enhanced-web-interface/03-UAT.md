---
status: testing
phase: 03-enhanced-web-interface
source: [03-VERIFICATION.md]
started: 2026-08-20T06:45:00Z
updated: 2026-08-22T00:00:00Z
---

## Current Test

number: 2
name: OpenStreetMap displays balloon position with trajectory history (SC-2 / WEB-02)
expected: |
  Banded track (green/amber/red by altitude) plus circleMarker position; auto-follow cancels on drag and Recenter restores it; AP mode swaps to the offline canvas plot of the identical track with the offline chip, swapping back when tiles return.
awaiting: user response (holding — balloon link down, see Gap G-03-1; tests 2/4/6 need balloon telemetry)

## Tests

### 1. Dashboard shows live temperature, altitude, GPS updating every 5 s (SC-1 / WEB-01 / WEB-03)
expected: Tiles update on each ~5 s poll from the 0x14 beacon; absent fields show em-dash/'No telemetry received yet'; after failures the amber stale badge appears with a live age count and the cadence backs off 5->15->30 s, snapping back to 5 s silently on recovery.
result: issue
reported: "the base station seems to be working well, however no data is coming from the balloon. is it possible to show some state information on the OLED displays so it's easy to see if something is wrong?"
severity: major

### 2. OpenStreetMap displays balloon position with trajectory history (SC-2 / WEB-02)
expected: Banded track (green/amber/red by altitude) plus circleMarker position; auto-follow cancels on drag and Recenter restores it; AP mode swaps to the offline canvas plot of the identical track with the offline chip, swapping back when tiles return.
result: [pending]

### 3. Top-down UI layout with map/telemetry prominently at top (SC-3 / WEB-04)
expected: Alerts bar (when present) first content section, then telemetry panel + map, then capture/settings cards, then queue/transfers, then gallery; sticky section nav; no visual regression of Phase 1 cards.
result: [pending]

### 4. Image gallery shows all captured images with pagination controls (SC-4 / IMG-06)
expected: 12-per-page newest-first grid, pager hidden on one page, amber Incomplete badges retained, inline detail with sidecar fields only, no id duplicated/dropped at page seams.
result: [pending]

### 5. Base station successfully runs in both AP and Station WiFi modes (SC-5 / WEB-05)
expected: STA join keeps the current interface serving until WL_CONNECTED; success converges single-mode STA; bogus join falls back to AP within 20 s with the locked error copy; reboot boots into the persisted mode; password never appears anywhere.
result: [pending]

### 6. All 6 alert types trigger with visual and/or audio notifications (SC-6 / ALRT-01..06)
expected: Warnings auto-clear on resolve with cooldown; criticals latch until Acknowledge; new critical beeps once after first gesture; muting never hides banners; thresholds persist across reboot and edit from the card.
result: [pending]

## Summary

total: 6
passed: 0
issues: 1
pending: 5
skipped: 0
blocked: 0

## Gaps

- gap_id: G-03-1
  truth: "Tiles update on each ~5 s poll from the 0x14 beacon; absent fields show em-dash/'No telemetry received yet'; stale badge + 5->15->30 s backoff"
  status: failed
  reason: "User reported: the base station seems to be working well, however no data is coming from the balloon."
  severity: major
  test: 1
  artifacts: []
  missing: []

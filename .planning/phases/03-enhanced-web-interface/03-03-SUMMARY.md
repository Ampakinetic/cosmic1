---
phase: 03-enhanced-web-interface
plan: "03"
subsystem: base-web-alerts
tags: [alerts, web-ui, nvs, telemetry]
requires:
  - "03-02 (single-page dashboard, /api/state combined serializer, trajectory ring)"
provides:
  - "AlertEngine singleton (src/alert_engine.h/.cpp): six base-side alert conditions with D-44 latch/auto-clear lifecycles and NVS-persisted WR-07-validated thresholds"
  - "POST /alerts + POST /alerts/ack routes; alerts[] + thresholds{} fields in /api/state"
  - "Dashboard alert bar (dynamic first section), critical beep with mute, Alert Thresholds card"
affects:
  - src/main_basestation.cpp
  - platformio.ini
tech-stack:
  added: []
  patterns:
    - "manager singleton with begin()/process() driven by the base loop millis idiom (mirrors trajectory_buffer)"
    - "in-module WR-07 revalidation of NVS values on load AND write (T-03-08 defense-in-depth)"
    - "oldest-first shifting sample ring for the 60 s beacon-loss window"
    - "DOM section created/removed by the poll renderer; signature-gated re-render; textContent-only copy"
key-files:
  created:
    - src/alert_engine.h
    - src/alert_engine.cpp
  modified:
    - src/main_basestation.cpp
    - platformio.ini
decisions:
  - "ALRT-06 loss computed as missed = expected - received over the sliding 60 s window (arrivals seq-gated): with the locked 5 s beacon cadence this equals the interior seq-gap count but also covers window-edge loss and total outage, which a pure interior-gap scan cannot see"
  - "Critical latch fires on the condition rising edge: acknowledging while the condition persists keeps the row hidden until it clears and re-fires (matches the plan's re-latch truth verbatim)"
  - "Latched rows live-update v1/v2 every 1 s tick ('live values' per must-have): a latched GPS_LOST banner shows the current no-fix age even after recovery, until acknowledged"
  - "Landing flat-rate timer resets when beacon data goes stale (> 3x cadence): stability is a claim about live beacons, not remembered ones"
  - "BATTERY latched rows carry v1 = -1 sentinel while the balloon reports no valid voltage — the browser renders the locked 'not reported' copy from it"
  - "Beacon-loss evaluation requires an established window (>= 60 s since first beacon) or >= 2 arrivals spanning >= 2 completed intervals; younger windows return no statement"
metrics:
  duration: 24m 11s
  completed: 2026-08-19T16:50:50Z
  tasks: 2
actuals:
  tokens: 13500
  tasks: 2
  commits: 2
status: complete
requirements-completed: [ALRT-01, ALRT-02, ALRT-03, ALRT-04, ALRT-05, ALRT-06]
---

# Phase 03 Plan 03: Base-Side Alert System Summary

**One-liner:** Six base-side alert conditions (altitude, battery, GPS-lost, landing, ascent/descent rate, beacon-loss signal quality) with D-44 latch/auto-clear lifecycles, NVS-persisted WR-07-validated thresholds, and a dynamic dashboard alert bar with critical beep and acknowledge.

## Overview

Plan 03-03 delivered the alert layer of the enhanced web interface. Everything evaluates base-side from data the base already has (D-41): the 0x14 telemetry snapshot supplies altitude/GPS/battery, consecutive-beacon altitude deltas supply the ascent rate, and seq-gated beacon arrivals supply the ALRT-06 loss percentage. No balloon firmware change, no extra LoRa airtime, and the alert_engine module never reads the radio driver. Thresholds persist base-side in NVS under namespace "alerts" (D-43) and evaluation is on from `begin()` — there is no opt-in flag anywhere.

## What Was Built

### Task 1 — AlertEngine module + routes (commit 88b54ae)

- `src/alert_engine.h` / `src/alert_engine.cpp`: `AlertType` enum in the locked order (ALTITUDE, BATTERY, GPS_LOST, LANDING, RATE, SIGNAL), `AlertThresholds` with the seven project-anchored defaults (1000 m / 3.3 V / 30 s / 15 m/s / 20 % / 1.0 m/s / 60 s), `AlertRow`, and the `Alerts()` singleton with `begin()/process()/setThresholds()/getThresholds()/ack()/getAlertSnapshot()`.
- Condition precision per must-have: ALTITUDE compares integer centimeters (`lroundf(altitudeM*100) > altWarnM*100`, strictly above); RATE compares the unrounded float from raw cm-delta/elapsed-ms between beacons (strictly above); BATTERY compares integer millivolts and is validity-gated (`batteryValid`); GPS_LOST latches on sustained age measured from the last beacon that carried a valid fix (never having had a fix never fires); LANDING requires > 50 m gain over the first-valid-fix launch baseline plus `|rate| < landingRateMps` continuously for `landingStableS`; SIGNAL is the 60 s sliding-window loss percent, strictly above the level.
- D-44 lifecycles: BATTERY/GPS_LOST/LANDING latch on the condition rising edge and persist until `ack()`; re-latch requires the condition to clear and re-fire. ALTITUDE/RATE/SIGNAL auto-clear on resolve with a 30 s re-arm cooldown.
- Freshness gate (Pitfall 9): altitude/rate/landing never evaluate a snapshot older than 3x the 5 s cadence; the landing flat-rate timer also resets on stale data.
- NVS (D-43): `begin()` loads every persisted key over the defaults with in-module WR-07 range revalidation (T-03-08); `setThresholds()` revalidates before persisting. A bypassed or corrupted writer can never reach evaluation.
- `src/main_basestation.cpp`: `POST /alerts` (seven args, full-value WR-07 range checks before narrowing, locked 400/500 copy) and `POST /alerts/ack` (numeric type 0-5 only); `/api/state` gains `alerts:[{type,sev,latched,v1,v2}]` newest-first plus `thresholds:{...}`; `Alerts().begin()` in setup, 1 s `process()` tick on the loop millis idiom.
- `platformio.ini`: balloon env excludes `alert_engine.cpp` (base-only).

### Task 2 — Dashboard alert bar, beep, thresholds card (commit 92c8c47)

- Alert bar is the FIRST content section, created/removed by the poll renderer — zero alerts means no bar in the DOM at all (no all-clear placeholder). Banners stack newest-first, severity-colored per the UI-SPEC tokens (critical #ef4444/#7f1d1d/#f87171, warning #eab308/#713f12/#fbbf24), 4 px left border, 8 px radius, 16 px padding, flex-wrap at 14 px/1.5, all copy via textContent only.
- Latched critical rows carry an Acknowledge button posting `/alerts/ack`; the banner collapses server-truthfully on the next poll (immediate `pollOnce()`).
- Locked copy for all six banners, including the Ascent/Descent variants selected by the sign of the live rate and the "Battery voltage not reported by the balloon" detail for invalid battery data.
- Beep: Web Audio oscillator ~1000 Hz with a 150 ms envelope, fired only on the none-to-critical transition per type (never per poll, never for warnings). Audio arms on the first gesture anywhere (`ctx.resume()`); until armed the "Audio muted until you interact with the page." hint shows. Mute beeps/Unmute toggle persisted in sessionStorage; muting never hides banners.
- Alert Thresholds card at the end of the capture section: seven labeled inputs (steps per spec: none/0.1/5/none/5/0.5/30) prefilled from `/api/state` `thresholds{}` — signature-gated so in-progress editing is never clobbered by a background poll — disabled while the save POST is in flight, with the Save Alert Thresholds button.
- "Alerts" nav link first in section-nav; the nav current-section lookup was made dynamic (per-call `getElementById`) since the alerts section exists only at runtime.

## Verification

- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS (Task 1).
- `pio run -e esp32-s3-basestation` — SUCCESS (Task 2).
- Task 1 greps: AlertType, Preferences, `"/alerts"`, /alerts/ack, alert_engine.cpp in platformio.ini, thresholds, no "rssi" in the header — all PASS.
- Task 2 greps: alert-banner, Acknowledge, Save Alert Thresholds, oscillator, sessionStorage, "beacons missed", no `>rssi<` — all PASS.
- `node scripts/verify_protocol_roundtrip.mjs` — all wire-format regression checks PASS, exit 0.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Beep oscillator local failed the plan's verify grep**
- **Found during:** Task 2 verification
- **Issue:** the plan's verify pattern `grep -Eq "OscillatorNode|oscillator"` does not match `createOscillator` (capital O after "create"), so the first grep run failed.
- **Fix:** renamed the beep function's local from `osc` to `oscillator`; the pattern matches and the code is unchanged in behavior.
- **Files modified:** src/main_basestation.cpp
- **Commit:** 92c8c47

Otherwise the plan executed exactly as written. Design interpretations the plan left open are recorded in the decisions section above (ALRT-06 edge-aware loss accounting, rising-edge latch semantics, live values on latched rows) — none contradict a must-have truth.

## Auth Gates

None.

## Known Stubs

None — no placeholder data paths exist; every banner value comes from the server-computed snapshot.

## Self-Check: PASSED

- src/alert_engine.h, src/alert_engine.cpp exist on disk.
- Commits 88b54ae and 92c8c47 exist on main.
- All plan verify commands ran with passing results.

---
phase: 02-image-transmission
plan: "04"
subsystem: image-transfer
tags: [lora, auto-capture, event-triggers, gps-deltas, flight-phase, capture-source, settings-ui, ctrl-05]

# Dependency graph
requires:
  - phase: 02-image-transmission (02-01)
    provides: CaptureSource enum (EVENT_ALTITUDE/EVENT_DISTANCE/EVENT_PHASE), PayloadSetEventThresholds 7-byte layout, ResponseStatusData event fields, setLastCaptureSource on CameraManager
  - phase: 02-image-transmission (02-02)
    provides: the extended AutoCapture module (single capture/image-ID authority, CR-05), Camera().getLastCaptureSource() consumption at ImageTx enqueue
  - phase: 01 (01-03/01-04)
    provides: WR-07 long-first settings-form pattern, tracked-command queue panel (D-16), AutoCapture millis idioms (T-01-08/09)
provides:
  - Event-trigger engine inside AutoCapture (D-25/D-27/D-28): altitude delta, distance delta (TinyGPSPlus::distanceBetween), flight-phase first-entry (LAUNCH/APEX/PARACHUTE_DESCENT/LANDING) with once-per-boot phaseSeenMask — all behind the SC-5 master enable
  - fire() single gated capture path: D-28 min spacing wraps interval AND event branches, source stamped via setLastCaptureSource before captureImage (D-30), shared allocateImageId sequence, T-01-09 baseline-before-attempt
  - setEventConfig/getEventConfig API with in-module bounds re-validation (10..5000 m / 10..50000 m / 5..3600 s) and flight defaults 150/500/20
  - SET_EVENT_THRESHOLDS command handling (7-byte big-endian, NACK_PARAM on range violation, ACK echoes payload) and GET_STATUS population of the four event fields
  - Base POST /set-event-thresholds route (WR-07 full-long validation naming the offending field), Event Capture settings card, GET_STATUS poll (30 s, idle-gated) + CommandSender STATUS latch feeding a balloon-reported-truth display
affects: [02-image-transmission (02-VERIFICATION hardware UAT), Phase 3 (event thresholds ride GET_STATUS unchanged)]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 7943     # 31773 diff chars / 4 (55e124b: 13136 + 6969d29: 18637) — plan estimated 32000; the module extension reused existing idioms heavily
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []         # no new libraries — TinyGPSPlus already in the balloon env
  patterns:
    - Single gated capture path (fire()): every automatic source — interval and event — passes one D-28 spacing gate, stamps its CaptureSource, and advances the shared lastCaptureTime baseline, so D-27 (event resets interval baseline) falls out of the arithmetic with no special-casing
    - Hysteresis measured from the last FIRED baseline: a spacing-gated-out delta leaves its baseline unchanged (re-arms), a gated-out phase still sets its seen bit (the moment passed) — skipped intent is never queued stale
    - STATUS latch in CommandSender (survives tracked-slot reuse, same discipline as the base's auto-capture chip) + idle-gated 30 s GET_STATUS poll — balloon-reported truth, never the last-submitted form
    - Double validation for radio-configured thresholds (T-02-11): handler bounds check + in-module re-validation, mirroring the Phase 1 interval pattern (T-01-08)

key-files:
  created: []      # no new files — this plan extends existing modules
  modified:
    - include/auto_capture.h
    - src/auto_capture.cpp
    - include/command_handler.h
    - src/command_handler.cpp
    - src/command_protocol.cpp
    - include/command_sender.h
    - src/command_sender.cpp
    - src/main_basestation.cpp

key-decisions:
  - "Events live INSIDE AutoCapture.process() behind the SC-5 master enable — no parallel timer (CR-05 preserved); AUTO_CAPTURE_DISABLE stops interval AND event captures because both branches sit below the single enabled guard"
  - "D-27 by arithmetic, not special-casing: the interval branch and fire() both compare against lastCaptureTime, which every automatic capture advances — an event near the interval deadline resets the interval baseline and cannot double-capture because the D-28 gate then holds the interval fire until spacing elapses"
  - "eventsEnabled defaults OFF at boot: event captures require explicit opt-in via the UI toggle (SET_EVENT_THRESHOLDS flags bit0); the numeric defaults are the research sketch constants 150 m / 500 m / 20 s"
  - "Delta baselines reset only on a FIRED event (hysteresis): altitude fire resets the altitude baseline; distance fire resets position AND altitude coherently (one movement event, one baseline reset) — per the plan text, exactly"
  - "GPS validity is satellites > 0 — the same signal the 0x14 telemetry beacon and main_balloon's gpsActive use; no new validity source invented (Pitfall 12)"
  - "TinyGPSPlus::distanceBetween verified present in the balloon env's bundled v1.0.3 (research A2 resolved — no haversine fallback needed)"
  - "Interval captures now explicitly stamp CaptureSource::INTERVAL through fire() — previously they inherited the CameraManager constructor default, so a stale MANUAL stamp from a prior manual capture could ride the manifest (D-30 truthfulness fix)"
  - "Base displays balloon-reported truth: a 30 s GET_STATUS poll (skipped while any command is in flight) feeds a CommandSender STATUS latch that survives slot reuse; the card chip renders those values, null-honestly before the first STATUS arrives"

patterns-established:
  - "Radio-configured capture parameters get double validation: handler bounds check answering NACK_PARAM + in-module re-validation, so a spoofed frame cannot bypass the handler (extends the Phase 1 T-01-08 interval pattern to thresholds)"
  - "Poll-only remote-state display: the base never optimistically renders submitted settings; it polls the device and latches the newest typed response"

requirements-completed: [CTRL-05]

metrics:
  duration: ~12 min active (single session)
  completed: 2026-08-19
  tasks_completed: 2
  commits: 2

status: complete
---

# Phase 02 Plan 04: Event-Based Capture Triggers Summary

Balloon-side capture intelligence completed: AutoCapture (the sole automatic-capture authority and image-ID owner) now fires on GPS altitude deltas, horizontal distance deltas, and the four operationally meaningful flight-phase moments, with UI-configurable thresholds (SET_EVENT_THRESHOLDS) that round-trip base form → command protocol → module → GET_STATUS → balloon-reported display. CTRL-05 closed at code level; the last Phase 2 requirement.

## Tasks Completed

| Task | Name | Commit | Key files |
|------|------|--------|-----------|
| 1 | Event-trigger engine inside AutoCapture (D-25/D-27/D-28) | 55e124b | include/auto_capture.h, src/auto_capture.cpp |
| 2 | SET_EVENT_THRESHOLDS command + GET_STATUS reporting + base UI threshold card (D-26) | 6969d29 | src/command_handler.cpp, src/main_basestation.cpp, include/command_handler.h, src/command_protocol.cpp, include/command_sender.h, src/command_sender.cpp |

## What Was Built

**Task 1 — Event engine in AutoCapture (commit 55e124b).** `eventConfig` (altDeltaM/distDeltaM/minSpacingS/eventsEnabled) with flight defaults 150/500/20 and constants `EVENT_MIN/MAX_*` (10..5000 m, 10..50000 m, 5..3600 s); `setEventConfig` re-validates bounds in-module (T-02-11) and is idempotent. `process()` keeps the single master guard (`enabled`), then runs the interval branch — now requiring BOTH interval elapsed AND D-28 spacing satisfied — followed by `processEvents()` when `eventsEnabled`. Event evaluation: flight-phase first-entry via `SysState().getFlightPhase()` against a `phaseSeenMask` (all 8 phases set their bit once per boot; only LAUNCH/APEX/PARACHUTE_DESCENT/LANDING capture — research Q3); altitude delta `|gps.altitude - lastEventAltM| >= altDeltaM`; distance delta via `TinyGPSPlus::distanceBetween` (research A2 — verified present in the bundled v1.0.3). Delta evaluation requires satellites > 0 (Pitfall 12) and the first valid fix seeds baselines without firing. `fire(source)` is the single gated path: D-28 spacing gate (`millis() - lastCaptureTime < minSpacingS*1000` → skip), source stamped via `setLastCaptureSource` BEFORE `captureImage()` (D-30), baseline advanced before the attempt (T-01-09), ID via the existing `allocateImageId()`. A spacing-skipped delta leaves its baseline unchanged (re-arms); a spacing-skipped phase still sets its seen bit. Because every capture advances `lastCaptureTime`, an event fire resets the interval baseline (D-27) with no special-casing. No second timer abstraction (grep-verified no `Timer<`).

**Task 2 — Command + status + UI (commit 6969d29).** Balloon: `case SET_EVENT_THRESHOLDS` decodes the 7-byte payload big-endian (`readUint16` x3 + flags, Pitfall 9), validates the same bounds the module enforces (NACK_PARAM naming the violated field), delegates to `AutoCap().setEventConfig`, ACK echoes the payload; `commandToString` gained the SET_EVENT_THRESHOLDS name. `handleGetStatus` populates `eventThresholdAltM/eventThresholdDistM/eventMinSpacingSec/eventFlags` from `getEventConfig()` (ResponseStatusData field-sum unchanged at 28 — harness clause img-h re-verified). Base: `CommandSender` latches the newest STATUS payload (`getStatusData`) so balloon truth survives slot reuse; the loop polls GET_STATUS every 30 s (`STATUS_POLL_INTERVAL_MS`), skipped while any command is pending; `/status` gains `eventThresholds{}` (null until the first STATUS). `POST /set-event-thresholds` validates every field as long against the full range BEFORE narrowing (WR-07 — 400 names the offending field), builds the 7-byte big-endian payload, and sends via the tracked-command machinery (normal ACK/retry, lands in the D-16 queue as "Set Event Thresholds"). The Event Capture card (three numeric inputs with min/max/step hints + enable toggle + Save) shows the balloon-reported values on a chip and disables its inputs while a SET_EVENT_THRESHOLDS command is in flight.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] GET_STATUS poll + STATUS latch did not exist on the base**

- **Found during:** Task 2
- **Issue:** The plan requires "current values displayed from the GET_STATUS poll (values shown are the balloon-reported truth, not the last-submitted form)", but the base never issued GET_STATUS and had no accessor for response payload data (responses live in private TrackedCommand slots that get reused).
- **Fix:** Added a 30 s idle-gated GET_STATUS poll in the base loop, a `latestStatus` latch inside `CommandSender::handleResponse` (filled only from typed STATUS responses with a full-size payload), and a `getStatusData` getter; `/status` serves the latched values, the card renders them.
- **Files modified:** include/command_sender.h, src/command_sender.cpp (beyond the plan's Task 2 file list)
- **Commit:** 6969d29

**2. [Rule 2 - Consistency] SET_EVENT_THRESHOLDS missing from commandToString**

- **Found during:** Task 2
- **Issue:** The debug/logging name table in command_protocol.cpp had no entry for the new command (would print "UNKNOWN"); IMAGE_WINDOW_REQUEST received the same treatment in 02-02.
- **Fix:** Added the case to `CommandProtocol::commandToString`.
- **Files modified:** src/command_protocol.cpp (beyond the plan's file list)
- **Commit:** 6969d29

**3. [Rule 1 - Latent bug] Interval captures inherited a stale capture-source stamp**

- **Found during:** Task 1
- **Issue:** The Phase 1 interval path never stamped `lastCaptureSource`; it relied on CameraManager's constructor default (INTERVAL). After any manual CAPTURE_NOW stamped MANUAL, a subsequent interval capture's manifest would carry the stale MANUAL source — violating D-30 truthfulness.
- **Fix:** The interval branch goes through `fire(CaptureSource::INTERVAL)`, which stamps the source explicitly like every event path.
- **Files modified:** src/auto_capture.cpp (within the plan's Task 1 file list — fire() unified the paths)
- **Commit:** 55e124b

Otherwise the plan executed exactly as written.

## Known Limitations / UAT Flags

- **Flagged assumption (plan frontmatter):** GPS altitude/distance deltas at the 150 m / 500 m defaults and the flight-phase machine's transitions are meaningful trigger signals on real flights — hardware UAT with live GPS validates both the deltas and that D-28 spacing holds under jitter (deferred to 02-VERIFICATION by plan design).
- **Capture-failure retry cadence:** a failed `captureImage()` in an event path leaves the delta exceeded, so the event retries once per spacing window while the delta remains beyond threshold — bounded by D-28, consistent with the interval path's failed-capture semantics.
- **Queue panel shows periodic "Get Status" rows** from the 30 s poll — honest existing vocabulary, accepted cost of balloon-reported display.

## Auth Gates

None — no authentication was required during execution.

## Known Stubs

None — every UI value (event chip, queue rows, transfer rows) is wired to live computed or balloon-reported state; the event chip is honestly null until the first STATUS response arrives.

## Verification Results

- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS (re-run after each task)
- Task 1 gates: EVENT_ALTITUDE / PARACHUTE_DESCENT / setLastCaptureSource / minSpacing / allocateImageId greps PASS; no `Timer<` in auto_capture.cpp PASS
- Task 2 gates: SET_EVENT_THRESHOLDS in command_handler.cpp, set-event-thresholds in main_basestation.cpp, eventThresholdAltM in command_handler.cpp — all PASS
- `node scripts/verify_protocol_roundtrip.mjs` — all clauses PASS, exit 0 (event-thresholds payload codec clause img-f and ResponseStatusData layout clause img-h both green)
- Prohibition verified by construction: the interval branch and processEvents() both sit below the single `enabled` guard in process() — AUTO_CAPTURE_DISABLE stops every automatic capture source

## Self-Check: PASSED

All 8 modified files exist on disk; commits 55e124b and 6969d29 present in git log; no tracked files deleted by either commit; no stubs introduced.

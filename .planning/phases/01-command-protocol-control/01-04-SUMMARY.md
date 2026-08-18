---
phase: 01-command-protocol-control
plan: "04"
subsystem: firmware
tags: [esp32-s3, esp32-camera, arduino, platformio, lora, timer]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (01-02)
    provides: Length-driven framing and typed responses in CommandHandler; protocol serializer fixes
  - phase: 01-command-protocol-control (01-03)
    provides: Base station /auto-capture route encoding the interval as a 4-byte big-endian payload
provides:
  - CameraManager sensor setters for saturation, exposure (ae_level), and white balance mode with cached-value getters — all 7 camera settings now execute against the real sensor
  - AutoCapture interval timer module (include/auto_capture.h + src/auto_capture.cpp) with the AutoCap() accessor — wraparound-safe millis timing, command-controlled enable/disable, shared uint16 image-ID sequence
  - Truthful GET_STATUS (tracked image ID, auto-capture state/interval, live camera settings)
  - Balloon main-loop wiring (AutoCap().begin/process) and basestation build exclusion for the balloon-only module
affects: [02-image-transmission, camera-control, auto-capture, uat]

# Actuals (#2632)
actuals:
  tokens: 4305
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Balloon-only module pattern: new manager class with static instance + global accessor (AutoCap()), balloon env picks it up via include-all filter, basestation env excludes it explicitly"
    - "Interval timer pattern: wraparound-safe millis() difference with baseline reset at enable() and baseline advance before the capture attempt (no tight failure loop)"
    - "Single-authority image ID: AutoCapture::allocateImageId() pre-increments one shared counter for manual and automatic captures"

key-files:
  created:
    - include/auto_capture.h
    - src/auto_capture.cpp
  modified:
    - src/camera_manager.h
    - src/camera_manager.cpp
    - src/command_handler.cpp
    - src/main_balloon.cpp
    - platformio.ini

key-decisions:
  - "AutoCapture owns the single uint16 image-ID sequence — CAPTURE_NOW and interval captures share it, so GET_STATUS lastImageId is truthful across both modes (wraps at 65535; Phase 2 owns durable IDs)"
  - "enable() resets the timing baseline so the first periodic capture fires one full interval after enable, not immediately; process() advances the baseline before the capture attempt so a failed capture cannot drive a tight retry loop (T-01-09)"
  - "Interval bounds (1000..3600000 ms) are re-validated inside AutoCapture::enable even though the handler validates first — a bypassed handler cannot drive the timer below 1 Hz (T-01-08)"

patterns-established:
  - "CameraManager setter pattern extended: initialized guard → esp_camera_sensor_get → sensor op → cache → bool, now covering all 7 settings"
  - "ACK-only-on-execution: every command handler's success path corresponds to a real sensor call or timer state change; failure paths return NACK_BUSY with a message"

requirements-completed: [CTRL-02, CTRL-03, CTRL-04]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "All 7 camera settings execute against the real camera sensor — CameraManager setSaturation/setExposure/setWBMode sensor setters (mirroring setContrast) called by the rewritten handlers, which ACK only on true sensor return and NACK_BUSY otherwise"
    requirement: CTRL-02
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon (SUCCESS) + structural gates: bool CameraManager::setSaturation, s->set_ae_level, camera->setSaturation/setExposure/setWBMode in handlers, no stub comments"
        status: pass
    human_judgment: true
    rationale: "Build and structural gates prove the wiring, but actual sensor acceptance of saturation/ae_level/wb_mode values and resulting image changes require the physical camera (hardware UAT item 4)"
  - id: D2
    description: "Interval auto-capture timer module — AutoCapture with enable/disable/isEnabled/getInterval/process/allocateImageId/getLastImageId, wraparound-safe millis timing, no delay() calls, first capture one full interval after enable"
    requirement: CTRL-03
    verification:
      - kind: other
        ref: "pio run both environments SUCCESS; grep gates: AutoCap().enable/disable wired in handlers, AutoCap().begin/process in main_balloon.cpp, no delay() in auto_capture.cpp"
        status: pass
    human_judgment: true
    rationale: "Interval spacing over the live radio link and camera capture timing are hardware behaviors; code-level automation cannot exercise the millis timer against a real capture"
  - id: D3
    description: "GET_STATUS returns tracked state (last image ID, auto-capture enabled flag and interval, live resolution/quality/brightness/contrast from CameraManager) and manual+automatic captures share one image-ID sequence"
    requirement: CTRL-04
    verification:
      - kind: other
        ref: "grep gates: zero placeholder occurrences in command_handler.cpp, TODO markers gone, static uint16_t imageId counter removed, handleGetStatus sourced from AutoCap() and camera getters"
        status: pass
    human_judgment: true
    rationale: "Truthfulness of the status payload end-to-end (base station display of balloon state) requires the radio round-trip; code inspection proves field sourcing only"
  - id: D4
    description: "Both firmware targets build green with the new balloon-only module — auto_capture.cpp excluded from esp32-s3-basestation, included automatically by esp32-s3-balloon"
    requirement: CTRL-03
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation → 2 succeeded; grep auto_capture.cpp present in platformio.ini basestation filter"
        status: pass
    human_judgment: false

# Metrics
duration: 14 min
completed: 2026-08-18
status: complete
---

# Phase 1 Plan 04: Balloon Auto-Capture + Real Camera Setters Summary

**Sensor-backed saturation/exposure/white-balance setters, a wraparound-safe AutoCapture interval timer with one shared image-ID sequence, and truthful GET_STATUS — every balloon ACK now corresponds to an executed action, and both firmware targets build green.**

## Performance

- **Duration:** 14 min
- **Started:** 2026-08-18T03:03:04Z
- **Completed:** 2026-08-18T03:17:13Z
- **Tasks:** 2
- **Files modified:** 7 (2 created, 5 modified)

## Accomplishments
- All 7 camera settings now execute against the real camera sensor: CameraManager gained setSaturation/setExposure (ae_level)/setWBMode following the exact setContrast pattern, plus cached getters; the three stub handlers were rewritten to call them, ACK only on true return, and NACK_BUSY with a failure message otherwise (WB validation now rejects anything outside 0..4, including negatives)
- New AutoCapture module delivers CTRL-03/CTRL-04 balloon execution: enable() re-validates 1000..3600000 ms and resets the baseline (first capture one full interval after enable), process() runs the wraparound-safe millis idiom with the baseline advancing before the attempt, and no delay() calls anywhere
- GET_STATUS reports tracked state (AutoCap last image ID / enabled / interval + live camera getters) instead of fabricated constants; CAPTURE_NOW's function-static counter was replaced by AutoCap().allocateImageId() so manual and automatic captures share one ID sequence
- main_balloon.cpp wires AutoCap().begin(&Camera()) in setup and AutoCap().process() next to CmdHandler().process() in the loop; platformio.ini excludes auto_capture.cpp from the basestation build — both environments compile SUCCESS
- The ACK-without-action anti-pattern is eliminated: zero "placeholder" strings and no TODO markers remain in command_handler.cpp

## Task Commits

Each task was committed atomically:

1. **Task 1: CameraManager sensor setters + real saturation/exposure/WB handlers (CTRL-02)** - `ef76e4d` (feat)
2. **Task 2: AutoCapture interval module, real auto-capture handlers, real GET_STATUS, loop wiring, build filter (CTRL-03/CTRL-04)** - `6850f54` (feat)

**Plan metadata:** committed with this summary (docs)

## Files Created/Modified
- `include/auto_capture.h` - AutoCapture class API (begin/enable/disable/isEnabled/getInterval/process/allocateImageId/getLastImageId) + AutoCap() accessor declaration
- `src/auto_capture.cpp` - Interval timer implementation: static instance + accessor, wraparound-safe timing, shared image-ID counter, T-01-08/T-01-09 mitigations
- `src/camera_manager.h` - Declarations for the three sensor setters, three const getters, three private cached members
- `src/camera_manager.cpp` - setSaturation/setExposure/setWBMode (setContrast pattern) + constructor initialization; initCamera block untouched
- `src/command_handler.cpp` - Real handlers for saturation/exposure/WB/auto-capture enable+disable, tracked GET_STATUS, shared image-ID CAPTURE_NOW
- `src/main_balloon.cpp` - AutoCap().begin(&Camera()) in setup, AutoCap().process() in processPacketHandling
- `platformio.ini` - `-<auto_capture.cpp>` added to the esp32-s3-basestation build_src_filter

## Decisions Made
- AutoCapture owns the single uint16 image-ID sequence (pre-increment counter shared by manual and automatic captures) so GET_STATUS lastImageId is truthful across both modes; wraps at 65535 — Phase 2 image sequencing owns durable IDs (T-01-10 accepted)
- Baseline-before-attempt ordering in process(): a failed capture does not reset the baseline early, preventing a tight failure loop (T-01-09); enable() while already enabled just updates the interval and resets the baseline
- Interval bounds re-validated inside AutoCapture::enable (defense in depth against handler-validation bypass, T-01-08)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- gsd-tools `state.advance-plan`/`state.update-progress` could not parse this project's STATE.md layout (same condition the 01-03 executor hit); STATE.md was updated manually to the same effect. `roadmap.update-plan-progress 01` and `requirements.mark-complete` worked via the tools.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 1 code execution is complete (4/4 plans): protocol, base station UI, and balloon execution all landed; both build targets green
- Remaining before phase close: code review gate (`/gsd-code-review 1`), verifier re-run, and hardware UAT (flash both units — interval timing, sensor setting acceptance, and command round-trips are code-proven only)
- Flagged for UAT: manual capture between intervals does not reset the auto-capture baseline (may cause back-to-back captures); mode coexistence assumed to interleave

## Self-Check: PASSED

All task acceptance criteria re-verified after both commits: pio run both environments SUCCESS; structural gates (sensor setter symbols, handler call sites, AutoCap wiring, zero placeholder, TODO markers gone, static imageId removed, platformio exclusion) all PASS; plan-level verification block satisfied.

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-18*

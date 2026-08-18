---
phase: 01-command-protocol-control
plan: "05"
subsystem: firmware
tags: [esp32-s3, arduino, platformio, lora, camera-control, gap-closure, dead-code-removal]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (plan 01-04)
    provides: AutoCapture interval module, single shared image-ID sequence, loop wiring (AutoCap().begin/process)
provides:
  - CR-05 closed — AutoCapture is the balloon's ONLY automatic capture trigger and ONLY image-ID sequence
  - SC-5 disable semantics restored at code level — no capture path exists outside AutoCap().process and the CAPTURE_NOW handler
  - Truthful AUTO_CAPTURE_DISABLE ACKs (T-01-14) and truthful GET_STATUS lastImageId at system level (T-01-15)
  - CameraManager dead timing helper swept project-wide (zero isTimeToCapture occurrences in src/ + include/)
affects: [02-image-transmission, hardware-uat]

# Actuals — pairs with the plan's estimate (22000 tokens); pure-deletion plan realized a tiny diff
actuals:
  tokens: 600        # chars/4 over the realized diff (59 changed lines across 4 files)
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Single-authority invariant enforced by source gates: negative greps prove no second capture trigger / image-ID counter exists"

key-files:
  created: []
  modified:
    - src/main_balloon.cpp
    - src/camera_manager.h
    - src/camera_manager.cpp
    - src/auto_capture.cpp

key-decisions:
  - "Full removal of the legacy 30 s path rather than a debug gate — its packets were never transmitted (processCommunications send path fully commented out), so the block's only live effects were the CR-05 harms"
  - "processIncomingCommands() vestigial placeholder deleted as same-path cleanup — command flow uses CmdHandler().process() inside processPacketHandling"

patterns-established:
  - "Removal plans verify by absence: recursive negative-grep gates over src/ + include/ prove an authority invariant, not just a build pass"

requirements-completed: [CTRL-01, CTRL-03, CTRL-04]

# Coverage metadata — per-deliverable UAT routing
coverage:
  - id: D1
    description: "Legacy 30 s capture timer fully removed — AutoCapture is the balloon's sole automatic capture trigger and sole image-ID sequence (CR-05 closure at code level)"
    requirement: CTRL-03
    verification:
      - kind: other
        ref: "grep gates: zero occurrences of processCamera/processIncomingCommands/nextImageId/createCameraPacket in src/main_balloon.cpp; zero isTimeToCapture in src+include; AutoCap().begin/process + CmdHandler().process present"
        status: pass
      - kind: integration
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation (both SUCCESS)"
        status: pass
      - kind: unit
        ref: "node scripts/verify_protocol_roundtrip.mjs (10/10 clauses, exit 0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Runtime auto-capture cadence and disable semantics over the live radio link (captures at exactly the commanded interval; zero captures after an ACKed AUTO_CAPTURE_DISABLE)"
    requirement: CTRL-04
    verification:
      - kind: manual_procedural
        ref: "01-VERIFICATION.md Human Verification item 4 — enable at 10 s and 60 s, then disable; expect exact cadence and no captures after disable"
        status: unknown
    human_judgment: true
    rationale: "Timing behavior across a real RF link with physical radios and camera cannot be exercised by host builds or the wire-format harness; requires powered hardware"

# Metrics
duration: 7min
completed: 2026-08-18
status: complete
---

# Phase 1 Plan 05: CR-05 Gap Closure — Legacy Capture Timer Removal Summary

**Legacy 30-second capture timer deleted from the balloon loop so AutoCapture becomes the only automatic capture trigger and image-ID authority — SC-5 disable semantics hold at code level, both firmware targets green, wire-format harness 10/10**

## Performance

- **Duration:** 7 min
- **Started:** 2026-08-18T05:15:19Z
- **Completed:** 2026-08-18T05:22:00Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments

- CR-05 closed: `processCamera()` (definition, forward declaration, loop call) removed with its colliding `static uint16_t nextImageId` counter and dead `createCameraPacket` call site — after an ACKed AUTO_CAPTURE_DISABLE no code path captures, and commanded intervals across 1000..3600000 ms keep exact cadence with no 30-second interleave
- One image-ID sequence project-wide: every capture (manual CAPTURE_NOW and interval) draws from `AutoCapture::allocateImageId()` — GET_STATUS lastImageId is now truthful at system level (T-01-15)
- Same-path cleanup: vestigial `processIncomingCommands()` placeholder deleted (definition, declaration, loop call, stale comment); command flow uses `CmdHandler().process()` in processPacketHandling
- CameraManager::isTimeToCapture() dead helper swept (declaration + definition); auto_capture.cpp process() comment reworded to keep the wraparound-safe idiom description and T-01-09 baseline-before-attempt note without citing the removed method
- Untouched as planned: `AutoCap().begin(&Camera())` (initializeSubsystems), `CmdHandler().process()` + `AutoCap().process()` (processPacketHandling), processPowerManagement's legitimate `appState.cameraActive` low-power use

## Task Commits

Each task was committed atomically:

1. **Task 1: Remove the legacy 30-second capture path — AutoCapture becomes the only capture/ID authority (CR-05)** - `352b195` (fix)
2. **Task 2: Same-path dead-code sweep + dual-target build and wire-format regression battery** - `b5726b3` (refactor)

## Verification Evidence

- Task 1 gates: `pio run -e esp32-s3-balloon` SUCCESS (42.8s); negative greps clean for processCamera / processIncomingCommands / nextImageId / createCameraPacket / CameraData in src/main_balloon.cpp; positive greps confirm AutoCap().process, AutoCap().begin, CmdHandler().process
- Task 2 gates: both targets SUCCESS (balloon 42.2s, basestation 20.0s); `node scripts/verify_protocol_roundtrip.mjs` all 10 clauses PASS, exit 0; recursive grep over src/ + include/ returns zero `isTimeToCapture` matches
- Post-commit checks: no tracked-file deletions in either commit; no new untracked files

## Files Created/Modified

- `src/main_balloon.cpp` — legacy path fully removed; the only periodic capture call in the loop is AutoCap().process() (inside processPacketHandling)
- `src/camera_manager.h` — isTimeToCapture() declaration removed from the Timing methods block
- `src/camera_manager.cpp` — isTimeToCapture() definition removed; capture/setter/getter methods untouched
- `src/auto_capture.cpp` — process() comment reworded (wraparound-safe idiom described without the CameraManager method reference); behavior unchanged

## Decisions Made

- Full removal rather than a debug gate: the legacy packet was never transmitted (processCommunications' send path is fully commented out), so the block's only live effects were the CR-05 harms — captures within 30 s of an ACKed disable, cadence pollution above 30 s intervals, ID collision from the second counter, and frame-buffer churn destroying commanded captures
- Placeholder `processIncomingCommands()` deleted with the legacy path (plan-directed; its body was only a "would process" comment predating Phase 1)

## Deviations from Plan

None - plan executed exactly as written.

(One incidental trailing-whitespace normalization on the "Low pressure detected" line immediately preceding the deleted processCamera block — the edit anchor for the deletion; zero functional impact.)

## Issues Encountered

None

## User Setup Required

None - no external service configuration required.

## Known Stubs

None — this plan only removed code; no stubs were created. The one named placeholder in the firmware (empty processIncomingCommands) was itself deleted.

## Threat Flags

None — no new security-relevant surface introduced. The plan's threat register entries are all mitigations applied by removal: T-01-14 (disable ACK truthful — no capture call exists outside AutoCap().process and the CAPTURE_NOW handler, proven by the negative-grep gates), T-01-15 (single image-ID sequence), T-01-16 (no unscheduled frame-buffer churn). The plan's flagged prohibition (no ACK for actions not executed) is restored at code level; runtime confirmation rides hardware UAT item 4.

## Next Phase Readiness

- Phase 1 has zero open code gaps: all 6 phase requirements satisfied at the same code-level standard; REQUIREMENTS.md CTRL-03/CTRL-04 Complete marks are now accurate
- Remaining before Phase 1 complete: `/gsd-secure-phase 1` (security gate), then hardware UAT — SC-2 (RF round trip), SC-3 (sensor acceptance), SC-4 (runtime retry/TIMEOUT), and 01-VERIFICATION.md item 4 (auto-capture cadence + disable half, which this fix unblocked)
- Carried review advisories (WR-01/02/06/08/09/10) are documented in 01-REVIEW.md and deferred to radio bring-up / Phase 2; none block phase completion

## Self-Check: PASSED

All 4 modified source files and the SUMMARY exist on disk; both task commits (352b195, b5726b3) present in git log.

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-18*

---
phase: 01-command-protocol-control
plan: 19
subsystem: protocol
tags: [lora, command-dispatch, esp32, camera-gate, auto-capture, reliability]

requires:
  - phase: 01-04
    provides: the AutoCapture module (shared image-ID sequence, lastCaptureTime baseline arithmetic, enable() baseline reset) whose public API markCaptureBaseline joins
  - phase: 01-16
    provides: re-verification #7 confirming CR-04 at source (executeCommand blanket gate :139-145; isReady==initialized camera_manager.h:134; low-battery enableCamera(false) main_balloon.cpp:864) and the 01-17 WINDOWS routing (entry 8) this plan discharges at code level
provides:
  - CR-04 code fix (balloon command dispatch): camera-ready NACK_BUSY refusal scoped to the eight camera-touching command classes via file-scope commandRequiresCamera() — IMAGE_WINDOW_REQUEST, GET_STATUS, SET_EVENT_THRESHOLDS, and AUTO_CAPTURE_ENABLE/DISABLE dispatch identically to HEAD regardless of camera state
  - WR-03 code fix: public AutoCapture::markCaptureBaseline() (lastCaptureTime = millis()) called exactly once in handleCaptureNow's success branch — a manual capture advances the shared interval baseline so an interval capture can never fire moments after a manual one
  - Bench discriminators for 01-20: camera-down window servability (WINDOWS entry 8 flip rules) and manual-then-interval capture timestamp spacing
affects: [01-20 bench, WINDOWS entry 8 (CR-04) flip, phase-01 close-out, G-01-7/G-01-9 series benches]

requirements: [IMG-03, IMG-05, CTRL-03, CTRL-04]

actuals:
  tokens: 1269     # chars/4 over the realized 2-commit diff (5,075 chars); estimate was 4,000 at confidence low (0 calibration samples)
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Refusal-reason symmetry at the dispatch gate: a readiness refusal is scoped by a pure command-class classifier (file-scope static switch, the evictionClassOf shape) so the refused resource matches the missing resource — a blanket gate that refuses commands whose resources are intact is a data-loss defect, not safety"
    - "Manual captures join the automatic baseline arithmetic through the same field fire() advances — no parallel timer, no special-casing in process(); the manual call is success-gated (a failed attempt must not defer the schedule), mirroring fire()'s success-gated ID allocation"

key-files:
  created: []
  modified:
    - src/command_handler.cpp
    - include/auto_capture.h
    - src/auto_capture.cpp

key-decisions:
  - "Gate scoping, not gate removal: commandRequiresCamera() returns true for exactly CAPTURE_NOW + the seven SET_* sensor classes; AUTO_CAPTURE_ENABLE/DISABLE, GET_STATUS, IMAGE_WINDOW_REQUEST, SET_EVENT_THRESHOLDS return false, and unknown commands fall through the default to false so the existing NACK_INVALID branch stays their honest verdict — the refusal message, commandsFailed accounting, and return shape are byte-identical to the old blanket block"
  - "GET_STATUS is safe-when-down by construction: it reads AutoCap() getters and camera_manager's cached settings getters (getFrameSize/getQuality/getBrightness/getContrast return member fields, camera_manager.h:121-127) — last-known truth, never fabricated sensor reads (T-01-19-02)"
  - "markCaptureBaseline is unconditional-safe while disabled: the interval branch in process() is gated on enabled and enable() resets the baseline anyway, so the manual-path call needs no enabled check; it also spaces event triggers from manual captures per D-28's any-two-captures intent"
  - "Success-gated manual baseline: fire() advances the baseline BEFORE the attempt (T-01-09, preventing a tight automatic failure loop) while the manual path advances AFTER success — CAPTURE_NOW failure is operator-visible and retry-driven, and a failed manual attempt must not defer the auto schedule"

patterns-established:
  - "Pure-classifier gating: readiness gates key on a static command-class switch colocated with the dispatcher, never on a blanket precondition — every future command class must be classified in commandRequiresCamera (camera-touching) or it inherits ungated dispatch"

requirements-completed: []   # IMG-03, IMG-05, CTRL-03, CTRL-04 all already [x] Complete in REQUIREMENTS.md before this plan (mark-complete IMG-05 returned updated:false / already_complete; IMG-03/CTRL-03/CTRL-04 shared-ID-blocked by sibling 01-20's declaration) — no flips this plan, per the no-gap-flips prohibition

coverage:
  - id: D1
    description: "CR-04 scoped camera gate — file-scope commandRequiresCamera() (eight camera-touching classes true; AUTO_CAPTURE_ENABLE/DISABLE, GET_STATUS, IMAGE_WINDOW_REQUEST, SET_EVENT_THRESHOLDS and unknown false) with the executeCommand NACK_BUSY 'Camera not ready' refusal conditioned on commandRequiresCamera(cmd.cmd) && !camera->isReady()"
    requirement: IMG-05
    verification:
      - kind: other
        ref: "plan Task 1 PowerShell assertions — commandRequiresCamera count >= 2 and 'Camera not ready' count == 1 in src/command_handler.cpp, both exit 0"
        status: pass
      - kind: other
        ref: "grep isReady() in src/command_handler.cpp == 1 site, inside the scoped gate at :176 (no ungated-class dispatch path checks camera readiness)"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — 2 succeeded; node scripts/verify_protocol_roundtrip.mjs — all wire-format regression checks pass, exit 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "WR-03 manual-capture baseline — public AutoCapture::markCaptureBaseline() (lastCaptureTime = millis(), the same wraparound-safe field fire() advances) called exactly once in handleCaptureNow's success branch after the allocateImageId block; failed captures leave the baseline untouched"
    requirement: CTRL-03
    verification:
      - kind: other
        ref: "plan Task 2 PowerShell assertions — markCaptureBaseline in include/auto_capture.h >= 1, in src/auto_capture.cpp >= 1, exactly one AutoCap().markCaptureBaseline() call site in src/command_handler.cpp, all exit 0"
        status: pass
      - kind: other
        ref: "pio run both targets — 2 succeeded; wire harness all green, exit 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "Behavioral proof that a real camera-down window still services window requests/status polls/threshold config/auto-capture enable-disable and that a manual capture spaces the next interval capture one full interval — rides the 01-20 bench (WINDOWS entry 8 flip rules defined there)"
    verification:
      - kind: manual_procedural
        ref: "deferred to 01-20 bench (operator hardware session; both boards reflash together)"
        status: unknown
    human_judgment: true
    rationale: "Camera-down servability and capture-timestamp spacing require the two-board hardware bench with a real low-battery camera-down window — no automation in this repo can exercise the power path; the 01-20 plan owns the discriminator and the WINDOWS entry 8 flip"

duration: 7min
completed: 2026-08-25
status: complete
---

# Phase 01 Plan 19: Balloon Command-Path Gap Closures (CR-04 + WR-03) Summary

**Scoped the balloon's blanket camera-ready refusal to the eight camera-touching command classes via a `commandRequiresCamera` classifier — window requests, status polls, threshold config, and auto-capture enable/disable now dispatch during camera-down windows while ImageTx buffers and the radio stay operational — and made every successful manual capture advance the shared AutoCapture interval baseline (`markCaptureBaseline`), closing the double-capture window the review carried as WR-03.**

## Performance

- **Duration:** ~7 min (2026-08-25T11:27:20Z → 11:34:35Z, executor session)
- **Tasks:** 2
- **Files modified:** 3 (`src/command_handler.cpp`, `include/auto_capture.h`, `src/auto_capture.cpp`)
- **Estimate vs actual:** estimate 4,000 tokens at confidence low (0 calibration samples); actuals 1,269 tokens over the realized 2-commit diff (5,075 chars)

## Accomplishments

- CR-04 removed at the dispatch level: `commandRequiresCamera()` (file-scope static, the `evictionClassOf` shape) classifies exactly CAPTURE_NOW + the seven SET_* sensor classes as camera-touching; the NACK_BUSY "Camera not ready" refusal — identical message, commandsFailed accounting, and return shape — now fires only for those eight. IMAGE_WINDOW_REQUEST, GET_STATUS, SET_EVENT_THRESHOLDS, and AUTO_CAPTURE_ENABLE/DISABLE dispatch exactly as at HEAD regardless of camera state, so announced fulls stay retrievable and the status poll keeps answering through a low-battery camera-down window
- WR-03 closed: public `AutoCapture::markCaptureBaseline()` sets `lastCaptureTime = millis()` — the same wraparound-safe field every automatic capture advances inside `fire()` — called exactly once in handleCaptureNow's success branch after the allocateImageId block. The next interval capture counts one full interval from a manual trigger, event triggers are spaced from manual captures (D-28 any-two-captures), and a failed manual capture leaves the baseline untouched
- All three prohibitions held: no camera-touching class ungated (single `isReady()` site in the file, inside the scoped gate), no ungated class gated (grep-proven), and no handler body / ACK-NACK vocabulary / response factory / counter beyond the existing block touched — the diff is one helper + one gate condition + one baseline call
- Both firmware targets green after each task (2 succeeded, run twice); wire harness all checks pass, exit 0 (run twice)

## Task Commits

1. **Task 1: CR-04 — scope the camera-ready gate to the eight camera-touching handlers** — `5b9a8a7` (fix)
2. **Task 2: WR-03 — a successful manual capture advances the auto-capture baseline** — `393c8db` (fix)

## Files Created/Modified

- `src/command_handler.cpp` — file-scope static `commandRequiresCamera(CameraCommand)` with the CR-04 provenance comment; the executeCommand gate conditioned on `commandRequiresCamera(cmd.cmd) && !camera->isReady()`; the `AutoCap().markCaptureBaseline()` call in handleCaptureNow's success branch (+38/-2 then +12 lines)
- `include/auto_capture.h` — public `void markCaptureBaseline()` with the WR-03 / D-27 / D-28 semantics comment (+9)
- `src/auto_capture.cpp` — `markCaptureBaseline()` implementation (`lastCaptureTime = millis()`) after `disable()`, with the wraparound-idiom note (+9)

## Decisions Made

See key-decisions frontmatter. In brief: scoping (not removal) with a byte-identical refusal for the gated eight; GET_STATUS safe-when-down via cached getters; markCaptureBaseline unconditional-safe while disabled (interval branch gated on `enabled`, `enable()` resets the baseline); manual baseline advance success-gated while `fire()`'s is before-the-attempt (T-01-09) — the asymmetry is deliberate: an automatic failure loop is a machine hazard, a manual failure is operator-retry-driven and must not defer the schedule.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## Auth Gates

None.

## Known Stubs

None — both changes are real dispatch/baseline logic with provenance comments; no placeholder values, no unwired data, no TODO/FIXME markers.

## Threat Surface

No new surface beyond the plan's threat model. All three register dispositions implemented/accepted as planned: T-01-19-01 (widened servability) mitigated by the scoping itself — window-request frames were always served when the camera was up and remain CRC16-validated, kind-checked, and range-validated (T-02-04/T-02-11 unchanged); T-01-19-02 (GET_STATUS during camera-down) accepted — cached getters return last-known sensor truth, no fabricated field, and the camera-down state is observable from the link; T-01-19-03 (AUTO_CAPTURE_ENABLE while down) accepted — later captures fail honestly through the still-gated CAPTURE_NOW path and `fire()`'s failure handling.

## Gaps Status (unchanged by this plan, per prohibition)

- **WINDOWS entry 8 (CR-04): code-level mechanism IN** — the dispatch gate is scoped; camera-down no longer blocks window service, status, thresholds, or auto-capture config at the code level. NOT closed: the entry flips at the 01-20 bench per that plan's rules (camera-down window servability evidence)
- **WR-03 (carried warning): code fix IN** — the manual-then-interval capture timestamp spacing is bench-discriminable at 01-20 via the two capture timestamps
- **No gap-status flips this plan** — behavioral proof rides the 01-20 bench by design

## Next Phase Readiness

- Ready for 01-20 (the bench round): both boards reflash together (01-13 discipline — this plan makes no wire-format change, but the 01-18 quiet-gate and this plan's dispatch/baseline changes ride the same flash)
- Both firmware targets build green on the 01-19 head; wire harness green — no new packet types, no layout changes, no UI changes, no env vars

## Self-Check: PASSED

- Modified files exist on disk: `src/command_handler.cpp`, `include/auto_capture.h`, `src/auto_capture.cpp`
- Commits exist on main: 5b9a8a7 (Task 1), 393c8db (Task 2); `git log --oneline --grep="01-19"` returns both
- Diff scope clean: the two commits touch exactly the plan's `files_modified` set (3 files, 65 insertions / 2 deletions); no file deletions; pre-existing working-tree noise (.pio/build/project.checksum, .planning/config.json, 03-UAT.md, untracked .gsd/ / .planning/research/ / balloon5.log / base5.log) left unstaged
- Task verify blocks re-run after each task: builds 2/2 SUCCESS (twice), harness exit 0 (twice), all six PowerShell source assertions PASS
- Acceptance criteria all PASS: single `isReady()` site at :176 inside the scoped gate; single "Camera not ready" message; single `markCaptureBaseline` call site in the success branch

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-25*

---
phase: 01-command-protocol-control
plan: 32
subsystem: firmware-debug
tags: [esp32-s3, d1-crash-regression, g-01-10, instrumentation, freertos, stack-watermark, task-watchdog, i2c, debug-round-14]

requires:
  - phase: 01-command-protocol-control (plans 01-30/01-31, round #13)
    provides: the [IDLE0]/[I2C]/[TWDT] instruments and session-10 console evidence (balloon4.log) that named the §10.5 round-#14 brief
provides:
  - The [STACK] IDLE0-watermark instrument — throttled in-hook uxTaskGetStackHighWaterMark(NULL) sample + running-minimum latch + 1 Hz new-low print — the designed one-session discriminator for the labeled stack-capacity hypothesis
  - The [TWDT] ForCPU(0) handle fix + the deployed WDT-config re-derivation from the pinned framework sdkconfig (wrong-handle reading predicted; ESP_OK expected at 01-34)
  - The [IDLE0] registered printf fix (esp_err_t == ESP_OK; registered=1 on success)
  - The [I2C] write-path coverage answer, dispositioned in writing (branch b — the HAL/library provably swallow write errors, source paths recorded in §11.3.1)
  - The G01_D1_IDLE_HOOK_DISABLED A/B guard + the §11.4 build recipe
  - Debug doc §11 (implementation record, discriminator table, A/B recipe, selection provenance) — exactly what 01-34's bench session greps
affects: [01-33, 01-34 bench session, G-01-7 admission-control round, phase close-out]

actuals:
  tokens: 32400
  tasks: 2
  commits: 3

tech-stack:
  added: []
  patterns:
    - "[STACK] instrument: the [MEM] new-low-latch convention applied to an idle-task context (sample throttled INSIDE the hook, print latched from the loopTask 1 Hz block — the instrument never perturbs what it measures)"
    - "A/B experiment support: preprocessor guard around a single registration call with a self-identifying #else console line, recipe recorded in the debug doc"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/01-32-SUMMARY.md
  modified:
    - src/main_balloon.cpp
    - src/image_tx_manager.cpp
    - .planning/debug/d1-crash-regression-push-start.md
    - .planning/WINDOWS.md
    - .planning/phases/01-command-protocol-control/01-UAT.md

key-decisions:
  - "[I2C] write-path coverage executed branch (b): the write path provably swallows errors end-to-end (Adafruit_SSD1306::display() returns void and discards all five endTransmission returns :401/:427/:435/:1044/:1052; TwoWire retains no error state — no lastError accessor in the pinned Wire.h), so no speculative poller was added; the HAL's [E] i2cWrite line stays the write-path observable; recorded in §11.3.1 with source paths"
  - "The A/B guard carries an #else self-identification line ('[IDLE0] hook cpu0 DISABLED for A/B') so the two experiment arms are distinguishable on console — the A/B result is attributable without console-side guesswork"
  - "xTaskGetIdleTaskHandleForCPU used verbatim per the plan; the declaration-site finding (idf_additions.h:645, deprecated inline → xTaskGetIdleTaskHandleForCore :144, not task.h) recorded in §11.2 rather than silently substituted"
  - "The pinned-sdkconfig re-derivation uses the ACTUAL pinned layout (framework-arduinoespressif32-libs/esp32s3/sdkconfig:2175-2180) — the plan-context's guessed tools/sdk/ path does not exist in pioarduino 3.3.9; the exact path is recorded as the plan required"
  - "REMOVAL CONDITION count arithmetic made explicit: 8 → 11 (+3 new constructs in main_balloon.cpp; image_tx_manager.cpp unchanged at 4 — the [TWDT] fix modifies an instrument whose citation already stands)"

patterns-established:
  - "Instrument-fix discipline: a broken instrument line (printf bug, ambiguous handle) is fixed with its citation updated in-source and the fix named in the debug doc the same round"

requirements-completed: [CTRL-01, CTRL-06, PRI-02]

coverage:
  - id: D1
    description: "The [STACK] IDLE0-watermark instrument is wired: throttled in-hook sample (every 1024 idle ticks) into a running-minimum latch, 1 Hz new-low print, [MEM] convention held, existing counter/frozen-latch byte-identical"
    requirement: CTRL-01
    verification:
      - kind: integration
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation → 2/2 SUCCESS"
        status: pass
      - kind: unit
        ref: "grep 'uxTaskGetStackHighWaterMark(NULL)' src/main_balloon.cpp && grep 'stack watermark' src/main_balloon.cpp"
        status: pass
    human_judgment: false
  - id: D2
    description: "The [TWDT] instrument calls esp_task_wdt_status(xTaskGetIdleTaskHandleForCPU(0)) with log format unchanged; the deployed WDT config is re-derived in §11.2 with the exact sdkconfig path and the two-reading prediction settled in writing"
    requirement: CTRL-06
    verification:
      - kind: integration
        ref: "pio run (2/2 SUCCESS) + grep 'xTaskGetIdleTaskHandleForCPU(0)' src/image_tx_manager.cpp"
        status: pass
      - kind: other
        ref: "grep '^## 11' + 'CONFIG_ESP_TASK_WDT' in .planning/debug/d1-crash-regression-push-start.md"
        status: pass
    human_judgment: false
  - id: D3
    description: "The [IDLE0] registered line derives from an esp_err_t compared against ESP_OK — registered=1 on success, genuine failure reads 0"
    requirement: CTRL-06
    verification:
      - kind: unit
        ref: "grep 'idle0HookErr == ESP_OK' src/main_balloon.cpp"
        status: pass
    human_judgment: false
  - id: D4
    description: "The [I2C] write-path coverage gap is closed in writing: branch (b) swallowing disposition with the HAL/library source paths (§11.3.1); no speculative poller added; the reading-path instrument untouched"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "grep '11.3.1' + 'Adafruit_SSD1306' + 'i2cWrite' in debug doc §11; grep i2c_master_probe unchanged in src/main_balloon.cpp"
        status: pass
    human_judgment: false
  - id: D5
    description: "The G01_D1_IDLE_HOOK_DISABLED A/B guard wraps only the registration call pair; default build behavior-identical (proven by building both ways); the DISABLED arm self-identifies on console; §11.4 records the recipe"
    requirement: CTRL-06
    verification:
      - kind: integration
        ref: "PLATFORMIO_BUILD_FLAGS=-DG01_D1_IDLE_HOOK_DISABLED=1 pio run -e esp32-s3-balloon → SUCCESS (and default rebuild SUCCESS)"
        status: pass
      - kind: unit
        ref: "grep 'G01_D1_IDLE_HOOK_DISABLED' src/main_balloon.cpp + debug doc"
        status: pass
    human_judgment: false
  - id: D6
    description: "Ledgers carry the round-#14 determination honestly open: WINDOWS entry 15 extended in BOTH copies (stays open, counts 2/17/19), 01-UAT.md G-01-10 root_cause extended + §10.5 items marked shipped in missing (stays open); §11.5 records the sixth pending operator confirmation"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "WINDOWS.md JSON validity check (19 entries, 2 open/17 fixed) + 'ROUND-#14 UPDATE' count = 2 + G-01-10 status: open"
        status: pass
    human_judgment: false
  - id: D7
    description: "The round's designed BENCH deliverable — the watermark trajectory (baseline → new lows → value at the crash instant or session minimum) that CONFIRMs/REFUTEs the stack-capacity hypothesis, plus the fixed [TWDT] live boot line — is READ at 01-34, not here"
    verification: []
    human_judgment: true
    rationale: "Hardware acceptance is explicitly out of this plan's scope (§10.5 item 5 guard; the plan makes no bench claims) — 01-34's bench session reads exactly the §11.3 discriminator table this plan armed"

duration: 33min
completed: 2026-08-28
status: complete
---

# Phase 01 Plan 32: D1 Debug Round #4 Instrument Package Summary

**The §10.5 round-#14 instrument package wired green on both targets — the [STACK] IDLE0-watermark one-session discriminator, the [TWDT] ForCPU(0) fix with the config question settled on paper, the [IDLE0] printf fix, the [I2C] write-path disposition, and the A/B-ready hook guard — with NO D1 lever shipped and both ledgers honestly still open.**

## Performance
- **Duration:** 33 min
- **Started:** 2026-08-28T10:08:55Z
- **Completed:** 2026-08-28T10:42:10Z
- **Tasks:** 2 completed
- **Files modified:** 6 (2 source, 3 planning docs, 1 summary)

## Accomplishments
- The round's designed one-session discriminator is armed: idle0TickHook now samples `uxTaskGetStackHighWaterMark(NULL)` (IDLE0's context → IDLE0's stack — the task [MEM] does not monitor) every 1024 idle ticks into a running-minimum latch, and the 1 Hz loopTask block prints `[IDLE0] stack watermark %u words free (new low, t=%lu ms)` — never per-sample, never from the hook (the [MEM] convention). Both predicted signatures are written down in §11.3 BEFORE the bench reads the line.
- The three session-10 instrument defects are fixed or honestly dispositioned: the printf bug (esp_err_t compared against ESP_OK — registered=1 on success), the core-ambiguous handle (`xTaskGetIdleTaskHandleForCPU(0)`, with the pinned-framework declaration-site finding recorded), and the [I2C] write-path coverage gap (branch (b): the write path provably swallows errors end-to-end — Adafruit_SSD1306 discards all five endTransmission returns, TwoWire retains no error state — recorded in §11.3.1 with source paths; no speculative poller).
- The deployed WDT config is re-derived from the pinned framework's sdkconfig with the exact path recorded (§11.2): INIT=y / CHECK_IDLE_TASK_CPU0=y / CPU1 not set / TIMEOUT_S=5 / PANIC=y → the config PREDICTS the wrong-handle reading (session-10's ESP_ERR_NOT_FOUND was IDLE1); ESP_OK expected at 01-34; the live boot line remains the deciding evidence.
- The A/B question is made cheap to kill: `G01_D1_IDLE_HOOK_DISABLED` guards only the registration call pair (default build behavior-identical, proven by building both ways); the DISABLED arm self-identifies on console; §11.4 records the build recipe and when to reach for it.
- §11 carries the round's record durably: provenance + the no-D1-lever statement, the config re-derivation, the 01-34 discriminator table (exact format strings + predicted signatures under both labeled hypotheses), the A/B recipe, and the selection-provenance record (sixth pending operator confirmation).
- Ledgers extended honestly: WINDOWS entry 15 (both copies) and G-01-10 carry the round-#14 package as SHIPPED while staying OPEN — the crash truth and the hypothesis verdict flip only on 01-34's hardware evidence. Zero ledger flips; counts unchanged 2 open / 17 fixed / 19 total.

## Task Commits
1. **Task 1: Implement the §10.5 instrument package** - `7d3abbc` (fix)
2. **Task 2: Debug doc §11 + the 01-34 discriminator table; ledgers extended honestly** - `b33d07c` (docs)

## Files Created/Modified
- `src/main_balloon.cpp` - [STACK] instrument (hook-side latch :140-166 + 1 Hz new-low print :412-433), the fixed registration print + A/B guard (:261-281), instrument include
- `src/image_tx_manager.cpp` - the [TWDT] ForCPU(0) handle fix (:170-172) with the ROUND-#14 comment block
- `.planning/debug/d1-crash-regression-push-start.md` - §11 appended (implementation record, WDT-config re-derivation, discriminator table, A/B recipe, selection provenance)
- `.planning/WINDOWS.md` - entry 15 round-#14 extension in BOTH copies; stays open; counts 2/17/19
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-10 round-#14 extension + shipped-items markers in missing; stays open

## Decisions Made
- [I2C] write-path branch (b) — no speculative poller; the swallowing chain (Adafruit_SSD1306 → TwoWire → HAL) documented with source paths as the coverage answer
- A/B guard gained an #else self-identification line so the two experiment arms are distinguishable on console (default build unchanged)
- ForCPU(0) used verbatim per the plan with the idf_additions.h:645 declaration finding recorded, not silently substituted
- The re-derivation names the ACTUAL pinned sdkconfig path (framework-arduinoespressif32-libs/esp32s3/sdkconfig) — the plan-context's tools/sdk/ guess does not exist in the pioarduino 3.3.9 layout
- REMOVAL CONDITION count arithmetic made explicit and grep-detectable: 8 → 11 (+3 new constructs; the [TWDT] fix's citation already stood)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical] A/B image self-identification line**
- **Found during:** Task 1
- **Issue:** The plan's guard wraps the registration pair in #ifndef/#endif with no #else — a flashed A/B image would print NO registration line, making the two experiment arms indistinguishable on console and the A/B result unattributable if the wrong image is on the board
- **Fix:** Added an #else branch printing `[IDLE0] hook cpu0 DISABLED for A/B (G-01-10)` — compiled out of the default build (behavior byte-identical), documented in §11.4 as the arm-verification step
- **Files modified:** src/main_balloon.cpp
- **Commit:** 7d3abbc

**Total deviations:** 1 auto-fixed (Rule 2 ×1). **Impact:** cosmetic-plus — makes the plan's own A/B experiment attributable at zero cost to the default build. The sdkconfig-path and idf_additions.h findings are the plan's own anticipated contingencies, executed as instructed and recorded in §11 — not deviations.

## Issues Encountered
None — builds 2/2 SUCCESS on the first attempt, harness exit 0, all protected-surface greps clean, both ledger copies extended, JSON valid.

## Next Phase Readiness
- 01-33 (the G-01-7 admission-control/oldest-eviction lever round, per the session-10 discriminator census) is unblocked — this plan touched no G-01-7 surface and shipped no D1 lever (the §10.5 item-5 guard holds: the D1 fix decision waits for the watermark answer).
- 01-34's bench session reads exactly the §11.3 discriminator table: the [STACK] baseline→new-low trajectory (stack-capacity CONFIRM/REFUTE), the fixed `registered=` line (1 expected), the [TWDT] ForCPU(0) line (ESP_OK expected; NOT_FOUND re-opens the sessions-8/9 TG0WDT attribution), plus the retained round-#13 instruments. The A/B recipe (§11.4) is staged if the watermark shows healthy margin through a crash.
- NO bench claims made in this plan — every hardware truth rides 01-34.

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

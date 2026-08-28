---
phase: 01-command-protocol-control
plan: 30
subsystem: firmware-diagnostics
tags: [d1-crash-regression, debug-round-13, instrumentation, idle0, i2c-probe, twdt, lull-audit]
requires: ["01-28 (round #12: WDT topology, Lever A, B1/B2)", "01-29 (session-9 evidence: third crash expression, §8.6 brief)"]
provides: ["debug doc §9 lull-phase audit + determinism + convergence verdict", "[IDLE0] 1 Hz CPU0-liveness instrument", "[I2C] probe-verdict instrument at the BMP280-invalid transition", "[TWDT] boot-time stage-0 subscription instrument", "round-#13 ledger determinations (WINDOWS 15, G-01-10)"]
affects: ["01-31 bench session (the three instruments are its discriminators)"]
tech-stack:
  added: ["esp_register_freertos_idle_hook_for_cpu (esp_freertos_hooks.h)", "i2c_master_probe + i2cBusHandle (IDF 5.5.4 / esp32-hal-i2c.h)", "esp_task_wdt_status + xTaskGetIdleTaskHandle"]
  patterns: ["latch-guarded one-line-per-episode diagnostics (the [MEM]/B1/B2 convention)", "in-source G-01-10 citation + removal condition on every diagnostic line"]
key-files:
  created: []
  modified:
    - ".planning/debug/d1-crash-regression-push-start.md (§9, 397 lines: lull audit, determinism, convergence, instruments, Task 2 record)"
    - "src/main_balloon.cpp ([IDLE0] hook + 1 Hz sample; [I2C] probe in processSensors)"
    - "src/image_tx_manager.cpp ([TWDT] boot one-shot beside B1)"
    - ".planning/WINDOWS.md (entry 15 round-#13 determination; STAYS OPEN)"
    - ".planning/phases/01-command-protocol-control/01-UAT.md (G-01-10 root_cause extended; round-#12-discriminator missing item marked DONE)"
decisions:
  - "Instrument-only (option b) selected at the Task 2 checkpoint, auto-advanced under workflow.auto_advance per the 01-23/01-25/01-26/01-28 convention — fifth pending end-of-phase operator confirmation; selected because §9.5 is elimination-only for the THIRD consecutive round and the honesty guard forbids a third speculative lever after two bench disconfirmations"
  - "INVALID_STATE re-read from deployed source as a bus-level non-DONE COMPLETION (NACK or timeout), not an FSM refusal — which demoted the driver-state-corruption reading to unfalsifiable and made a probe-verdict instrument the honest FSM-state substitute (IDF 5.5.4 has no error-flags accessor)"
  - "[IDLE0] sampled from the loop's existing 1 Hz block rather than a new task — zero new tasks, zero new locks; frozen-episode latch re-armed by a healthy sample"
metrics:
  duration: "~33 min (1946 s)"
  completed: 2026-08-28
  tasks: 3
  commits: 2
actuals:
  tokens: 28230
  tasks: 3
  commits: 2
status: complete
---

# Phase 01 Plan 30: D1 Crash Regression Debug Round #3 (round #13) Summary

**One-liner:** Lull-phase audit eliminates all eight lull candidates (INVALID_STATE = bus-level non-DONE completion, not an FSM refusal), verdicts H-phase-independent best fit — fault maturation time, not executing phase, sets the crash time — and ships three verified instruments ([IDLE0]/[I2C]/[TWDT]) instead of a third speculative lever.

## Requirements Coverage

Frontmatter requirements (copied verbatim): `requirements: [CTRL-01, CTRL-06, PRI-02, IMG-03]`

This plan is a gap-closure debug round riding the Phase 1 command-protocol requirements; it modified no requirement-facing behavior (wire format, pacing, and command handling untouched — harness exit 0 proves the wire unchanged). The round's work product is diagnostic instrumentation and the §9 audit in service of G-01-10 (the D1 crash-regression gap under CTRL-01/CTRL-06/PRI-02/IMG-03's operational surface).

## What Was Built

### Task 1 — §9 audit (commit d78a6e5)

`.planning/debug/d1-crash-regression-push-start.md` §9 (six subsections + the Task 2 record), every session-9 citation line-locatable in balloon3.log/base3.log and every framework claim path-cited against the deployed packages; balloon ELF SHA256 e09dd034… re-verified BEFORE any decode claim.

- **§9.1 Provenance + lull reconstruction:** Wire-0 runs at 100 kHz on the balloon (balloon3.log:58, the driver's own boot line; the 400 kHz upgrade is base-only). The lull (balloon3.log :491-:523) reconstructed activity-by-activity with cadence sources; back-computation puts the IDLE0 starvation window's OPENING at ≤ t≈114 s — the service→lull transition — so the lull is where the wedge's 10 s fuse burned out, not where the wedge formed. The :518-:519 legacy TX provably completed its full E32 handshake seconds before the reset.
- **§9.2 Lull candidate arithmetic:** the deployed-driver ESP_ERR_INVALID_STATE semantics read from source (HAL bus lock is a YIELDING mutex, not a portMUX; trans_queue_depth=0 → sync-only; s_i2c_transaction_start FSM-resets first on a busy bus — INVALID_STATE is returned only when a transaction RAN and its final status ≠ DONE: a NACK or a bus timeout). All eight lull candidates (BMP280 poll, OLED full-frame, GPS drain, NVS persistence, beacon + 2 legacy TXs, GET_STATUS response, Serial console, loop bookkeeping) ELIMINATED — bounded ms-class on CPU1 behind yielding locks; NVS writes fire only at capture allocation and are absent from the lull. NEGATIVE RESULT stated honestly: no project-code lull candidate can starve IDLE0 for 10 s — the starver is below project code in BOTH phases.
- **§9.3 Three-expression determinism:** the shared-constants table across sessions 7/8/9 and the hypothesis table with predicted 01-31 signatures — H-lull REJECTED, H-service WEAKENED, H-phase-independent BEST FIT (fault MATURATION time, not executing phase, sets the crash time).
- **§9.4 Convergence:** reading (b) kernel-contention linkage RANKED #1 (the I2C completion event rides cross-core kernel queue ops; session 7's observed CPU0 spinlock CAS spin is the same lock class; counter-evidence recorded, not explained away — the log ran to :520, so the contention is intermittent/marginal, and a plain NACK is not excludable from the log alone, which is exactly what [I2C] exposes); reading (a) driver-state corruption demoted to unfalsifiable (no FSM state exists to corrupt); reading (c) coincidence #3. Proximity question addressed: one mojibake line per session is print-timing luck at n=1 — not a trend, no instrument added beyond [IDLE0]'s 1 s terminal clock.
- **§9.5 Ranked cause + honest limit:** CPU0 scheduler/interrupt-delivery stall, kernel level, maturation-timed — ELIMINATION-ONLY for the third consecutive round; no mechanism site in project code is named.
- **§9.6 Menu disposition + instruments:** three instruments specified, each verified implementable against the deployed headers; no lever.

### Task 2 — checkpoint record (auto-advanced)

Recorded inside §9 ("FIX SHAPE SELECTED (round #13 Task 2 checkpoint record)"): option (b) instrument-only, auto-advanced under `workflow.auto_advance` per the 01-23/01-25/01-26/01-28 convention — the FIFTH pending end-of-phase operator confirmation. No separate commit (house convention records the selection inside the debug doc).

### Task 3 — instruments + ledgers (commit 1a737dd)

- **[IDLE0]** (src/main_balloon.cpp): `esp_register_freertos_idle_hook_for_cpu(idle0TickHook, 0)` registered in setup() before subsystem init (boot line `[IDLE0] hook cpu0 registered=%d (G-01-10)`); volatile tick counter sampled 1 Hz from the loop's existing 1 Hz block; delta==0 across a full second latches ONE `[IDLE0] frozen - 0 idle ticks in last %lu ms, t=%lu ms (G-01-10)` line, re-armed by a healthy sample. Fires ~9 s BEFORE a stage-1 reset instead of only at it.
- **[I2C]** (src/main_balloon.cpp processSensors): on the BMP280-invalid transition, one bounded `i2c_master_probe((i2c_master_bus_handle_t)i2cBusHandle(0), 0x76, 50)` (IDF 5.5.4 has no error-flags accessor — probe verdict is the honest substitute, stated in-source), latch-guarded with episode/probe counters: `[I2C] BMP280 invalid t=%lu ms - probe 0x76 -> ACK|NACK|TIMEOUT (episodes=… probes=…) (G-01-10)` + a recovery line on re-arm. ACK = transient (bus alive, device answered); NACK = bus alive, device silent; TIMEOUT = bus wedged — §9.4's readings discriminate on this verdict at the next occurrence.
- **[TWDT]** (src/image_tx_manager.cpp begin, beside B1): `esp_task_wdt_status(xTaskGetIdleTaskHandle())` one-shot — expected ESP_OK per sdkconfig (IDLE0 subscribed, stage-0 INT @5 s PANIC=y); turns stage-0-inferred-from-silence into a positive boot fact, so a future SILENT rst:0x7 is affirmative stage-0-death evidence.
- All three carry the G-01-10 citation and the in-source removal condition (strip with [MEM]/B1/B2 after G-01-10 closes on bench evidence); 4 `REMOVAL CONDITION` occurrences per file region verified.
- **Ledgers:** WINDOWS entry 15 reason extended with the round-#13 determination in BOTH the table row and the JSON block — STAYS OPEN, counts unchanged (2 open / 17 fixed / 19 total); 01-UAT.md G-01-10 root_cause extended with the ROUND-#13 DETERMINATION and the round-#12-discriminator missing item marked DONE (01-30 debug round #3 / round #13) with §9's verdict.
- **Untouched by explicit disposition (verified by grep):** CMD_TX_CHANNEL_QUIET_MS = 750; Lever A (e32_lora.cpp delay(1) drain); [MEM]/[LOOP]/[BOOT] (B1/B2); the D2 receipt-ever flag; the round-#10 discriminator lines.

## Verification

- Task 1 verify chain (verbatim from the plan): PASS (`## 9` + lull + INVALID_STATE + coincidence + e09dd034).
- Builds: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` → 2/2 SUCCESS.
- Harness: `node scripts/verify_protocol_roundtrip.mjs` → all checks PASS, exit 0.
- Protected greps: quiet-ms 750 PASS; `open_count: 2` PASS; round-#13 mention in WINDOWS.md PASS; [IDLE0]/[I2C]/[TWDT] + (G-01-10) citations PASS; Lever A / [MEM]/[LOOP]/[BOOT] / D2 receipt-ever PASS.
- Post-commit deletion check: no deletions in either commit.

## Deviations from Plan

None — plan executed as written. Note (not a deviation): four files listed in the plan's frontmatter `files_modified` (src/e32_lora.cpp, include/e32_lora.h, include/image_protocol.h, platformio.ini) were intentionally untouched — they were the lever's candidate surface, and the Task 2 instrument-only selection named no lever, so nothing touched them. The plan's Task 3 verify chain anticipated exactly this outcome (its protected greps assert those surfaces UNCHANGED).

## Known Stubs

None — the three instruments are fully wired production diagnostics (real hook registration, real probe against the live bus handle, real esp_task_wdt_status call), each with its removal condition in-source.

## Bench Claims

NONE — per the plan's ledger-honesty must-have: the crash truth flips only on 01-31's hardware evidence. WINDOWS 15 and G-01-10 remain OPEN.

## Self-Check: PASSED

- FOUND: .planning/debug/d1-crash-regression-push-start.md §9 (grep `^## 9` + all four content greps PASS)
- FOUND: commit d78a6e5 (Task 1 audit)
- FOUND: commit 1a737dd (Task 3 instruments + ledgers)
- FOUND: src/main_balloon.cpp [IDLE0]+[I2C] lines; src/image_tx_manager.cpp [TWDT] line
- FOUND: WINDOWS.md round-#13 determination + open_count: 2; 01-UAT.md G-01-10 ROUND-#13 DETERMINATION + DONE item

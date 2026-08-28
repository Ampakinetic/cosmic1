---
phase: 01-command-protocol-control
plan: 31
subsystem: firmware-bench-evidence
tags: [esp32-s3, lora, image-transfer, crash-debugging, bench-evidence, idle-task, stack-canary]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: round-#13 instrument package (01-30: [IDLE0] tick counter, [I2C] probe verdict, [TWDT] boot status), round-#10 series-A discriminators, D1 debug doc §1-§9, G-01-7 lever chain (01-17/01-18/01-21/01-22)
provides:
  - D1 fourth-expression evidence: an IDLE0 stack-canary panic with a full dump, starvation DISCONFIRMED by the [IDLE0] tick instrument (debug doc §10)
  - [TWDT] runtime reading ESP_ERR_NOT_FOUND — §7's "only IDLE0 feeds the TWDT" premise under re-examination (wrong-handle bug vs genuinely-unsubscribed)
  - First-ever full engagement of the round-#10 series-A discriminators; G-01-7 residual NAMED (depth-3 queue cycling evicts the base-activated prior full)
  - Round-#14 routing: [STACK] IDLE0-watermark instrument + [TWDT] handle fix + [I2C] write-path coverage; NO lever until [STACK] answers
affects: [01-32 D1 debug round #4, G-01-7 admission-control lever round, phase-close gates]

# Actuals (#2632)
actuals:
  tokens: 8600        # chars/4 over the 4 ledger files + this summary (166 insertions/13 deletions + summary)
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "esp_err_t-to-bool printf trap: assigning esp_register_freertos_idle_hook_for_cpu()'s esp_err_t return to bool prints 0 (ESP_OK) on SUCCESS — print err==ESP_OK instead"
    - "Instrument-handle pitfall: xTaskGetIdleTaskHandle() from CPU1 setup context is ambiguous on SMP — use xTaskGetIdleTaskHandleForCPU(0)"

key-files:
  created:
    - ".planning/phases/01-command-protocol-control/01-31-SUMMARY.md"
  modified:
    - ".planning/debug/d1-crash-regression-push-start.md (§10 appended — session-10 crash evidence)"
    - ".planning/phases/01-command-protocol-control/01-UAT.md (G-01-10/G-01-7 session-10 extensions; Test 3/Test 4 notes)"
    - ".planning/WINDOWS.md (entries 15/3 table+JSON session-10 clauses; last_updated)"
    - ".planning/STATE.md (Current Status, Progress, Current Phase, Next Steps round-#4 brief)"

key-decisions:
  - "D1 stays OPEN with the FOURTH expression recorded — never a forced close (plan honesty clause); the crash is a direct-evidence canary panic, not a silent-reset inference"
  - "Starvation is DISCONFIRMED for the session-10 crash: the [IDLE0] tick counter was armed and ran with zero frozen episodes to the crash instant — the first positive instrument answer of the D1 campaign"
  - "No lever landed this round: round #14 gets the [STACK] watermark instrument FIRST (uxTaskGetStackHighWaterMark from the idle hook); three elimination-only rounds + two disconfirmed levers make lever-speculation ahead of the watermark evidence dishonest"
  - "G-01-7 residual named FROM discriminator lines only (no code-level reasoning): depth-3 queue cycling — the newest capture's window request evicts the previous capture's base-activated full-pull; next lever class: admission control or oldest-queued-full eviction"
  - "Instrument defects discovered this session ([TWDT] handle, [IDLE0] printf, [I2C] coverage) are ROUTED to round #14, not fixed inline — the plan prohibits fixing bench defects inside this plan"

patterns-established:
  - "Instrument-answers ledger pattern: each §9.6 instrument's session reading recorded as evidence (armed/negative/coverage-gap), not assumed"

requirements-completed: []  # failed bench session — the 01-24/01-27/01-29 convention: no requirement claims ride a failed session

# Metrics
duration: multi-segment (continued across an agent restart; evidence analysis + ledger updates completed this segment)
completed: 2026-08-28
status: complete
---

# Phase 1 Plan 31: Bench session #10 — round-#13 hardware acceptance Summary

**D1's fourth expression captured with full-dump precision — an IDLE0 stack-canary panic that the round-#13 [IDLE0] instrument proves was NOT starvation — and the round-#10 series-A discriminators finally engaged, naming G-01-7's residual: depth-3 queue cycling.**

## Performance

- **Duration:** not precisely tracked (execution continued across an agent restart; the bench session itself was operator-run)
- **Completed:** 2026-08-28
- **Tasks:** 2 of 2 (Task 1 checkpoint satisfied by the operator-run bench session; Task 2 ledger updates executed)
- **Files modified:** 4 ledger files + this summary
- **Session evidence:** balloon4.log (1,768 lines) / base4.log (740 lines), repo root — retained UNTRACKED per the standing convention (prior session logs balloon.log..balloon3.log are likewise untracked; the plan commits ledgers separately from log retention)

## Accomplishments

- **D1 fourth expression recorded with the campaign's best crash evidence:** `Guru Meditation Error: Core 0 panic'ed (Unhandled debug exception)` / `Debug exception reason: Stack canary watchpoint triggered (IDLE0)` (balloon4.log:1428-:1429) at chunk 12/14 of image 43's re-armed 14-chunk FULL window (:1380→:1427), EXCVADDR 0x0, backtrace corrupted, zero project frames; boot 2 `[BOOT] reset-cause: PANIC` (:1576), `rst:0xc` (:1483), clean re-init, no second crash. Full record appended to the debug doc as §10 (provenance/ELF-SHA-first convention).
- **The round-#13 instruments ANSWERED — the campaign's biggest re-scope:** [IDLE0] tick counter ARMED both boots (the `registered=0` lines are an esp_err_t→bool printf bug — `esp_register_freertos_idle_hook_for_cpu` returns esp_err_t; ESP_OK=0 assigned to bool prints 0 on SUCCESS; header verified on-disk) with ZERO `frozen` lines whole-console → **starvation DISCONFIRMED** for this crash (IDLE0 alive and scheduled to the crash instant). [TWDT] returned `ESP_ERR_NOT_FOUND` both boots (:105/:1577) — either a wrong-handle instrument bug (`xTaskGetIdleTaskHandle()` from CPU1 setup context) or IDLE0 genuinely not TWDT-subscribed, putting §7's load-bearing WDT premise under re-examination. [I2C] did not fire at its trigger opportunity (write-path coverage gap). B1 delivered the campaign's first PANIC reading; B2 zero; [MEM] healthy (loopTask stackHW 5772 constant — the breached stack is IDLE0's, which [MEM] does not monitor).
- **Series A finally ran and the round-#10 discriminators engaged for the first time in nine attempts:** 6/8 finalize verdicts COMPLETE — the best series-A partial ever (image 40 both kinds COMPLETE base4.log:67-68/:156-157 — the first-post-boot push survived a THIRD consecutive session; image 42 both kinds COMPLETE :269-:270/:425-:426) — with seq=9 hitting the forbidden `timeout after 3 retries` (:250) and every discriminator firing: budget re-armed ×2 (:737/:815), FULL-manifest re-announces ×3 (:690/:799/:803), holds ×4 all genuine (:460/:733/:887/:1644), base deadline activation ×1 (:428), class-5 evictions ×3 (:816-:817/:1295), 7 evicted-rejections (:1003-:1162).
- **G-01-7's residual is now NAMED by the discriminators themselves (every step a quoted line):** the depth-3 queue cycles under the burst — the base's deadline activated image 41's full-pull (:428), image 42's window request evicted the pending entry (:817), the seven follow-up requests were honestly rejected → full 0/31, while the newest capture's full (42) completed. The levers work as designed; the design's queue depth vs the burst pattern is the mismatch.
- **CIF wire-8 half-executed cleanly:** SET_RESOLUTION ACKed (base4.log:558-:560), image 43 captured CIF-class (full 23800 B / 119 chunks vs QVGA 6136 B / 31; balloon4.log:1291), thumb COMPLETE 7/7, ZERO FB-OVF lines in either console.
- **WR-01 GREEN, WR-02 clean:** zero `Critical battery` lines both consoles, beacons batt=valid ×65 / batt=invalid ×0; zero `health check failed` lines.

## Task Commits

1. **Task 1: Bench session #10 (checkpoint:human-verify, gate blocking)** — satisfied by the operator-run session; consoles balloon4.log/base4.log retained (naming deviation from balloon15/base10 recorded per the 01-24 convention). The session FAILED its D1 acceptance bar (crash) — the fail path is the plan's own resume-signal branch: "a crash appends to the debug doc §10 and re-opens D1."
2. **Task 2: Flip the ledgers on evidence** - `a488661` (docs)

**Plan metadata:** (final docs commit — this summary + STATE.md reconciliation)

## Files Created/Modified

- `.planning/debug/d1-crash-regression-push-start.md` — §10 appended: session-10 provenance (ELF SHA acce78241 console-verified), event map, instrument answers, i2cWrite/mojibake census, the labeled stack-capacity hypothesis, round-#14 routing
- `.planning/phases/01-command-protocol-control/01-UAT.md` — G-01-10 root_cause session-10 extension (stays OPEN, fourth expression); G-01-7 root_cause session-10 update (stays OPEN, residual named); Test 3 note (CIF half-done, SC-3/WR-03/QVGA/LOOK ninth round riding); Test 4 note (WR-03 ninth round riding)
- `.planning/WINDOWS.md` — entry 15 (D1) and entry 3 (G-01-7) session-10 clauses in BOTH the markdown table and JSON copies; `last_updated` bumped; counts unchanged **2 open / 17 fixed / 19 total** (failed session — zero flips)
- `.planning/STATE.md` — Current Status round outcome, Progress list entry (01-30/01-31), Current Phase status, Next Steps item 1 replaced with the round-#4 brief

## Decisions Made

- **D1 re-opened, not closed** — the plan's honesty clause is explicit: a crashed session re-opens D1 with the dump appended to §10 and records requirements-completed: [].
- **No lever this round** — the [STACK] watermark instrument lands FIRST in round #14; a one-session CONFIRM/REFUTE of the stack-marginality hypothesis precedes any lever selection, per the standing guard against a third speculative lever.
- **Session-10 crash attribution discipline** — the image 43 full INCOMPLETE (14/119) is recorded CRASH-CAUSED per T-01-31-03 (never read as a G-01-7 transfer defect); the post-crash pass-exhaustion is recovery behavior after reboot.
- **Ledger flips: zero** — counts reconcile at 2 open / 17 fixed / 19 total, matching the 01-24/01-27/01-29 failed-session convention.

## Deviations from Plan

### Plan-tolerated deviations (recorded, per plan text)

1. **Log naming** — consoles retained as balloon4.log/base4.log rather than the plan's balloon15.log/base10.log request ("deviation-tolerant and recorded per the 01-24 convention").
2. **Steps not reached** — D1 class 3 (≥3-min lull dwell), WR-03 cadence discriminator, SC-3 visible-effect pairs, QVGA restore (wire 6), and the dashboard LOOK glance never ran: the session aborted at the crash (classes/steps ordered after the crash point). All ride the next bench moment (SC-3/WR-03 ninth round).

### Auto-fixed Issues

None — the plan prohibits fixing bench defects inside this plan ("this round's code work ended at 01-30"). Three instrument defects were DISCOVERED and ROUTED instead:

**1. [Rule 3-class discovery, routed] [TWDT] instrument handle ambiguity**
- **Found during:** Task 1 evidence analysis
- **Issue:** `esp_task_wdt_status(xTaskGetIdleTaskHandle())` (image_tx_manager.cpp:172) returns ESP_ERR_NOT_FOUND — the call runs in CPU1 setup context and the no-arg handle getter is ambiguous on SMP
- **Routing:** round #14 must call `xTaskGetIdleTaskHandleForCPU(0)` explicitly and re-verify the deployed WDT config (debug doc §10.3, STATE.md round-#4 brief)
- **Committed in:** documented in `a488661` (no code change)

**2. [Routed] [IDLE0] registered printf bug** — esp_err_t→bool conversion prints 0 (ESP_OK) on SUCCESS; fix is `print err == ESP_OK` (debug doc §10.3)
**3. [Routed] [I2C] write-path coverage gap** — the instrument fires on the invalid-READING transition (main_balloon.cpp:836-866) but the session's i2cWrite failure was on the HAL WRITE path; extend the probe to the write-failure return (debug doc §10.3)

**Total deviations:** 0 inline auto-fixes; 3 routed instrument defects; 2 plan-tolerated deviations recorded.

## Deferred Issues

- **[TWDT]/WDT-chain re-examination:** if IDLE0 is genuinely not TWDT-subscribed in the deployed image, §7's "rst:0x7 = TWDT stage-1 backstop of IDLE0 starvation" reading for sessions 8/9 needs re-attribution (no sdkconfig artifact is retained in .pio/build/esp32-s3-balloon/ to settle it from artifacts) — round #14 step 2.
- **G-01-7 admission-control lever:** named residual (depth-3 queue cycling) awaits its lever round after D1's [STACK] instrument answers.
- **Riding clauses ninth round:** SC-3 visible-effect pairs, WR-03 cadence discriminator (SC-5 advisory still not discharged), QVGA restore, dashboard LOOK glance (d8ba14e gallery-detail resLabel/filesize still unlooked-at after six sessions).
- **Five pending auto-advanced disposition confirmations** (01-23, 01-25, 01-26, 01-28, 01-30) remain end-of-phase operator decisions.

## Known Stubs

None — this plan created no code. The three routed instrument defects are debug-doc-tracked (§10.3/§10.5), not stubs.

## Self-Check: PASSED

- Found: .planning/debug/d1-crash-regression-push-start.md §10 (appended, 1313→~1400 lines)
- Found: .planning/phases/01-command-protocol-control/01-UAT.md session-10 extensions (G-01-10, G-01-7, Test 3, Test 4)
- Found: .planning/WINDOWS.md session-10 clauses (table + JSON, entries 15 and 3); counts 2/17/19 unchanged
- Found: .planning/STATE.md round-#4 brief and round outcome
- Found: commit a488661 (docs(01-31): bench session #10 evidence + ledger flips)
- Plan Task 2 automated verify: PASS (grep gates on 01-31, secure-phase, G-01-10, WINDOWS JSON ids 15/3)
- Session logs retained untracked per convention: balloon4.log (1,768 lines), base4.log (740 lines)

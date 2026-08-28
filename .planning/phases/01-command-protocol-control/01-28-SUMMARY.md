---
phase: "01"
plan: "28"
subsystem: debug-fix
tags: [crash-regression, watchdog, wdt-topology, evidence, discriminators, ledger-discipline]
requires:
  - "01-27 session-8 crash evidence (balloon2.log/base2.log + the §6 record that named the round-#12 axis)"
  - "01-25 D1 debug round (the §3 fix-shape precedent + the [MEM] instrumentation this round keeps)"
provides:
  - "Debug doc §7 (round #12): the deployed build's WDT topology pinned from config + framework sources, the post-chunk window reconstructed, all eight project-code candidates eliminated, the ONE-mechanism-family verdict, and a ranked root cause with the honest limit"
  - "The round-#12 fix: yielding bounded TX-drain in E32LoRa::transmit (replaces the unbounded serial->flush() busy-spin) — the fatal path's only non-yielding stretch removed"
  - "Bench discriminators for 01-29: 'ImageTx: [BOOT] reset-cause: <name> (G-01-10)' (every boot) and 'ImageTx: [LOOP] slow pass gap %lu ms (G-01-10)' (latched, IMG_LOOP_SLOW_PASS_MS 2500)"
  - "The Task 2 fix-shape checkpoint record (option (a)+(b), auto-advanced, pending end-of-phase operator confirmation — joins the three existing)"
affects:
  - ".planning/debug/d1-crash-regression-push-start.md"
  - ".planning/WINDOWS.md"
  - ".planning/phases/01-command-protocol-control/01-UAT.md"
  - ".planning/phases/01-command-protocol-control/01-VERIFICATION.md"
  - "src/e32_lora.cpp"
  - "include/e32_lora.h"
  - "src/image_tx_manager.cpp"
  - "include/image_tx_manager.h"
  - "include/image_protocol.h"
tech-stack:
  added: []
  patterns:
    - "WDT-topology determination from deployed build artifacts (prebuilt sdkconfig + framework-esp-idf sources + firmware.map object provenance)"
    - "constraint-triple reasoning (ticks alive / IDLE0 dead / stage-0 INT dead) as the pre-bench elimination bound"
    - "hazard-removal-not-cure fix scoping (§7.3 elimination recorded in-source next to the fix)"
key-files:
  created: []
  modified:
    - ".planning/debug/d1-crash-regression-push-start.md"
    - "include/e32_lora.h"
    - "src/e32_lora.cpp"
    - "include/image_tx_manager.h"
    - "src/image_tx_manager.cpp"
    - "include/image_protocol.h"
    - ".planning/WINDOWS.md"
    - ".planning/phases/01-command-protocol-control/01-UAT.md"
decisions:
  - "TG0WDT_SYS_RST pinned as the TASK watchdog's stage-1 hardware backstop (MWDT0/TG0, 10 s unfed, no print path exists at stage-1) — session 8's silence is structural, and in this firmware only IDLE0 feeds the TWDT (loopTask unsubscribed, pinned CPU1), so the reset PROVES IDLE0 starved ≥10 s on CPU0"
  - "Verdict on the §6.5 question: ONE mechanism family, two expressions — session-7's directly-observed kernel-portMUX CAS spin (§1.4) and session-8's tick_hook Saved PC sample the same tick-ISR chain (increment phase vs hook phase of xPortSysTickHandler); session-7's corruption trio read as a co-effect (inference, honestly labeled)"
  - "All eight project-code candidates ELIMINATED (§7.3): serial->flush() is the sole non-yielding stretch but holds only a yielding semaphore and runs on CPU1 — it cannot directly starve CPU0's IDLE0; the starver is below project-code visibility, so the honest limit (01-25 §3 convention) is recorded on the ranked cause"
  - "Task 2 fix shape (a)+(b) combined, auto-advanced under auto_advance per the 01-23/01-25/01-26 convention: the named lever (yielding bounded TX-drain, removing the fatal path's only unbounded no-yield stretch — hazard removal, NOT a claimed cure) PLUS the B1/B2 discriminators 01-29 needs to classify any recurrence"
  - "Rejected levers recorded with reasons (§7.6): WDT-config change in platformio.ini (masks the symptom, weakens every future bench tripwire) and serviceWindowChunk pacing (protected surface; the crash is not pacing-correlated — six identical-cadence windows served clean)"
  - "§6.4 mojibake count corrected: balloon2.log:155 IS one corruption-class line (the 'ZERO U+FFFD' grep matched the wrong encoding) — present but weak and non-adjacent"
metrics:
  duration: "~46 min executor session (research + 3 commits)"
  completed: 2026-08-28
status: complete
requirements-completed: []
estimate:
  tokens: 26000
actuals:
  tokens: 16589
  tasks: 3
  commits: 3
coverage:
  - id: T1-wdt-topology
    description: "WDT topology determined from the deployed build and recorded in §7.1 (which watchdog a silent TG0WDT_SYS_RST maps to, its period, its panic-vs-silent behavior, and why session 8 reset silently where session 7 printed)"
    requirement: "CTRL-06"
    verification:
      - kind: machine_checkable
        ref: "grep '^## 7' + 'tick_hook' + 'one mechanism' in .planning/debug/d1-crash-regression-push-start.md (Task 1 verify PASS); §7.1 cites sdkconfig:2172-2180, task_wdt_impl_timergroup.c:28-33/:124-126, int_wdt.c:32-41/:104-127, task_wdt.c:491"
        status: pass
  - id: T3-fix-wired-green
    description: "The Task 2-selected fix wired at the §7-named site with discriminators shipped; both targets build and the harness exits 0; protected surfaces byte-identical"
    requirement: "CTRL-06"
    verification:
      - kind: machine_checkable
        ref: "pio run 2/2 SUCCESS; verify_protocol_roundtrip.mjs exit 0; CMD_TX_CHANNEL_QUIET_MS = 750 unchanged; [MEM] + round-#10 discriminator lines present; commit 6ca9a36"
        status: pass
  - id: ledger-honesty
    description: "WINDOWS 15 and G-01-10 carry the round-#12 audit and stay OPEN — no code-level flip; the SUMMARY makes no bench claims"
    requirement: "CTRL-06"
    verification:
      - kind: machine_checkable
        ref: "open_count: 2 unchanged in .planning/WINDOWS.md frontmatter; 'round-#12' present in both ledgers; G-01-10 status: open with the missing-list debug-round item marked DONE (01-28)"
        status: pass
---

# Phase 01 Plan 28: D1 Debug Round #2 Summary

**One-liner:** Round-#12 root-cause audit + evidence-scoped fix: the deployed build's watchdog topology pinned (TG0WDT_SYS_RST = TWDT stage-1 silent 10 s backstop; only IDLE0 feeds it), all project-code starver candidates eliminated, session-7/8 reconciled as ONE mechanism family, and the yielding TX-drain fix + [BOOT]/[LOOP] bench discriminators landed green for 01-29.

## What Was Built

### Task 1 — the §7 audit (commit 60315b5)

Appended `## 7` to `.planning/debug/d1-crash-regression-push-start.md` (six subsections, every framework/config claim carrying a re-derivable path):

- **§7.1 WDT topology** — pinned from the deployed build's own artifacts (prebuilt `sdkconfig`, IDF 5.5.4 sources in framework-espidf v3.50504, arduino core 3.3.9, `firmware.map` object provenance): TWDT = MWDT0/TG0 (stage-0 INT @5 s printing `Tasks currently running:`; stage-1 RESET_SYSTEM @10 s with **no print path at all**), IWDT = MWDT1/TG1 (300/600 ms, fed only from tick_hook with the CPU1 interlock, hook phase runs before xTaskIncrementTick). In this firmware only IDLE0 feeds the TWDT. The audit's core result is the constraint triple: ticks alive on both cores to the end, IDLE0 scheduling dead on CPU0 ≥10 s, the stage-0 interrupt never serviced — a stall below project-code visibility.
- **§7.2 window reconstruction** — :1226-1227 prove the fatal transmit completed; the ~1102 ms/pass sustained-service cadence (Performance :915→:1189 arithmetic) puts 8-9 loop passes and beacon seq 47 inside the 10 s window; the log's terminal emptiness (not even one BMP280 line) disfavors every project-visible candidate.
- **§7.3 candidate arithmetic** — all eight candidates ELIMINATED with duration/masks/lock/cache properties (flush = 226 ms busy-spin but no kernel lock and on CPU1; AUX waits delay(10)-yield; OLED ~100 ms driver-based; sensors/beacon/memcpy/printf all ms-class or yielding). Includes the NB that an unbounded flush hang would hang loopTask on CPU1 forever (matching the log's terminal silence) but still cannot produce a TG0 stage-1 reset because IDLE1 is not subscribed.
- **§7.4 verdict** — ONE mechanism family, two expressions (session-7 "loud" IWDT panic + corruption co-effects vs session-8 "silent" stage-1 with ticks sustained); discriminating evidence named; §6.4's mojibake count corrected (balloon2.log:155 is one corruption-class line, non-adjacent); session-6 reconciliation recorded honestly as a weak point (its raw logs are not retained).
- **§7.5 ranked cause** — CPU0 scheduler/interrupt-delivery stall (kernel level) during sustained FULL-window TX service, with the 01-25 §3 honest limit: the stall's inner structure is not observable pre-bench; ranking is by elimination + blast radius only.
- **§7.6 lever menu** — Lever A + discriminators B1/B2 with removal conditions; rejected candidates (WDT-config lever, pacing) dispositioned with reasons.

### Task 2 — fix-shape checkpoint record (commit de6a278)

Auto-advanced under `auto_advance: true` per the 01-23/01-25/01-26 convention: **option (a)+(b) combined** — the named lever AND the discriminators — recorded in the FIX SHAPE SELECTED subsection closing §7, pending end-of-phase operator confirmation (joins the three existing pending confirmations).

### Task 3 — the fix + discriminators + ledgers (commit 6ca9a36)

- **Lever A** (`src/e32_lora.cpp`): `serial->flush()` in transmit replaced by a delay(1)-polled `uart_ll_is_tx_idle(UART_LL_GET_HW(uartPort))` drain bounded at 1000 ms; the UART port arrives as a new defaulted `begin()` parameter (`uartPort = 2`; both boards' call sites unchanged — balloon `&Serial2`, basestation `HardwareSerial LoRaSerial(2)`); the in-source comment carries the honest scope (the flush was ELIMINATED as the direct starver — hazard removal, not a claimed cure). The two remaining `serial->flush()` calls are boot-time config-path flushes (3-6 bytes, ms-class), outside the runtime TX path.
- **Discriminator B1** (`src/image_tx_manager.cpp` begin): `ImageTx: [BOOT] reset-cause: <name> (G-01-10)` every boot — local 16-case map over `esp_reset_reason()` (IDF 5.5.4 prebuilt headers expose no `esp_reset_reason_to_name()`; verified).
- **Discriminator B2** (process() top): `ImageTx: [LOOP] slow pass gap %lu ms (G-01-10)` when the process()-pass gap exceeds `IMG_LOOP_SLOW_PASS_MS = 2500` (new constant in `include/image_protocol.h` with the full rationale), latched once per episode, re-armed on a normal pass. 01-29's discriminating read: a TG0WDT reset with NO preceding [LOOP] line = the session-8 CPU0-side signature; [LOOP] lines before a reset = a loop-visible stall class.
- **Ledgers honestly extended, both stay OPEN**: WINDOWS entry 15 carries the round-#12 determination (frontmatter counts unchanged 2/17/19); 01-UAT G-01-10 root_cause extended and the missing-list debug-round item marked DONE. Neither ledger claims the crash fixed at code level.

## Verification Evidence

- Task 1 automated verify: PASS (`^## 7` present; "one mechanism" verdict; tick_hook; flush() arithmetic).
- Task 3 automated verify: PASS — `pio run -e esp32-s3-balloon -e esp32-s3-basestation` **2/2 SUCCESS**; `node scripts/verify_protocol_roundtrip.mjs` **exit 0** (all wire-format checks PASS); `CMD_TX_CHANNEL_QUIET_MS = 750` unchanged; `open_count: 2` unchanged; `round-#12` present in WINDOWS.md.
- Protected surfaces: [MEM] instrumentation (7 sites) and round-#10 discriminator lines (busy-hold / budget re-armed / deadline-release) present and untouched; no wire-format, quiet-gate, or D-05/D-07 pacing change.
- Audit provenance: the deployed ELF (SHA256 c53635e181…) was re-confirmed and all decoding ran BEFORE any code change (the key_links requirement); the rebuild happened only after the §7 audit was committed.

## Deviations from Plan

None - plan executed exactly as written. Notes within scope:

- `platformio.ini` was listed in files_modified as a candidate but was deliberately NOT touched — §7.6 dispositioned the WDT-config lever as REJECTED (masks the symptom, weakens future bench tripwires); the actually-touched subset is named above, as the plan's artifact spec requires.
- One research correction folded into §7.4 as a documented audit finding (not a deviation): §6.4's "zero mojibake" claim was wrong (balloon2.log:155); corrected in place with the encoding explanation.

## Known Stubs

None — no stubs introduced; every changed surface is fully wired.

## Honest Limits (for 01-29's bench protocol)

- The fix removes the fatal path's only unbounded no-yield stretch; it does NOT claim to remove the kernel-level stall §7.5 ranks (that stall's inner structure is unobservable pre-bench).
- Crash-truth closure for G-01-10 / WINDOWS 15 remains 01-29's hardware session: zero resets under the crashing pattern including sustained FULL-window service, reading the B1/B2 discriminators.
- Session-6 reconciliation is a weak point (raw logs not retained) — recorded as such in §7.4.

## Commits

- 60315b5 — docs(01-28): D1 debug round #2 — audit §7
- de6a278 — docs(01-28): round #12 Task 2 fix-shape selection record
- 6ca9a36 — fix(01-28): yielding bounded TX-drain + [BOOT]/[LOOP] bench discriminators

## Self-Check: PASSED

All 9 modified/created files exist on disk; all 3 task commits (60315b5, de6a278, 6ca9a36) verified in git log.

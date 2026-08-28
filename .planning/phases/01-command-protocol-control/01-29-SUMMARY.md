---
phase: "01"
plan: "29"
subsystem: bench-verification
tags: [bench, uat, evidence, crash-regression, watchdog, ledger-discipline]
requires:
  - "01-28 D1 debug round #2 (yielding bounded TX-drain + [BOOT]/[LOOP] bench discriminators)"
provides:
  - "Honest session-9 record in 01-UAT.md (bench FAILED at protocol step 3, the sustained full-service step; D1 re-opened with its third expression)"
  - "Session-9 crash evidence appended to .planning/debug/d1-crash-regression-push-start.md §8 (ELF-provenance-verified decode, [MEM] verdict, crash-adjacent i2cWrite with on-line mojibake, round-#3 brief)"
  - "D2 hold-line and WR-02 healthy-boot regression watches bench-verified green on the round-#12 firmware"
affects:
  - ".planning/phases/01-command-protocol-control/01-UAT.md"
  - ".planning/WINDOWS.md"
  - ".planning/STATE.md"
  - ".planning/debug/d1-crash-regression-push-start.md"
tech-stack:
  added: []
  patterns:
    - "citation-convention bench recording (every verdict carries a log line number)"
    - "ELF-SHA-first decode discipline (provenance verified before any decoded frame is trusted)"
    - "byte-level mojibake verification (raw-bytes grep over the replacement-char grep the §7.4 trap documents)"
key-files:
  created: []
  modified:
    - ".planning/debug/d1-crash-regression-push-start.md"
    - ".planning/phases/01-command-protocol-control/01-UAT.md"
    - ".planning/WINDOWS.md"
    - ".planning/STATE.md"
decisions:
  - "Record session 9 as FAILED with the D1 third expression (nothing forced): the 01-28 yielding bounded TX-drain fix is disconfirmed as sufficient — the reset recurred at the SAME sustained full-service step that killed session 8, in the inter-window lull after a clean window"
  - "Decode the session-9 Saved PC against the deployed ELF (SHA256 e09dd034a5ab… verified first): 0x4037c7fa = esp_vApplicationTickHook freertos_hooks.c:34 — one frame ABOVE session-8's tick_hook (int_wdt.c:111), the same WDT tick-ISR dispatch chain, zero project frames"
  - "Carry the crash-adjacent i2cWrite (:520, ESP_ERR_INVALID_STATE with the mojibake ON that very line, hex-dump verified) as discriminating evidence of the session-7 corruption class returning at the crash moment — correcting the incoming packet's mojibake=0, which was the twice-documented §7.4 grep trap"
  - "Keep D2 fixed and WR-02 green on their own clean evidence (D2's exactly-2 fires both genuine by timestamp correlation :492/:683; zero health-check-failed at both boots) — the hold-line and boot truths are independent of the crash and both watched green on the round-#12 firmware"
  - "Keep G-01-7 unjudgeable (series never ran) and attribute image 39's INCOMPLETE 15/36 (base3.log:194) to the crash, not to burst delivery"
metrics:
  duration: "~2.5h across executor pre-flight + operator bench session #9 + continuation closeout"
  completed: 2026-08-28
status: complete
requirements-completed: []
estimate:
  tokens: 9000
actuals:
  tokens: 53965
  tasks: 2
  commits: 2
coverage:
  - id: D1
    description: "D1 crash-regression acceptance (G-01-10 / WINDOWS 15): spaced smoke AND sustained full-service AND series A complete with zero crash signatures"
    requirement: "CTRL-06"
    verification:
      - kind: manual_procedural
        ref: "balloon3.log:520-524 — crash-adjacent i2cWrite ESP_ERR_INVALID_STATE (:520, mojibake on-line) → ROM banner (:521-522) → rst:0x7 TG0WDT_SYS_RST (:523) → Saved PC:0x4037c7fa (:524); reset-cause TASK_WDT (:615). Hit at protocol step 3 (sustained full-service) in the inter-window lull after image 39's window served clean 16/16 on air (:427-491; thumb COMPLETE 9/9 base3.log:79)"
        status: fail
    human_judgment: true
    rationale: "Operator bench session on hardware radios; the verdict is a console-evidence judgment that automation cannot make"
  - id: D2
    description: "D2 hold-line regression watch (G-01-11 / WINDOWS 16, fixed at 01-27): the 're-announce held' line fires only on genuine inbound traffic on the round-#12 firmware"
    requirement: "CTRL-06"
    verification:
      - kind: manual_procedural
        ref: "Exactly 2 fires in balloon3.log (:492, :683), both genuine by timestamp correlation; zero spurious fires at either boot window — the receipt-ever flag still truthful after the 01-28 firmware change"
        status: pass
    human_judgment: true
    rationale: "Operator bench console evidence; watch green, entry stays fixed"
  - id: D3
    description: "G-01-7 series-A acceptance (eighth attempt): 3 unspaced captures, 6/6 verdicts COMPLETE"
    requirement: "IMG-03"
    verification:
      - kind: manual_procedural
        ref: "Session aborted at protocol step 3 before the series; zero series-A discriminator lines in either console"
        status: unknown
    human_judgment: true
    rationale: "Operator bench protocol step never reached; stays open, eighth round riding"
  - id: D4
    description: "SC-3 visible-effect pairs (brightness -2/+2 plus one other class) judged at fixed QVGA"
    requirement: "CTRL-02"
    verification:
      - kind: manual_procedural
        ref: "Zero SET_BRIGHTNESS/contrast/saturation/quality commands in balloon3.log/base3.log"
        status: unknown
    human_judgment: true
    rationale: "Requires the operator's eyes on captured image pairs; session aborted before the step — eighth round riding"
  - id: D5
    description: "WR-03 cadence discriminator: manual capture resets the auto-capture schedule (SC-5 advisory discharge)"
    requirement: "CTRL-04"
    verification:
      - kind: manual_procedural
        ref: "Zero AUTO_CAPTURE commands in either console (eighth round riding)"
        status: unknown
    human_judgment: true
    rationale: "Operator bench protocol step never reached; SC-5 coincidental-reliance advisory still NOT discharged"
  - id: D6
    description: "CIF-cycle (SET_RESOLUTION wire 8, as specified) and QVGA-restore clauses with both kinds COMPLETE at expected size classes, zero FB-OVF"
    requirement: "CTRL-02"
    verification:
      - kind: manual_procedural
        ref: "Zero SET_RESOLUTION commands in either console (eighth round riding)"
        status: unknown
    human_judgment: true
    rationale: "Clause not exercised; session aborted at step 3"
  - id: D7
    description: "Dashboard LOOK glance (UI-SPEC considerations + gallery detail resLabel/filesize)"
    verification:
      - kind: manual_procedural
        ref: "Commands flowed (seq 1/2/3 ACKed base3.log:28/:61/:89) but no LOOK verdicts recorded before the abort"
        status: unknown
    human_judgment: true
    rationale: "Operator visual judgment; not reached"
  - id: D8
    description: "WR-02 bench discriminator (WINDOWS 18, fixed): healthy boots log no camera health-check failure"
    verification:
      - kind: manual_procedural
        ref: "Whole-console grep health-check-failed = 0; only the honest skipped notes at both boots (balloon3.log:102/:614 warm-up region)"
        status: pass
    human_judgment: true
    rationale: "Operator bench console evidence; watch green, entry stays fixed"
  - id: D9
    description: "Honest ledger recording: 01-UAT.md / WINDOWS.md / STATE.md reflect the failed session truthfully (Task 2)"
    verification:
      - kind: manual_procedural
        ref: "Task 2 automated verify greps PASS (01-29 + secure-phase in STATE.md; G-01-10 in 01-UAT.md; ids 15 and 3 in WINDOWS JSON); counts reconciled 2 open / 17 fixed / 19 total, table and JSON agree, zero flips"
        status: pass
    human_judgment: false
---

# Phase 01 Plan 29: Operator Bench Re-Verification Session #9 Summary

**One-liner:** Session 9 failed at protocol step 3 — window 1 of image 39's full served clean (16/16 chunks on air, thumb COMPLETE) then the board took a TG0WDT reset in the inter-window lull with a crash-adjacent i2cWrite ESP_ERR_INVALID_STATE carrying session-7-class mojibake ON the line itself; Saved PC 0x4037c7fa = esp_vApplicationTickHook (freertos_hooks.c:34), one frame above session-8's tick_hook — the 01-28 fix disconfirmed a second time, D1's third expression, debug round #3 routed, zero ledger flips while D2 and WR-02 watched green.

## What Was Built

Nothing was built - the deliverable is evidence. The prior executor's pre-flight (on record, not redone): repo HEAD `9261b9e`, `pio run` both envs SUCCESS exit 0, `node scripts/verify_protocol_roundtrip.mjs` exit 0, both boards flashed with the round-#12 firmware, deployed-ELF provenance balloon SHA256 `e09dd034a5ab…` retained for addr2line. The operator ran bench session #9 and reported failure during the sustained full-service step.

The continuation executor re-verified every cited line in the retained consoles (`balloon3.log` 804 lines / `base3.log` 221 lines — naming deviation from the plan's balloon14/base9 request, recorded, files never renamed), re-hashed the ELF and re-ran the decode independently, and resolved the session:

- **The fatal event (D1 third expression):** protocol steps 1-2 clean (boot healthy, spaced smoke survived — commands seq 1/2/3 ACKed base3.log:28/:61/:89, thumb COMPLETE 9/9 :79); image 39's full (7133 B / 36 chunks, manifest base3.log:81 CRC 447D30AB) enqueued with healthy [MEM] (balloon3.log:372 heap 8544812 / psram 8339972 / stackHW 5772); its first window armed 0..15 (:421) and served every chunk on air (:427-491, base3.log:84 window 0..15) — then in the inter-window lull (:495-519: GET_STATUS :497-501, beacon seq=23 :505, [MEM] :511 heap 8534176 / stackHW 5744, Performance :514 Max 1277 ms Avg 47 Count 993) the console corrupts an i2cWrite error (:520 `ESP_ERR_INVALID_STATE` with mojibake replacing the T of STATE — hex-dump verified) and the board resets: ROM banner :521-522 → `rst:0x7 (TG0WDT_SYS_RST)` :523 → `Saved PC:0x4037c7fa` :524 → `[BOOT] reset-cause: TASK_WDT` :615.
- **The 01-28 fix disconfirmed as sufficient:** the yielding bounded TX-drain ran on this firmware, the window itself served clean with no reset mid-service — the reset moved to the lull BETWEEN windows, which is a new timing but the same WDT chain. Decode (ELF SHA verified first): `0x4037c7fa → esp_vApplicationTickHook (freertos_hooks.c:34)` — the function that dispatches `tick_hook` (session-8's decoded frame, int_wdt.c:111), i.e. one frame up the identical WDT tick-ISR chain, zero project frames.
- **Session-7 corruption class returns AT the crash moment:** exactly 1 i2cWrite error in the whole console — adjacent to the reset, carrying the mojibake on-line. Grep counts: rst:0x7 = 1, Guru Meditation = 0, canary/watchpoint = 0, rst:0xc = 0, [LOOP] = 0. The corruption, the I2C error, and the WDT reset are now one observed chain at one timestamp — three PCs, one chain (debug doc §8.5).
- **D2 watch GREEN:** the 're-announce held' line fired exactly 2×, both genuine by timestamp correlation (:492, :683); zero spurious boot-window fires. **WR-02 watch GREEN:** zero health-check-failed at both boots.
- **Image 39's INCOMPLETE 15/36 (base3.log:194) attributed to the crash**, not burst delivery; retransmit machinery itself worked before the reset (retransmits 7..7 seq 5/6/7 :138/:167/:177, NACK_INVALID ×3 :162/:171/:181); zero command timeouts in base3.log.

## Task 1: Operator Bench Session (checkpoint:human-verify - failed-session resolution)

The prior executor stopped at the Task 1 operator checkpoint with the pre-flight on record. The operator ran session #9 and returned the failure verdict. This continuation resolved Task 1 on the failure path exactly as the plan's resume-signal specifies: captured the discriminating lines, appended the evidence to the debug doc §8, re-opened D1 — no forced closures.

Session facts (all operator-console evidence, line-cited in the ledgers):
- Steps 1-2 clean: healthy boot (build banner Aug 28 2026 12:06:09 :23/:535, warm-up :102/:614, reset-cause :103/:615), spaced smoke both kinds COMPLETE (thumb 9/9 base3.log:79).
- Step 3 (sustained full-service, session-8's killer): image 39's full window 1 served 16/16 on air; TG0WDT reset in the inter-window lull — the third expression of D1 across sessions 7/8/9.
- Crash-signature grep counts (whole balloon3.log): i2cWrite ESP_ERR_INVALID_STATE = 1 (crash-adjacent, mojibake on-line), Guru Meditation = 0, canary = 0, rst:0x7 = 1 (the fatal), rst:0xc = 0, [LOOP] = 0.
- D2 hold-line exactly 2 fires, both genuine; WR-01 battery watch GREEN; WR-02 clean at both boots.

## Task 2: Honest Ledger Updates (commit f25b41b)

**Debug doc** (`.planning/debug/d1-crash-regression-push-start.md` §8): full session-9 evidence — §8.1 provenance (ELF SHA256 `e09dd034a5ab…`, mtime matching the build banner, round-#12 code confirmed in-image via warm-up and reset-cause lines); §8.2 service map + decode table (esp_vApplicationTickHook freertos_hooks.c:34; boot-banner addresses unresolvable bootloader regions, recorded as such); §8.3 B1/B2 brief answers; §8.4 the crash-adjacent i2cWrite break + mojibake; §8.5 the three-PCs-one-chain table; §8.6 the round-#3 brief.

**01-UAT.md**: `updated: 2026-08-28T13:45:00Z`; Test 3 session-9 UPDATE appended (step-3 crash, service map); Test 4 eighth-round note; G-01-10 SESSION-9 EXTENSION in root_cause + missing list (ANSWERED entry; series-A entry updated to sessions 8 AND 9); G-01-7 session-9 unjudgeable note.

**WINDOWS.md**: `last_updated: 2026-08-28T13:45:00.000Z`; entry 15 (D1) extended in table + JSON with the third expression; entry 3 (G-01-7) SESSION 9 annotation in table + JSON. Zero flips; counts stand 2 open / 17 fixed / 19 total.

**STATE.md**: Current Status (01-28 + 01-29 story), Current Phase bullet, Progress list (new 01-28/01-29 combined entry), Next Steps item 1 (round-#3 brief) + item 4, footer.

## What Was NOT Done (the honesty list)

- **No D1 closure, no G-01-7 closure** - the crash re-opens D1 with its third expression; the series never ran.
- **SC-3 pairs, WR-03 discriminator, CIF-cycle-as-specified, QVGA restore, dashboard LOOK: NOT RUN** - eighth round riding (session aborted at step 3).
- **No requirement marked** - the plan's 8 shared requirement IDs are already `[x]` complete from earlier plans (verified in REQUIREMENTS.md); on a failed session nothing new is established, so `requirements-completed: []` (the 01-24/01-27 convention).
- **No code fix attempted** - the plan's prohibition holds; the third expression + I2C/mojibake convergence is routed to the debug doc §8 and the round-#3 brief.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Session log naming differs from the plan's convention**
- **Found during:** Task 1 checkpoint return (operator verdict)
- **Issue:** The plan requested balloon14.log/base9.log; the operator retained balloon3.log/base3.log.
- **Fix:** Provenance deviation recorded in 01-UAT.md, WINDOWS reasons, and this summary; citations adapted to the actual names; files never renamed (the 01-24 convention).
- **Files modified:** 01-UAT.md
- **Commit:** f25b41b

**2. [Rule 1 - Bug] Incoming evidence packet's mojibake count corrected**
- **Found during:** Task 1 failure-path resolution
- **Issue:** The operator packet reported mojibake = 0 — produced by the UTF-8-replacement-char grep the debug doc §7.4 has twice documented as a trap on Windows consoles. The corrected method (literal mojibake glyphs / raw bytes `e2 88 a9 e2 94 90 e2 95 9c`) finds exactly 1 — ON the crash-adjacent i2cWrite line :520 itself, replacing the T of STATE (hex-dump verified).
- **Fix:** Count corrected to 1 crash-adjacent in every ledger; carried as discriminating evidence (the session-7 corruption class returning at the crash moment), not noise. Minor companion correction: the last [MEM] sample pre-reset is :511, not :509 as the packet said.
- **Files modified:** .planning/debug/d1-crash-regression-push-start.md, 01-UAT.md, WINDOWS.md
- **Commit:** f25b41b

**3. [Rule 4 boundary respected - the third expression routed, not fixed]**
- **Found during:** Task 2
- **Issue:** The crash recurrence at the lull-with-I2C-corruption convergence names a new root-cause question whose fix choice needs its own debug round.
- **Action:** Routed to the debug doc §8 + WINDOWS 15 + STATE Next Steps (round-#3 brief); no code change made in this plan.
- **Commit:** f25b41b

---

**Total deviations:** 3 auto-fixed/routed (1 blocking, 1 bug, 1 Rule-4 boundary)
**Impact on plan:** All documentation-path; no scope creep, no fabricated closures.

## Auth Gates

None.

## Known Stubs

None - no code was written; every ledger claim carries a log-line citation.

## Windows Ledger

Zero flips this plan. Entry 15 (D1) stays open with the third-expression evidence appended; entry 3 (G-01-7) stays open with the session-9 annotation. D2 (16) and WR-02 (18) watched green and stay fixed. Ledger stands at 2 open / 17 fixed / 19 total and blocks `/gsd-ship` until D1 and G-01-7 close on a clean bench - exactly what an honest ledger should do after a second consecutive failed session.

## Round #13 Handoff (one line)

Debug round #3 must investigate the inter-window-lull TG0WDT reset on the round-#12 firmware — the window itself served clean, so audit what the lull runs (GET_STATUS handling, beacon tick, tick-hook dispatch) for WDT-period blocks, against the deployed ELF `e09dd034a5ab` with `Saved PC:0x4037c7fa = esp_vApplicationTickHook (freertos_hooks.c:34)`, the crash-adjacent on-line-mojibake i2cWrite (:520), and the three-PCs-one-chain table in debug doc §8 — then re-run the bench moment (sustained full-service + series A + the eighth-round clauses; D2 and WR-02 watches read clean).

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

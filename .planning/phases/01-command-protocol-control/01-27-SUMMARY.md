---
phase: "01"
plan: "27"
subsystem: bench-verification
tags: [bench, uat, evidence, crash-regression, watchdog, ledger-discipline]
requires:
  - "01-25 D1 debug round (PSRAM warm-up + [MEM] instrumentation + D2 receipt-ever flag)"
  - "01-26 review-routing companion fixes (WINDOWS 17-19)"
provides:
  - "Honest session-8 record in 01-UAT.md (bench FAILED at image 38's FULL TX; D1 re-opened, D2 resolved)"
  - "Session-8 crash evidence appended to .planning/debug/d1-crash-regression-push-start.md §6 (Saved PC decoded, [MEM] verdict, new root-cause axis)"
  - "G-01-11/WINDOWS 16 closed on bench evidence (D2 boot windows clean at two boots)"
  - "WR-02/WINDOWS 18 bench-verified (healthy boots warn nothing)"
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
key-files:
  created: []
  modified:
    - ".planning/debug/d1-crash-regression-push-start.md"
    - ".planning/phases/01-command-protocol-control/01-UAT.md"
    - ".planning/WINDOWS.md"
    - ".planning/STATE.md"
decisions:
  - "Record session 8 as FAILED with the D1 re-open (nothing forced) while still closing D2 on its own clean evidence - the boot-window truth is independent of the crash and the crash actually PROVIDED the second boot window that proved it"
  - "Disconfirm the 01-25 PSRAM-warm-up hypothesis as sufficient on the [MEM] evidence (healthy at enqueue :611 and at the last pre-reset sample :1175) and name the new root-cause axis: task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service"
  - "Judge both session-8 're-announce held' fires GENUINE by timestamp correlation (:335 mid-service of image 37's window; :1389 five lines after the rejected-but-received request at :1384-1388) - the receipt-ever flag working as designed, not a spurious fire"
  - "Keep G-01-7 unjudgeable (series never ran) and attribute image 38's kind-1 INCOMPLETE 109/137 to the crash, not to burst delivery"
metrics:
  duration: "~3h across executor pre-flight + operator bench session #8 + continuation closeout"
  completed: 2026-08-28
status: complete
requirements-completed: []
estimate:
  tokens: 9000
actuals:
  tokens: 21200
  tasks: 2
  commits: 2
coverage:
  - id: D1
    description: "D1 crash-regression acceptance (G-01-10 / WINDOWS 15): spaced smoke AND series A complete with zero crash signatures"
    requirement: "CTRL-06"
    verification:
      - kind: manual_procedural
        ref: "balloon2.log:1228-1231 rst:0x7 TG0WDT_SYS_RST at the first chunk of re-armed FULL window 96..109 (spaced smoke itself survived: image 37 both kinds COMPLETE base2.log:74-75/:238-239)"
        status: fail
    human_judgment: true
    rationale: "Operator bench session on hardware radios; the verdict is a console-evidence judgment that automation cannot make"
  - id: D2
    description: "D2 discriminator (G-01-11 / WINDOWS 16): every boot window clean of the busy-hold episode line before the first genuine inbound window request"
    requirement: "CTRL-06"
    verification:
      - kind: manual_procedural
        ref: "balloon2.log:85-135 (boot 1) and :1238-:1383 (boot 2, the crash reboot) - zero 're-announce held' before first inbound request (:260 / :1384); both session fires traffic-correlated (:335, :1389)"
        status: pass
    human_judgment: true
    rationale: "Operator bench console evidence; flipped resolved on the quoted boot windows"
  - id: D3
    description: "G-01-7 series-A acceptance: 3 unspaced captures, 6/6 verdicts COMPLETE"
    requirement: "IMG-03"
    verification:
      - kind: manual_procedural
        ref: "Session aborted at image 38's FULL service before any unspaced burst; zero round-#10 discriminator lines in either console"
        status: unknown
    human_judgment: true
    rationale: "Operator bench protocol step never reached; stays open, seventh round riding"
  - id: D4
    description: "SC-3 visible-effect pairs (brightness -2/+2 plus one other class) judged at fixed QVGA"
    requirement: "CTRL-02"
    verification:
      - kind: manual_procedural
        ref: "Zero SET_BRIGHTNESS/contrast/saturation/quality commands in balloon2.log/base2.log"
        status: unknown
    human_judgment: true
    rationale: "Requires the operator's eyes on captured image pairs; session aborted before the step - seventh round riding"
  - id: D5
    description: "WR-03 cadence discriminator: manual capture resets the auto-capture schedule (SC-5 advisory discharge)"
    requirement: "CTRL-04"
    verification:
      - kind: manual_procedural
        ref: "Zero AUTO_CAPTURE commands in either console (seventh round riding)"
        status: unknown
    human_judgment: true
    rationale: "Operator bench protocol step never reached; SC-5 coincidental-reliance advisory still NOT discharged"
  - id: D6
    description: "CIF-cycle (SET_RESOLUTION 8) and QVGA-restore clauses with both kinds COMPLETE at expected size classes"
    requirement: "CTRL-02"
    verification:
      - kind: manual_procedural
        ref: "Operator ran SET_RESOLUTION wire-10 (SVGA, balloon2.log:589-592 ACKed base2.log:263) instead of the specified wire-8 CIF cycle; its capture's full never completed (crash); no QVGA restore"
        status: unknown
    human_judgment: true
    rationale: "Clause not exercised as specified - the step that ran does not satisfy it"
  - id: D7
    description: "Dashboard LOOK glance (UI-SPEC considerations + gallery detail resLabel/filesize)"
    verification:
      - kind: manual_procedural
        ref: "Session aborted at image 38; /api/state serving (base2.log:527/:532) but no LOOK verdicts recorded"
        status: unknown
    human_judgment: true
    rationale: "Operator visual judgment; not reached"
  - id: D8
    description: "WR-02 bench discriminator (WINDOWS 18): healthy boots log no camera health-check failure"
    verification:
      - kind: manual_procedural
        ref: "balloon2.log:109-110 and :1328-1329 - only the honest skipped notes; zero 'health check failed' lines in the whole console (grep count 0)"
        status: pass
    human_judgment: true
    rationale: "Operator bench console evidence; bench-verified in WINDOWS 18's reason"
  - id: D9
    description: "Honest ledger recording: 01-UAT.md / WINDOWS.md / STATE.md reflect the failed session truthfully (Task 2)"
    verification:
      - kind: manual_procedural
        ref: "Task 2 automated verify greps PASS (01-27 + secure-phase in STATE.md; ids 15/16 in WINDOWS JSON); counts reconciled 2 open / 17 fixed / 19 total, table and JSON agree"
        status: pass
    human_judgment: false
---

# Phase 01 Plan 27: Operator Bench Re-Verification Session #8 Summary

**One-liner:** Bench session #8 FAILED at image 38's FULL TX — the 01-25 PSRAM-warm-up fix is disconfirmed as sufficient (crash recurred mid-service on healthy [MEM] values; Saved PC 0x40376430 decoded vs ELF c53635e181 as esp-idf tick_hook, zero project frames; new root-cause axis: task-watchdog starvation during SUSTAINED FULL window service) — while D2 closed clean at both boots and WR-02 bench-verified; D1 re-opened with the new evidence, G-01-7 unjudgeable, SC-3/WR-03/CIF/QVGA/LOOK seventh round riding.

## What Was Built

Nothing was built - the deliverable is evidence. The prior executor's pre-flight (on record, not redone): repo HEAD `56f3db6`, `pio run` both envs SUCCESS exit 0, `node scripts/verify_protocol_roundtrip.mjs` exit 0, deployed-ELF provenance balloon SHA256 `c53635e181…` / base `a2cce388d8…` both retained for addr2line. The operator ran bench session #8 and reported failure during a full-size image TX.

The continuation executor re-verified every cited line in the retained consoles (`balloon2.log` / `base2.log` — naming deviation from the plan's balloon13/base8 request, recorded, files never renamed), verified the ELF SHA before decoding, and resolved the session:

- **The fatal event (D1 re-opened):** after the spaced smoke SURVIVED (image 37, the session-7 crash-1 pattern, both kinds COMPLETE) and six clean FULL windows of image 38, the balloon reset at the FIRST chunk of the re-armed window 96..109 — balloon2.log:1220 armed → :1227 `window chunk(image 38 kind 1, 1/14, 200 B) sent` → :1228-1231 `rst:0x7 (TG0WDT_SYS_RST)` / `Saved PC:0x40376430`. Decode (ELF SHA verified first): `tick_hook at esp-idf/components/esp_system/int_wdt.c:111` — the WDT tick-ISR chain, ZERO project frames, the same neighborhood as session-7 crash 1's spinlock wedge.
- **The 01-25 fix disconfirmed as sufficient:** the warm-up ran at both boots (:102/:1321) and [MEM] stayed healthy at enqueue (:611: heap 8354328 / psram 8149436 / stackHW 5744) and at the last pre-reset sample (:1175: heap 8332460 / stackHW 5552).
- **Session-7 corruption corroboration ABSENT:** zero U+FFFD mojibake, zero Guru Meditation, zero canary; the 2 i2cWrite errors (:885/:1577) are non-adjacent BMP280 noise each followed by a successful read.
- **D2 CLOSED:** both boot windows clean of the spurious hold line; the session's exactly-2 fires genuine by timestamp correlation (:335 mid-window-service; :1389 five lines after the rejected-but-received request at :1384-1388 — the receipt-ever flag stamped by that very request).
- **Image 38's INCOMPLETE 109/137 (base2.log:651) attributed to the crash**, not burst delivery; zero command timeouts in base2.log (no forbidden CAPTURE_NOW terminals, unlike session 7).

## Task 1: Operator Bench Session (checkpoint:human-verify - failed-session resolution)

The prior executor stopped at the Task 1 operator checkpoint with the pre-flight on record. The operator ran session #8 and returned the failure verdict ("it failed during a full size image tx"). This continuation resolved Task 1 on the failure path exactly as the plan's resume-signal specifies: captured the discriminating lines, appended the dump to the debug doc, re-opened D1 — no forced closures.

Session facts (all operator-console evidence, line-cited in the ledgers):
- Spaced smoke: image 37 CAPTURE_NOW → both kinds COMPLETE (thumb 8/8 base2.log:74-75, full 36/36 :238-239) — the session-7 crash-1 pattern dead at this boot.
- SET_RESOLUTION wire-10 (SVGA) executed + ACKed (balloon2.log:589-592, base2.log:256/:260/:263) — the operator's own step, not the plan's CIF wire-8; image 38's full 27377 B / 137 chunks is SVGA-class.
- Image 38: thumb COMPLETE 8/8 (base2.log:290-291); full INCOMPLETE 109/137 after 3 passes (:651-652) — crash-caused.
- Crash-signature grep counts (whole balloon2.log): i2cWrite ESP_ERR_INVALID_STATE = 2 (noise), Guru Meditation = 0, canary/watchpoint = 0, rst:0x7 = 1 (the fatal), rst:0xc = 0, U+FFFD = 0.
- WR-01 battery watch GREEN (zero Critical battery; beacons batt=valid). WR-02 healthy-boot discriminator CLEAN at both boots.

## Task 2: Honest Ledger Updates (commit a2a14a7)

**Debug doc** (`.planning/debug/d1-crash-regression-push-start.md` §6): full session-8 evidence — provenance cross-check, the fatal-event timeline, the addr2line decode, the [MEM] verdict (01-25 hypothesis DISCONFIRMED as sufficient), the absent corruption corroboration, and the new root-cause question (task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service; one mechanism with session-7's spinlock wedge or two faults — round #12's brief).

**01-UAT.md**: Test 3 note carries the complete session-8 record; Test 4 note carries the WR-03 seventh-round not-run; G-01-10 root_cause extended with the session-8 discrimination and the missing list updated (debug-round item DONE, determinism question ANSWERED, round-#2 item OPEN); **G-01-11 flipped resolved** (resolved_by 01-25 receipt-ever flag; verified_by quotes both boot windows and both genuine fires); G-01-7 root_cause carries the session-8 unjudgeable note.

**WINDOWS.md** (both copies, counts reconciled — 2 open / 17 fixed / 19 total): entry 15 (D1) stays open with the session-8 evidence appended (fix disconfirmed, new Saved PC decode, new axis); **entry 16 (D2) flipped fixed** on the clean boot windows; entry 3 (G-01-7) stays open — series never ran, image-38 INCOMPLETE crash-caused, and the D2 discriminator pollution is now fixed so the next series-A run reads clean; entry 18 (WR-02) carries its bench-verified evidence.

**STATE.md**: Current Status records the session-8 outcome; Current Phase shows 2 open entries; Next Steps item 1 is the D1 debug round #2 brief; footer updated.

## What Was NOT Done (the honesty list)

- **No D1 closure, no G-01-7 closure** - the crash re-opens D1 with new evidence; the series never ran.
- **SC-3 pairs, WR-03 discriminator, CIF-cycle-as-specified, QVGA restore, dashboard LOOK: NOT RUN** - seventh round riding (session aborted at image 38; the SET_RESOLUTION that did run was wire-10/SVGA, and its full never completed).
- **No requirement marked** - the plan's 8 shared requirement IDs are already `[x]` complete from earlier plans (verified in REQUIREMENTS.md); on a failed session nothing new is established, so `requirements-completed: []` (the 01-24 convention).
- **No code fix attempted** - the plan's prohibition (MUST NOT fix newly observed bench defects inside this plan) holds; the new axis is routed to the debug doc for round #12.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Session log naming differs from the plan's convention**
- **Found during:** Task 1 checkpoint return (operator verdict)
- **Issue:** The plan specified balloon13.log/base8.log; the operator retained balloon2.log/base2.log.
- **Fix:** Provenance deviation recorded in 01-UAT.md, WINDOWS reasons, and this summary; citations adapted to the actual names; files never renamed (the 01-24 convention).
- **Files modified:** 01-UAT.md
- **Commit:** a2a14a7

**2. [Rule 3 - Blocking] addr2line decode added beyond the literal Task 2 list**
- **Found during:** Task 1 failure-path resolution
- **Issue:** The continuation instructions asked for the decode "if the toolchain resolves it quickly" — the toolchain was present and the ELF SHA matched, so the decode was performed rather than deferred.
- **Fix:** ELF SHA `c53635e181…` verified BEFORE the decode; `0x40376430 → tick_hook (int_wdt.c:111)`; boot-banner addresses recorded as unresolvable bootloader regions (expected, not presented as evidence).
- **Files modified:** .planning/debug/d1-crash-regression-push-start.md
- **Commit:** a2a14a7

**3. [Rule 4 boundary respected - the new axis routed, not fixed]**
- **Found during:** Task 2
- **Issue:** The crash recurrence names a new root-cause axis (WDT starvation during FULL window service) whose fix choice needs its own debug round.
- **Action:** Routed to the debug doc §6 + WINDOWS 15 + STATE Next Steps; no code change made in this plan.
- **Commit:** a2a14a7

---

**Total deviations:** 3 auto-fixed/routed (2 blocking, 1 Rule-4 boundary)
**Impact on plan:** All documentation-path; no scope creep, no fabricated closures.

## Auth Gates

None.

## Known Stubs

None - no code was written; every ledger claim carries a log-line citation.

## Windows Ledger

Entry 16 (D2) flipped FIXED this plan on bench evidence; entry 15 (D1) re-opened with session-8 evidence; entry 3 (G-01-7) annotated, status unchanged; entry 18 (WR-02) bench evidence appended. Ledger stands at 2 open / 17 fixed / 19 total and blocks `/gsd-ship` until D1 and G-01-7 close on a clean bench - exactly what an honest ledger should do after a failed session with one independently-proven truth.

## Round #12 Handoff (one line)

Debug round #2 must investigate task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service — audit the serviceWindowChunk → E32 transmit path for WDT-period blocks (AUX polling, UART flush, kernel lock held across a blocking wait, cache-suspended stretch) against the deployed ELF `c53635e181` with `Saved PC:0x40376430 = tick_hook (int_wdt.c:111)` and the [MEM]-healthy session-8 evidence in the debug doc §6 — then re-run the bench moment (series A + the seventh-round clauses; the D2 discriminator now reads clean).

## Self-Check: PASSED

- File check: `.planning/phases/01-command-protocol-control/01-27-SUMMARY.md` FOUND; debug doc / WINDOWS / UAT / STATE edits present in commit a2a14a7
- Commit check: a2a14a7 (evidence + ledgers) FOUND; closeout docs commit follows
- Task 2 verify greps re-run: STATE.md contains 01-27 and secure-phase; WINDOWS JSON contains ids 15 and 16
- WINDOWS integrity: table rows = JSON entries = 19; open = exactly {3, 15}; front matter 2/17/19 reconciles
- Honesty audit: no D1/G-01-7 closure claims; G-01-11 flip and WINDOWS 16 fix carry verbatim quotes with line numbers; requirements-completed is [] with all 8 shared IDs verified already-complete in REQUIREMENTS.md
- Untracked residue: balloon2.log/base2.log retained untracked (session evidence, prior-session convention); pre-existing dirty files (.pio/build/project.checksum, .planning/config.json, 03-UAT.md, .gsd/, milestone.lock, research/) untouched

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

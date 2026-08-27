---
phase: 01-command-protocol-control
plan: 20
subsystem: testing
tags: [bench-verification, lora, image-transfer, gap-ledger, evidence-honesty]

requires:
  - phase: 01-command-protocol-control (plans 01-17, 01-18, 01-19)
    provides: receipt-informed FULL-manifest re-announce + WR-08 success-gated cursors (01-17), base command-transmit channel-quiet gate (01-18), CR-04 scoped camera gate + WR-03 manual-capture baseline (01-19)
provides:
  - Bench session #6 evidence (base6.log + balloon11.log, 12 images) proving command survivability under unspaced load and the death of the silent manifest-air-loss class
  - G-01-9 resolved (defects A/B/C all bench-proven across sessions 5 and 6); WINDOWS entries 5/8/9 flipped fixed
  - G-01-7 honestly open, rescoped to the single named residual axis (burst full-delivery) with levers named
  - Updated 01-UAT.md Test 3 note recording the fifth-round ride of SC-3/WR-03/CIF/QVGA-restore clauses
affects: [phase-1-closeout, gsd-secure-phase-1, next-bench-round, gsd-ship]

actuals:
  tokens: 26449   # chars/4 over the realized ledger diff (df76ff4: 3 files, 52 ins / 43 del — the long evidence lines carry the bulk)
  tasks: 2
  commits: 1

tech-stack:
  added: []
  patterns:
    - "Evidence-class flips: unstageable (CR-04) and unexercised (WR-08) paths recorded BY NAME in the flip reason, never fabricated as bench-proven"
    - "Rescoped-open closure: a gap whose two structural holes are bench-solved stays open while a NEW named axis (burst full-delivery) carries the failing verdict — solved sub-truths recorded, no forced close"

key-files:
  created: []
  modified:
    - .planning/phases/01-command-protocol-control/01-UAT.md
    - .planning/WINDOWS.md
    - .planning/STATE.md

key-decisions:
  - "G-01-9 resolved on three-defect evidence: A/B bench-held across sessions 5 AND 6 (9/9 genuine QQVGA thumbs, zero stored-bytes CRC mismatches), C bench-proven session 6 (image 31 re-announce recovery to 82/82 COMPLETE; manifests re-delivered 3x/4x; every unrecovered drop named at the 3-re-announce bound)"
  - "G-01-7 stays open rescoped: both session-5 axes are bench-dead (3/3 unspaced ACKs via the quiet gate — command survivability SOLVED; silent manifest loss dead), but verdicts 2/6 fail on burst full-delivery (re-announce bound expires during burst thumb serialization; depth-3 queue eviction) — levers named, interim mitigation unchanged (space captures)"
  - "WINDOWS 8 (CR-04) flipped on code fix + builds 2/2 + harness exit 0 with the unstageable camera-down scenario honestly recorded; WINDOWS 9 (WR-08) flipped WIRED-but-unexercised (zero chunk TX failures at bench)"

patterns-established:
  - "Log-provenance note in the ledger when a bench session's console file set deviates from the naming convention (balloon11.log carries session 6 after the balloon6-10 bootloop saga)"

requirements-completed: [CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02, IMG-02, IMG-03]

coverage:
  - id: D1
    description: "Bench session #6 executed on round-#8 firmware (series A unspaced triggers, WR-03 discriminator attempt, SC-3 pair attempt, settings regression, dashboard glance) with both consoles retained"
    requirement: "CTRL-01"
    verification:
      - kind: manual_procedural
        ref: "base6.log + balloon11.log (21,554 + 30,252 lines, repo root); series-A lifecycle base6.log:3909/:3915/:3922/:3933/:3955/:3958"
        status: pass
    human_judgment: true
    rationale: "Operator bench session — verdicts are human-observed console/dashboard evidence by house convention; no automated test exercises the RF pair"
  - id: D2
    description: "G-01-9 flipped resolved and WINDOWS entry 5 flipped fixed on bench evidence (defects A/B held, defect C live re-announce recovery + named drops)"
    requirement: "IMG-02"
    verification:
      - kind: manual_procedural
        ref: "balloon11.log:25435/:25438 -> base6.log:13096 (image 31 recovery); base6.log:4015/:4044/:4074 and :4005/:4035/:4065/:4090 (manifest re-delivery); balloon11.log:553/:575 (named drops)"
        status: pass
    human_judgment: true
    rationale: "Closure rides verbatim operator-observed console lines with line numbers — the house evidence convention"
  - id: D3
    description: "G-01-7 honestly open, rescoped to the burst full-delivery residual (2/6 series-A verdicts) with the next levers named; WINDOWS entry 3 reason updated, status unchanged"
    requirement: "CTRL-06"
    verification:
      - kind: manual_procedural
        ref: "base6.log:4231/:4306 (INCOMPLETE 0/22, 0/14), balloon11.log:406 + :495-:656 (image 24 eviction + rejects), base6.log:4170"
        status: fail
    human_judgment: true
    rationale: "Failed series must leave its gap open with the residual mechanism named — never a forced close"
  - id: D4
    description: "WINDOWS entries 8 (CR-04) and 9 (WR-08) flipped fixed per their encoded evidence-class rules; counts reconciled open 1 / fixed 8"
    requirement: "CTRL-03"
    verification:
      - kind: manual_procedural
        ref: "Entry 8: 01-19 commit 5b9a8a7 + builds 2/2 + harness exit 0 on HEAD 898fcc6, unstageable camera-down scenario recorded; Entry 9: 01-17 commit c517c4f, zero 'chunk ... FAILED' / 'skipped after' lines in balloon11.log (WIRED but unexercised)"
        status: pass
    human_judgment: true
    rationale: "Both flips carry explicit evidence-class reasons (unstageable / unexercised) that no automation can confirm — honesty is the deliverable"
  - id: D5
    description: "Ledgers consistent and STATE.md records the 01-20 round outcome with /gsd-secure-phase 1 named as the remaining phase-close gate"
    requirement: "CTRL-02"
    verification:
      - kind: integration
        ref: "Plan Task 2 automated verify (6 PowerShell Select-String checks incl. WINDOWS open_count reconciliation) — exit 0"
        status: pass
    human_judgment: false

duration: ~2h executor time spanning the operator bench session (2026-08-27 -> 2026-08-28)
completed: 2026-08-28
status: complete
---

# Phase 01 Plan 20: Bench Re-Verification Ledger Round Summary

**Session-#6 bench evidence closed G-01-9 (all three defects bench-proven, silent manifest loss dead) and solved unspaced command survivability via the quiet gate, while G-01-7 stays honestly open on one newly-named residual — burst full-delivery — and WINDOWS 5/8/9 flipped with evidence-class honesty**

## Performance

- **Duration:** ~2h executor time across 2026-08-27/28 (the operator bench session #6 ran in between: 2026-08-27 evening through 2026-08-28 ~00:20 local)
- **Started:** 2026-08-27T12:20:00Z
- **Completed:** 2026-08-27T13:05:00Z
- **Tasks:** 2 (1 checkpoint human-verify resolved from retained evidence under auto-mode; 1 auto ledger-flip task)
- **Files modified:** 3

## Accomplishments

- Bench session #6 ran on round-#8 firmware (both boards; GPS UART1 RX pin fix 3f2c2c6 landed before the session after a bootloop saga — provenance recorded in the ledgers): 12 images, 19 COMPLETE / 5 INCOMPLETE verdicts, zero silently-absent fulls
- COMMAND SURVIVABILITY UNDER UNSPACED LOAD SOLVED AT BENCH: series A's 3 unspaced CAPTURE_NOW (seq 182/183/184) all ACKed (base6.log:3915/:3955/:3958) with zero series-A command timeouts — the 01-18 quiet gate held both retried transmits under image-24's chunk storm (:3945/:3946) and five more held-command lifecycles ran to ACKED in the late session
- G-01-9 RESOLVED: defects A/B bench-held across sessions 5 and 6 (all 9 logged thumbs genuine QQVGA 1073-1879 B with the drain discriminator at every capture; zero stored-bytes CRC mismatches; zero CRC FAIL); defect C bench-proven session 6 — the 01-17 receipt-informed re-announce ran live (image 31 recovered 82/82: balloon11.log:25435 -> :25438 -> base6.log:13096; images 25/26 manifests re-delivered 3x/4x; every unrecovered drop named at the bound: balloon11.log:553/:575)
- WINDOWS ledger: entries 5 (G-01-9 defect C), 8 (CR-04 — code fix + builds + harness with the unstageable camera-down scenario recorded), 9 (WR-08 — WIRED but unexercised, zero chunk TX failures at bench) flipped fixed; entry 3 (G-01-7) rescoped open to the single burst full-delivery axis; counts reconciled open 1 / fixed 8 — /gsd-ship blocks on exactly the one honest residual
- G-01-7 rescoped with named levers: full-window-priority interleave during thumb serialization (or arm FULL windows between thumb passes), re-arm the re-announce bound on manifest receipt, burst-admission depth; interim mitigation unchanged — space captures
- Pre-flight re-verified on HEAD 898fcc6: both targets build green, protocol harness exit 0

## Task Commits

Each task was committed atomically:

1. **Task 1: Bench session #6 (checkpoint:human-verify)** — resolved from retained operator evidence under auto-mode; no code commit (evidence = base6.log + balloon11.log, repo root, untracked per session-log convention)
2. **Task 2: Flip the ledgers on evidence** - `df76ff4` (docs)

**Plan metadata:** this SUMMARY commit (docs: complete bench re-verification plan)

## Files Created/Modified

- `.planning/phases/01-command-protocol-control/01-UAT.md` — Test 3 note extended with the session-6 UPDATE (provenance, series-A lifecycle, quiet-gate holds, SC-3/WR-03/CIF/QVGA-restore clauses riding); G-01-7 root_cause extended + missing updated (both session-5 axes closed, new burst-full-delivery residual); G-01-9 flipped resolved with resolved_by/resolved_at/verified_by/artifacts_final
- `.planning/WINDOWS.md` — entries 3 (rescoped open), 5/8/9 (fixed with evidence-class reasons) in BOTH the table and JSON copy; front matter reconciled open_count 1 / fixed_count 8
- `.planning/STATE.md` — Current Status records the round outcome; Current Phase status + Next Steps set to the next bench moment (SC-3 pairs + WR-03 discriminator) + /gsd-secure-phase 1 as the remaining phase-close gate; progress bullets for 01-17..01-20 added

## Decisions Made

- G-01-9 resolved on the three-defect structure with per-defect bench evidence (sessions 5 and 6), matching the G-01-8 resolution's verified_by shape
- G-01-7 NOT closed despite two solved sub-truths — the series-A verdict truth failed 2/6 on a new named axis; house rule: never a forced close
- WINDOWS 8/9 flipped per the plan's encoded evidence-class rules, with the unstageable camera-down scenario and the unexercised retry path recorded BY NAME in the flip reasons
- Logs retained untracked in the repo root per every prior session's convention (balloon5-11.log, base5/6.log); the plan's balloon6.log naming deviation is documented in the ledgers themselves

## Deviations from Plan

### Process Deviations (documented, no rule-violation)

**1. Task 1 blocking checkpoint resolved from retained evidence under auto-mode**
- **Found during:** Task 1 (checkpoint:human-verify, bench session #6)
- **Issue:** The bench session had already run before the executor resumed (operator session 2026-08-27 evening -> 2026-08-28 ~00:20); no live verification was possible
- **Resolution:** With `auto_advance: true` active, the executor proceeded on the retained evidence (base6.log, 21,554 lines; balloon11.log, 30,252 lines), quoting operator-observed lines verbatim with line numbers/counts; every unjudged clause (SC-3 pairs, WR-03 discriminator, CIF cycle, QVGA restore, dashboard glance) was left open as unjudged — never fabricated
- **Files modified:** none beyond Task 2's ledger updates
- **Verification:** all quoted line numbers pinned by grep during evidence extraction (handoff §5)
- **Committed in:** df76ff4 (evidence lands in the ledgers)

**2. Log provenance: session-6 balloon evidence lives in balloon11.log, not balloon6.log**
- **Found during:** Task 1 evidence extraction
- **Issue:** The balloon console cycled balloon6..balloon11 captures through the day's bootloop debug saga (root cause: GPS UART1 RX on reserved OPI-PSRAM pin 35; fixed 3f2c2c6 -> GPIO 41); base6.log lines 1-~1618 are the bootloop period (170 GET_STATUS timeouts = pre-session noise)
- **Resolution:** Provenance recorded in the 01-UAT.md Test 3 note and STATE.md; balloon6-10 documented as debug captures; resolved/debug knowledge at .planning/debug/resolved/balloon-bootloops-at-bench.md
- **Committed in:** df76ff4

**3. SC-3 / WR-03 / CIF-cycle / QVGA-restore / dashboard clauses NOT exercised at session 6**
- **Found during:** Task 1 evidence extraction (queue mix: 13 CAPTURE_NOW, 849 GET_STATUS, 103 IMAGE_WINDOW_REQUEST, 2 SET_RESOLUTION — zero settings/auto-capture commands)
- **Issue:** The riding clauses remain unjudged a fifth round; Test 3 stays `issue`
- **Resolution:** Recorded as STILL-unjudged items riding the next bench moment in the G-01-7 missing list and Test 3 note; not fabricated; next-bench-moment step set in STATE.md Next Steps
- **Committed in:** df76ff4

---

**Total deviations:** 3 process deviations (evidence-provenance and honesty documentation; no code changes, no scope creep — this plan's code work ended at 01-19 by design)
**Impact on plan:** None on plan structure; all ledger flips trace to quotable retained-log lines or carry their explicit evidence-class reason

## Issues Encountered

- The pre-session bootloop saga (balloon would not boot at the bench) consumed balloon6-10.log captures; root-caused to GPS UART1 RX claiming reserved OPI-PSRAM pin 35 and fixed in 3f2c2c6 (GPIO 41) — resolved in .planning/debug/resolved/balloon-bootloops-at-bench.md. The session then ran round-#8 behavior on both boards with the wire format unchanged
- Session 6 was lossier than session 5 on the wire (41 END MARKER MISS vs 0-6; zero CRC FAIL) — image 29's larger full (238 chunks) ended INCOMPLETE 222/238 'retransmit passes exhausted' (base6.log:6337); named in the Test 3 note as early-stretch lossiness, no new defect routed (transfer-quality holds held: zero stored-bytes CRC mismatches)

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - documentation-only plan; no code stubs created.

## Next Phase Readiness

- All 20 Phase 01 plans executed. Remaining before phase complete: (1) the next bench moment — SC-3 visible-effect pairs (fifth round riding), WR-03 cadence discriminator, CIF-cycle + QVGA-restore clauses; optionally a G-01-7 burst-full-delivery residual round (levers named; interim mitigation: space captures); (2) `/gsd-secure-phase 1` (Phase 2/3 gates also pending)
- WINDOWS ledger: 1 open entry (G-01-7, entry 3) — /gsd-ship stays blocked until the residual round closes it or it is waived with a reason
- Phase 3 backlog todos captured at 898fcc6 (thumb-first image delivery, antenna-pointing overlay)

## Self-Check: PASSED

- Files exist: 01-20-SUMMARY.md, 01-UAT.md, WINDOWS.md, STATE.md (all FOUND)
- Commits exist: df76ff4 (Task 2 ledger flips), 4aad921 (plan metadata) (both FOUND)
- Task 2 automated verify: 6/6 checks exit 0 (UAT/WINDOWS contain session-log citations; STATE contains 01-20; WINDOWS id 8/9 present; open_count 1 == actual open rows)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

---
phase: 01-command-protocol-control
plan: 34
subsystem: firmware-image-transfer-debug
tags: [esp32-s3, bench-session-11, g-01-10, d1-crash-regression, g-01-7, over-rejection, twdt, stack-watermark, round-14, zero-crash-session]

requires:
  - phase: 01-command-protocol-control (plan 01-32, round #14 instruments)
    provides: the [STACK]/[TWDT]/[I2C] instrument package + §11's pre-registered predicted signatures this session reads
  - phase: 01-command-protocol-control (plan 01-33, round #14 lever)
    provides: the G-01-7 admission lever (depth 5 + supersede receipt-evidenced protection) and its two discriminator lines this session judges
provides:
  - The [TWDT] two-reading answer: ESP_OK (balloon5.log:105) — IDLE0 genuinely TWDT-subscribed, §7's WDT topology premise STANDS, the sessions-8/9 TG0WDT attribution survives, session-10's NOT_FOUND was the wrong-handle instrument bug (handle fix proven by [IDLE0] registered=1 :33)
  - The [STACK] watermark answer: 240-word boot baseline only (balloon5.log:138), ZERO new-lows whole-console — the IDLE-stack-capacity hypothesis REFUTED per §11.3's pre-written rule (caveats recorded: no crash instant; TX lighter than session 10)
  - The [I2C] coverage answer by absence: 0 write-path lines + 0 i2cWrite errors — no episode; §11.3.1's branch-b swallowing disposition stands as the coverage answer
  - Debug doc §11.6 — the session-11 record (provenance/ELF SHAs, instrument readings, crash census, ladder honesty, round-#15 routing); NO §12 (nothing crashed)
  - A NEW NAMED G-01-7 MODE: OVER-REJECTION — evictEntriesOlderThan (image_tx_manager.cpp:1381-1456) rejects REGARDLESS of capacity need when the lowest-class older candidate is class-5 (image 45's SVGA FULL starved 0/165 behind image 44's fully-SERVED COMPLETE entry at 2/5 queue occupancy); fix class routed (capacity-aware supersede admission)
  - WINDOWS entries 15/3 SESSION-11 records in BOTH copies; 01-UAT.md G-01-10/G-01-7 SESSION-11 extensions + Test 3/Test 4 note updates; STATE.md round-#15 routing
affects: [round #15 (D1 A/B hook build + G-01-7 capacity-aware admission), phase close-out, /gsd-secure-phase 1]

actuals:
  tokens: 8000
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Instrument-answer bookkeeping: a pre-registered predicted-signatures table (§11) read against live boot lines keeps the hypothesis disposition honest — the prediction was written before the data (the forward-predictive pattern first used this campaign)"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/01-34-SUMMARY.md
  modified:
    - .planning/phases/01-command-protocol-control/01-UAT.md
    - .planning/WINDOWS.md
    - .planning/debug/d1-crash-regression-push-start.md
    - .planning/STATE.md

key-decisions:
  - "D1 stays OPEN on a zero-crash session — zero-crash is survival evidence, not a fix verdict: no lever landed between sessions, the ladder's heavy-tail bars were not met (class 2 >=7-window SVGA NOT met; class 3 >=3-min dwell NOT met at ~50 s), and the session's TX-heaviest stretch was lighter than session 10's — the §11.3 pre-written refutation rule is applied with its caveats recorded verbatim in §11.6"
  - "The wedge family (sessions 7/8/9's scheduler/tick-chain class) re-ranks as the remaining D1 candidate class now that starvation (sessions 10/11) and stack capacity (session 11) are both disconfirmed — the §11.4 A/B hook-unregistered build (G01_D1_IDLE_HOOK_DISABLED) rises to the round-#15 discriminator"
  - "G-01-7 verdict = engaged-and-failing (a distinct recorded verdict, never conflated with never-engaged): the round-#14 lever's honest-rejection form ENGAGED and produced a NEW named mode, OVER-REJECTION — every rejection was legitimate-shaped while a safely-reclaimable SERVED entry was preserved regardless of capacity need"
  - "Fix class routed, not patched this round: capacity-aware supersede admission (force-evict only on actual capacity need; SERVED/complete entries as the safe reclaim class) — per the MUST-NOT-fix-new-bench-defects-inside-this-plan prohibition, the routing carries to round #15 as the companion lever"
  - "Riding clauses recorded NOT RUN (tenth round): SC-3 pairs, WR-03 cadence discriminator, QVGA restore, dashboard LOOK — the session reached neither the ladder's heavy-tail bars nor series A at full breadth; WR-01/WR-02 watches green again (0 Critical battery, 0 health-check-failed)"

patterns-established:
  - "Over-rejection as a distinct failure shape: honest-looking rejections can still be a starvation mode when the discriminator is the starved-to-SERVED-waste ratio (0/165 starved while a COMPLETE entry occupied a slot it did not need) — recorded beside the known queue-overflow starvation in WINDOWS entry 3"

requirements-completed: []

coverage:
  - id: D1
    description: "The [TWDT] two-reading answer ([TWDT]=ESP_OK balloon5.log:105; registered=1 :33), the [STACK] trajectory read (240-word baseline :138 the ONLY watermark line, zero new-lows), and the whole-console crash census (rst:0x7/rst:0xc/Guru/canary/i2cWrite/corrected-raw-byte-mojibake all 0) recorded in debug doc §11.6 + ledgers"
    requirement: CTRL-01
    verification:
      - kind: other
        ref: "Executor greps over the operator-retained logs: [TWDT]=ESP_OK at :105; [STACK] 240 words at :138 sole watermark line; rst:0x7=0, rst:0xc=0, Guru=0, canary=0, i2cWrite=0, mojibake raw-byte=0; [BOOT] POWERON :104 single boot; [MEM] x40 heap floor 8310804 :807"
        status: pass
    human_judgment: false
  - id: D2
    description: "D1 disposition recorded honestly OPEN: §11.6 carries provenance (round-#14 ELFs SHA-verified on-disk: balloon 32d31f27..., base ce65709...), the ladder-honesty bars (class 1 passed 4th session; class 2/3 NOT met), the §11.3 refutation with caveats, and the round-#15 routing (A/B hook build + heavy-tail re-run); NO §12 — nothing crashed"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "sha256sum .pio/build/esp32-s3-balloon/firmware.elf + esp32-s3-basestation match the pre-flight SHAs; grep SESSION-11 in §11.6; WINDOWS entry 15 stays open, counts 2/17/19 in both copies"
        status: pass
    human_judgment: true
    rationale: "The bench session itself is operator-run hardware evidence; the executor reads and records it but the D1 root-cause verdict stays an open campaign question routed to round #15"
  - id: D3
    description: "G-01-7 disposition = engaged-and-failing via the NEW over-rejection mode, recorded with every step a quoted line: 8 rejections balloon5.log:883/:896/:916/:944/:986/:1011/:1034/:1056 ↔ NACK_INVALID seq 16/17/18/20/21/23/24/25 base5.log:403-:516, two finalize INCOMPLETE 0/165 lifecycles (:456-:457/:522-:523), mechanism evictEntriesOlderThan image_tx_manager.cpp:1381-1456, thumbs immune :1162, zero command timeouts 4th session"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "Executor greps: 8 reject lines + 8 base NACK lines at the cited coordinates; zero 'timeout after 3 retries' both logs; zero avoid-form/queue-overflow/evict lines; source read of the eviction scan confirms the lowest-class-match forces eviction"
        status: pass
    human_judgment: false
  - id: D4
    description: "The riding clauses (SC-3 pairs, WR-03 cadence discriminator, QVGA restore, dashboard LOOK) recorded NOT RUN — tenth round — in Test 3/Test 4 notes with the session's actual reach (ladder class 1 + spaced captures only); Tests 1/2/5 untouched (no regression evidence)"
    requirement: CTRL-03
    verification: []
    human_judgment: true
    rationale: "Non-reach is recorded honestly per the plan's step-11 rule; these clauses ride the round-#15 shared bench session"

duration: ~4h across continuation executor sessions + operator bench session #11 (2026-08-29 ~01:05 local)
completed: 2026-08-29
status: complete
---

# Phase 01 Plan 34: Bench Session #11 Summary

**The campaign's first zero-crash session answers the round-#14 instruments — [TWDT]=ESP_OK settles the WDT premise, [STACK] refutes stack capacity, [I2C] answers by absence — while G-01-7's lever ENGAGES in honest-reject form and FAILS via a NEW over-rejection mode (image 45's SVGA FULL starved 0/165 behind a fully-SERVED COMPLETE entry); both blockers stay open, counts 2/17/19, round #15 routed (D1 A/B hook-unregistered build + ladder heavy-tail re-run, G-01-7 capacity-aware supersede admission as companion lever, one shared bench session).**

## Performance
- **Duration:** ~4h across continuation executor sessions + the operator bench session #11 (logs written 2026-08-29 ~01:05 local)
- **Completed:** 2026-08-29
- **Tasks:** 2 completed (Task 1 = operator-run blocking checkpoint, resolved on console evidence; Task 2 = ledger flips)
- **Files:** 5 (1 new summary, 4 ledger/debug/state docs)

## Accomplishments
- **The [TWDT] two-reading question is RESOLVED:** `[TWDT] idle0 wdt status=ESP_OK` (balloon5.log:105) — exactly §11's config prediction. IDLE0 genuinely IS TWDT-subscribed: §7's WDT topology premise STANDS, the sessions-8/9 TG0WDT attribution survives, and session-10's ESP_ERR_NOT_FOUND is confirmed as the wrong-handle instrument bug. The fixed handle is proven live by `[IDLE0] hook cpu0 registered=1` (:33 — the printf fix reading 1).
- **The [STACK] watermark REFUTES stack capacity:** the 240-word boot baseline (balloon5.log:138, t=2392 ms) is the ONLY `[IDLE0] stack watermark` line whole-console — zero new-low lines through every phase including all multi-window service. Per §11.3's pre-written rule this routes AWAY from the stack fix; §11.6 records the honest caveats (no crash instant to read; the session's TX-heaviest stretch was lighter than session 10's — AUX-missed x40 vs x81, largest SERVED image 41 chunks vs 119).
- **The [I2C] coverage answer lands by absence:** zero `[I2C]` lines and zero i2cWrite errors whole-console — no episode occurred, so §11.3.1's branch-b swallowing disposition stands as the coverage answer (the instrument can only speak on an episode).
- **The campaign's first zero-crash session is on record with a full census:** single boot ([BOOT] POWERON :104), B2 zero, [MEM] x40 healthy (heap floor 8310804 :807), and rst:0x7 / rst:0xc / Guru Meditation / stack canary / i2cWrite / corrected-raw-byte mojibake each grep-counted ZERO. NO §12 — §11.6 IS the session's debug-doc record.
- **G-01-7's lever verdict is engaged-and-failing via a NEW named mode — OVER-REJECTION:** with the queue at 2/5 slots, the sole older-than-incoming candidate was image 44's fully-SERVED COMPLETE entry, yet evictEntriesOlderThan (image_tx_manager.cpp:1381-1456) returned false — lowest-class-match forces eviction REGARDLESS of capacity need. Image 45's SVGA FULL (32850 B / 165 chunks, camera re-init 6->11 zero FB-OVF) starved 0/165: EIGHT rejections (balloon5.log:883/:896/:916/:944/:986/:1011/:1034/:1056) mirroring NACK_INVALID seq 16/17/18/20/21/23/24/25 (base5.log:403-:516) and two finalize INCOMPLETE 0/165 lifecycles (:456-:457, :522-:523). Thumbnails immune (kind-0 bypasses the older-candidate scan at :1162 — image 45's thumb 6/6 while its FULL starved). Zero avoid-form lines, zero queue-overflow, zero evict lines, zero command timeouts (4th consecutive session). Fix class routed: capacity-aware supersede admission.
- **The D1 ladder is read honestly:** class 1 passed a FOURTH session (image 44 first-post-boot push both kinds COMPLETE, base5.log:99/:275 — thumb 1194 B/6 ch, full 8176 B/41 ch across 3 windows); class 2's >=7-window SVGA bar NOT met (image 45's full never completed — it IS the over-rejection evidence); class 3's >=3-min dwell NOT met (~50 s, [BCN] seq=27 :641 -> seq=37 :767). D1 STAYS OPEN; the wedge family is the remaining candidate class and the §11.4 A/B hook-unregistered build rises to discriminator.
- **Ledgers flipped/extended strictly on quoted evidence:** 01-UAT.md G-01-10 SESSION-11 EXTENSION + G-01-7 SESSION-11 UPDATE + Test 3/Test 4 note updates; WINDOWS entries 15/3 SESSION-11 records in BOTH copies (table + JSON), front-matter counts unchanged 2 open / 17 fixed / 19 total; STATE.md Current Status + round-#15 routing + footer. WR-01/WR-02 green again (0 Critical battery, 52 batt=valid; 0 health-check-failed).
- **Provenance chain intact:** round-#14 ELFs SHA-verified on-disk by the executor (balloon 32d31f270cb8..., base ce657093cb0a... matching the pre-flight values), build banner Aug 28 2026 23:13:40, single boot; the console SHA banner's absence is expected (nothing panicked).

## Task Commits
1. **Task 1: Bench session #11 checkpoint** — operator-resolved (balloon5.log/base5.log evidence); no executor commit
2. **Task 2: Ledger flips on evidence** — `docs(01-34): bench session #11 evidence + ledger flips` (hash in git log; 01-UAT.md, WINDOWS.md, debug doc §11.6)
3. **Final metadata commit** — `docs(01-34): complete bench re-verification session #11 plan` (SUMMARY + STATE + ROADMAP)

## Files Created/Modified
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-10 root_cause SESSION-11 EXTENSION (instruments answered, stays open); G-01-7 root_cause SESSION-11 UPDATE (over-rejection mode, stays open); Test 3 note (D1 survival outcome, SC-3/QVGA tenth round riding, WR-01/WR-02 green); Test 4 note (WR-03 tenth round riding)
- `.planning/WINDOWS.md` - entries 15 and 3 SESSION-11 records in BOTH copies; counts 2/17/19 unchanged; front-matter last_updated bumped
- `.planning/debug/d1-crash-regression-push-start.md` - §11.6 SESSION-11 READING appended (provenance, instrument answers, census, ladder honesty, round-#15 routing); NO §12
- `.planning/STATE.md` - 01-34 outcome in Current Status; Next Steps item 1 = round #15 routing (A/B hook build + heavy-tail ladder + capacity-aware admission, one shared bench session); security gate + six pending operator confirmations noted; footer updated
- `.planning/phases/01-command-protocol-control/01-34-SUMMARY.md` - this file

## Decisions Made
- Zero-crash is NOT a flip: both gaps stay open — the plan's own bar (full ladder + passing series A + instrument-consistent readings) was not met, and the standing honesty convention forbids closing on a survival stretch
- The §11.3 refutation is applied WITH its caveats verbatim (no crash instant; lighter TX load than session 10) — the stack-capacity hypothesis is refuted for this session's conditions, and the heavy-tail re-run at round #15 re-tests under session-10-class load
- The wedge family re-ranks as the remaining candidate; the §11.4 A/B hook-unregistered build (G01_D1_IDLE_HOOK_DISABLED) is the round-#15 discriminator — if the A/B build survives what wedged the round-10..13 builds, hook dispatch is in-family; if it still wedges, the family is kernel-side below the hook
- OVER-REJECTION is recorded as a DISTINCT mode beside the known queue-overflow starvation (WINDOWS entry 3): the discriminator is the starved-to-SERVED-waste ratio, not the rejection count
- Riding clauses recorded NOT RUN (tenth round), never fabricated; requirements-completed: [] per the failed-session convention (01-24/01-27/01-29/01-31)

## Deviations from Plan

None — plan executed exactly as written. Task 1 resolved on operator console evidence (balloon5.log/base5.log, the requested names, no collision with session #5's files); Task 2 applied the failure-path branches (both gaps stay open) exactly as the plan's action items specify for a session that does not meet its acceptance bars.

## Issues Encountered
None blocking. The session's substantive findings (over-rejection mode; the unmet ladder bars) are recorded outcomes, not execution obstacles. All executor greps verified against the retained logs before any ledger write.

## Known Stubs
None — documentation-only plan; no code, no skipped tests, no unrun executor verify (the plan's Task 2 verify chain ran green; the bench steps that did not run are recorded as NOT RUN, never as passes).

## Next Phase Readiness
- **Round #15 is fully routed (STATE.md Next Steps item 1):** D1 A/B hook-unregistered build (G01_D1_IDLE_HOOK_DISABLED, recipe §11.4) + the ladder heavy-tail re-run (>=7-window SVGA, >=3-min dwell, series A) + the G-01-7 capacity-aware supersede admission fix as companion lever — one shared bench session.
- **WINDOWS stays 2 open / 17 fixed / 19 total:** entry 15 (D1 — instruments answered, wedge family the remaining candidate) and entry 3 (G-01-7 — engaged-and-failing, over-rejection named). /gsd-ship stays blocked until a clean bench session closes them.
- **The remaining phase-close gates:** the round-#15 shared bench session, then `/gsd-secure-phase 1` (Phase 2/3 gates also pending). Six auto-advanced disposition confirmations (01-23/01-25/01-26/01-28/01-30/01-32) remain end-of-phase operator decisions.
- WR-01/WR-02 watches green; the interim spaced-captures discipline stands unchanged (session #11 was crash-free but also the campaign's lightest TX session — no fix verdict exists).

## Self-Check: PASSED

- Found: .planning/phases/01-command-protocol-control/01-34-SUMMARY.md
- Found: commit e2b1ff2 (docs(01-34): bench session #11 evidence + ledger flips)
- Found: commit 83ab6b1 (docs(01-34): complete bench re-verification session #11 plan)
- Found: debug doc SESSION-11 reading (§11.6); NO §12 (nothing crashed)
- Found: 01-UAT.md SESSION-11 markers (G-01-10 + G-01-7)
- Found: WINDOWS.md SESSION-11 records x4 (entries 15/3, table + JSON); counts 2/17/19; JSON ids 15/3 present
- Plan verify chain green: "01-34" in STATE.md; secure-phase + watermark in STATE.md; G-01-10 count 7 in 01-UAT.md
- Deletion check: zero file deletions in both commits

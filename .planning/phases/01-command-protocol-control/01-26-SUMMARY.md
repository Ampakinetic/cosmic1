---
phase: 01-command-protocol-control
plan: 26
subsystem: command-protocol-control
tags: [review-routing, windows-ledger, psram-degradation, health-check, led-truth, uint32-widening]

requires:
  - "01-VERIFICATION.md re-verification #9 gap 4 (the three confirmed 7d96a98 warnings, verifier re-derived at HEAD)"
  - "01-25 post-debug-round source state (enqueueCapture read fresh; :276-283 citations were pre-01-25 line numbers)"
provides:
  - "WINDOWS entries 17-19 — review 7d96a98's WR-01/WR-02/WR-03 routed AND dispositioned fixed with evidence classes (the unrouted-findings gap's routing half AND decision half closed)"
  - "enqueueCapture full-buffer ps_malloc failure fall-through (src/image_tx_manager.cpp) — the degradation ladder keeps its cheapest payload under PSRAM exhaustion"
  - "honest camera health-check branch (src/main_balloon.cpp) — healthy active camera warns nothing; absent handle warns naming it; inactive camera gets the skipped-note"
  - "uint32 ackedAtLastPoll end-to-end (src/main_basestation.cpp) — each 65536-ACK wrap can no longer skip a lastAckTime refresh"
  - "WR-02's stageable discriminator carried into the 01-27 bench (every healthy boot discriminates)"
affects:
  - "01-27 bench session (boot windows judge WR-02 live alongside D1/D2 discriminators; all three fixes ride the reflash)"
  - "01-VERIFICATION gap 4 (closed by this round)"
  - "end-of-phase operator confirmation list (this round's option-a disposition joins 01-23's fix-all as pending human confirmations)"

actuals:
  tokens: 12000   # chars/4 over the realized diff (2 production commits 38,390 B + docs closeout)
  tasks: 3
  commits: 2

tech-stack:
  added: []
  patterns:
    - "degradation-ladder fall-through on allocation failure (mark unavailable + named log + fall through to the cheaper payload, guarded by the existing both-empty check)"
    - "handle-presence health check (warn only when the driver handle is genuinely absent; honest skipped-note for disabled subsystems)"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/01-26-SUMMARY.md
  modified:
    - .planning/WINDOWS.md
    - .planning/phases/01-command-protocol-control/COVERAGE.md
    - src/image_tx_manager.cpp
    - src/main_balloon.cpp
    - src/main_basestation.cpp

key-decisions:
  - "Task 2 disposition auto-selected: option-a (fix all three) at the gate=blocking checkpoint under workflow.auto_advance=true — the plan's own 01-23/01-25 house convention (recommended-first-option auto-select recorded for end-of-phase operator confirmation); no waive, no defer, hence no waive reasons required"
  - "Evidence classes per the entries 8-14 convention: WR-01/WR-03 closed on code fix + builds 2/2 + harness exit 0 with their unstageable triggers recorded BY NAME (on-demand PSRAM exhaustion / a 65536-ACK wrap between polls); WR-02 is the stageable exception — entry 18 names the 01-27 healthy-boot discriminator as its live check"
  - "WR-01 log made unconditional (mirroring the oversize-skip branch and the review's fix shape), not DEBUG_IMAGE_TX-gated: it is a named degradation event in the same class as the oversize skip, and the verify greps pin the renamed literal"

patterns-established:
  - "Routing-before-disposition: confirmed review findings enter the ledger open-first (Task 1) regardless of the later disposition decision, so the routing half of the convention can never depend on the checkpoint outcome"

requirements-completed: [IMG-02, IMG-03, CTRL-06]

coverage:
  - id: D1
    description: "WINDOWS entries 17-19 routed in BOTH copies (markdown table + JSON) with descriptions citing review 7d96a98 + re-verification #9, front-matter counts reconciled (final: 3 open / 16 fixed / 19 total, open ids exactly {3,15,16}), COVERAGE.md round-#11 re-check line appended"
    requirement: IMG-02
    verification:
      - kind: other
        ref: "grep '\"id\": 17|18|19' .planning/WINDOWS.md; node JSON re-parse (19 entries, ids sequential, counts match front matter and 19 table rows); grep 'round-#11' COVERAGE.md"
        status: pass
    human_judgment: false
  - id: D2
    description: "WR-01 fix: enqueueCapture full-buffer ps_malloc failure no longer early-returns — nullptr/0/0 full with the renamed 'full dropped, thumbnail still pushes' log, falls through to the unchanged thumbnail branch, both-empty guard intact; unstageable trigger (on-demand PSRAM exhaustion) recorded by name in entry 17 per the entries 8-14 convention"
    requirement: IMG-03
    verification:
      - kind: other
        ref: "git show 8041b27 (diff confined to the ps_malloc block); builds 2/2 SUCCESS; node scripts/verify_protocol_roundtrip.mjs exit 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "WR-02 fix: honest camera health-check branch — healthy active camera warns nothing, absent sensor handle warns naming it, inactive camera gets the skipped-note SYS_WARNING in the neighboring power/communication shape; rides the 01-27 reflash"
    verification:
      - kind: other
        ref: "source-visible at src/main_balloon.cpp performSystemChecks; builds 2/2 SUCCESS; harness exit 0"
        status: pass
    human_judgment: true
    rationale: "The one stageable fix of the round: its live discriminator is the 01-27 bench session's boot windows (pre-fix firmware logged 'Camera system health check failed' on every healthy boot; post-fix a healthy boot shows no camera warning). Entry 18 records this check by name; a bench-free auto-pass would fabricate the runtime truth this project prohibits."
  - id: D4
    description: "WR-03 fix: ackedAtLastPoll widened to uint32 with both truncating static_cast<uint16_t> calls deleted from the LED-truth poll site; counter, poll cadence, lastAckTime assignment, staleness thresholds untouched; unstageable trigger (a 65536-ACK wrap between polls) recorded by name in entry 19"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "git show 8041b27 (2 hunks: member :81 + poll site); negative grep — zero 'static_cast<uint16_t>(acked)' in src/main_basestation.cpp; builds 2/2 SUCCESS; harness exit 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "Per-finding disposition decisions recorded (fix/fix/fix, option-a) with the auto-advance provenance and the pending end-of-phase operator confirmation"
    verification: []
    human_judgment: true
    rationale: "01-VERIFICATION Human Verification #5 makes fix-vs-waive a maintainer judgment; under workflow.auto_advance the recommended option auto-selected per the 01-23 house convention (mirrored by 01-25 Task 2). The operator's end-of-phase confirmation is pending for BOTH this round's option-a and 01-23's fix-all (entries 10-14) — one confirmation message covers both."

duration: 9min
completed: 2026-08-28
status: complete
---

# Phase 01 Plan 26: Review 7d96a98 Finding Routing + Companion Fixes Summary

**One-liner:** The fresh adversarial review's three confirmed warnings (7d96a98: PSRAM-exhaustion thumbnail drop, inverted boot health check, uint16 ACK-counter truncation) routed as WINDOWS 17-19 and ALL FIXED per the auto-selected option-a disposition — ps_malloc failure now falls through to the thumbnail branch with a named degradation log, the camera health check warns only on a genuinely absent sensor handle (stageable at every 01-27 healthy boot), and ackedAtLastPoll is uint32 end-to-end — builds 2/2, harness exit 0, ledger reconciled at 3 open / 16 fixed / 19 total.

## Performance

- **Duration:** ~9 min (546 s)
- **Started:** 2026-08-28T09:44 (ledger clock; 2026-08-27T21:44:14Z UTC)
- **Completed:** 2026-08-28T09:53
- **Tasks:** 3 (2 auto + 1 decision checkpoint)
- **Files modified:** 5 (+ SUMMARY/STATE/ROADMAP closeout)

## Accomplishments

- **01-VERIFICATION gap 4 closed on both halves**: every confirmed 7d96a98 finding lives in the ledger (routing, Task 1) AND carries a decided terminal disposition (decision, Task 2; implementation, Task 3). The unrouted-findings class that round #9's routing closed and the new review reopened is closed again — this time with the fixes in the same round.
- **The degradation ladder survives PSRAM exhaustion**: a failed full-buffer allocation degrades to thumbnail-only instead of dropping the capture from the airlink entirely (consistent with the oversize path and the thumb-alloc-failure path; the both-empty guard still refuses to queue a nothing-transferable capture).
- **The boot warning channel tells the truth**: two review cycles of "every healthy camera reports failed" ends — WR-02 is the rare bench-stageable fix, and its discriminator (a healthy boot with no camera warning) rides the 01-27 session's boot windows.
- **The LED-truth refresh is wrap-proof**: the uint32 counter compares against a uint32 snapshot with zero truncating casts; one-missed-refresh-per-65536-ACKs is gone.

## Task Commits

1. **Task 1: Route WR-01..WR-03 as WINDOWS entries 17-19 + COVERAGE re-check line** — `0e84ca1` (docs)
2. **Task 2: Disposition decision** — checkpoint:decision gate=blocking, auto-advanced (see selection record below; no commit)
3. **Task 3: Implement the approved fixes + builds + harness + entry updates** — `8041b27` (fix)

**Plan metadata:** this commit (docs: complete plan)

## Files Created/Modified

- `.planning/WINDOWS.md` — entries 17-19 added (Task 1) and flipped fixed with evidence classes (Task 3), in BOTH copies; front matter 3 open / 0 waived / 16 fixed / 19 total
- `.planning/phases/01-command-protocol-control/COVERAGE.md` — round-#11 re-check line (no external API surface)
- `src/image_tx_manager.cpp` — WR-01 fall-through restructure in enqueueCapture's fullArmable branch
- `src/main_balloon.cpp` — WR-02 honest health-check branch in performSystemChecks
- `src/main_basestation.cpp` — WR-03 uint32 widening (appState member :81 + poll site :2205-2210)

## Verification

- **Task 1 verify chain: PASS** — ids 17/18/19 present in the JSON copy; 6 occurrences of "7d96a98" and 6 of "re-verification #9" (3 table + 3 JSON descriptions); round-#11 token in COVERAGE.md. One verify-loop iteration: the first COVERAGE draft wrote "round #11" (spaced) and failed the plan's literal `grep "round-#11"` — corrected to the hyphenated token before the Task 1 commit.
- **Task 3 verify chain: PASS** — `pio run -e esp32-s3-balloon -e esp32-s3-basestation` exit 0 with SUCCESS for both environments (the one `CAMERA_MODEL_ESP32S3_EYE redefined` warning is pre-existing, out of scope); `node scripts/verify_protocol_roundtrip.mjs` exit 0 (all checks PASS); per-entry awk status extraction + source-pattern greps all PASS; negative grep — zero `static_cast<uint16_t>(acked)` in src/main_basestation.cpp.
- **Protected surfaces (diff-scoped, git show 8041b27):** exactly 4 files; image_tx_manager.cpp diff confined to the ps_malloc block (oversize-skip, thumb-alloc-failure, both-empty guard, and the 01-25 [MEM]/warm-up instrumentation untouched); main_balloon.cpp diff confined to the camera health-check block; main_basestation.cpp diff confined to the member + poll site (cadence and thresholds untouched). Pre-existing working-tree noise (.pio checksum, config.json, 03-UAT.md, untracked logs) left unstaged and uncommitted.

## Task 2 Selection Record (operator confirmation point)

Auto-selected under `workflow.auto_advance: true` at the `checkpoint:decision gate="blocking"` per the plan's own 01-23/01-25 house convention: **option-a — fix all three this round** (also the plan's stated recommendation). Per-finding: WR-01 fix, WR-02 fix, WR-03 fix; zero waives, zero defers. Recorded in all three ledger entries' reason fields with the auto-advance provenance. **Pending end-of-phase operator confirmations now number two:** this round's option-a and 01-23's fix-all (entries 10-14) — one confirmation message covers both. An operator who disagrees can route back: each fix is small, guarded, and independently revertible at its named site.

## Decisions Made

- Routing-before-disposition sequencing honored: Task 1 routed all three open-first unconditionally, so the convention's routing bar is met regardless of the checkpoint outcome.
- WR-01's renamed degradation log is unconditional (matching the oversize-skip branch's shape and the review's fix shape) rather than DEBUG_IMAGE_TX-gated — it names a real degradation event the operator must see, and the plan's verify pins the literal.
- WR-02's inactive-camera note uses the neighboring power/communication SYS_WARNING shape ("Camera system health check skipped - camera inactive"), per the plan's explicit instruction to mirror that shape.

## Deviations from Plan

None - plan executed exactly as written. The Task 2 auto-selection is the plan's own documented convention (its context block cites the 01-23 precedent and the end-of-phase confirmation), not a deviation; the one verify-loop text correction (round #11 → round-#11) was caught and fixed inside the Task 1 acceptance gate before commit.

## Issues Encountered

None.

## Known Stubs

None. The three fixes are complete implementations at their named sites; the bench-unexercised status of WR-01/WR-03 is recorded honestly inside their ledger entries (unstageable triggers by name), not stubbed.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

**Ready for 01-27** (the bench re-verification round). All three fixes ride the reflash; the session's discriminators now include, alongside D1 (crash signatures + [MEM] anomalies) and D2 (zero spurious 're-announce held' boot lines): **WR-02's healthy-boot check — no 'Camera system health check failed' line on any healthy boot**. The ledger stands at 3 open (3 = G-01-7 burst full-delivery, 15 = D1 crash, 16 = D2 spurious hold) / 16 fixed / 19 total; /gsd-ship stays blocked on the three open entries pending the 01-27 bench. End-of-phase operator items carried: the 01-23 fix-all confirmation, this round's option-a confirmation, and the standing SC-3 visible-effect pairs + WR-03 cadence discriminator + CIF/QVGA clauses from the riding bench-moment list.

## Self-Check: PASSED

- `.planning/phases/01-command-protocol-control/01-26-SUMMARY.md` — FOUND
- Task 1 commit `0e84ca1` — FOUND in git log
- Task 3 commit `8041b27` — FOUND in git log
- All five modified files — FOUND (WINDOWS.md, COVERAGE.md, image_tx_manager.cpp, main_balloon.cpp, main_basestation.cpp)
- No file deletions in either commit (diff-filter=D empty for both)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

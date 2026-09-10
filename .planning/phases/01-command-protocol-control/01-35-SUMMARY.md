---
phase: 01-command-protocol-control
plan: 35
subsystem: firmware
tags: [esp32-s3, lora, image-transfer, queue-eviction, receipt-evidence, g-01-7, round-15]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (rounds #10-#14)
    provides: "evictionClassOf shared ranked eviction (01-33, unforked discipline T-01-33-02); fullAcked[16] WINDOW_COMPLETE receipt merge in handleImageAck + KIND_FAILED_CRC clearing; 01-12 mid-service deferral guard; IMG_TX_QUEUE_DEPTH 5"
provides:
  - "G-01-7 round-#15 lever at code level: capacity-aware supersede admission — the supersede scan runs only when the queue is full (free-slot gate), a fully-receipted older entry is the class-0 safe-reclaim victim ahead of any in-flight receipt-evidenced class-5 entry, and a named class-0 discriminator line makes engaged-and-passing / engaged-and-failing / never-engaged distinct 01-36 bench verdicts"
  - "Session-11 over-rejection mode structurally dead: a FULL window request at partial queue occupancy can never again be refused by the supersede path"
  - "Round #15 ready for the 01-36 shared bench session (this lever + the §38.10 [SCHEDSNAP] interrogation + series-A verdicts)"
affects: [01-36 bench session, phase-01 verification, /gsd-verify-work]

# Actuals (#2632) — same estimateTokens scale (chars/4 over the realized diff)
actuals:
  tokens: 17474    # 69,897 chars over the realized 4-file diff (bulk = the WINDOWS/UAT ledger notes)
  tasks: 2
  commits: 1       # MEASURED: git rev-list --count ea867f7..HEAD at SUMMARY write (= the fix commit; the docs commit follows)

# Tech tracking
tech-stack:
  added: []        # no new libraries, files, or protocol constants
  patterns:
    - "Capacity-aware admission gate: eviction scans run only when capacity is actually needed (a free slot admits untouched)"
    - "Receipt-evidence-ranked safe-reclaim class (0) inside the ONE shared evictionClassOf — both the enqueue-overflow and supersede scans inherit it, never forked"
    - "File-local predicate over existing entry state (fullReceiptComplete) — no header change, no new receipt surface"

key-files:
  created: []      # no new files (plan-specified: no new files in src/ or include/)
  modified:
    - src/image_tx_manager.cpp
    - .planning/WINDOWS.md
    - .planning/phases/01-command-protocol-control/01-UAT.md
    - .planning/phases/01-command-protocol-control/COVERAGE.md

key-decisions:
  - "G-01-7 round-#15 lever shipped code-level only: capacity-aware supersede admission = free-slot gate (supersede scan runs only when the queue is full) + shared class-0 safe-reclaim rank in evictionClassOf via file-local fullReceiptComplete + named class-0 discriminator line — queue policy only in image_tx_manager.cpp, no wire/timing change"
  - "The lever answers the session-11 over-rejection geometry (image 45 starved 0/165 at 2/5 occupancy) — a partial-occupancy FULL request can never be refused by the supersede path again; at full occupancy a fully-receipted entry is the safe victim ahead of in-flight receipt-evidenced entries"
  - "G-01-7 STAYS OPEN with ZERO bench claims — closure requires 01-36's operator-observed series-A verdicts (engaged-and-passing / engaged-and-failing / never-engaged separated by the discriminator lines); the 4 plan requirements stay unmarked (shared-ID gate: 01-36 declares the same IDs)"
  - "WR-02 companion verified ALREADY FIXED upstream (receipt stamp after full range validation, image_tx_manager.cpp:1436-1463) — verified in-source, not re-fixed"

patterns-established:
  - "Safe-reclaim-before-in-flight: at a full queue, receipt-complete entries (class 0) are reclaimed ahead of in-flight receipt-evidenced entries (class 5) — the base provably holds every byte"
  - "Capacity-need gating: forcing eviction is reserved for actual capacity need; partial-occupancy requests bypass the supersede scan entirely (no lines printed)"

requirements-completed: []  # all four plan requirements (CTRL-01, CTRL-03, CTRL-06, PRI-02) are BLOCKED by the shared-ID gate (#2388): sibling plan 01-36 declares the same IDs and has no summary; they flip at 01-36's bench-verdict round, which is also the evidence round this plan's prohibition routes closure to

coverage:
  - id: D1
    description: "Capacity-aware supersede admission lever in src/image_tx_manager.cpp: free-slot gate at the supersede call site, shared class-0 safe-reclaim ranking (fullReceiptComplete helper), named class-0 discriminator line — queue policy only, all protected surfaces intact"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — 2 succeeded"
        status: pass
      - kind: other
        ref: "node scripts/verify_protocol_roundtrip.mjs — all wire-format checks pass, exit 0"
        status: pass
      - kind: other
        ref: "Task 2 verify grep chain (fullReceiptComplete / safe reclaim / round-#14 reject line / CMD_TX_CHANNEL_QUIET_MS = 2000;) — ALL PASS"
        status: pass
      - kind: other
        ref: "PRI-01 source gates: beacon early-return :305 < pushPending :311; preemption :968-973 < findActiveEntry :981; exactly one evictionClassOf (:443) called by both scans (:736, :2052)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Ledger honesty notes: WINDOWS.md entry 3 ROUND-#15 LEVER SHIPPED note in BOTH the markdown-table and JSON copies (counts unchanged 2 open / 17 fixed / 19 total; entry STAYS OPEN), 01-UAT.md G-01-7 ROUND-#15 UPDATE (names the WR-02-already-fixed verification explicitly, zero bench claims), COVERAGE.md round-#15 re-affirmation"
    verification:
      - kind: other
        ref: "grep chain: ROUND-#15 present in 01-UAT.md; 'ROUND-#15 LEVER SHIPPED' count 2 in WINDOWS.md; node JSON.parse of the WINDOWS json block — valid, 21 entries, entry 3 status open with the note present"
        status: pass
    human_judgment: false
  - id: D3
    description: "Round-#15 bench separability: the named class-0 line + surviving round-#14 lines make engaged-and-passing / engaged-and-failing / never-engaged distinct verdicts at the 01-36 shared bench session (this lever flashed at HEAD alongside the round-#25 instruments)"
    verification: []
    human_judgment: true
    rationale: "The verdicts require operator-observed bench evidence at 01-36 (the plan's own prohibition: zero bench claims in this round; closure flips only on the series-A run) — not automatable at authoring time"

# Metrics
duration: 13min
completed: 2026-09-10
status: complete
---

# Phase 01 Plan 35: G-01-7 Capacity-Aware Supersede Admission Summary

**Round-#15 lever shipped at code level: the supersede scan now runs only when the queue is full (free-slot gate), and at a full queue a fully-receipted entry (every WINDOW_COMPLETE receipt bit set) is the class-0 safe-reclaim victim ahead of any in-flight receipt-evidenced entry — the session-11 over-rejection mode is structurally dead, behind 2/2 builds + harness exit 0, with zero bench claims.**

## Performance

- **Duration:** 13 min (761 s)
- **Started:** 2026-09-10T04:48:01Z
- **Completed:** 2026-09-10T05:01:51Z (state updates) / SUMMARY 05:05Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Capacity-need gate at the supersede call site (image_tx_manager.cpp:1496-1506): `evictEntriesOlderThan` runs ONLY when the queue holds no free slot; a FULL window request at partial occupancy is admitted untouched — no scan, no eviction, no reject line — directly negating the session-11 geometry (image 45's SVGA FULL starved 0/165 at 2/5 occupancy behind image 44's fully-SERVED COMPLETE entry, balloon5.log:883-:1056)
- Shared `evictionClassOf` extended IN PLACE (:443, unforked — overflow scan :736 and supersede scan :2052 both inherit it, T-01-33-02 discipline) with a class-0 safe-reclaim rank via the new file-local `fullReceiptComplete` helper (:410: fullTotalChunks > 0 AND every fullAcked WINDOW_COMPLETE bit set — evidence handleImageAck already merges at :1627 and KIND_FAILED_CRC already clears; no header change); thumb-only entries never rank class 0
- Named discriminator line (:2096): `ImageTx: supersede victim image %u fully receipt-confirmed - safe reclaim class 0, every WINDOW_COMPLETE receipt in (G-01-7)` prints ahead of the generic evicted line for every class-0 victim; the generic line still prints for EVERY eviction (bench-log tooling greps both)
- Composition guards held verbatim: 01-12 mid-service deferral precedes everything; class-5 refuse path survives with its narrowed honest domain (full queue AND every older non-mid-service candidate in-flight receipt-evidenced); round-#14 discriminator lines byte-intact (:1504 honest-reject, :2083 avoid-form); KIND_COMPLETE-frees-entry (:1658) untouched
- Ledgers carry the ship honestly: WINDOWS entry 3 + 01-UAT G-01-7 round-#15 notes in both copies/formats, counts unchanged (2 open / 17 fixed / 19 total), entry STAYS OPEN, ZERO bench claims; COVERAGE round-#15 re-affirmation

## Task Commits

The plan places the single fix commit inside Task 2 covering all four of the plan's files (Task 1's code + Task 2's ledger notes) with the stated message — mirroring round #14's 01-33 structure (61e19b7):

1. **Task 1 + Task 2: the lever, the source gates, and the ledger notes** - `3d2dd84` (fix) — `fix(01-35): G-01-7 capacity-aware supersede admission — free-slot gate + fully-receipted safe-reclaim class per the session-11 over-rejection routing` (4 files)

**Plan metadata:** this docs commit (SUMMARY + STATE + ROADMAP)

## Files Created/Modified
- `src/image_tx_manager.cpp` — free-slot capacity gate at the supersede call site (:1496-1506); `fullReceiptComplete` helper (:410-421); class-0 case + class-contract comment in the shared `evictionClassOf` (:443-462); named class-0 line in pass-2 eviction (:2090-2100); class-0 arm in the overflow scan's class-name label (:746)
- `.planning/WINDOWS.md` — entry 3 ROUND-#15 LEVER SHIPPED note in BOTH the markdown-table and JSON copies; front-matter counts unchanged (2/0/19/21); entry 3 status open
- `.planning/phases/01-command-protocol-control/01-UAT.md` — G-01-7 ROUND-#15 UPDATE in the `missing:` list (WR-02-already-fixed verification named; zero bench claims)
- `.planning/phases/01-command-protocol-control/COVERAGE.md` — round-#15 re-affirmation (still no external API surface — on-device queue scheduling)

## Verification (plan <verification> block, all recorded at commit 3d2dd84)

- Builds: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — **2 succeeded** (balloon 1:04, base 0:23); harness `verify_protocol_roundtrip.mjs` — **exit 0**, all checks pass
- Task 2 verify grep chain — ALL PASS (helper present, safe-reclaim naming present, round-#14 reject line present, quiet-gate constant intact, `"id": 3` present, ROUND-#15 present in 01-UAT.md)
- PRI-01 orderings: beacon early-return **:305** precedes pushPending call **:311**; preemption check (**:968-973**) precedes `findActiveEntry` (**:981**) — all in src/image_tx_manager.cpp
- Protected surfaces: CMD_TX_CHANNEL_QUIET_MS = 2000 (include/command_sender.h:36); IMG_WINDOW_STALL_MS 15000 (include/image_protocol.h:169); IMG_TX_QUEUE_DEPTH 5 (include/image_protocol.h:95); IMG_ENTRY_TTL_MS 900000 (include/image_protocol.h:204); KIND_COMPLETE freeEntry at :1658 (present, not recreated)
- Exactly one `evictionClassOf` definition (:443); both scans call it (:736, :2052) + the avoid-form re-check (:2082) — no fork
- WR-02 ordering verified: per-kind range validation (:1436-1451) and tail clamp precede the receipt stamp (:1463); the WR-02 comment (:1453-1462) intact — confirmed ALREADY FIXED upstream, not re-fixed
- WINDOWS counts reconcile: table + JSON both carry the round-#15 note; front-matter open_count 2 / fixed_count 19 / total_count 21 unchanged (phase-01 phrasing 2 open / 17 fixed / 19 total); zero bench claims anywhere in the round's records

## Decisions Made
See `key-decisions` in frontmatter — all four recorded in STATE.md. Headline: the lever composes with the session-35 image-transfer rework by reading ONLY receipt state that already exists on the entry (the fullAcked bitmap), so no new receipt surface, no header change, no wire change.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added the class-0 arm to the enqueue-overflow scan's class-name label**
- **Found during:** Task 1 (implementing action item 2's shared-ranking extension)
- **Issue:** The overflow scan's human-readable victim label (the ternary at the old :710-715) had arms only for classes 1-5; with the shared ranking extended to class 0, a fully-receipted overflow victim would have printed the MISLEADING label "ACTIVE-PULL context or receipt-evidenced (last resort)" — corrupting exactly the bench-log evidence this round's discriminator discipline exists to keep honest
- **Fix:** Prepended `victimClass == 0 ? "fully receipt-confirmed (safe reclaim)"` to the selector — the overflow scan inherits the ranking (plan action item 2), so its label must name the new class truthfully
- **Files modified:** src/image_tx_manager.cpp
- **Verification:** builds 2/2 + harness exit 0 (same commit); label arm is data-parallel to the existing five arms
- **Committed in:** 3d2dd84 (part of the plan's single fix commit)

---

**Total deviations:** 1 auto-fixed (1 missing critical). **Impact on plan:** none on scope — the plan's three named changes landed exactly as specified; the label arm keeps the inherited-ranking extension's log evidence truthful. No scope creep, no wire/protocol/timing change.

**Commit-structure note (plan as written, not a deviation):** the plan explicitly places the commit inside Task 2 ("Commit: fix(01-35): ..." with "the plan's four files"), so Task 1's code and Task 2's ledger notes share the single fix commit — the 01-33 house convention for lever rounds.

## Issues Encountered
None on the plan itself — builds and harness passed on the first run after implementation; all greps and orderings held at HEAD.

**Tool-arithmetic fix (close-out):** `roadmap.update-plan-progress "01"` wrote "34/36" (earlier run: "33/36") into ROADMAP.md while the disk truth after this SUMMARY is 35 summaries of 36 plans — the handler undercounts by one (verified: `ls *-SUMMARY.md | wc -l` = 35; handler JSON `summary_count: 34`). The "Plans:" line was manually corrected to 35/36 and re-phrased for round #15's state (01-35 executed, 01-36 remaining). No planning data was lost — prose-only correction of the count line.

## User Setup Required
None — no external service configuration required.

## Next Phase Readiness
- Round #15 code is complete: 01-36 can flash a HEAD containing both the round-#25 instruments and this lever, then record fresh SHAs and run the shared bench session ([SCHEDSNAP] interrogation + series-A verdicts + the riding clauses SC-3/WR-03/QVGA-restore/dashboard LOOK)
- G-01-7 (WINDOWS 3) and D1 (WINDOWS 15) both stay OPEN pending 01-36's operator-observed evidence; the discriminator lines make engaged-and-passing / engaged-and-failing / never-engaged distinct verdicts
- The 4 plan requirements (CTRL-01, CTRL-03, CTRL-06, PRI-02) are blocked from marking by the shared-ID gate: 01-36 declares the same IDs — they flip at 01-36's close
- WINDOWS ledger counts unchanged (2 open / 17 fixed / 19 total phase-01; front-matter 2/0/19/21 all-phase)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-09-10*

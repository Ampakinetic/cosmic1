---
phase: 01-command-protocol-control
plan: 33
subsystem: firmware-image-transfer
tags: [esp32-s3, g-01-7, burst-full-delivery, queue-depth, supersede-path, eviction-class, discriminator-lines, round-14]

requires:
  - phase: 01-command-protocol-control (plans 01-30/01-31, round #13)
    provides: the session-10 discriminator census (balloon4.log/base4.log) that NAMED the depth-3 queue-cycling residual this plan's lever answers
  - phase: 01-command-protocol-control (plan 01-32, round #14)
    provides: the D1 instrument package — this plan lands AFTER it so 01-34's single bench session judges both gaps at once (the ledger routing honored literally)
provides:
  - IMG_TX_QUEUE_DEPTH 3 -> 5 (include/image_protocol.h) with the arithmetic comment — the ledger-named depth-3 mismatch answered at its named site
  - Supersede-path receipt-evidenced protection (src/image_tx_manager.cpp evictEntriesOlderThan): victim selection ranked through evictionClassOf (reused verbatim, unforked); a class-5 entry is never the supersede victim while a lower-class candidate exists
  - Honest-rejection path when ONLY receipt-evidenced candidates exist — rides the EXISTING unknown/evicted NACK_INVALID class (no new protocol surface; the base's D-24 machinery retries within its existing bound)
  - Two bench discriminator lines for 01-34: 'supersede of receipt-evidenced image %u avoided...' (engagement) and 'queue holds only receipt-evidenced entries (G-01-7)' (honest rejection)
  - 01-G01-7-LEVER.md — the lever-selection record (evidence base, supersede-path source reading, depth arithmetic, ranked menu, LEVER SELECTED + checkpoint resolution + IMPLEMENTED record)
  - WINDOWS entry 3 round-#14 record in BOTH copies + the census completeness amendment (seq=29 second terminal named)
affects: [01-34 bench session, G-01-7 closure, phase close-out]

actuals:
  tokens: 8000
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Ranked-eviction reuse: the supersede path now ranks victims through the SAME evictionClassOf the enqueue-overflow scan uses — two eviction paths, one ranking (the two-standards gap the census proved is structurally closed)"
    - "Guard-then-rank structure: the 01-12 mid-service deferral runs as a verbatim pre-pass, then ascending-class eviction with the overflow scan's oldest-first tie-break"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/01-G01-7-LEVER.md
    - .planning/phases/01-command-protocol-control/01-33-SUMMARY.md
  modified:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp
    - .planning/WINDOWS.md
    - .planning/phases/01-command-protocol-control/01-UAT.md

key-decisions:
  - "Option (a) of 01-G01-7-LEVER.md shipped as selected at the Task 2 checkpoint (auto-advance resolution, sixth pending end-of-phase operator confirmation): depth raise + supersede-path receipt-evidenced protection — the only menu entry acting on both named halves (depth AND victim selection) without operator-visible semantics change"
  - "evictionClassOf REUSED verbatim, never forked (T-01-33-02 held): the function body is byte-unchanged; the supersede path becomes its second call site beside the enqueue-overflow scan"
  - "Flush-all supersede semantics preserved for lower classes (ascending-class order, oldest-first tie-break — same set evicted when no class-5 candidates exist); only the class-5 remainder is spared, each spared entry printing the engagement line"
  - "The rejection rides the EXISTING UNKNOWN_IMAGE/NACK_INVALID class — the receipt stamp + budget re-arm above the supersede stay (a rejected request still genuinely matched its target), so the base's D-24 pass accounting retries within its existing bound"
  - "Census completeness amendment records the VERIFIED facts: seq=29 is GET_STATUS (queued base4.log:601), the beacon seq restart 64->0 is at :675 — correcting the plan's IMAGE_WINDOW_REQUEST/:671 citation detail"

patterns-established:
  - "Census-replay discipline: a scheduling lever is validated by replaying it against the quoted census lines BEFORE bench (option (a) replays as: image 40 class-2 evicted under the original line, image 41 class-5 spared under the new line — the seven rejections never fire)"

requirements-completed: [CTRL-01, CTRL-03, CTRL-06, PRI-02]

coverage:
  - id: D1
    description: "IMG_TX_QUEUE_DEPTH 3 -> 5 with the section-3 arithmetic comment beside it (worst case 5 x (50000 + 8192) = 290,960 B = 3.5% of observed free PSRAM; +176 B internal RAM; TTL/buffer lifetimes untouched)"
    requirement: CTRL-03
    verification:
      - kind: integration
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation → 2/2 SUCCESS"
        status: pass
      - kind: unit
        ref: "grep IMG_TX_QUEUE_DEPTH = 5 include/image_protocol.h && grep 'SHIPPED DEPTH: 5' 01-G01-7-LEVER.md"
        status: pass
    human_judgment: false
  - id: D2
    description: "Supersede path ranked through evictionClassOf (unforked): class-5 never the victim while a lower-class candidate exists; only-receipt-evidenced candidates -> honest rejection through the existing NACK_INVALID class; both discriminator lines verbatim in source"
    requirement: CTRL-01
    verification:
      - kind: unit
        ref: "grep 'supersede of receipt-evidenced' + 'queue holds only receipt-evidenced entries' src/image_tx_manager.cpp; git diff shows evictionClassOf body unchanged"
        status: pass
      - kind: integration
        ref: "harness exit 0 (54/54 PASS) + PRI-01 node order-gates green (beacon early-return precedes pushPending; preempt precedes findActiveEntry)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Ledgers extended honestly open: WINDOWS entry 3 round-#14 record in BOTH copies (stays open, counts 2/17/19) + census completeness amendment; 01-UAT.md G-01-7 lever-shipped marker in root_cause + missing; ZERO bench claims"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "grep -c 'ROUND-#14 LEVER SHIPPED' WINDOWS.md = 2; JSON block parses (19 entries, entry 3 open); open_count: 2; counts 2/17/19"
        status: pass
    human_judgment: false
  - id: D4
    description: "The round's designed BENCH deliverable — 01-34's operator-observed series-A verdicts on this lever (engaged-and-passing / engaged-and-failing / never-engaged, separated by the new discriminator lines) — is READ at 01-34, not here"
    requirement: CTRL-06
    verification: []
    human_judgment: true
    rationale: "The plan's prohibition forbids bench claims this round (code + builds + harness only); G-01-7 flips only on 01-34's hardware evidence"

duration: ~14min continuation session (Tasks 1-2 audit + checkpoint in the prior session)
completed: 2026-08-28
status: complete
---

# Phase 01 Plan 33: G-01-7 Burst Full-Delivery Lever Summary

**The session-10 census's named residual gets its lever in the build — IMG_TX_QUEUE_DEPTH 3→5 with bounded arithmetic plus supersede-path victim selection ranked through an unforked evictionClassOf, with two discriminator lines letting 01-34 distinguish engaged-and-passing from engaged-and-failing from never-engaged — builds 2/2, harness exit 0, PRI-01 green, ledgers honestly open with zero bench claims.**

## Performance
- **Duration:** ~14 min (continuation session; Tasks 1-2 + Task 1's commit b98934d in the prior session)
- **Completed:** 2026-08-28
- **Tasks:** 3 completed (Task 2 = checkpoint gate, resolved in auto mode)
- **Files:** 7 (2 new planning docs, 3 source/header files, 2 ledger docs)

## Accomplishments
- **The depth-3 mismatch is answered at its named site:** `IMG_TX_QUEUE_DEPTH = 5` with the arithmetic comment citing 01-G01-7-LEVER.md section 3 (worst case 5 x (IMG_MAX_IMAGE_SIZE 50000 B + THUMB_MAX_BYTES 8192 B) = 290,960 B = 3.5% of the bench-observed free PSRAM 8.35 MB; internal RAM +2 x 88 B = +176 B; sweepExpiredEntries' 15-min TTL and freeEntry lifetimes untouched). The overflow label prints the constant verbatim, so the depth stays self-evidencing in future logs.
- **The supersede path's two-standards gap is closed:** the session-10 census proved the supersede path consulted no class-5 protection at all (the audit's central finding — the protection was ABSENT, not exhausted). `evictEntriesOlderThan` now ranks victims through `evictionClassOf` — REUSED verbatim, body byte-unchanged — with the 01-12 mid-service deferral kept verbatim as a pre-pass: a receipt-evidenced entry (class 5: windowEverArmed || lastWindowRequestMs != 0) is never the supersede victim while a lower-class candidate exists; when ONLY class-5 candidates exist the incoming request is honestly rejected through the EXISTING unknown/evicted NACK_INVALID class — no new protocol surface, and the base's D-24 pass machinery retries within its existing bound.
- **Bench discriminators named for 01-34:** `ImageTx: supersede of receipt-evidenced image %u avoided - evicting class %u entry image %u instead (G-01-7)` and `ImageTx: window request for image %u rejected - queue holds only receipt-evidenced entries (G-01-7)` — both unconditional event lines, verbatim-greppable.
- **Census replay preserved:** against session 10, the new path evicts image 40 (SERVED, class 2) under the ORIGINAL supersede-evicted line, then spares receipt-evidenced image 41 with the avoided line — the seven rejections never fire and 41's full pull continues on its already-activated schedule.
- **Lever-selection record durable:** 01-G01-7-LEVER.md carries the evidence base (census quoted with log lines), the supersede-path source reading, the bounded arithmetic, the ranked menu, the LEVER SELECTED checkpoint record with auto-advance provenance, the orchestrator resolution line, and the IMPLEMENTED record.
- **Ledgers extended, honestly open:** WINDOWS entry 3 round-#14 record in BOTH copies (stays open; counts unchanged 2/17/19) plus the census completeness amendment naming the session's SECOND 'timeout after 3 retries' (seq=29, base4.log:681, post-crash recovery class — the beacon seq restart 64→0 at :675 evidences the reboot), closing the round-#13 verifier's ledger-completeness note; 01-UAT.md G-01-7 root_cause + missing carry the lever-shipped marker. ZERO ledger flips; zero bench claims.
- **Composition guards held:** builds 2/2 SUCCESS; harness exit 0 (54/54); PRI-01 node order-gates green (beacon early-return precedes pushPending; IMG_WINDOW_SERVICE_PREEMPT_MS check precedes findActiveEntry); quiet gate 750, uart_ll_is_tx_idle x3, every 01-32 instrument grep clean; re-announce ladder, budget re-arm, base deadline release, two-pass overflow scan, D2 flag all byte-intact.

## Task Commits
1. **Task 1: Lever-selection audit** (prior session) - `b98934d` (docs)
2. **Task 2: Lever-selection checkpoint** - gate task, no commit; RESOLVED in auto mode (option (a), sixth pending end-of-phase operator confirmation); resolution recorded in 01-G01-7-LEVER.md
3. **Task 3: Implement the selected lever + discriminator lines; ledgers extended** - `61e19b7` (fix)

## Files Created/Modified
- `include/image_protocol.h` - IMG_TX_QUEUE_DEPTH 3→5 + arithmetic comment (G-01-7 + record-section citation)
- `include/image_tx_manager.h` - evictEntriesOlderThan declaration: void→bool with the return-contract comment (Rule 3, see deviations)
- `src/image_tx_manager.cpp` - supersede path rewritten: ranked victim selection through evictionClassOf, verbatim mid-service deferral pre-pass, the two discriminator lines, honest-rejection return path at the handleWindowRequest call site
- `.planning/phases/01-command-protocol-control/01-G01-7-LEVER.md` - NEW: the four-section audit + LEVER SELECTED + checkpoint resolution + IMPLEMENTED record
- `.planning/WINDOWS.md` - entry 3 round-#14 extension in BOTH copies; stays open; counts 2/17/19; census completeness amendment
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-7 round-#14 lever-shipped marker (root_cause + missing); stays open

## Decisions Made
- Option (a) shipped as selected: depth raise + supersede receipt-evidenced protection — the evidence-ranked default from the audit, approved at the Task 2 checkpoint under workflow.auto_advance (sixth pending operator confirmation)
- evictionClassOf reused, never forked — the class-5 semantics can only be strengthened (T-01-33-02 mitigation held)
- Lower-class supersede candidates still flush ALL (ascending-class order, overflow-scan tie-break) — only the class-5 remainder is spared; the original "supersedes older entry ... evicted" line is preserved for every actual eviction
- The rejection rides UNKNOWN_IMAGE/NACK_INVALID; the receipt stamp + budget re-arm above the supersede intentionally stay (a rejected request still matched its target — true liveness evidence)
- Census amendment records the verified class/line for seq=29 (GET_STATUS, :601; restart at :675) rather than the plan's mislabel

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Census-amendment citation facts corrected (seq=29 class and restart line)**
- **Found during:** Task 3 (ledger step)
- **Issue:** The plan's action item 5 named "seq=29 IMAGE_WINDOW_REQUEST at base4.log:681" with "the beacon seq restart at :671". Source verification of base4.log shows seq=29 is a GET_STATUS command (queued :601, quiet-gate held :602/:629/:648, retries :632/:651/:672, timeout :681) and the beacon seq restart 64→0 ('[BCNRX] seq=0 accepted') is at :675 — :671 is the '[E32TX] cmd=20' beacon-TX line. Recording the plan's labels verbatim would have put wrong facts in two ledgers
- **Fix:** Ledgers record the verified facts (GET_STATUS, :601/:675 citations) while keeping the plan's intent — the second terminal named alongside seq=9's series-A terminal at :250
- **Files modified:** .planning/WINDOWS.md (both copies), 01-UAT.md
- **Verification:** base4.log greps (seq=29 lifecycle, [BCNRX] seq=0 accepted at :675, [BCNRX] seq=64 at :655)
- **Commit:** 61e19b7

**2. [Rule 3 - Blocking] evictEntriesOlderThan signature change (void → bool) requires include/image_tx_manager.h**
- **Found during:** Task 3 (implementation)
- **Issue:** The honest-rejection path the plan's action item 2 mandates ("when ONLY receipt-evidenced entries exist... reject the incoming request") requires the supersede function to report refusal to its caller; the current void signature cannot. include/image_tx_manager.h is not in the plan's files_modified list
- **Fix:** Declaration changed to `bool evictEntriesOlderThan(const ImageTxEntry&)` with the return-contract comment; the single caller (handleWindowRequest, grep-verified only call site) handles false with the named rejection line returning UNKNOWN_IMAGE
- **Files modified:** include/image_tx_manager.h
- **Verification:** builds 2/2 SUCCESS; harness exit 0
- **Commit:** 61e19b7

**Total deviations:** 2 auto-fixed (Rule 1 x1, Rule 3 x1). **Impact:** citation-fact hygiene + the plan's own mandated rejection path made implementable. No architectural changes (Rule 4 never triggered).

## Issues Encountered
None blocking — builds 2/2 on the first attempt, harness exit 0, all order-gates and protected-surface greps clean, both ledger copies extended, WINDOWS JSON block parses (19 entries, entry 3 open).

## Known Stubs
None — no stub code, no skipped tests, no unrun `<verify>` (the plan's verify chain ran green end-to-end).

## Next Phase Readiness
- **01-34 (bench session)** is unblocked and now judges BOTH gaps in one session: the D1 [STACK] watermark answer (01-32's discriminator table) AND series A on this lever — the round-#14 lever record in WINDOWS entry 3 names exactly what 01-34 greps (the two new discriminator lines, the depth-5 overflow labels, the honest-reject lines) and the three distinct verdicts they separate (engaged-and-passing / engaged-and-failing with residual re-named / never-engaged).
- **No ledger flips, no bench claims** — G-01-7 (WINDOWS 3) and G-01-10 (WINDOWS 15) both stay open pending 01-34's operator-observed evidence; counts 2 open / 17 fixed / 19 total.
- The D1 evidence path is untouched by this lever (the no-D1-lever boundary holds): this plan touched queue admission/eviction policy only, and if the balloon still crashes at 01-34, [STACK]/[TWDT]/[I2C] answer independently of these scheduling changes.
- The sixth pending end-of-phase operator confirmation (this round's option (a) selection) joins the campaign's existing pending confirmations.

## Self-Check: PASSED

- Found: include/image_protocol.h IMG_TX_QUEUE_DEPTH = 5 with arithmetic comment
- Found: include/image_tx_manager.h bool evictEntriesOlderThan declaration
- Found: src/image_tx_manager.cpp ranked supersede path + both discriminator lines verbatim
- Found: .planning/phases/01-command-protocol-control/01-G01-7-LEVER.md (audit + LEVER SELECTED + resolution + IMPLEMENTED)
- Found: .planning/WINDOWS.md entry 3 round-#14 extension in BOTH copies; counts 2/17/19 unchanged; JSON valid
- Found: .planning/phases/01-command-protocol-control/01-UAT.md G-01-7 lever-shipped marker; stays open
- Found: commit b98934d (docs: audit) and 61e19b7 (fix: lever)
- Builds 2/2 SUCCESS + harness exit 0 + PRI-01 order-gates green verified on the round's final code commit

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

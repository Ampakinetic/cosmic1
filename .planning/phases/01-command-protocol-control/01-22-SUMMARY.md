---
phase: 01-command-protocol-control
plan: 22
subsystem: image-rx
tags: [lora, image-transfer, scheduling, deadline, esp32, arq]

requires:
  - phase: 01-12
    provides: the pendingHealThumbnail serialization hold in activateNextPull (the mechanism this plan bounds, not deletes)
  - phase: 01-17
    provides: balloon-side FULL-manifest re-announce keyed on fullWindowEverArmed (the drop clock the deadline must beat)
  - phase: 01-21
    provides: balloon-side busy-hold + receipt re-arm + receipt-evidence eviction ranking (the TX half this composes with)
provides:
  - IMG_FULL_ARM_DEADLINE_MS 20000 base-side full-arm deadline bounding the 01-12 thumbnail-heal serialization hold
  - ImageRxTransfer.manifestArrivedMs arrival clock stamped at manifest acceptance
  - Named deadline-release discriminator log for the 01-24 bench series A ('full-pull activation deadline reached - activating image N despite thumbnail heal pending for image M')
affects: [01-24 bench series A, G-01-7 closure judgment, image_rx_manager scheduling]

actuals:
  tokens: 1810    # chars/4 over the realized diff (7242 diff chars) — estimate was 9000, confidence low
  tasks: 2
  commits: 2      # 1 feat (84f8aea) + this docs commit

tech-stack:
  added: []
  patterns:
    - "Deadline-bounded hold: a priority-ordered hold carries a named escape bound judged on the waiting work's arrival clock (wrap-safe millis - start >= bound), releasing to the unchanged downstream tail — composes with, never duplicates, the peer side's levers"

key-files:
  created: []
  modified:
    - include/image_rx_manager.h   # IMG_FULL_ARM_DEADLINE_MS constant + ImageRxTransfer.manifestArrivedMs field
    - src/image_rx_manager.cpp     # activateNextPull FIFO-scan hoist + deadline branch + stamp site; one new named log

key-decisions:
  - "The deadline is judged on the FIFO-oldest queued full's manifestArrivedMs — the FIFO scan is hoisted BEFORE the hold branch (a pure read; hoisting changes nothing outside the deadline path), so the bound names exactly the manifest the hold is starving"
  - "best == nullptr keeps the pre-existing one-shot hold log and return byte-identical (the no-queued-full case has no deadline interplay); inside the deadline the 01-12 hold, its healHoldLoggedId latch, and the heal loop's gates are untouched"
  - "manifestArrivedMs is stamped beside arrivalSeq at the startTransfer slot fill and every slot reset is an ImageRxTransfer{} value-init, so the field zeroes with the struct — no explicit zeroing sites were needed (8 reset sites verified, no memset on transfers)"
  - "IMG_FULL_ARM_DEADLINE_MS lives in include/image_rx_manager.h (the consumer's header, the CMD_TX_CHANNEL_QUIET_MS precedent), not image_protocol.h — 01-21's same-wave file is untouched"

patterns-established:
  - "Bounded-hold composition: base-side deadline (20 s) fires inside the balloon's unheld re-announce budget (10 s idle x 3 attempts) with 10 s margin, and 01-21's busy-hold extends that budget further — the two halves compose by construction, G-01-7 closure judged only at the bench"

requirements-completed: [IMG-02, IMG-03, PRI-03]   # REQUIREMENTS.md marking gated: only PRI-03 flippable now (IMG-02/IMG-03 shared with 01-24, no SUMMARY yet)

coverage:
  - id: D1
    description: "Deadline-bounded serialization hold in the base RX manager: IMG_FULL_ARM_DEADLINE_MS 20000 constant, manifestArrivedMs arrival clock, FIFO scan hoisted before the hold branch, one-shot hold log intact inside the deadline, named deadline-release log past it falling through to the unchanged activation tail"
    requirement: PRI-03
    verification:
      - kind: other
        ref: "grep gate: IMG_FULL_ARM_DEADLINE_MS + manifestArrivedMs present in both files; 'full-pull activation deadline reached' and 'full-pull activation held - thumbnail heal pending' both present in src/image_rx_manager.cpp"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — 2 succeeded (84.43 s / 1:15.2 / 1:24.4)"
        status: pass
      - kind: other
        ref: "node scripts/verify_protocol_roundtrip.mjs — exit 0, 52/52 PASS, zero harness edits in the diff"
        status: pass
      - kind: other
        ref: "git diff HEAD~1 HEAD — exactly src/image_rx_manager.cpp + include/image_rx_manager.h; image_tx_manager.*, image_protocol.h, and command-path files zero hunks"
        status: pass
    human_judgment: false
  - id: D2
    description: "Burst full-delivery behavior at the bench: a queued FULL manifest arms its window within 20 s behind burst thumbnail serialization, composing with 01-21's balloon-side levers into 6/6 series-A COMPLETE verdicts (G-01-7 closure)"
    requirement: IMG-03
    verification: []
    human_judgment: true
    rationale: "G-01-7 closure is explicitly NOT claimed in this plan — it is judged at the 01-24 bench series-A re-run on real RF hardware; no automated test exercises the link, and the deadline-release discriminator log exists precisely so the bench can attribute the release"

duration: 10min
completed: 2026-08-27
status: complete
---

# Phase 01 Plan 22: Full-Arm Deadline (G-01-7 Burst Full-Delivery, Base RX Half) Summary

**20 s full-arm deadline (IMG_FULL_ARM_DEADLINE_MS + manifestArrivedMs arrival clock) bounds the 01-12 thumbnail-heal serialization hold in activateNextPull — a queued FULL manifest arms its balloon window within 20 s even behind burst thumb serialization, with the hold byte-identical outside the deadline and zero wire-format change (builds 2/2, harness 52/52)**

## Performance

- **Duration:** ~10 min (585 s)
- **Started:** 2026-08-27T15:39:46Z
- **Completed:** 2026-08-27T15:49:32Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Base RX half of the G-01-7 burst full-delivery gap closed at source: the session-6 race (images 25/26 manifests in hand, activation held behind image 24's failing thumb heal, every request post-drop at the balloon's re-announce bound) now has a named escape — the FIFO-oldest queued FULL activates within 20 s of its manifest arrival, arming a window that stops the balloon's re-announce clock AND promotes the entry to the protected eviction class
- Deadline arithmetic: 20 s fires with 10 s margin inside the balloon's unheld 10 s x 3 re-announce budget, and wider once 01-21's busy-hold extends it; comparison uses the wrap-safe house millis idiom (D-26)
- The 01-12 hold survives untouched outside the deadline (prohibition honored: bounded, not deleted) — same one-shot 'full-pull activation held' log, same healHoldLoggedId latch, same D-24 3-pass finalization bound for the preempted heal (which defers via the existing gate 1)
- No new D-21 trigger path (prohibition honored): activation still happens only at the existing FIFO-advance call sites; the deadline releases an existing hold, the base still speaks only on the documented triggers; diff provably confined to the image_rx_manager pair (image_tx_manager.* / image_protocol.h / command-path files: zero hunks)

## Task Commits

Each task was verified against its acceptance criteria before the plan's single code commit (the plan places the feat commit in Task 2 step 4):

1. **Task 1: Full-arm deadline — bound the serialization hold in activateNextPull** — verified via the plan's grep gate + acceptance review (constant + rationale comment, stamp site, scan-before-hold restructure, both log lines, wrap-safe subtraction)
2. **Task 2: Dual-target builds, harness, and round gates** - `84f8aea` (feat)

**Plan metadata:** this docs commit

## Files Created/Modified

- `include/image_rx_manager.h` — IMG_FULL_ARM_DEADLINE_MS 20000 file-scope constant with the balloon-budget rationale (re-announce bound, 10 s margin, 01-21 composition); ImageRxTransfer.manifestArrivedMs field beside arrivalSeq
- `src/image_rx_manager.cpp` — manifestArrivedMs stamped at the startTransfer slot fill beside arrivalSeq (all 8 slot resets are ImageRxTransfer{} value-init, so the field zeroes with the struct); activateNextPull restructured (FIFO scan hoisted, deadline branch, new named log, unchanged activation tail)

## Decisions Made

- The deadline names exactly the manifest the hold is starving: judged on the FIFO-oldest queued full's manifestArrivedMs, which is why the (pure-read) FIFO scan is hoisted before the hold branch
- The no-queued-full case (best == nullptr) keeps the pre-existing one-shot hold log and return byte-identical — it has no deadline interplay, and outside-the-deadline ordering must be unchanged per the plan's prohibition
- Constant placement follows the CMD_TX_CHANNEL_QUIET_MS precedent (consumer's header, file-scope static constexpr) keeping 01-21's same-wave files untouched

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None

## Authentication Gates

None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The base RX half of G-01-7 burst full-delivery is closed at source and committed for the 01-24 bench flash; together with 01-21's balloon TX half (busy-hold, receipt re-arm, receipt-evidence eviction ranking), both halves of the named lever set are in tree
- G-01-7 closure itself is NOT claimed here — the 01-24 bench series-A re-run (6/6 verdicts COMPLETE target) judges the composition; the deadline-release discriminator log is the attribution line to watch
- Wire-change note: both boards reflash together before the bench as usual (no wire-format bytes changed this round, so the constraint is routine rather than breaking)

## Self-Check: PASSED

- `include/image_rx_manager.h` and `src/image_rx_manager.cpp` exist on disk (modified)
- Commit 84f8aea present in git log with the feat(01-22) prefix
- Task 1 automated verify: all four grep clauses PASS (TASK1-GREP-OK)
- Task 2 automated verify: pio 2 succeeded environments; harness exit 0 (52/52 PASS, unchanged); git diff --stat confined to the plan's two files; prohibited files zero hunks

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-27*

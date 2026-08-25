---
phase: 01-command-protocol-control
plan: 17
subsystem: protocol
tags: [lora, image-transfer, esp32, state-machine, reliability, windows-ledger]

requires:
  - phase: 01-14
    provides: success-gated manifest machinery (IMG_MANIFEST_MAX_ATTEMPTS park-and-free template) and manifestAttempts reset discipline this plan mirrors
  - phase: 01-16
    provides: bench session #5 evidence bounding the two residuals this plan closes in code (balloon5.log:274 air-lost manifest; verifier-confirmed WR-08 cursor sites) + the unrouted CR-04/WR-08 findings
provides:
  - WR-08 fix: chunk-cursor advance and the SERVED transition are transmit-success-gated in both push paths, with a bounded same-index retry (IMG_CHUNK_TX_RETRY_MAX 3) and named skip logs — a transmit failure never permanently skips a chunk silently and never marks a never-offered window SERVED
  - G-01-9 defect C fix: receipt-informed FULL-manifest re-announce (IMG_FULL_REANNOUNCE_IDLE_MS 10000 / IMG_FULL_REANNOUNCE_MAX 3) — an ANNOUNCED full with no FULL window ever armed is re-announced in the channel's idle slot, stopped permanently by the first FULL window arm, and dropped with a named log at the bound (park at THUMB_PUSHED keeping thumbBuffer) — the silent-loss class is structurally gone
  - ImageTxEntry fields thumbChunkFailStreak / windowChunkFailStreak / fullWindowEverArmed / reannounceAttempts, all reset in freeEntry
  - Shared fillFullManifestBody helper — one 0x12 FULL body construction site for announce + re-announce (no wire change; body stays 27 B)
  - WINDOWS ledger entries 8 (CR-04) and 9 (WR-08) routed open with named fixes and flip conditions (01-20 bench); counts reconciled 4 open / 5 fixed / 9 total
affects: [01-19 CR-04 dispatch scoping, 01-20 bench re-verification (flips entries 3/5/8/9), phase-01 close-out, /gsd-ship gate]

requirements: [IMG-02, IMG-03, IMG-04, PRI-03]

actuals:
  tokens: 7096      # chars/4 over the realized 3-commit diff (28,387 chars); estimate was 7,000 at confidence low (0 calibration samples) — first data point stored
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Window-arm IS the receipt signal: the only balloon-observable proof a manifest arrived is the base arming its first window — recovery mechanisms for air loss key on that observation (fullWindowEverArmed), not on the TX verdict, and a THUMBNAIL heal arm must not be mistaken for full-pull receipt"
    - "SERVED means bytes offered, not cursor reached: an eviction-preference transition that frees the active-pull context must be gated on the transmit-success of the final chunk; a skipped-after-bound tail completes the window but leaves the entry re-pullable"

key-files:
  created: []
  modified:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp
    - .planning/WINDOWS.md

key-decisions:
  - "WR-08 bound semantics: a failed chunk transmit retries the SAME index on later process() passes up to IMG_CHUNK_TX_RETRY_MAX 3, then skips with a named log — the D-22 kind-addressable window re-request stays the recovery path; 3 cheap same-index retries trade against a multi-second stall-timeout round trip"
  - "A final window chunk skipped after the bound completes the window (windowArmed cleared, base tail re-request re-opens service) but leaves the entry at ANNOUNCED — never SERVED for bytes that never left the balloon (finalChunkSkipped gate)"
  - "Re-announce receipt key is fullWindowEverArmed, set ONLY at non-thumb arms — image 15's exact shape is thumb-healed-COMPLETE with the FULL manifest air-lost, so a THUMBNAIL heal arm must not stop the re-announce; duplicates are thereby confined to the pre-first-arm phase where the base-side restart is benign (IN-08)"
  - "Each re-announce transmit consumes one bounded attempt AND one idle period regardless of TX verdict (air loss is the treated class); at IMG_FULL_REANNOUNCE_MAX 3 the full park-and-frees exactly like the 01-14 announce bound — named drop log, thumbBuffer kept for heals (PRI-03)"
  - "The re-announce occupies ONLY pushPending's idle tail — after push work and window service both found nothing — so PRI-01 (beacon early-return in process()) and the D-19 push-over-window ordering are untouched"
  - "One manifest body construction site: fillFullManifestBody serves both the one-shot announce and the re-announce, so the two paths can never drift apart on the wire"

patterns-established:
  - "Same-index bounded retry before skip: every TX cursor advance is success-gated; failure retries the same index bounded, then skips loudly — silent permanent holes are a prohibited class"
  - "Idle-slot recovery: recovery transmissions ride the channel's natural idle slot (no push, no armed window), never competing with active transfer work"

requirements-completed: [IMG-02, IMG-03, IMG-04, PRI-03]

coverage:
  - id: D1
    description: "WR-08: chunk cursor advance and SERVED transition transmit-success-gated in pushThumbChunk/serviceWindowChunk with bounded same-index retry (IMG_CHUNK_TX_RETRY_MAX 3) and named skip logs in both paths"
    requirement: IMG-04
    verification:
      - kind: other
        ref: "Select-String greps (constant + both streak fields + 'skipped after' x2) + pio run -e esp32-s3-balloon -e esp32-s3-basestation (2 succeeded) + node scripts/verify_protocol_roundtrip.mjs (exit 0, 52 PASS)"
        status: pass
    human_judgment: true
    rationale: "Code-level wiring proven by source checks, dual-target builds, and the wire harness; runtime behavior (same-index retry lines under a real TX failure) is unexercised — session 5 had zero TX failures — and rides the 01-20 bench (WINDOWS entry 9 stays open until then)"
  - id: D2
    description: "G-01-9 defect C: receipt-informed bounded FULL-manifest re-announce while ANNOUNCED-and-idle with no FULL window ever armed, ending in a named drop log at the bound (park at THUMB_PUSHED keeping thumbBuffer)"
    requirement: IMG-03
    verification:
      - kind: other
        ref: "Select-String greps (both constants + fullWindowEverArmed + 're-announced' + 'no FULL window armed') + every pre-existing pacing constant still exactly once + builds 2/2 + harness exit 0 (manifest body 27, beacon body 19)"
        status: pass
    human_judgment: true
    rationale: "Mechanism verified at source and by builds/harness; recovery of an air-lost manifest under real air loss requires the 01-20 bench (WINDOWS entry 5 defect C / entry 3 ride it) — no gap status flips were claimed this plan by design"
  - id: D3
    description: "WINDOWS ledger entries 8 (CR-04) and 9 (WR-08) routed open with verifier-confirmed sites, named fixes, and flip conditions; counts reconciled 4 open / 5 fixed / 9 total across table, JSON copy, and front matter"
    verification:
      - kind: other
        ref: "Select-String greps ('CR-04 blanket camera-ready gate' x2, 'WR-08 push paths advance chunk cursors' x2, open_count: 4, total_count: 9) + node reconciliation script (9 rows / 4 open / 5 fixed, ids 1-9, entries 1-7 untouched)"
        status: pass
    human_judgment: false

duration: 15min
completed: 2026-08-25
status: complete
---

# Phase 1 Plan 17: WR-08 Cursor Gating + G-01-9 Defect C Re-Announce + WINDOWS Routing Summary

**Success-gated chunk cursors/SERVED with a bounded same-index retry (WR-08) and a receipt-informed bounded FULL-manifest re-announce keyed on first FULL window arm (G-01-9 defect C), plus WINDOWS entries 8/9 routed open**

## Performance

- **Duration:** 15 min
- **Started:** 2026-08-25T10:54:04Z
- **Completed:** 2026-08-25T11:09:29Z
- **Tasks:** 3
- **Files modified:** 4

## Accomplishments
- WR-08 closed in code: both TX push paths (pushThumbChunk, serviceWindowChunk) advance their cursors only on transmit success, retrying the same index up to IMG_CHUNK_TX_RETRY_MAX 3 with named skip logs; the SERVED marking is reachable only from the transmit-success path of the final chunk
- G-01-9 defect C closed in code: an announced full with no FULL window ever armed is re-announced (bounded 3x, 10 s idle spacing) in pushPending's idle slot, permanently stopped by the first FULL window arm, and dropped with a named log at the bound — the image-15 silent-loss class (balloon5.log:274) is structurally eliminated
- CR-04 and WR-08 routed in the WINDOWS ledger as open entries 8/9 with named fixes (01-19 dispatch scoping / 01-17 Task 1) and flip conditions (01-20 bench) — /gsd-ship blocking stays truthful
- Zero regression surface: every pre-existing pacing constant still greps exactly once, manifest body 27 / beacon body 19 byte-identical, beacon path, eviction ranking, TTL sweep, and the 01-14 manifest-attempt machinery untouched; builds 2/2 and wire harness 52 PASS exit 0

## Task Commits

Each task was committed atomically:

1. **Task 1: WR-08 — gate chunk-cursor advance and the SERVED transition on transmit success** - `c517c4f` (fix)
2. **Task 2: G-01-9 defect C — receipt-informed FULL-manifest re-announce with named drop at the bound** - `f0ac5ac` (feat)
3. **Task 3: Route CR-04 and WR-08 in the WINDOWS ledger (entries 8/9, open)** - `b1948db` (docs)

## Files Created/Modified
- `include/image_protocol.h` - IMG_CHUNK_TX_RETRY_MAX 3, IMG_FULL_REANNOUNCE_IDLE_MS 10000, IMG_FULL_REANNOUNCE_MAX 3 with rationale comments (additions only — every pre-existing constant untouched)
- `include/image_tx_manager.h` - ImageTxEntry fields thumbChunkFailStreak, windowChunkFailStreak, fullWindowEverArmed, reannounceAttempts; private methods findReannounceCandidate, reannounceFullManifest, fillFullManifestBody
- `src/image_tx_manager.cpp` - success-gated cursor/SERVED logic in both push paths; fillFullManifestBody extraction; findReannounceCandidate/reannounceFullManifest; non-thumb arm sets fullWindowEverArmed; pushPending idle-tail re-announce; freeEntry resets all four new fields
- `.planning/WINDOWS.md` - entries 8 (CR-04, src/command_handler.cpp:139) and 9 (WR-08, src/image_tx_manager.cpp:601) open; counts 4/5/9 reconciled

## Decisions Made
- See key-decisions above; additionally: no gap-status flips were made this plan by design — bench closure is 01-20's job (routing only in the ledger), exactly as the plan specified

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- WR-08 and G-01-9 defect C code halves are in; both ride the 01-20 bench for ledger flips (entries 3/5/8/9 open)
- CR-04 dispatch scoping lands in 01-19; command survivability (G-01-7 residual) is 01-18's scope
- Both firmware targets must be reflashed TOGETHER before the next bench run — no wire-format change this plan (manifest 27 / beacon 19 / chunk layout untouched), so this is the standing both-boards discipline rather than a framing requirement, but the re-announce and retry behaviors only exist on new balloon firmware

## Self-Check: PASSED

- Files exist on disk: include/image_protocol.h, include/image_tx_manager.h, src/image_tx_manager.cpp, .planning/WINDOWS.md — all FOUND
- Commits exist: c517c4f, f0ac5ac, b1948db — all FOUND in git log
- Task acceptance criteria re-run: Task 1 greps PASS, builds 2 succeeded, harness 52 PASS exit 0; Task 2 greps PASS, all 17 constants exactly once, builds/harness green; Task 3 greps PASS, ledger reconciliation 9 rows / 4 open / 5 fixed
- Plan-level verification: builds green (twice), harness exit 0 (twice), pacing constants pinned exactly once, WINDOWS counts reconcile with row statuses

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-25*

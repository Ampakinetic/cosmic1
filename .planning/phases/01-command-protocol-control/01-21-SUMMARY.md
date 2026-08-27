---
phase: 01-command-protocol-control
plan: 21
subsystem: image-transfer
tags: [lora, scheduling, re-announce, eviction, receipt-evidence, g-01-7, esp32]

requires:
  - phase: 01-17
    provides: receipt-informed bounded FULL-manifest re-announce (fullWindowEverArmed keying, idle-slot firing, IMG_FULL_REANNOUNCE_MAX 3 park-and-free named drop) — the machinery this plan makes receipt-aware
provides:
  - IMG_FULL_REANNOUNCE_BUSY_MS 15000 known-busy hold — inbound window traffic (any kind, any verdict, including unknown/evicted rejects) holds the re-announce drop-clock, consuming nothing
  - Per-entry receipt re-arm — a window request MATCHING an ANNOUNCED, never-FULL-armed entry resets its reannounceAttempts budget with a named log
  - Receipt-evidence eviction ranking — evictionClassOf protects requested ANNOUNCED entries (lastWindowRequestMs != 0) in class 5 alongside windowEverArmed; class 3 stays the never-requested class
  - Three new named bench discriminator log lines for the 01-24 series-A re-run
affects: [01-command-protocol-control close-out, 01-24 bench (G-01-7 series-A re-run), Phase 1 WINDOWS ledger entry 3]

actuals:
  tokens: 1700     # chars/4 over the realized code diff (6.9k diff chars); estimate was 14000 — the single-manager wrap-don't-rewrite shape landed far smaller than the estimate's low-confidence projection
  tasks: 3
  commits: 2        # 1 production (866087b) + 1 docs close-out

tech-stack:
  added: []
  patterns:
    - "Receipt-evidence scheduling: untrusted inbound frames double as liveness/receipt stamps (global stamp before kind validation as write-only timing data; per-entry stamp only after full untrusted-input validation)"
    - "Latch-guarded episode logging: one named log line per hold episode, cleared at the mechanism's transmit site"

key-files:
  created: []
  modified:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp

key-decisions:
  - "01-21: the re-announce drop-clock is receipt-informed on two levers — every inbound IMAGE_WINDOW_REQUEST (any kind, any verdict, including unknown/evicted rejects) stamps a channel-liveness clock that holds the idle-slot re-announce for IMG_FULL_REANNOUNCE_BUSY_MS 15 s consuming nothing, and a request MATCHING an ANNOUNCED never-FULL-armed entry re-arms its reannounceAttempts budget with a named log; the 01-17 IMG_FULL_REANNOUNCE_MAX 3 named-drop terminal path survives verbatim (receipt evidence holds the clock, it never deletes the bound)"
  - "01-21: eviction ranking is by receipt evidence, not airtime — evictionClassOf's ANNOUNCED case returns protected last-resort class 5 when windowEverArmed OR lastWindowRequestMs != 0; class 3 stays 'queued, no airtime invested' for never-requested entries only, and the class-5 eviction label names receipt evidence (session-6 image-24 class dead)"
  - "01-21: the global liveness stamp fires BEFORE kind validation by design (the session-6 rejects are the evidence class) and is write-only timing data never used for content decisions; the per-entry receipt stamp fires only after the kind enum check and entry match, so a crafted frame cannot reach it (T-01-21-01)"

patterns-established:
  - "Receipt-evidence stamps: inbound request traffic is treated as proof of receiver state, consumed at two altitudes (channel liveness = any verdict; entry receipt = matched target) with the trust boundary documented at each site"

requirements-completed: [IMG-03, IMG-04, PRI-03]

coverage:
  - id: D1
    description: "Re-announce drop-clock made receipt-aware: IMG_FULL_REANNOUNCE_BUSY_MS known-busy hold (no attempt/idle/drop consumed while inbound window traffic flows, one named hold log per episode) + per-entry budget re-arm with named log on a matched ANNOUNCED never-FULL-armed entry; 01-17 terminal path (MAX 3, named drop, park-and-free) and IDLE_MS cadence untouched"
    requirement: IMG-03
    verification:
      - kind: other
        ref: "Task 1 automated grep battery (all PASS: IMG_FULL_REANNOUNCE_BUSY_MS + IMG_FULL_REANNOUNCE_MAX    = 3 in image_protocol.h; lastWindowRequestMs/lastInboundWindowRequestMs in header+cpp; 're-announce budget re-armed', 're-announce held - inbound window traffic active', 'full dropped - no FULL window armed after' in cpp; zero lastInboundWindowRequestMs in command_sender.cpp)"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — 2 succeeded (Task 3)"
        status: pass
      - kind: other
        ref: "node scripts/verify_protocol_roundtrip.mjs — exit 0, all checks PASS, zero harness edits (Task 3)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Overflow eviction ranks ANNOUNCED entries by receipt evidence: class 5 when windowEverArmed OR lastWindowRequestMs != 0; class 3 label 'queued, no airtime invested' survives for never-requested entries only; class-5 eviction label names receipt evidence; two-pass scan / mid-service skip / TTL sweep / supersede guard unmodified"
    requirement: PRI-03
    verification:
      - kind: other
        ref: "Task 2 automated grep battery (all PASS: lastWindowRequestMs != 0, receipt-evidenced, no airtime invested — single occurrence as the class-3 label) + git diff vs round start confined to the three plan files with zero hunks in command_sender/command_handler/image_rx"
        status: pass
      - kind: other
        ref: "pio dual-target build SUCCESS + harness exit 0 (Task 3)"
        status: pass
    human_judgment: false
  - id: D3
    description: "G-01-7 burst full-delivery closure itself — series-A re-run verdicts (6/6 COMPLETE) on firmware carrying these levers"
    requirement: IMG-04
    verification: []
    human_judgment: true
    rationale: "Plan prohibition (flagged): G-01-7 closure is judged at the 01-24 bench series-A re-run, never on this plan's code alone — RF-pair behavior under real half-duplex burst load is the acceptance surface; the three new discriminator lines exist at source for that bench to watch"

duration: 12min
completed: 2026-08-27
status: complete
---

# Phase 01 Plan 21: Receipt-Informed Re-Announce + Eviction Ranking Summary

**G-01-7 balloon TX half: inbound window traffic now holds the re-announce drop-clock (IMG_FULL_REANNOUNCE_BUSY_MS 15 s busy-hold + per-entry budget re-arm on a matched request), and receipt evidence protects ANNOUNCED entries from class-3 overflow eviction — zero wire-format bytes moved**

## Performance

- **Duration:** 12 min
- **Started:** 2026-08-27T15:22:28Z
- **Completed:** 2026-08-27T15:34:41Z
- **Tasks:** 3
- **Files modified:** 3

## Accomplishments

- **Known-busy hold (lever 1a):** every inbound IMAGE_WINDOW_REQUEST — any kind, any verdict, including the unknown/evicted rejects that were session-6's exact liveness evidence — stamps `lastInboundWindowRequestMs` before kind validation; while that stamp is younger than IMG_FULL_REANNOUNCE_BUSY_MS (15000 ms), pushPending's idle slot never even calls findReannounceCandidate. The hold consumes nothing (no attempt, no idle-period advance, no drop) and prints one named line per episode (`re-announce held - inbound window traffic active`, latch cleared whenever a re-announce actually transmits). The images-25/26 class (3-re-announce bound expiring while the base serializes the burst's thumbnails) cannot recur while base window traffic flows.
- **Per-entry receipt re-arm (lever 1b):** after the target-matching loop (and only there — a crafted frame cannot reach the stamp without passing the kind enum check and entry match), a matched request stamps `target->lastWindowRequestMs` and, when the entry is ANNOUNCED with no FULL window ever armed and a partially consumed budget, resets `reannounceAttempts` to 0 with the named log `re-announce budget re-armed - window request received for image N`. A manifest the base demonstrably holds never drops at the bound while the base keeps asking.
- **Receipt-evidence eviction ranking (lever 2):** evictionClassOf's ANNOUNCED case returns the protected last-resort class 5 when `windowEverArmed || lastWindowRequestMs != 0`; class 3 remains exactly the never-armed-never-requested class with its greppable `queued, no airtime invested` label, and the class-5 eviction log label now reads `ACTIVE-PULL context or receipt-evidenced (last resort)`. The image-24 class (evicted at depth-3 overflow while the base held its manifest and was requesting windows) is dead at source.
- **Terminal path and prohibitions preserved:** IMG_FULL_REANNOUNCE_MAX 3 with the named `full dropped` log and park-and-free branch byte-identical; IMG_FULL_REANNOUNCE_IDLE_MS cadence untouched; findReannounceCandidate predicates, the 01-14 two-pass victim scan with mid-service skip, sweepExpiredEntries, and evictEntriesOlderThan unmodified; zero hunks in command_sender.cpp / command_handler.cpp / image_rx_manager.cpp (quiet gate, dispatch, base hold belong to 01-18/01-19/01-22); both targets build green and the wire harness passes 52/52 unchanged.

## Task Commits

Each task's work was verified against its acceptance criteria; per the plan's Task 3 design (matching the single-mechanism house pattern of 01-17/01-18), the code work of Tasks 1+2+3 lands as ONE atomic commit gated on the builds:

1. **Tasks 1-3: receipt-informed re-announce + eviction ranking** - `866087b` (feat)

**Plan metadata:** docs close-out commit (this file + STATE/ROADMAP/REQUIREMENTS)

## Files Created/Modified

- `include/image_protocol.h` - IMG_FULL_REANNOUNCE_BUSY_MS 15000 constant beside the existing re-announce block, with the session-6 evidence rationale comment
- `include/image_tx_manager.h` - ImageTxEntry.lastWindowRequestMs (receipt-evidence stamp, reset in freeEntry); ImageTxManager.lastInboundWindowRequestMs + reannounceHoldLogged private members (initialized in constructor + begin())
- `src/image_tx_manager.cpp` - the five behavioral sites: global liveness stamp (handleWindowRequest, pre-kind-validation), per-entry stamp + budget re-arm (post-target-resolution), busy-hold gate (pushPending idle slot, pre-findReannounceCandidate), hold-latch reset (reannounceFullManifest entry), receipt-evidence eviction ranking (evictionClassOf + class-table comment + class-5 label); freeEntry reset for the new entry field

## Decisions Made

- Global liveness stamp placement: before kind validation, per the plan action and must-have truth 1 ("any kind, any verdict including unknown/evicted rejects") — the trust-boundary mitigation for that stamp is its write-only-timing property (T-01-21-01); the per-entry stamp (which feeds content decisions: eviction ranking, budget re-arm) sits strictly after kind validation + entry match.
- The new named log lines print unconditionally (not DEBUG_IMAGE_TX-gated), matching the 01-17 re-announce/drop family they extend — they are the bench discriminators the 01-24 series-A re-run greps for.
- Boot-epoch semantics: lastInboundWindowRequestMs starting at 0 gives the hold gate a 15 s quiet floor after boot (same idiom as lastBeaconMs) — harmless (a re-announce needs 10 s idle anyway) and documented in begin().

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The two session-6 balloon-side loss mechanisms are structurally closed at source; G-01-7 closure itself is NOT claimed here — it is judged at the 01-24 bench series-A re-run (6/6 verdicts COMPLETE), per the plan's flagged prohibition on fabricating gap-closure evidence.
- Both boards must be reflashed together before the bench run (wire-change discipline is not triggered — zero wire bytes moved — but the pair should carry the same firmware regardless).
- The base TX half of G-01-7 (burst thumb serialization vs FULL-window arming) belongs to plan 01-22 (image_rx_manager) — untouched by this plan.

## Self-Check: PASSED

- Created/modified files exist: include/image_protocol.h, include/image_tx_manager.h, src/image_tx_manager.cpp — FOUND (commit-stat verified, 3 files / +106 −5)
- Production commit exists: 866087b `feat(01-21): receipt-informed re-announce + eviction ranking (G-01-7 burst full-delivery, balloon half)` — FOUND in git log
- Builds: 2 succeeded environments (esp32-s3-balloon 01:38, esp32-s3-basestation 01:39)
- Harness: exit 0, all checks PASS, zero FAIL, zero harness edits in the diff
- Diff discipline: code-path diff vs round start (cfe2613) lists exactly the three plan files; zero hunks in command_sender.cpp / command_handler.cpp / image_rx_manager.cpp / command_sender.h

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-27*

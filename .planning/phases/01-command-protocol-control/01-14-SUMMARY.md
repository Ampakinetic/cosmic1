---
phase: 01-command-protocol-control
plan: 14
subsystem: protocol
tags: [lora, image-transfer, esp32, state-machine, reliability, psram]

requires:
  - phase: 01-13
    provides: kind-tagged 0x13 chunk wire format + kind-exact routing + CR-03 thumbnail payload guard; the TX state machine this plan hardens
provides:
  - Success-gated manifest transitions (CR-02/WR-01): ANNOUNCED and PUSH_THUMB_CHUNKS consumed only on transmit success, bounded by IMG_MANIFEST_MAX_ATTEMPTS 3 with honest park-and-free at the bound
  - ImageTxEntry.manifestAttempts shared counter (reset on success and in freeEntry)
  - Two named drop logs ('FULL manifest for image %u failed after %u attempts; full dropped' + thumbnail twin)
  - WR-02 two-pass mid-service-aware overflow victim scan with the 'all entries mid-service' never-wedge fallback log
affects: [01-15 defer-aware pass accounting, 01-16 bench re-verification, phase-01 close-out, G-01-9 defect C closure]

requirements: [IMG-02, IMG-03, PRI-03]

actuals:
  tokens: 2253      # chars/4 over the realized 2-commit diff (9,014 chars); estimate was 11,000 at confidence low (0 calibration samples)
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "One-shot states are consumed only on success: a state machine transition that gates a remote's ability to ever request again (a manifest the base must see before it can ask) must retry on failure, bounded, with honest degradation at the bound — never advance on a failed transmit"
    - "Mid-service awareness belongs in every eviction victim scan, not just one: the overflow scan now skips armed/incomplete windows (same predicate as the BUSY check, copied verbatim) with an unless-every-entry fallback so liveness of the queue beats protection of the window at the wedge point"

key-files:
  created: []
  modified:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp

key-decisions:
  - "CR-02 fix shape: announceFullManifest consumes ANNOUNCED only inside the transmit-success branch; a failed FULL-manifest transmit stays in ANNOUNCE_FULL and retries one-per-pass, bounded by the shared IMG_MANIFEST_MAX_ATTEMPTS (3); at the bound fullBuffer is freed, fullLength/fullCrc32/fullTotalChunks zeroed, and the entry parks at THUMB_PUSHED with thumbBuffer still owned so THUMBNAIL window heals keep working — the base degrades to thumbnail-only for that capture (PRI-03: bounded, logged, never silent)"
  - "WR-01 fix shape: pushThumbManifest advances to PUSH_THUMB_CHUNKS only on success; at the bound the thumbnail is dropped honestly (thumbBuffer freed + zeroed) and the entry proceeds via completedThumbState exactly as a completed push would, minus thumbnail bytes — the full announce still happens when armable"
  - "manifestAttempts is ONE shared counter for the two mutually exclusive manifest phases (PUSH_THUMB_MANIFEST and ANNOUNCE_FULL are sequential per entry), reset on each success and in freeEntry — a thumb manifest that succeeded on attempt 2 never pre-charges the full announce"
  - "WR-02 fix shape: two-pass overflow scan — pass 1 adds the mid-service skip (windowArmed && windowNextIndex < windowStart + windowCount, the BUSY predicate verbatim), pass 2 (only when every entry is mid-service) drops the skip and logs 'all entries mid-service' so the operator sees the queue chose liveness; the skip lives ONLY in this scan, evictionClassOf's ranking and sweepExpiredEntries' TTL path are byte-identical to HEAD (carried 01-12 prohibition)"
  - "Prohibitions held: one-transmit-per-pass discipline, beacon early-return, TX arbitration order, evictionClassOf class ranking, and sweepExpiredEntries TTL path all untouched; G-01-9 defect C is NOT closed by this plan (code-level only — the 01-16 bench must observe a re-announce recovery before closure)"

patterns-established:
  - "Copied-verbatim predicate discipline: when two code sites must agree on a predicate (mid-service detection), copy the expression character-for-character and prove it with a grep that counts the shared expression at every site — a paraphrased predicate recreates the hole or over-blocks"

requirements-completed: []

duration: 8min
completed: 2026-08-24
status: complete
---

# Phase 01 Plan 14: TX State-Machine Hardening (CR-02 + WR-01 + WR-02) Summary

**Success-gated, bounded-retry manifest transitions (one shared 3-attempt counter, honest park-and-free at the bound) plus a mid-service-aware two-pass overflow victim scan — the silently-lost-FULL-manifest defect family closed on the TX side; bench proof rides 01-16.**

## Performance

- Builds: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — 2 succeeded (after each task)
- Wire harness: `node scripts/verify_protocol_roundtrip.mjs` — 52 PASS / 0 FAIL, exit 0 (after each task)
- Pacing anchors: 9 constants in image_protocol.h each defined exactly once (IMG_WINDOW_MAX_CHUNKS, IMG_RETRANSMIT_MAX_PASSES, IMG_MANIFEST_MAX_ATTEMPTS, IMG_WINDOW_STALL_MS, IMG_WINDOW_SERVICE_PREEMPT_MS, IMG_WINDOW_RX_SETTLE_MS, IMG_THUMB_HEAL_IDLE_MS, IMG_ENTRY_TTL_MS, TELEMETRY_BEACON_INTERVAL_MS)
- Duration: ~8 min (executor session 2026-08-24T04:08-04:16Z); estimate was 11,000 tokens at confidence low, actuals 2,253 tokens over the realized 2-commit diff

## Tasks Completed

### Task 1: CR-02 + WR-01 — success-gated manifest transitions with a shared bounded attempt counter (bb2c517)

- `include/image_protocol.h`: `IMG_MANIFEST_MAX_ATTEMPTS = 3` added beside IMG_RETRANSMIT_MAX_PASSES with the CR-02/WR-01 rationale (one-shot state consumed only on success; bounded so a dead link cannot spin; buffer freed + kind degrades honestly at the bound)
- `include/image_tx_manager.h`: `uint8_t manifestAttempts` added to ImageTxEntry beside lastActivityMs, commented as shared by the two mutually exclusive manifest phases; ImageTxEntryState block comment and the ANNOUNCE_FULL/ANNOUNCED/PUSH_THUMB_MANIFEST inline comments updated from "emitted exactly ONCE" to the retry semantics (comment honesty)
- `src/image_tx_manager.cpp` `announceFullManifest`: ANNOUNCED assigned only inside the success branch (manifestAttempts = 0); failure increments, stays in ANNOUNCE_FULL below the bound; at the bound logs `ImageTx: FULL manifest for image %u failed after %u attempts; full dropped`, frees+nulls fullBuffer, zeroes fullLength/fullCrc32/fullTotalChunks, parks at THUMB_PUSHED (thumbBuffer kept for window heals)
- `src/image_tx_manager.cpp` `pushThumbManifest`: PUSH_THUMB_CHUNKS + nextThumbChunk reset only on success; at the bound logs the thumbnail twin, frees+nulls thumbBuffer, zeroes thumbLength/thumbCrc32/thumbTotalChunks, and lands in `completedThumbState(entry)` (ANNOUNCE_FULL or THUMB_PUSHED)
- `freeEntry`: resets manifestAttempts = 0 beside the other resets; lastActivityMs still advances on every attempt (today's line stays last)
- Structural grep: manifestAttempts appears exactly 7 times in src/image_tx_manager.cpp (2 increments, 2 bound checks, 2 resets-on-success, 1 freeEntry reset)

### Task 2: WR-02 — mid-service-aware overflow victim scan (ae16991)

- `src/image_tx_manager.cpp` `enqueueCapture` overflow branch: two-pass scan — pass 1 is today's class-then-age scan plus the mid-service skip (`windowArmed && windowNextIndex < windowStart + windowCount`, the BUSY predicate copied verbatim); pass 2 runs only when pass 1 found nothing (every entry mid-service), drops the skip, and logs `ImageTx: queue overflow - all entries mid-service; evicting class %u (%s) entry image %u for image %u` — the depth-3 queue can never reject a capture because three windows happened to be armed
- Overflow scope comment states the WR-02 boundary: the skip lives ONLY in this scan; evictionClassOf's ranking and sweepExpiredEntries' TTL path keep their full eviction rights
- `evictEntriesOlderThan` predicate joined onto one line (whitespace-only, zero semantic change — that function is not under the byte-identical prohibition) so the shared-predicate grep provably covers all three sites: BUSY check, supersede guard, overflow pass-1 (grep count = 3)
- evictionClassOf references: declaration + the two overflow-scan calls + 1 comment mention (4 >= the required 3); evictionClassOf and sweepExpiredEntries function bodies byte-identical to HEAD (verified via diff-hunk inspection — zero hunks touch them)

## Verification Results

- Task 1 automated verify: all five PowerShell greps PASS; `pio run` 2 succeeded; harness exit 0
- Task 2 automated verify: all three PowerShell greps PASS ('all entries mid-service' == 1, evictionClassOf >= 3, shared window predicate >= 3); `pio run` 2 succeeded; harness exit 0
- Diff hygiene: Task 1 commit touches exactly the three declared files; Task 2 commit touches exactly src/image_tx_manager.cpp; no file deletions in either commit
- Full Task-2 diff inspected hunk-by-hunk: 3 hunks in enqueueCapture's overflow branch + 1 whitespace hunk in evictEntriesOlderThan, nothing else

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Formatting blocker for a machine-checkable prohibition] Supersede-guard predicate joined to one line**
- **Found during:** Task 2
- **Issue:** The plan requires the shared mid-service predicate to be grep-countable at three sites (BUSY check, supersede guard, overflow pass-1), but the supersede guard in evictEntriesOlderThan had the expression split across two lines at HEAD, so the single-line grep pattern could not match it (count would have been 2, not >= 3)
- **Fix:** Joined the two-line expression onto one line — whitespace only, identical tokens, zero semantic change; evictEntriesOlderThan is not among the byte-identical-required functions (only evictionClassOf and sweepExpiredEntries are, per the plan's prohibition)
- **Files modified:** src/image_tx_manager.cpp
- **Commit:** ae16991

Otherwise: plan executed exactly as written.

## Auth Gates

None.

## Known Stubs

None — both drop paths and the fallback path are real logged behavior with buffer hygiene, not placeholders.

## Gaps Status (unchanged by this plan, per prohibition)

- **G-01-9 defect C (lost FULL manifest): code-level mechanism removed** — a failed FULL-manifest transmit now retries bounded and a lost manifest can no longer silently strand a full. NOT closed: closure requires the 01-16 bench (or at minimum an observed re-announce recovery at bench)
- **G-01-7 residual (defer-aware D-24 pass accounting): untouched** — rides 01-15
- No gap status flips in this plan

## Self-Check: PASSED

- Commits exist on main: bb2c517 (Task 1), ae16991 (Task 2)
- All three declared files modified across the two commits; no undeclared files staged
- Builds 2/2 and harness 52/52 verified after each task

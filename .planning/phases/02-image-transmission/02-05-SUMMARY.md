---
phase: 02-image-transmission
plan: "05"
subsystem: image-transfer
tags: [lora, windowed-arq, transfer-state-machines, gap-closure, d-19, d-20, d-22, d-24, d-30, sidecars, img-02, img-03, img-04, img-05, pri-01, pri-03]

# Dependency graph
requires:
  - phase: 02-image-transmission (02-01/02-02/02-03)
    provides: the image TX/RX managers, PayloadImageWindowRequest (5-byte), handleWindowRequest/serviceWindowChunk/evictEntriesOlderThan, base reassembly + SD streaming + sidecars, the wire harness
  - phase: 02-image-transmission (02-02)
    provides: the 0x14 telemetry beacon with the PRI-01 beacon early-return in ImageTxManager::process()
  - phase: 02-image-transmission (02-04)
    provides: AutoCapture as the sole capture/image-ID authority feeding enqueueCapture
provides:
  - Kind-addressable window requests: PayloadImageWindowRequest is 6 bytes (imageId BE16, imageKind u8, startChunk BE16, count u8), harness-locked as the img-f exact-byte clause
  - Balloon thumbnail servicing (D-22 end-to-end): THUMBNAIL windows arm against thumbBuffer on any entry whose push finished (THUMB_PUSHED parks admitted), ranges validated against thumbTotalChunks; THUMBNAIL requests never evict anything
  - Bounded push/window interleaving: pushPending() preempts push work when an armed window's entry waited > IMG_WINDOW_SERVICE_PREEMPT_MS (5000 ms, strictly below the base's 8000 ms stall) — PRI-01 beacon/command arbitration untouched (node order-gates prove branch order)
  - Completion-aware eviction: ImageTxEntryState::SERVED + ImageTxEntry.windowEverArmed; enqueueCapture overflow evicts by 5-class ranking (parked > served > queued > push-in-flight > ACTIVE-PULL last resort), oldest-within-class, class + both image ids logged
  - Base D-24 correctness: acceptChunk is the sole passCount zero-writer — only CONSECUTIVE unhealed stalls finalize INCOMPLETE
  - Gated thumbnail heal: never while a full pull is active AND only after the same-id FULL manifest (push provably finished), with the IMG_THUMB_HEAL_IDLE_MS 24000 ms oversize fallback; heal-window chunks route to the THUMBNAIL slot before the FULL-first precedence
  - Slot-pressure hygiene (D-20): allocateSlot step 3 fully resets the evicted slot (*oldest = ImageRxTransfer{}) — no zombie accounting
  - Kind-suffixed sidecars (D-30): IMG_{id}_T.JSON vs IMG_{id}.JSON — the full's finalization no longer truncates the thumbnail's record; _T mirrors D-31's _T.JPG and is the Phase 3 gallery contract
affects: [02-image-transmission (02-VERIFICATION hardware UAT — fulls complete under 20 s cadence, thumbnail heal RETRYING→COMPLETE, telemetry age during transfers, 4-file SD check), Phase 3 (gallery reads the _T sidecar convention)]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 9448     # 37792 diff chars / 4 (d728108 + b867878 + d418454 + 7cd2169) — plan estimated 30000 at low confidence; the fixes reused existing idioms (matcher split, buffer select, class table) rather than new machinery
  tasks: 3
  commits: 4

# Tech tracking
tech-stack:
  added: []         # no new libraries
  patterns:
    - Kind-parameterized window context: the window arming carries the ImageKind it serves; target matching, range validation, source-buffer selection, and eviction side-effects all key off that one byte (validated against the enum before any buffer access — T-02-11)
    - Preemptive fairness inside a strict-priority chunk branch: push work still outranks window service per pass, but a bounded age check at the top of pushPending() converts unbounded starvation into at-most-5 s gaps without touching the beacon early-return (PRI-01 preserved by construction, proven by node order-gates)
    - Class-ranked overflow eviction: eviction preference is a pure function of entry state (evictionClassOf), so queue pressure can never free the active pull's target while any finished/parked/queued entry exists — D-19 under pressure
    - Progress-retires-passes: retransmit pass counters reset on accepted-chunk commits (sole zero-writer in acceptChunk), so bounded-pass semantics measure consecutive unhealed stalls, not cumulative healed ones

key-files:
  created: []      # no new files — this plan amends the existing transfer machinery
  modified:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp
    - src/command_handler.cpp
    - src/image_rx_manager.cpp
    - include/sd_storage.h
    - src/sd_storage.cpp
    - scripts/verify_protocol_roundtrip.mjs

key-decisions:
  - "The kind byte rides BETWEEN imageId and startChunk exactly as the review's CR-01 fix prescribed (imageId BE16, imageKind u8, startChunk BE16, count u8 — 6 bytes); the harness pins the literal bytes 00 2A 01 00 07 10 so a layout regression cannot pass silently"
  - "THUMBNAIL target matching is defined by what the push provably finished, not by a named state list: any same-id entry whose state is NOT PUSH_THUMB_MANIFEST/PUSH_THUMB_CHUNKS and that still owns thumbBuffer serves a heal — this admits THUMB_PUSHED oversize parks, ANNOUNCED, and SERVED entries alike without enumerating them"
  - "evictEntriesOlderThan is reachable ONLY from the FULL_IMAGE request path (single call site inside if (!thumbWindow)); a thumbnail heal can never free the active pull's target mid-stream — the verifier's :561 failure chain is structurally unreachable now"
  - "SERVED keeps both buffers: the marker means every full chunk was OFFERED at least once (windowStart+windowCount == fullTotalChunks), not that the base received them — tail-chunk loss must stay healable (re-arm re-enters ANNOUNCED), while eviction preference flips"
  - "ANNOUNCE_FULL rides overflow class 4 (push in flight): the plan's class list omitted the state, which would leave no candidate class when all 3 slots await announcement (slot==nullptr crash on *slot = entry) — see Deviation 1"
  - "The heal's oversize fallback is time-based evidence (24 s idle = 3x stall window): an oversize image never announces a full, so idle-beyond-any-plausible-queued-push is the only proof its thumbnail push drained; after that the D-24 3-pass bound resolves the row honestly instead of an infinite RECEIVING stall"
  - "Heal-window chunk routing keys off the requester's own state (thumbnail slot windowActive) because the 0x13 chunk body carries no kind byte — per-image-id routing plus the no-active-pull gate makes the armed heal window unambiguously own its id's chunks; a QUEUED full's SD file can never receive thumbnail bytes"
  - "Sidecar naming derives from meta.kind at the single writeSidecar call site — an additive change only; no SD file is ever removed, renamed, or overwritten across kinds (carried prohibition)"

patterns-established:
  - "Wire-contract changes land as harness clauses FIRST (TDD RED pins the exact bytes), then the firmware — the harness is the executable spec a 5-byte regression fails"
  - "Fairness preemptions live inside the branch they bound, never ahead of the beacon early-return: branch order itself is the PRI-01 guarantee and gets a node order-gate"

requirements-completed: [IMG-02, IMG-03, IMG-04, IMG-05, PRI-01, PRI-03]

metrics:
  duration: ~21 min (23:07–23:28 UTC, single session)
  completed: 2026-08-19
  tasks_completed: 3
  commits: 4

status: complete
---

# Phase 02 Plan 05: Transfer State-Machine Gap Closure Summary

All four 02-VERIFICATION gaps closed at code level: full pulls survive their documented operating point (progress retires retransmit passes; the balloon preempts push work for an armed window at 5 s, strictly under the base's 8 s stall; overflow evicts by class with the active pull last), thumbnail heals ride the same windowed ARQ behind two link-discipline gates (no active pull + push provably finished, 24 s oversize fallback), slot-pressure manifests start at 0/N on a fully reset slot, and thumbnail/full D-30 sidecars persist as separate files (IMG_{id}_T.JSON) — with the 6-byte kind-carrying window request locked in the wire harness and PRI-01 arbitration untouched.

## Tasks Completed

| Task | Name | Commit | Key files |
|------|------|--------|-----------|
| 1 | Wire contract — kind-addressable window requests + balloon thumbnail servicing (TDD) | d728108 (RED) + b867878 (GREEN) | include/image_protocol.h, include/image_tx_manager.h, src/image_tx_manager.cpp, src/command_handler.cpp, scripts/verify_protocol_roundtrip.mjs |
| 2 | Balloon TX policy — bounded push/window interleaving + completion-aware eviction | d418454 | include/image_tx_manager.h, src/image_tx_manager.cpp |
| 3 | Base RX — pass reset, gated thumbnail heal, heal routing, slot-pressure reset, kind-suffixed sidecars | 7cd2169 | src/image_rx_manager.cpp, include/sd_storage.h, src/sd_storage.cpp |

## What Was Built

**Task 1 — Kind-addressable wire contract + balloon servicing (d728108 + b867878).** TDD: the img-f clause was rewritten first to pin the exact 6-byte layout (`00 2A 01 00 07 10`) and round-trip both kinds' boundary values — both new assertions failed against the 5-byte codecs (RED, suite exit 1). GREEN: harness codecs allocate 6 bytes (imageKind at offset 2, startChunk at 3, count at 5); `PayloadImageWindowRequest` gains `uint8_t imageKind` between imageId and startChunk; new constants `IMG_WINDOW_SERVICE_PREEMPT_MS` (5000) and `IMG_THUMB_HEAL_IDLE_MS` (24000) with load-bearing comments. `handleWindowRequest`: 6-byte guard, the kind byte validated against the ImageKind enum BEFORE any entry matching (T-02-11), kind-split matcher — THUMBNAIL matches any same-id entry whose push finished (state not PUSH_THUMB_*, thumbBuffer non-null; admits THUMB_PUSHED/ANNOUNCED/SERVED) with ranges validated against thumbTotalChunks, FULL matches ANNOUNCED (Task 2 adds SERVED) against fullTotalChunks; `evictEntriesOlderThan` now runs ONLY inside `if (!thumbWindow)`. `serviceWindowChunk` selects its source buffer/length by `entry.windowKind`; `findWindowServiceEntry` admits THUMB_PUSHED entries with an armed window. The command handler guard and ACK echo widened to 6 bytes (`responseLength 6`).

**Task 2 — Bounded interleaving + completion-aware eviction (d418454).** `pushPending()` opens with the preemption: an armed window whose entry waited > `IMG_WINDOW_SERVICE_PREEMPT_MS` consumes the pass's single transmit before any `findActiveEntry()` push work — the armed entry's lastActivityMs advances only via its own transmits/re-arms, so a neighbor capture's push ages it naturally, and 5000 < 8000 means the base's stall clock (reset on every accepted chunk) can never trip while the balloon holds an armed window. The beacon early-return in `process()` is untouched (node order-gates re-prove beacon → pushPending ordering). `SERVED`: a FULL window whose clamped span reached fullTotalChunks marks the entry (buffers kept for tail heals; arming a SERVED entry re-enters ANNOUNCED; THUMBNAIL windows never change state). Overflow eviction replaces drop-oldest with the 5-class ranking via `evictionClassOf` (1 THUMB_PUSHED, 2 SERVED, 3 ANNOUNCED-never-armed, 4 PUSH_THUMB_*/ANNOUNCE_FULL, 5 ANNOUNCED-windowEverArmed = ACTIVE-PULL, last resort), oldest-within-class, each eviction logged with class name + both image ids; `windowEverArmed` set on every arming (both kinds).

**Task 3 — Base RX accounting/gating/routing + sidecars (7cd2169).** `acceptChunk` commits now write `t.passCount = 0` (sole zero-writer — D-24 bounds only consecutive unhealed stalls). `issueWindowRequest` builds the 6-byte kind-carrying payload the Task 1 balloon decoder accepts. The thumbnail stall loop gains the two gates before any heal request: gate 1 skips while `findActivePull() != nullptr`; gate 2 requires a same-id FULL slot, else waits out `IMG_THUMB_HEAL_IDLE_MS` (oversize fallback) — gate-skips charge no pass, so the 3-pass bound resolves the row only after real unhealed stalls. `onChunkFrame` routes to an armed heal window's non-terminal THUMBNAIL slot BEFORE the FULL-first precedence (per-id routing + gate 1 make the heal window unambiguously own its id's chunks — no kind byte in the 0x13 body). `allocateSlot` step 3 resets the evicted slot with `*oldest = ImageRxTransfer{}` after the slot-pressure finalizeIncomplete (CR-02 zombie closed). `sidecarPath(out, cap, id, kind)` emits `/images/IMG_%05u_T.JSON` for thumbnails (WR-05) — both D-30 records persist independently.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] ANNOUNCE_FULL added to overflow-eviction class 4**

- **Found during:** Task 2
- **Issue:** The plan's 5-class eviction list names THUMB_PUSHED, SERVED, ANNOUNCED(×2), and PUSH_THUMB_MANIFEST/PUSH_THUMB_CHUNKS — but not ANNOUNCE_FULL (thumbnail done, full announcement pending). With all 3 queue slots in ANNOUNCE_FULL (reachable: 3 captures while the base is busy), no class is non-empty, the victim search finds nothing, `slot` stays nullptr, and `*slot = entry` dereferences null — a crash on a documented reachable path.
- **Fix:** `evictionClassOf` maps ANNOUNCE_FULL to class 4 (push in flight): like PUSH_THUMB_* it still has undelivered push work — the one-time full manifest — and no pull can reference it yet (the base has not seen the manifest), so it carries the same eviction priority.
- **Files modified:** src/image_tx_manager.cpp (within the plan's Task 2 file list)
- **Commit:** d418454

**2. [Rule 1 - Build] evictionClassOf placement (compile error, fixed pre-commit)**

- **Found during:** Task 2 verification
- **Issue:** The static helper was initially defined in the bottom Helpers section, after its first use in `enqueueCapture` — balloon build failed with 'evictionClassOf' was not declared in this scope.
- **Fix:** Moved the definition above `enqueueCapture` (after the Enqueue section header). Both targets then built SUCCESS; no intermediate broken commit exists.
- **Files modified:** src/image_tx_manager.cpp
- **Commit:** d418454

Otherwise the plan executed exactly as written, including the prescribed TDD split for Task 1.

## Known Limitations / UAT Flags

- **Flagged assumption (plan frontmatter):** the 5000 ms preemption bound and the whole completion argument rest on research A1/A3/A6 airtime (9.6 kbps, ~210–280 ms/packet) — hardware UAT must confirm fulls complete under the D-28 20 s cadence (carried from 02-VERIFICATION).
- **Flagged assumption (plan frontmatter):** a healed thumbnail's end-to-end RETRYING→COMPLETE timing needs real chunk loss on hardware; the code path is proven by build + harness only.
- **PRI-01 runtime interleaving** (beacon/chunk timing over the half-duplex link) stays a UAT item; code-level branch order is proven by the node order-gates.
- **T-02-13 accepted residual (threat register):** the class-5 last-resort eviction of an active-pull entry remains reachable only when all 3 queue slots are pull-context or mid-push entries; bounded by construction, logged, recoverable by the base's bounded retry machinery.
- **IMG-05 wiring depth, CTRL-05, PRI-03 loss rates:** coverage rows owned and carried by plans 02-03/02-04 — this plan touched no camera/GPS/SD-mount surface beyond the sidecar name.

## Auth Gates

None — no authentication was required during execution.

## Known Stubs

None — every code path added is live (kind decode → matcher → buffer select → transmit; gates → heal request → routing → bitmap commit; class table → victim → free). No placeholder data, no unwired UI.

## Verification Results

- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS (re-run after each task; final run after all Task 3 edits)
- `node scripts/verify_protocol_roundtrip.mjs` — exits 0; img-f now asserts the exact bytes `00 2A 01 00 07 10`, length 6, and symmetric boundary round-trips for both kinds; 47 checks pass
- All task acceptance greps pass: `imageKind`/`payload[2]`/`windowKind` (Task 1); `IMG_WINDOW_SERVICE_PREEMPT_MS`/`SERVED`/`windowEverArmed` (Task 2); `passCount = 0`, slot-pressure `ImageRxTransfer{}` within the -A3 window, `IMG_THUMB_HEAL_IDLE_MS`, `payload, 6`, `IMG_%05u_T.JSON` (Task 3)
- Node order-gates pass: preemption constant precedes `findActiveEntry()` inside pushPending; the beacon `return; // one transmit per pass` precedes the `pushPending()` call in process() (PRI-01)
- Structural re-checks (verifier's method): `evictEntriesOlderThan` has exactly ONE call site, inside `if (!thumbWindow)` — unreachable from any THUMBNAIL request path; `passCount = 0` appears exactly once (inside acceptChunk, line 573, sole zero-writer); `serviceWindowChunk` selects its source buffer by `windowKind`

## TDD Gate Compliance

Task 1 (tdd="true") followed RED/GREEN: `test(02-05)` commit d728108 added the failing 6-byte clause (both new assertions FAILED against the 5-byte codecs, verified before commit), then `feat(02-05)` commit b867878 turned it green alongside the firmware. No refactor commit needed.

## Self-Check: PASSED

All 8 modified files exist on disk; all 4 commit hashes (d728108, b867878, d418454, 7cd2169) verified in git log. No WINDOWS.md ledger exists in this repo; no stub/skipped-test/unrun-verify defects to record.

---
phase: 01-command-protocol-control
plan: 18
subsystem: protocol
tags: [lora, command-retry, esp32, half-duplex, channel-arbitration, reliability]

requires:
  - phase: 01-15
    provides: defer-aware D-24 pass accounting (windowRequestInFlight PENDING/SENT guard) and the BUSY-deferral branch the quiet gate must compose with, not collide
  - phase: 01-16
    provides: session-5 bench evidence naming the seq=6 4/4-loss class (base5.log:55/:73/:81/:92) this plan treats base-side
provides:
  - Channel-quiet command transmit gate (G-01-7 residual, code half): CommandSender holds BOTH the PENDING first transmit and the SENT timeout retry while an inbound 0x13 chunk frame arrived within CMD_TX_CHANNEL_QUIET_MS 750 ms — the hold consumes nothing (no retry, no failure, no D-05 window start)
  - Bounded hold: CMD_TX_CHANNEL_HOLD_MAX_MS 30000 best-effort escape with a named log — a sustained chunk stream can never strand a command; honest D-05/D-07 terminal semantics apply to every transmitted attempt
  - Bench discriminators for 01-20: log-once 'transmit held - inbound chunk stream active' per hold episode and 'held >= 30000 ms; transmitting best-effort' at the bound
affects: [01-20 series-A bench, G-01-7 residual closure evidence, WINDOWS entry 3, phase-01 close-out]

requirements: [CTRL-01, CTRL-06, PRI-02, IMG-03]

actuals:
  tokens: 2276     # chars/4 over the realized 2-commit diff (9,105 chars); estimate was 4,500 at confidence low (0 calibration samples)
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Channel-quiet transmit gating: a receive-time latch at the single inbound storm-class dispatch site (0x13 chunks only — manifests/beacons are single frames and stay unlatched) feeds a bounded, logged, consume-nothing hold at every transmit decision — the gate wraps existing retry/backoff machinery, it never rewrites it"
    - "A hold is a wait, not an outcome: held commands write only their own hold latch (channelHoldStartMs); sendTime is set exclusively by actual transmits so ACK windows and stall clocks stay honest"

key-files:
  created: []
  modified:
    - include/command_sender.h
    - src/command_sender.cpp

key-decisions:
  - "Latch scope: lastChunkFrameMs is stamped ONLY in processIncomingByte's PACKET_TYPE_IMAGE_CHUNK case (immediately before the ImageRx().onChunkFrame forward — the sole inbound chunk path on the base); manifests (0x12) and beacons (0x14) are deliberately NOT latched — single frames, not the storm class that ate seq=6's 4 transmissions"
  - "Hold consumes nothing by construction: canTransmitNow's only write is cmd->channelHoldStartMs — the PENDING gate continue-skips before transmitCommand and the SENT gate skips retryCommand, so retryCount/lastRetryTime/sendTime/pendingCommandCount are provably untouched by a hold; sendTime (hence the D-05 ACK window) is set only by an actual transmit"
  - "Composition with 01-15 (recorded in the gate comment): a held PENDING IMAGE_WINDOW_REQUEST stays non-terminal, so windowRequestInFlight still counts it — the base D-24 stall clock extends and no pass is charged; the D-07 backoff block is untouched and only paces attempts that are about to fire"
  - "Best-effort bound keeps terminal honesty: at 30 s of continuous hold the command transmits with a named log and the ordinary D-05/D-07 semantics govern that attempt; the terminal TIMEOUT path at budget exhaustion is un-gated and unchanged"
  - "channelHoldStartMs cleared on every successful transmit (PENDING->SENT sendTime path, retryCommand success, and both canTransmitNow release paths) plus slot init in sendCommand — a later hold episode always logs freshly"

patterns-established:
  - "Wrap-don't-rewrite gating with diff-verifiable prohibitions: the D-07 backoff block, 01-15 BUSY-deferral branch, terminal-state guard, ackTimeoutFor, and frame dispatch were proven byte-identical to pre-plan HEAD by hunk-position diff, not by assertion"

requirements-completed: []   # CTRL-01, CTRL-06, PRI-02, IMG-03 all already [x] in REQUIREMENTS.md (requirements.ready-ids: 0/4 pending) — no flips this plan, per the no-gap-flips prohibition

coverage:
  - id: D1
    description: "Channel-activity latch and quiet/hold decision machinery — CMD_TX_CHANNEL_QUIET_MS 750 / CMD_TX_CHANNEL_HOLD_MAX_MS 30000 constants, TrackedCommand.channelHoldStartMs, lastChunkFrameMs latched at the sole inbound 0x13 dispatch site, channelQuietForTx/canTransmitNow three-way helpers (quiet releases+clears; first block logs once and holds; 30 s bound logs best-effort and releases)"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "plan Task 1 six PowerShell greps (QUIET_MS, HOLD_MAX_MS, channelHoldStartMs in header; lastChunkFrameMs >= 2, canTransmitNow >= 1, 'transmit held' in cpp) — all exit 0"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — 2 succeeded"
        status: pass
    human_judgment: false
  - id: D2
    description: "Gate wired at both transmit sites — PENDING first transmit (hold continue-skips, consuming nothing) and SENT ACK-timeout retry (retryCommand fires only on a quiet pass; terminal TIMEOUT path un-gated), with the BUSY-deferral branch, terminal-state guard, backoff block, ackTimeoutFor, and frame dispatch byte-identical to pre-plan HEAD"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "grep canTransmitNow in src/command_sender.cpp == 3 (1 definition + call sites :236 PENDING, :279 SENT); 'best-effort' literal present"
        status: pass
      - kind: other
        ref: "git diff d8ba14e..HEAD hunk map — no hunk in the backoff block (orig :207-220), handleResponse BUSY-deferral/terminal-state guard (orig :393-477), ackTimeoutFor (orig :25-55), or the frame dispatch beyond the chunk-case latch comment"
        status: pass
      - kind: other
        ref: "pio run both targets — 2 succeeded; node scripts/verify_protocol_roundtrip.mjs — 52 PASS / 0 FAIL, exit 0"
        status: pass
    human_judgment: false
  - id: D3
    description: "Structural remedy for the seq=6 4/4-loss class in place and ready for the 01-20 series-A bench discriminator (3 unspaced CAPTURE_NOW triggers -> 3 ACKs / 3 captures, 'transmit held' lines observable under storm)"
    verification:
      - kind: manual_procedural
        ref: "deferred to 01-20 series-A bench (operator hardware session)"
        status: unknown
    human_judgment: true
    rationale: "Behavioral proof requires the two-board hardware bench under a real chunk storm — no automation in this repo can exercise the half-duplex air behavior; the 01-20 plan owns the discriminator"

duration: 7min
completed: 2026-08-25
status: complete
---

# Phase 01 Plan 18: Command Transmit Channel-Quiet Gate (G-01-7 Residual Code Half) Summary

**Base-side channel-quiet gate for command transmits: a receive-time latch on inbound 0x13 chunk frames makes CommandSender hold both the PENDING first transmit and the SENT timeout retry while a chunk stream is active — consuming no retry budget, bounded by a 30 s best-effort escape — so command frames land in the quiet gap that follows every window/push instead of into the storm that ate seq=6's 4 transmissions.**

## Performance

- **Duration:** ~7 min (2026-08-25T11:17:04Z → 11:23:48Z, executor session)
- **Tasks:** 2
- **Files modified:** 2 (`include/command_sender.h`, `src/command_sender.cpp`)
- **Estimate vs actual:** estimate 4,500 tokens at confidence low (0 calibration samples); actuals 2,276 tokens over the realized 2-commit diff (9,105 chars)

## Accomplishments

- Channel-quiet decision machinery in place: `CMD_TX_CHANNEL_QUIET_MS` 750 / `CMD_TX_CHANNEL_HOLD_MAX_MS` 30000 constants naming the G-01-7 residual, `TrackedCommand.channelHoldStartMs` hold latch, `lastChunkFrameMs` stamped at the ONLY inbound chunk path, and the three-way `channelQuietForTx`/`canTransmitNow` helpers
- Both transmit sites gated: the PENDING first transmit and the SENT ACK-timeout retry hold while the stream is active and fire on the first quiet pass; a hold writes only the hold latch — retryCount, lastRetryTime, sendTime, and pendingCommandCount are provably untouched, so no retry is consumed, no failure is booked, and no D-05 ACK window starts
- Bounded hold with honest escapes: 30 s of continuous hold transmits best-effort with a named log; the terminal TIMEOUT path at budget exhaustion is unchanged and un-gated
- 01-15 composition preserved (diff-proven): backoff block, BUSY-deferral branch, terminal-state guard, `ackTimeoutFor`, and frame dispatch byte-identical to pre-plan HEAD; a held PENDING IMAGE_WINDOW_REQUEST stays non-terminal so `windowRequestInFlight` keeps extending the D-24 stall clock without charging a pass
- Both firmware targets green (2 succeeded) after each task; wire harness 52 PASS / 0 FAIL, exit 0 (defer-aware, backoff, and PRI order-gate checks prove the untouched machinery)

## Task Commits

1. **Task 1: Channel-activity latch and the quiet/hold decision helpers** — `57893ff` (feat)
2. **Task 2: Gate the two transmit sites — PENDING first transmit and SENT timeout retry** — `101ebd7` (feat)

## Files Created/Modified

- `include/command_sender.h` — constants with G-01-7 provenance comments; `TrackedCommand.channelHoldStartMs`; private member `lastChunkFrameMs`; `channelQuietForTx`/`canTransmitNow` declarations
- `src/command_sender.cpp` — constructor init and `sendCommand` slot init for the new field; the chunk-case latch before the `ImageRx().onChunkFrame` forward; the two helper definitions; the gate at both transmit sites; latch clears on every successful transmit

## Decisions Made

See key-decisions frontmatter. In brief: latch only the storm class (0x13, not 0x12/0x14); a hold is a wait, not an outcome (only the hold latch is written); composition with the 01-15 defer-aware machinery recorded in the gate comment; the 30 s best-effort bound keeps terminal semantics honest; latch cleared on every successful transmit so each hold episode logs freshly.

## Deviations from Plan

None - plan executed exactly as written.

(One wiring detail beyond the plan's literal action list: `sendCommand`'s slot-init block gained `slot->channelHoldStartMs = 0` beside the existing `sendTime`/`lastRetryTime` resets — the same house-style init discipline for a new TrackedCommand field, guarding the "0 = not currently held" invariant on slot reuse. All acquired slots are already zeroed by memset paths, so this is belt-and-braces, not a behavior change.)

## Issues Encountered

None.

## Auth Gates

None.

## Known Stubs

None — the gate is real scheduling logic at both transmit sites with named logs; no placeholder values, no unwired data.

## Threat Surface

No new surface beyond the plan's threat model. All three register dispositions are implemented as planned: T-01-18-01 (DoS hold) mitigated by the 30 s best-effort bound plus the fact that only CRC-valid, type-dispatched 0x13 frames move the latch; T-01-18-02 (silent never-runs) mitigated by the log-once hold line, the best-effort log, and untouched queue-panel/LED truth (a held command shows its truthful pre-transmit state); T-01-18-03 (self-deadlock) accepted as structurally impossible — every chunk stream is bounded, and the held request stays non-terminal so the 01-15 stall clock extends.

## Gaps Status (unchanged by this plan, per prohibition)

- **G-01-7 residual / WINDOWS entry 3: code-level mechanism IN** — command transmits now hold on active inbound chunk streams without consuming budget, bounded at 30 s. NOT closed: closure is the 01-20 series-A bench (3 unspaced CAPTURE_NOW triggers -> 3 ACKs / 3 captures / zero command timeouts, 'transmit held' lines observable under storm)
- **No gap-status flips this plan** — the balloon-side periodic-yield alternative remains the named next lever if the bench still fails (single-lever discipline, the 01-08 lesson)

## Next Phase Readiness

- Ready for 01-19 (the next plan in this gap-closure round); the 01-20 bench flashes both boards and owns every verdict flip, including this plan's series-A discriminator
- Both firmware targets build green on 01-18 + 01-17 heads; wire harness green — no wire-format change in this plan (no new packet types, no layout change), but both boards reflash together at 01-20 regardless (01-13 discipline)

## Self-Check: PASSED

- Modified files exist on disk: include/command_sender.h, src/command_sender.cpp, .planning/phases/01-command-protocol-control/01-18-SUMMARY.md
- Commits exist on main: 57893ff (Task 1), 101ebd7 (Task 2); `git log --oneline --grep="01-18"` returns both
- Diff scope clean: the two commits touch exactly the plan's `files_modified` set (header 27 insertions, cpp 77 insertions / 1 replaced line); no file deletions; pre-existing working-tree noise (.pio/build/project.checksum, .planning/config.json, 03-UAT.md, untracked .gsd//.planning/research//balloon5.log/base5.log) left unstaged
- Builds 2/2 and harness 52 PASS / 0 FAIL exit 0 verified after each task; Task 1 six greps and Task 2 gate-count/diff-hunk acceptance checks all PASS

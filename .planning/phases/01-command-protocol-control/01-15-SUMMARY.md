---
phase: 01-command-protocol-control
plan: 15
subsystem: protocol
tags: [lora, image-transfer, esp32, state-machine, reliability, arq]

requires:
  - phase: 01-13
    provides: kind-exact base routing (single findTransfer on (imageId, imageKind)) + the 01-12/01-14 stall/window machinery this plan makes defer-aware; 01-14's success-gated manifest transitions bound the TX side this RX side watches
provides:
  - Defer-aware D-24 pass accounting (G-01-7 rescoped lever): ImageRxTransfer.windowRequestSeq tracking + windowRequestInFlight() guard as the FIRST check of both stall sites (full-pull branch + thumbnail heal loop) — a stall never charges a pass or queues a duplicate request while the transfer's outstanding IMAGE_WINDOW_REQUEST is non-terminal (PENDING/SENT); the stall clock extends instead
  - Request cancellation at the window-complete advance and both finalize paths (WR-04 duplicate-request churn + the post-terminal re-arm traffic class)
  - NACK_BUSY deferral branch scoped to IMAGE_WINDOW_REQUEST in CommandSender::handleResponse (WR-05 window-request class) — defers inside the existing D-05/D-07 budget, stays non-terminal so the in-flight guard is correct; link-truth LED stops flagging routine deferrals as a consequence
  - Retry-ordinal log label '(retry %d/%d)' in retryCommand (WINDOWS entry 6 / IN-01 code half) — the 'attempt 4/3' overrun label is gone
affects: [01-16 bench re-verification, G-01-7 residual closure evidence, phase-01 close-out, WINDOWS entry 6]

requirements: [CTRL-06, PRI-02, IMG-03]

actuals:
  tokens: 3297      # chars/4 over the realized 2-commit diff (13,188 chars); estimate was 13,000 at confidence low (0 calibration samples)
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Defer-aware pass accounting: a retransmit budget must count transfer OPPORTUNITIES, not re-requests — while the request's own command budget (ACK timeout + backoff retries, including BUSY deferrals) is still working, the stall clock extends; the pass is charged only after the request reaches a terminal state (ACKED/FAILED/TIMEOUT, with a recycled IDLE slot reading as terminal in the conservative direction)"
    - "One mechanism split across two files must be gated as one: the NACK_BUSY deferral only works because the deferred request STAYS non-terminal (PENDING) — terminalizing it in CommandSender would make the ImageRxManager in-flight check charge passes for requests that never had an opportunity"

key-files:
  created: []
  modified:
    - include/image_rx_manager.h
    - src/image_rx_manager.cpp
    - src/command_sender.cpp

key-decisions:
  - "Defer-aware guard shape: windowRequestInFlight() returns true iff windowRequestSeq != 0 AND getCommandState is PENDING or SENT — ACKED/FAILED/TIMEOUT mean had-its-opportunity, and IDLE (a recycled slot; findFreeSlot memsets only terminal commands) is equally terminal (T-01-15-03, conservative direction). The guard is the FIRST check inside both stall else-if branches, so the existing windowActive==false queue-full no-charge semantics and the D-24 3-pass bound check are structurally unchanged"
  - "windowRequestSeq lifecycle: written ONLY on queue success ('windowRequestSeq = seq' beside the windowActive/windowBase/windowCount writes; the seq==0 queue-full path leaves it as-is, where a stale-but-live seq still correctly extends the stall clock), replaced at the next issueWindowRequest, and never manually zeroed — every slot re-init path already goes through ImageRxTransfer{}"
  - "Cancellation sites: the window-complete advance cancels the serviced request before issuing the next (its ACK-lost retries would re-arm an already-satisfied span), and finalizeTransfer/finalizeIncomplete cancel unconditionally before releaseSlotWork/activate-next — cancelCommand is safe on terminal slots because the WR-03 guard skips the pendingCommandCount decrement; no pre-check needed beyond seq != 0"
  - "NACK_BUSY deferral (WR-05 window class): handleResponse inserts the branch after the terminal-state guard and before response storage — gated on NACK_BUSY AND packet.cmd == IMAGE_WINDOW_REQUEST AND retryCount < maxRetries, it advances retryCount, sets lastRetryTime/sendTime, returns to PENDING, and stores nothing; the existing D-07 backoff block (retryCount > 0 gates on lastRetryTime) paces the retransmit through the PENDING branch. Budget exhaustion or ANY other command class falls through to today's terminal FAILED semantics unchanged (CAPTURE_NOW/SET_* BUSY behavior intact, prohibition held)"
  - "Retry label truth (Entry 6 / IN-01): retryCommand prints the post-increment retryCount (1..maxRetries) against maxRetries — the timeout branch retries only while retryCount < maxRetries, so exactly maxRetries retries fire (D-05/D-07 design) and the label can no longer express an ordinal beyond its bound; counters, bound checks, and sendTime/lastRetryTime writes are byte-identical apart from the format string"
  - "Prohibitions held: IMG_RETRANSMIT_MAX_PASSES 3, IMG_WINDOW_STALL_MS 8000, IMG_WINDOW_RX_SETTLE_MS 500 all untouched; acceptChunk remains the sole passCount zero-writer; main_basestation.cpp absent from both commits (the link-LED fix falls out of PENDING clearing the lastOutcomeBad latch); no gap status flips — bench proof is 01-16's job"

patterns-established:
  - "Log-once latches per semantic identity, not per site: the defer-skip latch (deferSkipLoggedSeq) is shared by both stall sites because a window-request seq belongs to exactly one transfer, so one latch yields exactly one log per sequence regardless of which site defers it"

requirements-completed: []   # CTRL-06, PRI-02, IMG-03 already [x] in REQUIREMENTS.md — no flips this plan

duration: 11min
completed: 2026-08-24
status: complete
---

# Phase 01 Plan 15: Defer-Aware D-24 Pass Accounting + CommandSender Ride-Alongs (G-01-7 Residual / WR-04 / WR-05 / WINDOWS 6) Summary

**Base-side defer-aware pass accounting (windowRequestSeq-tracked stall guards at both stall sites, request cancellation at advance/finalize) plus the NACK_BUSY window-request deferral and the retry-ordinal label — the session-4 BUSY-deferral pass-burn mechanism is closed at code level; bench proof rides 01-16.**

## Performance

- Builds: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — 2 succeeded (after each task)
- Wire harness: `node scripts/verify_protocol_roundtrip.mjs` — 52 PASS / 0 FAIL, exit 0 (after each task; PRI-01 order gates green)
- Pacing anchors unchanged: IMG_RETRANSMIT_MAX_PASSES = 3, IMG_WINDOW_STALL_MS = 8000, IMG_WINDOW_RX_SETTLE_MS = 500 — each defined exactly once in include/image_protocol.h
- Duration: ~11 min (executor session 2026-08-24T04:18-04:30Z); estimate was 13,000 tokens at confidence low (0 calibration samples), actuals 3,297 tokens over the realized 2-commit diff (13,188 chars)

## Tasks Completed

### Task 1: Defer-aware D-24 pass accounting — windowRequestSeq tracking, in-flight stall guards, request cancellation at advance and finalize (7dd5dab)

- `include/image_rx_manager.h`: `uint16_t windowRequestSeq` added inside the D-21 window-context block (0 = none; a recycled terminal command slot reads as IDLE which the in-flight check treats as terminal — correct, because only terminal commands are recycled); private const helper `bool windowRequestInFlight(const ImageRxTransfer&) const` declared beside findActivePull; member `deferSkipLoggedSeq` (log-once latch) declared beside healHoldLoggedId
- `src/image_rx_manager.cpp`: helper implemented after findActivePull (PENDING/SENT = in flight; everything else = had its opportunity); `issueWindowRequest` stores `windowRequestSeq = seq` beside the windowActive/windowBase/windowCount writes on queue success only
- Full-pull stall branch: the in-flight guard is the FIRST check inside the stall else-if — refresh lastProgressMs, log 'ImageRx: stall deferred - window request seq %u still in flight for image %u kind %u' once per sequence (deferSkipLoggedSeq latch), no pass, no request; the D-24 bound check and charge fall into the else-chain unchanged, with the charge-site comments now stating the defer-aware rule
- Heal loop: the same guard as the first check of the per-transfer stall handling (after the used/terminal/kind and lastProgressMs preconditions, before gates and the pass-exhausted check) — this is the exact session-4 failure site (image 7 thumb 35/36 after 3 passes, balloon4.log:1154/:1157)
- Window-complete advance cancels the serviced request before issuing the next; finalizeTransfer and finalizeIncomplete cancel the outstanding request before releaseSlotWork/activate-next (WR-04 duplicate churn + the 14-16 post-terminal re-arm ignores on image 11, base4.log:3286-3310)
- Static gates: windowRequestSeq >= 2 in header (field + helper comment), windowRequestInFlight >= 4 in cpp (definition + 2 call sites + charge-site comment), 'stall deferred - window request seq' == 1, cancelCommand >= 3, 'windowRequestSeq = seq' == 1, IMG_RETRANSMIT_MAX_PASSES 3 anchor == 1 — all PASS; builds 2 succeeded; harness exit 0

### Task 2: CommandSender — NACK_BUSY deferral for IMAGE_WINDOW_REQUEST + the retry-ordinal label (45c8b80)

- `handleResponse`: deferral branch inserted after the terminal-state guard and before response storage — NACK_BUSY AND IMAGE_WINDOW_REQUEST AND retryCount < maxRetries advances retryCount, sets lastRetryTime/sendTime, returns the command to PENDING, logs 'CommandSender: Command seq=%d deferred (balloon window BUSY) - retry pending', and returns with NO response storage, NO pendingCommandCount decrement, NO failure booked; the existing D-07 backoff block paces the retransmit and the PENDING branch performs it (retry-with-backoff through existing machinery, not a new timer)
- Budget exhaustion (retryCount == maxRetries) or any other command class falls through to today's storage + terminal NACK handling unchanged — CAPTURE_NOW/SET_*/GET_STATUS keep FAILED-on-BUSY (prohibition held); the deferral branch's class gate is the only new condition in handleResponse
- `retryCommand` label: '(retry %d/%d)' with the post-increment retryCount (1..maxRetries) against maxRetries — the old '(attempt %d/%d)' with retryCount+1 (which printed 4/3) is gone; every counter, bound check, and timing write byte-identical apart from the format string
- No main_basestation.cpp change: with BUSY no longer terminalizing window requests, the lastOutcomeBad latch never sees FAILED for a mere deferral (PENDING clears it via the existing else-branch) — the link LED stops poisoning during serialized heal/pull sequences as a consequence, and budget-exhausted deferrals still count as real failures exactly as today (verified against the latch at main_basestation.cpp:2185-2193)
- Static gates: 'deferred (balloon window BUSY)' == 1, IMAGE_WINDOW_REQUEST >= 3, '(retry %d/%d)' == 1, NACK_BUSY >= 2, old 'attempt %d/%d' == 0 — all PASS; builds 2 succeeded; harness exit 0

## Verification Results

- Task 1 automated verify: all six PowerShell greps PASS; `pio run` 2 succeeded; harness exit 0
- Task 2 automated verify: all four plan PowerShell greps PASS plus the old-label-absent check; `pio run` 2 succeeded; harness exit 0
- Diff hygiene: Task 1 commit touches exactly include/image_rx_manager.h + src/image_rx_manager.cpp; Task 2 commit touches exactly src/command_sender.cpp; main_basestation.cpp absent from both; no file deletions in either commit
- Pacing anchors verified post-hoc: IMG_RETRANSMIT_MAX_PASSES 3 / IMG_WINDOW_STALL_MS 8000 / IMG_WINDOW_RX_SETTLE_MS 500 each exactly once in include/image_protocol.h

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Observability for the 01-16 bench] Heal-site defer log added beyond the named artifacts**
- **Found during:** Task 1
- **Issue:** The plan's step 5 (heal-loop guard) specifies refresh-and-continue with no log, and the artifacts list names only two log lines — but session 4's G-01-7 failure occurred AT the thumb-heal site, so a bench operator reading 01-16 logs could not discriminate defer-engagement where it mattered without a log there; the plan's own must_haves truth states the mechanism logs 'log-once per sequence' without limiting it to the pull site
- **Fix:** The heal-site guard logs 'ImageRx: heal deferred - window request seq %u still in flight for image %u kind %u' gated on the SAME deferSkipLoggedSeq latch (one log per sequence across both sites; a seq belongs to exactly one transfer, so cross-site suppression cannot hide a real event). Wording deliberately avoids the pinned substring so the plan's machine-checkable 'stall deferred - window request seq' == 1 gate still passes exactly
- **Files modified:** src/image_rx_manager.cpp
- **Commit:** 7dd5dab

Otherwise: plan executed exactly as written.

## Auth Gates

None.

## Known Stubs

None — every added path is real scheduling/response logic with named logs; no placeholder values, no unwired data.

## Threat Surface

No new surface beyond the plan's threat model — the two trust-boundary crossings it names (RF-link NACK_BUSY driving CommandSender state; getCommandState results driving D-24 charging) are the exact surfaces the four register entries (T-01-15-01..04) disposition, and the mitigations were implemented as planned (deferral bounded by the existing maxRetries budget and scoped to the one class; stall extension bounded by the tracked command's own lifetime; staleness reading terminal in the conservative direction; truthful retry ordinals for bench evidence).

## Gaps Status (unchanged by this plan, per prohibition)

- **G-01-7 rescoped lever (defer-aware D-24 pass accounting): code-level mechanism IN** — a BUSY-deferred or ACK-lost window request no longer consumes D-24 passes or duplicates itself, and finalized transfers cancel their outstanding requests. NOT closed: closure requires the 01-16 bench observing a heal surviving BUSY deferrals without pass burn
- **WR-04 / WR-05 (window-request class) / WINDOWS entry 6 (retry label): code-level closed** — bench confirmation rides 01-16
- **G-01-9 / G-01-7 full series-A truth: untouched this plan** — no gap status flips; verdicts come from bench finalize lines (bench is 01-16)

## Self-Check: PASSED

- Commits exist on main: 7dd5dab (Task 1), 45c8b80 (Task 2)
- All three declared files modified across the two commits (git diff 7dd5dab~1..45c8b80 --name-only = exactly the 3); no undeclared files staged; unrelated working-tree changes (build artifacts, logs, .planning/research/, .gsd/, 03-UAT.md, config.json) left unstaged
- Builds 2/2 and harness 52/52 verified after each task

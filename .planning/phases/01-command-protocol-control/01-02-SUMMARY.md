---
phase: 01-command-protocol-control
plan: "02"
subsystem: protocol
tags: [lora, crc16, framing, retry-state-machine, esp32, e32]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (plan 01-01)
    provides: command protocol, sender with retry slots, balloon handler, both build envs
provides:
  - CRC-correct command serialization for every sequence number 1-65535 (CR-01 closed)
  - Shared CMD_MAX_PACKET_SIZE=240 limit with oversize rejection and correctly sized buffers (CR-02 closed)
  - Length-driven receive framing in both endpoints — embedded 0x0D 0x0A payload/data survives (CR-03 closed)
  - Retry terminal states with D-05 per-command ACK timeouts and D-07 exponential backoff (CR-04/WR-03/WR-04 closed)
  - STATUS responses typed end-to-end and counted as success by the sender (WR-05 closed)
  - getCommandRetryCount / getCommandQueue queue-snapshot API feeding the D-16 UI live queue view (plan 01-03)
  - Host wire-format regression harness (scripts/verify_protocol_roundtrip.mjs)
affects: [01-03 (UI per-command outcomes + D-16 queue view), 01-04 (auto-capture), Phase 2 image sequencing]

# Actuals (#2632) — pairs with the plan's `estimate` to calibrate future estimates.
actuals:
  tokens: 8800
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Length-driven packet framing: header bytes 4-5 announce body length; end marker tested only at the framed position (both endpoints share the rule)"
    - "Host-side wire-format transcription tests: JS mirror of the C++ serializer/CRC/framing with a deliberately defective variant to prove the sweep fails"

key-files:
  created:
    - scripts/verify_protocol_roundtrip.mjs
  modified:
    - include/command_protocol.h
    - src/command_protocol.cpp
    - include/command_sender.h
    - src/command_sender.cpp
    - src/command_handler.cpp

key-decisions:
  - "D-05 encoded: ACK-timeout window selected per command type via ackTimeoutFor() — CAPTURE_NOW 2000ms, seven SET_* + AUTO_CAPTURE_ENABLE/DISABLE 5000ms, GET_STATUS 10000ms; flat CMD_ACK_TIMEOUT_MS/ackTimeoutMs/setAckTimeout removed"
  - "D-07 encoded: retry pacing is exponential backoff (CMD_RETRY_BACKOFF_BASE_MS << shift, shift clamped at 2) placed above the PENDING/SENT branches so it gates both transmit retries and ACK-timeout retries; flat retryDelayMs removed"
  - "CR-03 fixed by length-driven framing rather than byte escaping — symmetric fix in both receivers keeps the wire format unchanged from plan 01-01 (D-01..D-03 preserved)"
  - "Regression harness keeps a defective pre-CR-01 serializer variant so the sequence sweep provably has teeth (exactly 255/65535 accepted, matching the verifier's independent simulation)"

patterns-established:
  - "Framing twin rule: CommandSender (responses: header+4+bodyLen+4) and CommandHandler (commands: header+5+bodyLen+4) must always receive the same framing change"
  - "Counted transitions: pendingCommandCount-- appears at exactly five sites (ACKED, NACK-FAILED, TIMEOUT, PENDING-transmit-FAILED, guarded cancel) — new state transitions must preserve this invariant"

requirements-completed: [CTRL-01, CTRL-06, PRI-02]

# Coverage metadata (#1602) — one entry per shipped deliverable.
coverage:
  - id: D1
    description: "Command wire format corrected: CRC16 computed over the real sequence byte (all 65535 sequences validate), 240-byte packet limit shared via CMD_MAX_PACKET_SIZE with oversize rejection and resized send/receive buffers (CR-01, CR-02)"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "node scripts/verify_protocol_roundtrip.mjs — full 1..65535 sweep validates, 225-byte payload rejected, defective variant fails 255/65535"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation"
        status: pass
    human_judgment: false
  - id: D2
    description: "Length-driven receive framing in both endpoints (embedded 0x0D 0x0A in payload/response data received intact; truncated and bogus-length streams rejected) and STATUS responses typed end-to-end, counted as success by the sender (CR-03, WR-05)"
    requirement: PRI-02
    verification:
      - kind: other
        ref: "node scripts/verify_protocol_roundtrip.mjs — framing accumulators for both flavors, truncated/bogus-length rejection"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation"
        status: pass
    human_judgment: false
  - id: D3
    description: "Retry state machine: PENDING transmit failures terminate in FAILED after maxRetries paced attempts; D-05 per-command ACK timeouts; D-07 exponential backoff (2000/4000/8000ms); cancelCommand guard; getCommandRetryCount/getCommandQueue snapshot API (CR-04, WR-03, WR-04, D-16 support)"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "structural gates: pendingCommandCount-- count == 5, D-05/D-07 constants present, ackTimeoutFor/getCommandQueue present, retryDelayMs/ackTimeoutMs absent"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation"
        status: pass
    human_judgment: false
  - id: D4
    description: "End-to-end LoRa command round-trip on physical radios (trigger command, ACK within the D-05 window, retry/TIMEOUT on degraded link)"
    requirement: CTRL-01
    verification: []
    human_judgment: true
    rationale: "RF delivery, E32 AUX timing, and mode switching require physical modules (WR-01/WR-02 affect this); the plan explicitly scopes this closure to code-level blockers and leaves the hardware round-trip as UAT items 1-2 in 01-VERIFICATION.md."

# Metrics
duration: 15 min
completed: 2026-08-18
status: complete
---

# Phase 1 Plan 02: Protocol Gap Closure Summary

**All four verifier-proven protocol defects closed: every sequence number now survives balloon CRC validation, packets are bounded by a shared 240-byte constant, embedded CR/LF pairs no longer truncate either receiver, and retries reach terminal states under D-05 per-command ACK timeouts and D-07 exponential backoff**

## Performance

- **Duration:** ~15 min (including three full dual-target builds)
- **Started:** 2026-08-18T02:08Z
- **Completed:** 2026-08-18T02:24Z
- **Tasks:** 3 (1 tracer + 2 auto)
- **Files modified:** 6 (5 modified, 1 created)

## Accomplishments
- CR-01/CR-02 closed: `serializeCommand` writes the real sequence low byte at header offset 3 before the CRC is computed (post-hoc patch deleted); `CMD_MAX_PACKET_SIZE=240` shared constant with explicit `payloadLength > CMD_MAX_PACKET_SIZE-16` rejection; sender stack buffer and receive buffer sized to the constant
- CR-03/WR-05 closed: both `processIncomingByte` implementations now frame by the header-announced body length (responses header+4+bodyLen+4, commands header+5+bodyLen+4) and test the end marker only at the framed position; GET_STATUS round-trips as a typed STATUS response the sender counts as ACKED
- CR-04/WR-03/WR-04 closed: PENDING transmit failures increment retryCount and terminate in FAILED (pendingCommandCount--/commandsFailed++) at maxRetries; retry pacing is D-07 exponential backoff placed above the state branches; cancelCommand decrements only from PENDING/SENT (exactly five counted transitions remain)
- D-05 encoded: `ackTimeoutFor()` selects the ACK-timeout window per command type (TRIGGER 2000ms / SETTINGS 5000ms / COMPLEX 10000ms); flat timeout constant, member, and setter removed
- New `getCommandRetryCount` and `CommandQueueEntry`/`getCommandQueue` API for the D-16 live queue view consumed by plan 01-03
- `scripts/verify_protocol_roundtrip.mjs`: zero-dependency host regression harness (CRC sweep over all sequences, oversize rejection, framing accumulators, defective-variant teeth proof) — exits 0

## Task Commits

Each task was committed atomically:

1. **Task 1: CRC/sequence ordering + packet-size limits (CR-01/CR-02)** - `fd77066` (fix)
2. **Task 2: Length-driven framing both receivers + STATUS typing (CR-03/WR-05)** - `ce40407` (fix)
3. **Task 3: Retry terminal states, D-05/D-07, cancel guard + queue getters (CR-04/WR-03/WR-04)** - `ae9710a` (fix)

**Plan metadata:** this commit (docs: complete plan)

## Files Created/Modified
- `include/command_protocol.h` - CMD_MAX_PACKET_SIZE constant; D-05 timeout constants; D-07 backoff base; flat CMD_ACK_TIMEOUT_MS removed
- `src/command_protocol.cpp` - real sequence byte before CRC; CMD_MAX_PACKET_SIZE-based rejection in both serializers; post-serialization patch deleted
- `include/command_sender.h` - receiveBuffer sized to CMD_MAX_PACKET_SIZE; CommandQueueEntry struct; getCommandRetryCount/getCommandQueue declarations; flat retry config members removed
- `src/command_sender.cpp` - length-driven response framing; PENDING transmit-failure terminal path; D-05 ackTimeoutFor; D-07 backoff pacing above state branches; cancel guard; STATUS-as-success; getCommandRetryCount/getCommandQueue implementations
- `src/command_handler.cpp` - length-driven command framing; success responses typed by result.responseType
- `scripts/verify_protocol_roundtrip.mjs` - NEW host wire-format regression harness

## Decisions Made
- Pacing semantics for D-07: `retryCount` is the retry attempt about to run (incremented by the previous failed/un-ACKed attempt), so backoff = `CMD_RETRY_BACKOFF_BASE_MS << min(retryCount-1, 2)` yields exactly 2000/4000/8000ms gaps between attempts on the transmit-failure path, and the ACK-timeout window (D-05) dominates when longer
- Removed now-meaningless flat-timeout API surface (`CMD_ACK_TIMEOUT_MS`, `ackTimeoutMs`, `setAckTimeout`, `retryDelayMs`) once D-05/D-07 replaced their uses — nothing outside command_sender referenced them (verified by grep before removal)
- Kept the defective pre-CR-01 serializer transcribed inside the harness (clearly named `defective` option) rather than deleting it, so every future run re-proves the sweep can fail
- `getCommandQueue` fills entries in slot order and reports the slot's stored CameraCommand value as a plain byte, matching the D-16 render contract in plan 01-03

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None. All three task verify commands and acceptance gates passed on first execution; both PlatformIO environments built SUCCESS after every task.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Plans 01-03 (UI completion, per-command outcomes, D-16 live queue view via getCommandQueue) and 01-04 (auto-capture timer) can proceed; both consume APIs delivered here
- The wire format is now regression-locked: any future change to packet layout, CRC, or framing must update `scripts/verify_protocol_roundtrip.mjs` (header contract in the script)
- Hardware round-trip remains the outstanding proof for CTRL-01/CTRL-06 — UAT items 1-2 in 01-VERIFICATION.md; WR-01 (blocking E32 transmit) and WR-02 (E32 config frame) remain deferred to radio bring-up as planned

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-18*

## Self-Check: PASSED

- All 3 tasks executed; 3 atomic production commits (fd77066, ce40407, ae9710a) + this docs commit
- `node scripts/verify_protocol_roundtrip.mjs` — exit 0 (10/10 clauses)
- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — 2 succeeded (re-run after final task)
- Structural gates re-verified: no "will be overwritten" / "Fix sequence number in header" / rolling end-marker patterns; CMD_MAX_PACKET_SIZE/D-05/D-07 constants, ackTimeoutFor, getCommandQueue, getCommandRetryCount present; pendingCommandCount-- exactly 5; buffer[CMD_MAX_PACKET_SIZE] in sender
- Key files exist on disk (6/6)

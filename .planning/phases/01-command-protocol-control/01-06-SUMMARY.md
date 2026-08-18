---
phase: 01-command-protocol-control
plan: "06"
subsystem: protocol
tags: [lora, protocol-conformance, esp32, crc16, regression-harness, enum-mapping]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (01-05)
    provides: CR-05 closure baseline — AutoCapture sole capture authority, both targets green, wire harness 10/10
provides:
  - Response packet-type conformance: every ACK/STATUS built by createResponsePacket carries PACKET_TYPE_RESPONSE (0x11), assigned at construction and emitted verbatim by the serializer
  - Faithful response-construction harness: createResponsePacketMirror transcription + serializer emitting the packet's own type field + clause (f) with defective-variant teeth (WR-05 closed)
  - Truthful GET_STATUS resolution: frameSizeFromEsp name-based reverse mapping (real framesize_t -> project FrameSize), both directions translated by name
  - Exactly-once response accounting: terminal-state guard in CommandSender::handleResponse discards duplicate/late responses before any counter change or response overwrite
affects: [02-image-transmission]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 4059    # 16235 diff chars / 4 over the 7 files actually changed
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Packet type assigned in the factory (construction), never only hardcoded in a serializer — struct-level symmetry so every construction path emits the documented byte
    - Cross-enum translation by NAME (framesizeFromInt forward / frameSizeFromEsp reverse) between differently-numbered enums; numeric reinterpretation prohibited
    - Terminal-state guard before any counter mutation in transition-performing consumers; count-mutating consumers that resolve terminal slots guard their own decrement

key-files:
  created: []
  modified:
    - src/command_protocol.cpp
    - include/command_protocol.h
    - src/command_handler.cpp
    - include/command_handler.h
    - src/command_sender.cpp
    - src/main_basestation.cpp
    - scripts/verify_protocol_roundtrip.mjs

key-decisions:
  - "Packet type set at construction, not by serializer hardcoding: createResponsePacket/createCommandPacket assign the type as the first field; serializers keep emitting the struct field verbatim — the wire conforms on every path, including future factories"
  - "FrameSize value 8 relabeled FRAMESIZE_QXGA -> FRAMESIZE_CIF (same wire code): 400x296 is the real CIF mode; real QXGA 2048x1536 is unreachable on the OV2640, so the old label named a mode that could only NACK"
  - "Reverse-mapping default: real sizes without a protocol code (96x96, QCIF, 240x240, HVGA, HD) report as boot-default QVGA — a documented fallback, never a numeric cast"
  - "Terminal-state guard placed in handleResponse only: cancelCommand legitimately resolves terminal slots (own decrement guard no-ops them, WR-03) and UI queries read terminal slots, so findTrackedCommand keeps matching them"

patterns-established:
  - "Factory-owns-type: wire-format-defining fields are assigned where packets are constructed; harness transcribes the factory (zero-init + field assignments), not just the serializer"

requirements-completed: [CTRL-01, CTRL-02, CTRL-06, PRI-02]

# Coverage metadata (#1602) — one entry per shipped deliverable.
coverage:
  - id: D1
    description: "Every successful balloon response (ACK/STATUS) serializes with header type byte 0x11 on the real construction path (createResponsePacket -> serializeResponse); commands carry 0x10 by assignment"
    requirement: CTRL-01
    verification:
      - kind: unit
        ref: "scripts/verify_protocol_roundtrip.mjs#clause (f1)-(f4) — factory-mirrored ACK and NACK paths assert byte 2 == 0x11, identical type bytes, defective no-type variant yields 0x00"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — both SUCCESS"
        status: pass
    human_judgment: false
  - id: D2
    description: "GET_STATUS reports the camera's actual resolution through name-based translation in both directions; value-8 UI option executes real FRAMESIZE_CIF and is labeled CIF 400x296"
    requirement: CTRL-02
    verification:
      - kind: other
        ref: "source gates: frameSizeFromEsp(camera->getFrameSize()) wired in handleGetStatus; by-name cases for nine sizes + documented QVGA default; FRAMESIZE_QXGA absent from src/ + include/; false matching-comment absent; both pio targets SUCCESS"
        status: pass
    human_judgment: true
    rationale: "The truthful report over the radio link and real OV2640 sensor state require hardware UAT (01-VERIFICATION.md items 1/3); code-level proof is the by-name switch, source gates, and builds only"
  - id: D3
    description: "Duplicate or late response for a terminal command (ACKED/FAILED/TIMEOUT) is discarded before response storage, counter changes, or statistics — pendingCommandCount decrements exactly once per command"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "region-scoped gate: single-line ACKED/FAILED/TIMEOUT guard inside CommandSender::handleResponse before cmd->response storage; cancelCommand/findTrackedCommand/retryCommand unchanged; both pio targets SUCCESS"
        status: pass
    human_judgment: true
    rationale: "The retry-edge duplicate-ACK race is a runtime phenomenon over a degraded RF link (UAT item 2, to be run after this fix per the verifier's note); code-level proof is the guard placement and unchanged sibling mechanics"

# Metrics
duration: 11min
completed: 2026-08-18
status: complete
---

# Phase 1 Plan 06: Response-Path Gap Closure Summary

**All three verifier-confirmed response-path criticals closed: 0x11 assigned at packet construction with a factory-faithful harness clause, FrameSize translated by name in both directions, and a terminal-state guard making pending-command accounting exactly-once.**

## Performance

- **Duration:** 11 min
- **Started:** 2026-08-18T10:47:37Z
- **Completed:** 2026-08-18T10:58:53Z
- **Tasks:** 3
- **Files modified:** 7

## Accomplishments

- Gap 1 (CR-01/WR-05): `createResponsePacket` assigns `packet.type = PACKET_TYPE_RESPONSE` as the first field set, so the entire success path of `CommandHandler::process` (every ACK and STATUS) goes on the wire with header type 0x11; `createCommandPacket` assigns `PACKET_TYPE_COMMAND` for symmetry; `createACK`/`createNACK`/`createStatus` derive the type from one shared constant (zero inline 0x11 casts remain)
- Harness faithfulness: `createResponsePacketMirror` transcribes the C++ factory (zero-initialized type, `defective` option models the pre-fix omission); the `serializeResponse` mirror emits the packet's own type field at byte 2 exactly as the firmware emits `resp.type` verbatim; clauses (c)/(e) routed through the factory; new clause (f) proves byte 2 == 0x11 on ACK and NACK paths, identical type bytes, defective variant 0x00, and CRC validity of the fixed ACK
- Gap 2 (CR-02/WR-01): new `CommandHandler::frameSizeFromEsp(framesize_t) const` maps the real esp32-camera enum to project codes by NAME (nine supported sizes; documented QVGA fallback for 96x96/QCIF/240x240/HVGA/HD); `handleGetStatus` uses it, so boot QVGA reports as QVGA 320x240 instead of 160x120; FrameSize value 8 renamed `FRAMESIZE_CIF` (wire code unchanged), maps to real FRAMESIZE_CIF 400x296, and the UI option now names the mode the OV2640 actually executes
- Gap 3 (CR-03): terminal-state guard at the top of `CommandSender::handleResponse` — duplicate/late responses for ACKED/FAILED/TIMEOUT slots return before response storage, `pendingCommandCount` changes, or statistics updates, so the retry-edge duplicate ACK can no longer underflow the uint8 counter and latch `hasPendingCommands()` true

## Task Commits

Each task was committed atomically:

1. **Task 1: Response packet-type conformance + faithful harness clause (gap 1, CR-01/WR-05)** - `75b8514` (fix)
2. **Task 2: Truthful GET_STATUS resolution via name-based reverse mapping + enum relabel (gap 2, CR-02/WR-01)** - `36674ff` (fix)
3. **Task 3: Terminal-state guard in CommandSender::handleResponse + full regression battery (gap 3, CR-03)** - `56704e2` (fix)

## Files Created/Modified

- `src/command_protocol.cpp` - packet.type assignments in both factories; shared PACKET_TYPE_RESPONSE constant in createACK/createNACK/createStatus
- `include/command_protocol.h` - PACKET_TYPE_RESPONSE constant; FrameSize block rewritten as protocol-internal wire codes translated by name; value 8 renamed FRAMESIZE_CIF
- `src/command_handler.cpp` - frameSizeFromEsp definition (by-name reverse mapping); handleGetStatus call-site substitution; framesizeFromInt CIF case
- `include/command_handler.h` - frameSizeFromEsp declaration next to framesizeFromInt
- `src/command_sender.cpp` - terminal-state guard in handleResponse
- `src/main_basestation.cpp` - resolution select value-8 option relabeled "CIF 400x296"
- `scripts/verify_protocol_roundtrip.mjs` - createResponsePacketMirror, packet-object serializeResponse mirror, clause (f) with defective-variant teeth, header clause list updated

## Decisions Made

- Packet type is owned by the factory, not the serializer: both construction functions assign it as the first field, so any future factory or serializer change preserves the documented wire byte
- The reverse mapping's default reports boot-default QVGA for real sizes with no protocol code — a truthful, documented fallback rather than a numeric reinterpretation
- The terminal-state guard lives in `handleResponse` only (the one consumer whose job is transitions); `findTrackedCommand` still matches terminal slots because `cancelCommand` and UI queries legitimately resolve them

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] frameSizeFromEsp definition missing the const qualifier**
- **Found during:** Task 2 (first build after the edits)
- **Issue:** Header declared `FrameSize frameSizeFromEsp(framesize_t) const` but the definition omitted `const` — "no declaration matches" compile error on the balloon target
- **Fix:** Added the `const` qualifier to the definition
- **Files modified:** src/command_handler.cpp
- **Verification:** `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS
- **Committed in:** 36674ff (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Trivial signature fix within the task's own new code. No scope creep.

## Issues Encountered

None beyond the auto-fix above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- All three verifier-confirmed response-path criticals closed at code level; both firmware targets build SUCCESS; the extended wire-format harness passes 15/15 clauses including the new factory-faithful clause (f)
- Remaining before phase complete: security gate (`/gsd-secure-phase 1`) and hardware UAT items 1-4 (01-VERIFICATION.md) — UAT item 2 (degraded-link retry) is now meaningful with the duplicate-ACK guard in place, per the verifier's note to run it after this fix
- Known carried warnings (out of scope for this plan, recorded in 01-VERIFICATION.md): WR-06 ACK-edge truncation, WR-07 E32 config API, WR-10 auto-capture chip latch, WR-11 thumbnail estimate — deferred to their noted phases

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-18*

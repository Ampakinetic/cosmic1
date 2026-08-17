---
phase: 01-command-protocol-control
plan: "01"
subsystem: firmware
tags: [esp32-s3, lora, e32-900t30d, uart, camera-control, crc16, retry-logic, arduino]

# Dependency graph
requires:
  - phase: existing platform
    provides: CameraManager, packet conventions from common_types.h, PlatformIO build system
provides:
  - Camera command packet protocol with CRC16 validation and sequence matching
  - E32-900T30D UART LoRa driver (mode control, AUX monitoring, config mode)
  - Base station CommandSender with ACK timeout and retry (3 attempts, 2000ms)
  - Balloon CommandHandler executing commands against CameraManager with ACK/NACK
  - Base station web UI with capture trigger and settings forms
affects: [02-image-transmission, 03-telemetry-map]

# Actuals (#2632)
actuals:
  tokens: 39000
  tasks: 8
  commits: 9

# Tech tracking
tech-stack:
  added: [E32 UART driver (custom, no external lib), WebServer (ESP32)]
  patterns: [static singleton accessor (CmdSender()/CmdHandler()/E32LoRaModule()), byte-stream packet framing with start/end markers, big-endian wire serialization]

key-files:
  created:
    - include/command_protocol.h
    - include/e32_lora.h
    - include/command_sender.h
    - include/command_handler.h
    - src/command_protocol.cpp
    - src/e32_lora.cpp
    - src/command_sender.cpp
    - src/command_handler.cpp
    - src/main_basestation.cpp
  modified:
    - src/main_balloon.cpp
    - include/common_types.h
    - platformio.ini

key-decisions:
  - "Custom E32 UART driver instead of a library — E32 uses fixed-parameter frames, not AT commands"
  - "Retry state machine lives on the base station (CommandSender); balloon stays stateless per-command"
  - "Auto-capture handlers accept and validate commands but the interval timer is a placeholder (not implemented)"

patterns-established:
  - "Singleton accessor: `extern T& Name()` returning a static instance (CmdSender, CmdHandler, E32LoRaModule)"
  - "Packet framing: 0xAA 0x55 start, 0x0D 0x0A end, CRC16-CCITT over payload, big-endian multi-byte fields"

requirements-completed: [CTRL-01, CTRL-06, PRI-02]  # code-complete; hardware verification still required. CTRL-02 partial (UI exposes 3 of 7 settings). CTRL-03/CTRL-04 NOT delivered (auto-capture placeholder).

coverage:
  - id: D1
    description: "Camera command protocol definitions with CRC16 serializer/deserializer (command, ACK/NACK/STATUS responses)"
    requirement: CTRL-01
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon && pio run -e esp32-s3-basestation (both SUCCESS)"
        status: pass
    human_judgment: false
  - id: D2
    description: "E32-900T30D UART LoRa driver (transmit, receive, config mode, AUX monitoring)"
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon && pio run -e esp32-s3-basestation (both SUCCESS)"
        status: pass
    human_judgment: true
    rationale: "Radio communication behavior (AUX timing, mode switching, actual RF delivery) can only be confirmed with real E32 modules on hardware."
  - id: D3
    description: "Base station CommandSender with 2000ms ACK timeout, 3 retries, 5-slot pending queue"
    requirement: CTRL-06
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation (SUCCESS)"
        status: pass
    human_judgment: true
    rationale: "Retry/timeout behavior over a real LoRa link requires two radios; no host-side unit tests exist for this firmware."
  - id: D4
    description: "Balloon CommandHandler executing capture + all 7 settings commands against CameraManager with ACK/NACK"
    requirement: CTRL-02
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon (SUCCESS)"
        status: pass
    human_judgment: true
    rationale: "Command execution against a real camera module requires hardware."
  - id: D5
    description: "Base station web UI: capture trigger button, quality/brightness/contrast forms, status polling"
    requirement: CTRL-01
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation (SUCCESS)"
        status: pass
    human_judgment: true
    rationale: "End-to-end web-to-balloon command flow requires both units powered with radios linked."

# Metrics
duration: closeout session 2026-08-18 (implementation by prior session, uncommitted)
completed: 2026-08-18
status: complete
---

# Phase 1: Command Protocol & Control Summary

**Bidirectional LoRa camera control: command protocol with CRC16, custom E32 UART driver, retrying base-station sender, balloon ACK/NACK handler, and web UI — auto-capture timer left as placeholder**

## Performance

- **Duration:** Prior session (uncommitted) + closeout 2026-08-18
- **Tasks:** 8 of 8 tracer tasks (T1–T8); T9 hardware test pending
- **Files modified:** 12 (9 created, 3 modified + docs)

## Accomplishments
- Command packet protocol (0x10/0x11 types, sequence-matched ACK/NACK, CRC16-CCITT, 240-byte LoRa-safe frames)
- Custom E32-900T30D UART driver with M0/M1 mode control, AUX monitoring, and config-mode register access
- Base station sender tracking up to 5 commands with 2000ms ACK timeout and 3 retries
- Balloon handler executing CAPTURE_NOW plus all 7 settings commands against CameraManager
- Base station web UI with capture trigger, quality/brightness/contrast forms, command statistics
- Both firmware targets build clean (balloon: RAM 40.9%/Flash 14.6%; basestation: RAM 14.7%/Flash 29.4%)

## Task Commits

Each task was committed atomically (during closeout — implementation predates commits):

1. **T1: Command protocol structure** - `1f9de55` (feat)
2. **T2: E32-900T30D UART LoRa driver** - `037af41` (feat)
3. **T3: Command serializer/deserializer** - `9d8a1ac` (feat)
4. **T4: Base station command sender with retry** - `3ccafb5` (feat)
5. **T5–T7: Balloon command handler, ACK/NACK, camera wiring** - `c9ebbf3` (feat)
6. **T8: Base station web UI** - `bd50a30` (feat)
7. **Integration: balloon firmware + build targets** - `a6c5e61` (feat)
8. **Plan + context docs** - `3962ccf` (docs)

## Files Created/Modified
- `include/command_protocol.h` - Command/response packet structures, payload structs, protocol constants
- `src/command_protocol.cpp` - CRC16, serialization, ACK/NACK/STATUS factories
- `include/e32_lora.h` / `src/e32_lora.cpp` - E32 UART driver (transmit, receive, config, AUX)
- `include/command_sender.h` / `src/command_sender.cpp` - Retry state machine, pending queue, response matching
- `include/command_handler.h` / `src/command_handler.cpp` - Command dispatch, camera execution, ACK/NACK generation
- `src/main_basestation.cpp` - WiFi AP, web server, camera control endpoints, status JSON
- `src/main_balloon.cpp` - E32 + CommandHandler init and main-loop processing
- `include/common_types.h` - COMMAND (0x10) / RESPONSE (0x11) packet types
- `platformio.ini` - `esp32-s3-basestation` environment; balloon env excludes base-station sources

## Decisions Made
- Custom E32 driver (no external library) — E32 config frames are fixed-parameter binary, poor fit for generic LoRa libs
- Retry logic centralized on base station; balloon handler is per-command stateless
- Web UI embedded as generated HTML strings in `main_basestation.cpp` (no filesystem dependency)

## Deviations from Plan

### Auto-fixed Issues

**1. [Recovery - Blocking] Prior session left all work uncommitted**
- **Found during:** execute-phase safe-resume gate
- **Issue:** Previous session implemented T1–T8 (3,286 lines) but committed nothing and wrote no SUMMARY.md
- **Fix:** Closeout per user instruction: verified builds, committed as 8 task-aligned atomic commits, wrote this SUMMARY
- **Files modified:** none beyond committing
- **Verification:** both `pio run` targets SUCCESS; `git log` shows 01-01 commits
- **Committed in:** `1f9de55..a6c5e61`

---

**Total deviations:** 1 recovery auto-fix
**Impact on plan:** None on scope; restored git history fidelity.

## Issues Encountered
- **Auto-capture is a placeholder** (`command_handler.cpp:519` logs "placeholder"; `:546` TODO for state tracking; no interval timer fires captures). CTRL-03/CTRL-04 not functionally delivered. Web UI has no auto-capture controls (A4 not started).
- **Settings UI partial** (E4): balloon supports all 7 settings; web UI exposes only quality/brightness/contrast.
- Hardware tests (T9/E5/A5) not run — require both ESP32-S3 boards + E32 radios + camera.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Command channel + retry protocol established — Phase 2 image transmission can ride the same framing
- Blockers: auto-capture timer (CTRL-03/04), full settings UI, and hardware verification outstanding

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-18*

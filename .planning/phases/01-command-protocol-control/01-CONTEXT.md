# Phase 1: Command Protocol & Control - Context

**Gathered:** 2025-08-18
**Status:** Ready for planning

## Phase Boundary

This phase delivers **bidirectional LoRa communication for remote camera control**. Base station sends camera commands to balloon, balloon executes and acknowledges, with retry logic for reliability. This does NOT include image transmission (Phase 2) or enhanced web UI with maps/telemetry (Phase 3).

## Implementation Decisions

### Command Packet Structure
- **D-01:** Extend existing `PacketType` enum to add command packet types (CAMERA_TRIGGER, CAMERA_SET) rather than creating separate protocol — maintains compatibility with existing telemetry infrastructure
- **D-02:** Use numeric action codes for individual camera commands (TRIGGER=1, SET_RESOLUTION=2, SET_QUALITY=3, etc.) — efficient encoding, easy to parse
- **D-03:** Camera settings encoded as binary struct matching existing sensor data format — consistent parsing, minimal overhead
- **D-04:** Balloon sends ACK + echoes back current setting value — confirms command received and provides feedback on new state

### Retry Mechanism Design
- **D-05:** Command-specific timeout durations — TRIGGER: 2s (fast capture), SETTINGS: 5s (configuration), COMPLEX: 10s (multi-setting commands)
- **D-06:** Maximum 3 retry attempts before giving up — balanced persistence, avoids indefinite waiting
- **D-07:** Exponential backoff between retries (2s, 4s, 8s) — standard approach, respects LoRa congestion
- **D-08:** Show retry progress to user — displays each attempt, provides transparency on command status

### Camera Settings Format
- **D-09:** Resolution as predefined enum codes (QVGA=0, VGA=1, SVGA=2, XGA=3, SXGA=4, UXGA=5) — matches ESP32 supported resolutions, single byte
- **D-10:** Image quality as 0-10 integer scale (0=low, 10=high) — intuitive for users, maps to ESP32 quality range
- **D-11:** Brightness as signed int8 (-2 to +2) — direct ESP32 compatibility, concise encoding
- **D-12:** Exposure as separate mode enum + gain value — provides full control while keeping clear separation

### Base Station UI Layout
- **D-13:** Camera controls on separate page from telemetry display — clear separation of concerns, avoids clutter
- **D-14:** Large prominent "CAPTURE" button for camera trigger — primary action easy to access
- **D-15:** Camera settings organized in accordion panels (Resolution, Quality, Exposure, White Balance, etc.) — compact when closed, organized expansion
- **D-16:** Display live command queue showing pending/in-progress states — full visibility of all commands, not just last one

### Claude's Discretion
No areas delegated to Claude's discretion — all decisions explicitly specified by user.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing Codebase
- `.planning/codebase/ARCHITECTURE.md` — Component responsibilities (PacketHandler, LoRaComm, CameraManager)
- `.planning/codebase/CONVENTIONS.md` — Naming patterns (camelCase functions, PascalCase structs)
- `.planning/codebase/STACK.md` — PlatformIO build system, Arduino framework dependencies

### Project Documentation
- `.planning/PROJECT.md` — Core value, constraints, existing validated requirements
- `.planning/REQUIREMENTS.md` — Full requirement list with REQ-IDs
- `.planning/ROADMAP.md` — Phase scope, success criteria, deliverables

### Existing Source Files (for reference)
- `src/packet_handler.cpp` — Existing packet serialization, CRC validation
- `src/lora_comm.cpp` — Priority queue, adaptive transmission, packet handling
- `src/camera_manager.cpp` — Camera control methods, settings structures
- `src/app_httpd.cpp` — Web interface patterns, HTML serving

## Existing Code Insights

### Reusable Assets
- **`PacketHandler` class**: Existing packet serialization with CRC validation — extend with command packet types
- **`LoRaComm` priority queue**: Telemetry priority system — command packets should respect this hierarchy
- **`CameraManager` settings structures**: Existing camera configuration — mirror in command payload structs
- **Web server infrastructure**: `app_httpd.cpp` patterns — base for new command interface endpoints

### Established Patterns
- **Singleton access pattern**: Global functions (Sensors(), Camera(), LoRaComm()) — maintain for new command handler
- **Packet header structure**: Header + Type + Sequence + Payload + CRC — follow for command packets
- **Priority-based transmission**: Emergency > GPS > Telemetry > Camera > Status — commands fit at appropriate priority level
- **Error handling with validation flags**: Data structures include `valid` boolean — apply to command responses

### Integration Points
- **Base station LoRa receiver**: Add command packet transmission to existing `LoRaComm::send()` methods
- **Balloon packet handler**: Add command packet type switch in `PacketHandler::processPacket()`
- **Camera manager**: Add command execution methods that mirror existing settings application
- **Base station web UI**: New routes/endpoint for camera controls (separate from existing telemetry display)

## Specific Ideas

No specific requirements — open to standard approaches consistent with existing codebase patterns.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 1-Command Protocol & Control*
*Context gathered: 2025-08-18*

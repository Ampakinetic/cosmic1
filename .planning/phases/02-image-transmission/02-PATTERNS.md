# Phase 2: Image Transmission - Pattern Map

**Mapped:** 2026-08-19
**Files analyzed:** 13 (5 new, 7 modified, 1 script)
**Analogs found:** 12 / 13 (SD storage module has no in-codebase analog — see No Analog Found)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/image_protocol.h` (NEW) | model/protocol | request-response (wire) | `include/command_protocol.h` | exact |
| `include/image_tx_manager.h` + `src/image_tx_manager.cpp` (NEW, balloon) | service | streaming / batch | `src/auto_capture.cpp` + `src/command_sender.cpp` | role-match |
| `include/image_rx_manager.h` + `src/image_rx_manager.cpp` (NEW, base) | service | streaming (reassembly) | `src/command_sender.cpp` (receive path) | role-match |
| `include/sd_storage.h` + `src/sd_storage.cpp` (NEW, base) | service | file-I/O | none | none (see below) |
| `include/auto_capture.h` / `src/auto_capture.cpp` (EXTENDED) | service | event-driven | itself + `src/system_state.cpp` | exact |
| `src/command_protocol.cpp` + `include/command_protocol.h` (EXTENDED) | protocol/model | transform | itself | exact |
| `src/command_handler.cpp` (EXTENDED) | controller | request-response | itself | exact |
| `src/command_sender.cpp` (EXTENDED) | controller | request-response | itself | exact |
| `src/camera_manager.cpp` (FIXED — CR-04/WR-11) | service | file-I/O-ish (buffer) | itself (`captureImage`) | exact |
| `src/main_balloon.cpp` (EXTENDED) | wiring | event-driven loop | itself | exact |
| `src/main_basestation.cpp` (EXTENDED) | wiring/UI | request-response | itself | exact |
| `scripts/verify_protocol_roundtrip.mjs` (EXTENDED) | test | transform | itself | exact |

## Pattern Assignments

### `include/image_protocol.h` (NEW — protocol structs/constants)

**Analog:** `include/command_protocol.h`

Follow its layout section-for-section (packet-type constants at top lines 14-15, command ID enum lines 28-47, packet structs lines 78-104, payload structs lines 111-173, constants block lines 179-205):

```cpp
// include/command_protocol.h:14-15 — add 0x12/0x13 alongside
static constexpr PacketType PACKET_TYPE_COMMAND  = static_cast<PacketType>(0x10);
static constexpr PacketType PACKET_TYPE_RESPONSE = static_cast<PacketType>(0x11);
// NEW:
static constexpr PacketType PACKET_TYPE_IMAGE_MANIFEST = static_cast<PacketType>(0x12);
static constexpr PacketType PACKET_TYPE_IMAGE_CHUNK    = static_cast<PacketType>(0x13);
```

```cpp
// include/command_protocol.h:28-47 — new CameraCommand rides the same enum (IMAGE_WINDOW_REQUEST ~0x30)
enum class CameraCommand : uint8_t {
    CAPTURE_NOW = 0x01,
    ...
    GET_STATUS = 0x20
};
```

```cpp
// include/command_protocol.h:180-181 — reuse, don't redefine, the budget constants
static constexpr size_t CMD_MAX_PAYLOAD_SIZE = 200;
static constexpr size_t CMD_MAX_PACKET_SIZE = 240;
// NEW timeout class beside these (include/command_protocol.h:184-187):
static constexpr uint32_t CMD_ACK_TIMEOUT_TRIGGER_MS = 2000;
static constexpr uint32_t CMD_ACK_TIMEOUT_COMPLEX_MS = 10000;
static constexpr uint32_t CMD_ACK_TIMEOUT_WINDOW_MS = 15000;  // 16-chunk stream (RESEARCH Pitfall 4)
```

Body structs per RESEARCH Pattern 2 (`ImageManifestBody`, `ImageChunkBody`) — all multi-byte fields big-endian on wire.

### `src/command_protocol.cpp` + header (EXTENDED — serialize/deserialize)

**Analog:** itself; byte-order helpers at `src/command_protocol.cpp:325-347`

```cpp
// src/command_protocol.cpp:325-335 — ALL new wire fields go through these (Pitfall 9 / IN-08)
void CommandProtocol::writeUint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}
void CommandProtocol::writeUint32(uint8_t* buffer, uint32_t value) { ... }
```

```cpp
// src/command_protocol.cpp:353-368 — factory assigns wire type at construction (CR-01 lesson)
CommandPacket createCommandPacket(CameraCommand cmd, uint16_t sequence, const void* payload, size_t payloadSize) {
    CommandPacket packet{};
    packet.type = PACKET_TYPE_COMMAND; // wire type assigned at construction
    ...
}
// NEW factories: createManifestPacket / createChunkPacket must set packet.type themselves
```

CRC16: reuse `CommandProtocol::calculateCRC16` / `validateCRC` unchanged. CRC32: `esp_rom_crc32_le(0, data, len)` from `<esp_rom_crc.h>` on both firmwares (no hand-rolled table).

### `src/command_sender.cpp` (EXTENDED — base-side receive + window requests)

**Analog:** itself — two excerpts are load-bearing.

**Length-driven framing receive path with type dispatch** (`src/command_sender.cpp:267-317`) — the WR-12 fix inserts a type-byte switch before the body arithmetic:

```cpp
// src/command_sender.cpp:286-298 (existing) — insert switch on receiveBuffer[2] here:
    if (receiveIndex < CMD_HEADER_SIZE) { return; }
    size_t bodyLen = (static_cast<size_t>(receiveBuffer[4]) << 8) | receiveBuffer[5];
    // Responses: header + 4 + bodyLen + 4
    size_t expectedTotal = CMD_HEADER_SIZE + 4 + bodyLen + 4;
    if (expectedTotal > sizeof(receiveBuffer) || bodyLen > CMD_MAX_RESPONSE_DATA) {
        resetReceiveState(); return;
    }
// NEW (shape): switch (receiveBuffer[2]) { RESPONSE / MANIFEST / CHUNK branches with
// per-type overhead (manifest: fixed struct len; chunk: 5), default: resetReceiveState(); }
```

**Tracked-command machinery** (`src/command_sender.cpp:24-46` `ackTimeoutFor`, `108-146` `sendCommand`, `319-367` `handleResponse`): window requests ride this unchanged — add `IMAGE_WINDOW_REQUEST` to the timeout switch returning the new ~15 s class; the duplicate/terminal-state guard at lines 336-338 (`if (state == ACKED || FAILED || TIMEOUT) return;`) applies verbatim to window-complete responses.

**Retry backoff pacing** (`src/command_sender.cpp:199-209`): do not re-request windows on a free-running timer (half-duplex, Pitfall 5).

### `src/command_handler.cpp` (EXTENDED — balloon-side dispatch + chunk servicing)

**Analog:** itself.

```cpp
// src/command_handler.cpp:147-186 — add cases to the same switch:
    switch (cmd.cmd) {
        case CameraCommand::CAPTURE_NOW: return handleCaptureNow(cmd);
        ...
        // NEW: IMAGE_WINDOW_REQUEST, SET_EVENT_THRESHOLD_* -> handleImageWindowRequest etc.
        default:
            result.responseType = ResponseType::NACK_INVALID; ...
```

```cpp
// src/command_handler.cpp:95-116 — result->response mapping pattern (WR-05: typed, not flattened)
    if (result.success) {
        response = createResponsePacket(result.responseType, pendingCommand.packet.sequenceNumber,
                                        result.responseData, result.responseLength);
    } else {
        response = CommandProtocol::createNACK(...);
    }
    sendResponse(response);
    hasCommand = false;
```

```cpp
// src/command_handler.cpp:209-211 — big-endian response data by hand (existing style to copy for lastImageId etc.)
    result.responseData[0] = (imageId >> 8) & 0xFF;
    result.responseData[1] = imageId & 0xFF;
```

Receive framing mirror at `src/command_handler.cpp:630-683` gets the same type-byte switch (its `expectedTotal = CMD_HEADER_SIZE + 5 + bodyLen + 4` at line 658 becomes one branch among several).

### `src/image_tx_manager.cpp` + `include/image_tx_manager.h` (NEW — balloon transfer queue/arbitration)

**Analog:** `src/auto_capture.cpp` (module skeleton, singleton, loop process) + `src/command_sender.cpp` (slot-table pattern).

```cpp
// src/auto_capture.cpp:8-15 — singleton accessor pattern (CONVENTIONS.md):
static AutoCapture autoCaptureInstance;
AutoCapture& AutoCap() { return autoCaptureInstance; }
// NEW: ImageTx() / ImageRx() in the same shape
```

```cpp
// src/auto_capture.cpp:86-106 — non-blocking loop pattern to copy for chunk pacing
// (one chunk per loop pass — RESEARCH Pattern 4):
void AutoCapture::process() {
    if (!enabled || camera == nullptr) return;
    if (millis() - lastCaptureTime >= intervalMs) {   // wraparound-safe millis() idiom
        lastCaptureTime = millis();                   // baseline BEFORE attempt (T-01-09)
        if (camera->captureImage()) { ... }
    }
}
```

```cpp
// src/command_sender.cpp:398-418 — fixed-slot table with find/evict; reuse shape for the
// FIFO transfer queue (depth 2-3, drop-oldest + logged warning — RESEARCH Pitfall 7):
TrackedCommand* CommandSender::findFreeSlot() {
    for (uint8_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (pendingCommands[i].state == CommandState::IDLE) return &pendingCommands[i];
    }
    ...
}
```

Header guard / comment-banner / debug-flag style: `include/auto_capture.h:1-17` (`#ifndef X_H`, banner comment, bounds constants re-validated in-module).

### `src/image_rx_manager.cpp` + `include/image_rx_manager.h` (NEW — base reassembly/window state machine)

**Analog:** `src/command_sender.cpp` receive path (`267-317`) + tracked-state discipline.

- Copy the framing accumulator (`inPacket`/`receiveIndex`/`resetReceiveState`, lines 267-317 and 454-458) with the new type dispatch.
- Window state machine mirrors `CommandState` vocabulary discipline: locked terminal states, counted transitions performed exactly once (duplicate guard at 336-338 is the model for "window-complete applied once").
- Progress snapshot API mirrors `getCommandQueue()` (`src/command_sender.cpp:492-512`): caller fills an array of plain structs, module returns count — feeds D-20 UI rows from real chunk-bitmap accounting only.

### `src/sd_storage.cpp` + `include/sd_storage.h` (NEW — base SD persistence)

No in-codebase analog. Use RESEARCH.md Code Examples (SD init with custom SPI pins, `SPIClass sdSPI(HSPI)` + `SD.begin(CS, sdSPI)`; write per-chunk via `file.seek(chunkIndex * chunkSize)` then write; sidecar written once at finalization with hand-built String JSON, matching `main_basestation.cpp` style). Degrade without SD exactly like the link-LED truth pattern: surface failure, never fabricate stored state.

### `src/camera_manager.cpp` (FIXED — CR-04/WR-11 entry task)

**Analog:** its own `captureImage()` success path; defect site read this session at `src/camera_manager.cpp:281-342`.

The two failure paths (lines 313 and 329) do `free(thumbnail.buffer);` with no `= nullptr` and no `thumbnail.valid` reset — the rework must: capture `fb` first, then allocate exactly `fb->len` (eliminating `estimateImageSize(FRAMESIZE_QQVGA, 15)` = 4000 B at line 291), null the member on every early return, and restore frame size/quality on every path (currently done correctly in both failure branches — keep that).

### `src/auto_capture.cpp` / `include/auto_capture.h` (EXTENDED — event triggers)

**Analog:** itself; add alongside `process()`:

- Keep `allocateImageId()` (`src/auto_capture.cpp:112-116`) the single ID authority — event triggers call it exactly as interval captures do (`src/auto_capture.cpp:98-99`).
- D-27: event capture calls the same `lastCaptureTime = millis();` reset (line 96).
- D-28: one minimum-spacing check gating BOTH interval and event paths, mirroring the bounds re-validation style at lines 53-56.
- Baseline/threshold fields follow the header's constants-with-comment style (`include/auto_capture.h:13-17`).
- Event evaluation reads `Sensors().getGPSData()` / `SysState().getFlightPhase()` the same way `main_balloon.cpp:639` feeds `setCurrentAltitude(gpsData.altitude)`.

### `src/main_balloon.cpp` (EXTENDED — loop wiring)

**Analog:** itself. Existing wiring points:

```cpp
// src/main_balloon.cpp:381 — begin() alongside CmdHandler:
    if (!CmdHandler().begin(&E32LoRaModule(), &Camera())) { ... }
// src/main_balloon.cpp:725 — process() in the loop:
    CmdHandler().process();
```

Add `ImageTx().begin/process` at the same sites; keep loop cadence (`MAIN_LOOP_INTERVAL_MS 100`, line 59). TX arbitration (PRI-01) is a small fixed-priority function called once per transmit opportunity — do NOT resurrect `lora_comm.cpp`/`LoRaManager` (excluded from builds, `platformio.ini:75-76, 267`). `processCommunications()` (lines 654-684) is dead/commented — the telemetry beacon decision (Open Question Q1) revives or replaces it.

### `src/main_basestation.cpp` (EXTENDED — SD init, UI, image routes)

**Analog:** itself — three patterns:

```cpp
// src/main_basestation.cpp:917-951 — settings-command handler pattern (D-26 threshold forms copy this):
void handleAutoCaptureEnable() {
    if (!server.hasArg("interval")) { sendResponse(400, "Error", "Missing interval parameter"); return; }
    long intervalSec = server.arg("interval").toInt();
    if (intervalSec < 1 || intervalSec > 3600) { sendResponse(400, ...); return; }  // range-check full long (WR-07)
    uint8_t payload[4];
    CommandProtocol::writeUint32(payload, intervalMs);  // big-endian on wire (Pitfall 9)
    uint16_t seq = CmdSender().sendCommand(CameraCommand::AUTO_CAPTURE_ENABLE, payload, 4);
    if (seq > 0) { appState.lastCommandSequence = seq; ... sendResponse(200, "OK", ...); }
    else { sendResponse(500, "Error", ...); }
}
```

```cpp
// src/main_basestation.cpp:972-987 — LOCKED status vocabulary: one state-to-string mapping shared
// by pinned row and queue rows (SC-4). Transfer-progress states need the same single mapping (D-20).
```

```cpp
// src/main_basestation.cpp:1008-1049 — hand-built String JSON in handleStatus() + computed-truth LED
// (IN-03: connected is COMPUTED from ack activity, never hardcoded). Progress rows and the
// completeness flag must be computed from real chunk accounting the same way (no-fabricated-state).
```

SD init goes beside `initLoRa()` (call site `src/main_basestation.cpp:442-444`), with `LoRaSerial.setRxBufferSize(1024)` inserted before `E32LoRaModule().begin(...)` (RESEARCH Pitfall 2).

### `scripts/verify_protocol_roundtrip.mjs` (EXTENDED)

Add manifest/chunk wire-format round-trip clauses alongside the existing 15 command/response clauses; include type-byte-rejection cases (WR-12).

## Shared Patterns

### Singleton module skeleton
**Source:** `src/auto_capture.cpp:8-15` (also `command_sender.cpp:12-15`)
**Apply to:** `image_tx_manager.cpp`, `image_rx_manager.cpp`, `sd_storage.cpp`
```cpp
static ImageTxManager imageTxInstance;
ImageTxManager& ImageTx() { return imageTxInstance; }
```

### Non-blocking loop processing + wraparound-safe millis()
**Source:** `src/auto_capture.cpp:86-106`
**Apply to:** TX chunk pacing, RX window state machine, event triggers. Pattern: guard clauses, `millis() - lastX >= interval`, baseline updated before the action.

### Length-driven framing + bounds rejection
**Source:** `src/command_handler.cpp:644-683` (mirror `src/command_sender.cpp:281-317`)
**Apply to:** every new receive path on both firmwares. End markers tested only at `CMD_HEADER_SIZE + <type overhead> + bodyLen + 4`; bogus lengths reset receive state; unknown type bytes reset (WR-12).

### Terminal-state / duplicate-transition guard
**Source:** `src/command_sender.cpp:336-338` (+ cancelCommand count discipline 148-168)
**Apply to:** window-complete handling, chunk ACK accounting, transfer-slot freeing. Counted state transitions happen exactly once.

### Big-endian wire encoding via CommandProtocol helpers
**Source:** `src/command_protocol.cpp:325-347`; usage example `src/main_basestation.cpp:935-936`
**Apply to:** every multi-byte field in manifest/chunk/window payloads and threshold commands. Never `memcpy` native structs to the wire (Pitfall 9). GET_STATUS struct stays a documented little-endian island — do not extend it.

### Debug banner + gated Serial logging
**Source:** every module (`src/command_sender.cpp:3-6` `#ifndef DEBUG_X ... #define DEBUG_X true`, `Serial.printf` at transitions)
**Apply to:** all three new modules.

### Build-filter discipline
**Source:** `platformio.ini:277` (`-<auto_capture.cpp>` in the base env's `build_src_filter`)
**Apply to:** `image_tx_manager.cpp` (exclude from base env), `image_rx_manager.cpp` + `sd_storage.cpp` (exclude from balloon env).

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `src/sd_storage.cpp` / `include/sd_storage.h` | service | file-I/O | No SD or file-write code exists anywhere in the codebase (0 hits for `SD.` in main_basestation.cpp). Planner should use RESEARCH.md Code Examples (SD init, seek+offset streaming write, sidecar at finalization) and the singleton/loop/header conventions from `auto_capture`. SD pins unestablished (Open Question Q2) — code against config constants with a human-verify checkpoint. |

## Metadata

**Analog search scope:** `include/`, `src/` (command_protocol, command_sender, command_handler, auto_capture, camera_manager, main_balloon, main_basestation, stubs), `platformio.ini`, `scripts/`
**Files read:** 9 source files (targeted ranges)
**Pattern extraction date:** 2026-08-19

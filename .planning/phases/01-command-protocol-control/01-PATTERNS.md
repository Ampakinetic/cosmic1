# Phase 1: Command Protocol & Control - Gap Closure Pattern Map

**Mapped:** 2026-08-18
**Files analyzed:** 7 (6 modified, 1 new module pair)
**Analogs found:** 7 / 7

This is gap-closure planning. The phase's own delivered modules are the primary analogs for each other; `camera_manager.cpp` and `app_httpd.cpp` provide the sensor-access pattern for the three placeholder handlers. All excerpts below were read from current working-tree code and line numbers reflect the unfixed state (they will shift as fixes land — anchor by content, not line).

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `include/command_protocol.h` | protocol/headers | transform (serialize/deserialize) | itself + `common_types.h` constants | exact (in-place fix) |
| `src/command_protocol.cpp` | protocol | transform | `serializeResponse()` in same file (CRC-correct) | exact (in-place fix) |
| `src/command_sender.cpp` | service (retry state machine) | request-response / event-driven | `command_handler.cpp` receive path; own `handleResponse`/`retryCommand` | exact (in-place fix) |
| `src/command_handler.cpp` | service (command dispatch) | event-driven | own `handleSetContrast` (real handler) vs placeholder handlers | exact (in-place fix) |
| `src/main_basestation.cpp` | controller (HTTP + UI) | request-response | own `handleSetQuality` route + settings forms | exact (in-place extension) |
| `include/auto_capture.h` + `src/auto_capture.cpp` (NEW) | service (interval timer) | event-driven (periodic) | `CameraManager::isTimeToCapture()` timing pattern + Manager singleton pattern | role-match |
| `src/main_balloon.cpp` | controller (main loop) | event-driven | own `CmdHandler()` integration (lines 378-391, 758) | exact (small addition) |
| `src/camera_manager.h/.cpp` (implied by gap 5) | service (sensor settings) | request-response | own `setContrast()` (lines 404-421) | exact (add 3 methods) |

## Pattern Assignments

### `src/command_protocol.cpp` (CRC-order fix, payload rejection)

**Analog:** `CommandProtocol::serializeResponse()` in the same file — it already does it right.

Correct pattern to copy (response serializer, lines 157-164): the real refSequence byte is written into `buffer[3]` **before** the CRC is computed at line 179. No post-hoc patch exists.

Defect to fix in `serializeCommand()` (lines 56-63, 79, 87-88): `buffer[3] = 0x00` placeholder, CRC computed over it at line 79, then patched at line 88. Fix:
```cpp
// line 60 becomes:
buffer[offset++] = static_cast<uint8_t>(cmd.sequenceNumber & 0xFF);
// delete lines 87-88 (the post-serialization patch)
```

Payload-size rejection (CR-02): the check at lines 50-54 uses a literal `240`; replace with shared constant and also reject `cmd.payloadLength > CMD_MAX_PACKET_SIZE - 16` explicitly (createCommandPacket currently truncates to 200, sender buffer must be sized to CMD_MAX_PACKET_SIZE anyway):

```cpp
if (packetLength > CMD_MAX_PACKET_SIZE || cmd.payloadLength > CMD_MAX_PACKET_SIZE - 16) {
    return false;
}
```

### `include/command_protocol.h` (shared constant)

Add next to the existing limits block (lines 175-180):
```cpp
static constexpr size_t CMD_MAX_PACKET_SIZE = 240; // LoRa packet limit (shared by sender/handler buffers)
```
Keep `static constexpr` file-scope style used by `CMD_MAX_PAYLOAD_SIZE` / `CMD_HEADER_SIZE` — do not introduce a class member.

### `src/command_sender.cpp` (framing, retry state machine, buffer sizing)

**Analog:** same file; plus `command_handler.cpp` framing twin.

**Buffer sizing (CR-02):** `transmitCommand()` line 337 `uint8_t buffer[128];` → `uint8_t buffer[CMD_MAX_PACKET_SIZE];`. Note the receiver already uses `uint8_t receiveBuffer[128]` (command_sender.h line 101) — resize both to `CMD_MAX_PACKET_SIZE`. CommandHandler's `receiveBuffer[256]` (command_handler.h line 65) is already adequate.

**Length-aware framing (CR-03):** replace the end-marker check in `processIncomingByte()` (lines 224-235) with length-driven framing. Once `inPacket && receiveIndex >= CMD_HEADER_SIZE`, the header's bytes 4-5 (big-endian `writeUint16` layout, serializeResponse lines 162-163) carry the body length. Expected total for responses: `CMD_HEADER_SIZE + 4 + dataLength + 4`:

```cpp
// after writing each byte, when receiveIndex >= CMD_HEADER_SIZE:
size_t bodyLen = (static_cast<size_t>(receiveBuffer[4]) << 8) | receiveBuffer[5];
size_t expected = CMD_HEADER_SIZE + 4 + bodyLen + 4;
if (expected > sizeof(receiveBuffer)) { resetReceiveState(); return; }
if (receiveIndex >= expected) {
    // verify end marker at receiveBuffer[expected-2..expected-1], then
    // validatePacket + deserializeResponse + handleResponse
}
```

**Retry state machine (CR-04 / WR-03 / WR-04):** the SENT-timeout branch (lines 176-193) is the model for the missing PENDING-failure terminal path. Mirror its structure at lines 163-173:

```cpp
if (cmd->state == CommandState::PENDING) {
    if (transmitCommand(cmd)) {
        cmd->state = CommandState::SENT;
        cmd->sendTime = currentTime;
        commandsSent++;
    } else {
        cmd->retryCount++;
        cmd->lastRetryTime = currentTime;
        if (cmd->retryCount >= maxRetries) {
            cmd->state = CommandState::FAILED;
            pendingCommandCount--;
            commandsFailed++;   // terminal state, mirrors TIMEOUT branch
        }
        // on non-final failure stay PENDING; retry-delay check below paces it
    }
}
```

Retry pacing (WR-04): move the existing dead-code check (lines 196-200) **above** the timeout/retry branches and make it skip the whole body (change `continue` placement so it guards transmit/retry), consulting `lastRetryTime` in the timeout branch before calling `retryCommand()`. Also update `sendTime`/`lastRetryTime` on both success and failure inside `retryCommand()` (lines 350-362 currently only set them on success).

`cancelCommand()` guard (WR-03, lines 119-134): only decrement when leaving a counted state:
```cpp
if (cmd->state == CommandState::PENDING || cmd->state == CommandState::SENT) {
    pendingCommandCount--;
}
cmd->state = CommandState::FAILED;
```

### `src/command_handler.cpp` (framing twin, real handlers, auto-capture wiring)

**Analog for framing:** the fix above, applied symmetrically to `processIncomingByte()` (lines 599-634; end-marker check at 614-627). For commands the expected total is `CMD_HEADER_SIZE + 5 + payloadLength + 4` using the payloadLength field at header offset 4-5.

**Analog for real handlers:** `handleSetContrast()` (lines 348-387) is the canonical structure — payloadLength check → range check → NACK_PARAM with `strncpy(result.message, ...)` → CameraManager call → ACK + responseData echo → `commandsExecuted++`. Rewrite `handleSetSaturation` (389-421), `handleSetExposure` (423-455), `handleSetWBMode` (457-489) by deleting the "For now, just acknowledge" blocks and inserting the same `if (camera->setX(...)) { ... } else { NACK_BUSY }` shape.

**Sensor access for the 3 new setters:** CameraManager does not expose saturation/exposure/WB. Copy the exact getter pattern from `camera_manager.cpp` `setContrast()` (lines 404-421) — guard `initialized`, `esp_camera_sensor_get()`, call sensor op, cache current value. The sensor ops to use (proven at camera_manager.cpp lines 109-113 and app_httpd.cpp 352-389):

```cpp
s->set_saturation(s, saturation);       // app_httpd.cpp:353
s->set_ae_level(s, exposureLevel);      // exposure level; see camera_manager.cpp:112
s->set_wb_mode(s, wbMode);              // camera_manager.cpp:111 / app_httpd.cpp:389
```
Add `setSaturation/setExposure/setWBMode` declarations to `src/camera_manager.h` next to `setContrast` (line 101) plus cached-current-value getters for GET_STATUS.

**Auto-capture handlers (491-539):** keep the existing interval validation (1000-3600000 ms, lines 494-508), then replace the placeholder ACK blocks with calls into the new AutoCapture module (see below). `handleGetStatus` (541-564): replace the hardcoded `status.imageId = 0; // TODO` block (545-551) with values from tracked state + `AutoCapture().isEnabled()/getInterval()` + camera getters.

### `include/auto_capture.h` + `src/auto_capture.cpp` (NEW — interval timer)

**Structure analog:** `command_sender.h/.cpp` Manager pattern — include guard, `class AutoCapture`, `begin()/process()`, static instance + global accessor (command_sender.cpp lines 12-15):

```cpp
static AutoCapture autoCaptureInstance;
AutoCapture& AutoCap() { return autoCaptureInstance; }
```

**Timing analog:** `CameraManager::isTimeToCapture()` (camera_manager.cpp lines 434-439) — the project's established millis-interval idiom:
```cpp
if (lastCaptureTime == 0) return true;              // first capture
return (millis() - lastCaptureTime) >= intervalMs;  // wraparound-safe
```
Build `process()` around it:
```cpp
void AutoCapture::process() {
    if (!enabled) return;
    if (millis() - lastCaptureTime >= intervalMs) {
        lastCaptureTime = millis();
        if (onIntervalElapsed) onIntervalElapsed(); // callback → camera->captureImage()
    }
}
```
API surface to satisfy the handlers and GET_STATUS: `enable(uint32_t intervalMs)`, `disable()`, `isEnabled() const`, `getInterval() const`, `process()`, plus an optional capture callback or a `CameraManager*` in `begin()` (prefer the pointer style used by `CommandHandler::begin(E32LoRa*, CameraManager*)`, command_handler.cpp lines 42-59).

**Loop integration analog:** `main_balloon.cpp` lines 378-391 (begin in setup) and line 758 (`CmdHandler().process();` in loop) — add `AutoCap().begin(&Camera());` and `AutoCap().process();` in the same style, exactly as sketched in CONTEXT.md lines 106-116.

### `src/main_basestation.cpp` (4 new settings routes, auto-capture UI, per-command outcome)

**Route registration analog:** `initWebServer()` lines 391-397 — add `/set-resolution`, `/set-saturation`, `/set-exposure`, `/set-wb`, `/auto-capture`, `/auto-capture-stop` POST routes.

**Handler analog:** `handleSetQuality()` (lines 521-544) is the template for all new handlers — `server.hasArg` check → `toInt()` → range check → `sendResponse(400, "Error", ...)` → `CmdSender().sendCommand(CameraCommand::SET_X, &value, sizeof)` → 200/500 JSON. Payload encoding for multi-byte values uses `CommandProtocol::writeUint32(result.responseData, intervalMs)` (command_handler.cpp line 514) / `readUint32(cmd.payload)` (line 501) as the byte-order convention.

WR-07 caution when copying: `server.arg("quality").toInt()` truncates to int before the uint8_t cast (line 527). For new handlers, range-check the `long` from `toInt()` before narrowing:
```cpp
long v = server.arg("saturation").toInt();
if (v < -2 || v > 2) { sendResponse(400, ...); return; }
int8_t saturation = static_cast<int8_t>(v);
```

**Form analog:** the quality form (lines 468-474) — `String html += "<form action=\"/set-saturation\" method=\"POST\">..."` with `<hr style="border-color: #475569; margin: 20px 0;\">` separators between forms (line 476). Auto-capture card mirrors the capture card (456-461) with an interval number input (min 1000, max 3600000).

**Per-command outcome surfacing:** the footer script (lines 268-286) already polls `/status` at 1 Hz via `fetch('/status')`. Extend `handleStatus()` (lines 596-606) with a per-command field using the existing-but-unused `CmdSender().getCommandState(seq)` (command_sender.cpp lines 382-390, declared at command_sender.h line 60), e.g. track `appState.lastCommandSequence` (already set in `handleCapture`, lines 509-510) and emit `"lastState":"ACKED"|"TIMEOUT"|"FAILED"|"SENT"|"PENDING"`; the script maps it to the existing `.message` CSS classes (success/error/info, lines 230-245).

---

## Shared Patterns

### Manager singleton + global accessor
**Source:** `src/command_sender.cpp` lines 12-15, `src/command_handler.cpp` lines 12-15
**Apply to:** new `auto_capture.cpp` and any new code touching subsystems.
```cpp
static CommandSender commandSenderInstance;
CommandSender& CmdSender() { return commandSenderInstance; }
```

### millis() interval timing (wraparound-safe)
**Source:** `src/camera_manager.cpp` lines 434-439 (`isTimeToCapture`), `src/main_basestation.cpp` lines 415-426 (`updateStatus`)
**Apply to:** auto-capture timer, retry pacing. Idiom: `if (millis() - lastTime >= intervalMs) { lastTime = millis(); ... }` — never absolute-time comparisons.

### CommandResult / NACK error convention
**Source:** `src/command_handler.cpp` lines 224-264 (`handleSetResolution`)
**Apply to:** all rewritten handlers. `result.responseType = ResponseType::NACK_PARAM/NACK_BUSY; strncpy(result.message, "...", sizeof(result.message)-1); commandsFailed++;` on failure; `result.success = true; result.responseType = ResponseType::ACK; result.responseData[0] = value; commandsExecuted++;` on success.

### CameraManager setter pattern
**Source:** `src/camera_manager.cpp` lines 404-421 (`setContrast`)
**Apply to:** new `setSaturation`/`setExposure`/`setWBMode`. Guard `initialized` → `esp_camera_sensor_get()` → sensor op → cache `currentX` → return bool.

### HTTP handler convention (base station)
**Source:** `src/main_basestation.cpp` lines 521-544 + `sendResponse()` lines 616-627
**Apply to:** all new routes. JSON via `sendResponse(code, status, message)`; sequence returned by `sendCommand()` is captured in `appState.lastCommandSequence`.

### Framing state machine (both endpoints)
**Source:** `src/command_sender.cpp` lines 208-241 and `src/command_handler.cpp` lines 599-634 (identical twins)
**Apply to:** both files must receive the **same** length-driven fix (commands: header + 5 + payloadLength + 4; responses: header + 4 + dataLength + 4). Any fix applied to only one side recreates the CR-03 bug on the other.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | | | All gap artifacts have strong in-codebase analogs; no external patterns required |

Note on scope: hardware-dependent items from 01-VERIFICATION.md (WR-01 blocking transmit, WR-02 E32 config API, IN-03 hardcoded `connected`) are warnings, not gap artifacts — out of scope for this closure unless the planner chooses to fold them in.

## Metadata

**Analog search scope:** `src/`, `include/` (project root); confirmed `camera_manager.h` lives in `src/` not `include/`
**Files scanned:** command_protocol.h/.cpp, command_sender.h/.cpp, command_handler.h/.cpp, main_basestation.cpp, main_balloon.cpp, camera_manager.h/.cpp, app_httpd.cpp (grep)
**Pattern extraction date:** 2026-08-18

---
phase: 01-command-protocol-control
reviewed: 2026-08-17T23:54:09Z
depth: standard
files_reviewed: 12
files_reviewed_list:
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - platformio.ini
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
findings:
  critical: 4
  warning: 9
  info: 8
  total: 21
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-17T23:54:09Z
**Depth:** standard
**Files Reviewed:** 12
**Status:** issues_found

## Summary

Reviewed the Phase 1 command-protocol stack: CRC16-framed protocol (command_protocol), E32 UART driver (e32_lora), base-station retry sender (command_sender), balloon command handler (command_handler), both firmware entry points, and build config. Cross-referenced camera_manager.h, camera_pins.h, sensor_pins.h, board_config.h, and stubs.cpp for API and pin-contract verification.

The core finding: **the command link as written cannot work end-to-end.** The serializer computes CRC before patching the sequence byte into the header (CR-01), so every command with a nonzero sequence fails CRC on the balloon and is silently dropped — all commands, all retries, forever. Three further blockers: a latent stack buffer overflow in the sender path (CR-02), non-transparent framing that deterministically drops any packet whose payload contains `0D 0A` (CR-03 — triggered by plausible real values such as UXGA resolution + default quality in status data), and an unbounded PENDING retry path that wedges the base-station event loop when the radio is unreachable (CR-04). The E32 driver's transmit is also blocking for up to 7 s inside `loop()`, violating the project's no-blocking constraint. The E32 configuration API contradicts the E32 datasheet (no `0xC0` command prefix, wrong address layout) — unused today but broken on arrival for hardware bring-up.

Accepted gaps (auto-capture placeholder, 3-of-7 settings in web UI, hardware tests pending) were not reported as findings.

## Critical Issues

### CR-01: CRC16 computed before sequence number is patched into header — every command fails validation

**File:** `src/command_protocol.cpp:60-88`
**Issue:** `serializeCommand()` writes `0x00` as a "sequence placeholder" into `buffer[3]` (line 60), computes CRC16 over the buffer including that placeholder (line 79), and only afterwards overwrites `buffer[3]` with `cmd.sequenceNumber & 0xFF` (line 88). The receiver (`validateCRC`, line 24-38) recomputes CRC over the transmitted bytes, which now contain the real sequence byte. For any sequence where `(seq & 0xFF) != 0` the CRCs mismatch, `CommandHandler::validatePacket()` rejects the packet, and the command is silently dropped. Since `nextSequenceNumber` starts at 1 (`command_sender.cpp:24`), **every command issued by the base station is discarded**, exhausts its retries, and reports TIMEOUT. The Phase 1 link is non-functional.
**Fix:** Write the real sequence byte before the CRC and delete the post-hoc patch:
```cpp
// header
buffer[offset++] = CMD_START_BYTE1;
buffer[offset++] = CMD_START_BYTE2;
buffer[offset++] = static_cast<uint8_t>(PACKET_TYPE_COMMAND);
buffer[offset++] = static_cast<uint8_t>(cmd.sequenceNumber & 0xFF); // real value, no placeholder
writeUint16(buffer + offset, cmd.payloadLength);
offset += 2;
buffer[offset++] = 0x00; // CRC8 unused
...
uint16_t crc16 = calculateCRC16(buffer, offset); // now covers the real sequence byte
writeUint16(buffer + offset, crc16);
// DELETE line 88: buffer[3] = static_cast<uint8_t>(cmd.sequenceNumber & 0xFF);
```

### CR-02: Stack buffer overflow — sender's 128-byte buffer vs serializer's 216-byte maximum output

**File:** `src/command_sender.cpp:337` (with `src/command_protocol.cpp:50-54,73-75`)
**Issue:** `transmitCommand()` serializes into `uint8_t buffer[128]`, but `serializeCommand()` permits packet lengths up to 240 and payload lengths up to `CMD_MAX_PAYLOAD_SIZE` (200). A command with payload ≥ 113 bytes produces 16 + payloadLength ≥ 129 bytes written into the 128-byte stack buffer — an 88-byte overflow at the maximum. `sendCommand()` is a public API that accepts arbitrary `payloadSize` (`createCommandPacket` truncates to 200, not to 112), so any future caller (image-ID lists, batch settings, protocol extension) silently corrupts the stack of the base-station loop task. Note `deserializeCommand`'s bounds checks do not protect the send path.
**Fix:** Size the buffer to the protocol maximum and make the limit a shared constant:
```cpp
// command_protocol.h
static constexpr size_t CMD_MAX_PACKET_SIZE = 240;

// command_sender.cpp
uint8_t buffer[CMD_MAX_PACKET_SIZE];
size_t length = 0;
```
Additionally, reject (return `false`) rather than silently skip when `cmd.payloadLength > CMD_MAX_PAYLOAD_SIZE` inside `serializeCommand`.

### CR-03: Framing is not byte-transparent — payload containing `0D 0A` truncates the packet and guarantees CRC failure

**File:** `src/command_handler.cpp:615-627`, `src/command_sender.cpp:224-235`
**Issue:** Both receivers terminate a packet the moment the last two buffered bytes equal `0x0D 0x0A`, with no notion of expected length. If that pair occurs **inside** the payload/data region, the packet is cut short, `validateCRC` fails, the receive state resets, and the remainder of the real packet (including its true terminator) is discarded as garbage. This is deterministic, not noise-dependent. Concrete triggers with real values:
- `AUTO_CAPTURE_ENABLE` with interval 0x000D0Axx (855040–855295 ms, inside the accepted 1000–3600000 range): payload bytes `00 0D 0A xx` cut the packet — the command can never succeed (`command_handler.cpp:501`).
- `ResponseStatusData` bytes: `currentResolution = FRAMESIZE_UXGA (0x0D)` followed by `currentQuality = 10 (0x0A)` produce `0D 0A` mid-packet (`command_protocol.cpp:160-169`) — the balloon's ACK for a successful UXGA+Q10 status/set is always dropped, so the base station retries a command that already executed (duplicate captures, false TIMEOUTs).

With CR-01 fixed, this becomes the next link-killing bug.
**Fix:** Switch to length-driven framing. The header already carries `payloadLength`/`dataLength`: after the start bytes and header, accumulate exactly `headerLen + payloadLength + 4` bytes (CRC16 + end marker), then validate. Alternatively escape `0x0D`/`0x0A` in payload on send and unescape on receive. Length-driven parsing is simpler given both sides know the format:
```cpp
// after start bytes, wait until CMD_HEADER_SIZE bytes buffered, then:
uint16_t payloadLen = CommandProtocol::readUint16(receiveBuffer + 5);
size_t expected = CMD_HEADER_SIZE + 5 + payloadLen + 4; // command packets
// only test end-marker / CRC once receiveIndex == expected
```

### CR-04: PENDING commands never time out on transmit failure — unbounded retry storm blocks the event loop and exhausts slots

**File:** `src/command_sender.cpp:163-173`
**Issue:** In `process()`, a `PENDING` command that fails to transmit (e.g., E32 absent or AUX stuck low — `transmit()` returns false after ~1 s of AUX polling) stays `PENDING`. `retryCount` is only incremented in `retryCommand()`, which is reachable only from the `SENT` timeout branch. So a PENDING command retries **every `process()` call forever**, each attempt blocking ~1 s in `E32LoRa::transmit()`'s `waitForAuxHigh(1000)`. Consequences: the base-station `loop()` (`main_basestation.cpp:322-341`) spends nearly all its time inside `CmdSender().process()`, starving `server.handleClient()`; five such commands fill all `MAX_PENDING_COMMANDS` slots permanently; no TIMEOUT/FAILED state is ever reached and no statistics reflect the failure. With the radio disconnected the web UI becomes effectively unusable.
**Fix:** Apply retry accounting to the initial-send failure path:
```cpp
if (cmd->state == CommandState::PENDING) {
    if (transmitCommand(cmd)) {
        cmd->state = CommandState::SENT;
        cmd->sendTime = currentTime;
        commandsSent++;
    } else {
        cmd->retryCount++;
        cmd->lastRetryTime = currentTime;
        if (cmd->retryCount > maxRetries) {
            cmd->state = CommandState::FAILED;
            pendingCommandCount--;
            commandsFailed++;
        }
    }
}
```

## Warnings

### WR-01: E32 transmit blocks up to 7 seconds inside the main loop; AUX-low wait can also false-fail short packets

**File:** `src/e32_lora.cpp:158-202`
**Issue:** `transmit()` performs `waitForAuxHigh(1000)` + `waitForAuxLow(1000)` + `waitForAuxHigh(5000)` — up to 7 s of `delay(10)` polling inside the single-threaded event loop, violating the project constraint "Single-threaded event loop (no blocking)". On the balloon this stalls telemetry and the watchdog feed (`main_balloon.cpp:241-244`) during every command response; on the base station it freezes the web server during every send/retry. Additionally, `serial->flush()` returns after the UART drains, by which time a short packet may already have completed its air time and AUX may be back high — `waitForAuxLow` then times out and a **successful** transmission is reported as failure (counted in `transmitErrors`, drives CR-04's retry storm and false NACK behavior).
**Fix:** Convert to a non-blocking AUX state machine (sample `digitalRead(auxPin)` in `process()`), or at minimum: check AUX high once before write, drop the `waitForAuxLow` step, and cap the post-write wait at an air-time-derived bound. Do not treat "AUX never observed low" as failure.

### WR-02: E32 configuration API contradicts the E32-900T30D datasheet — will fail on hardware bring-up

**File:** `src/e32_lora.cpp:204-222, 264-344, 425-443`
**Issue:** Multiple datasheet violations in the (currently uncalled) config path:
- `writeConfig()` (line 290) sends 6 raw bytes with **no `0xC0` command prefix**. The E32 config-write frame is `C0 ADDH ADDL SPED CHAN OPTION`. It also encodes the address as 4 bytes (`addressHigh`/`addressLow` are `uint16_t` each) when the module address is `ADDH`+`ADDL` (1 byte each), and omits channel and option bytes. Any call writes garbage to the module's register 0.
- `setParameters()` (line 316) initializes `E32Config` partially (channel, option, etc. uninitialized) and hardcodes `addressHigh=0x0000, addressLow=0xFFFF`, which through the broken `writeConfig` layout becomes `SPED=0xFF, CHAN=0xFF`.
- `readConfig()` (line 264) sends the correct `C1 C1 C1` but never parses the 6-byte reply into `config` — returns `true` with an untouched out-parameter (misleading API contract).
- `transmitToAddress()` (line 204) prepends a 4-byte address prefix; E32 fixed transmission uses a 3-byte prefix (`ADDH ADDL CHAN`). Also uses a stack VLA `uint8_t buffer[length + 4]` with no length bound.
- `enterConfigMode()` (line 425) saves `previousMode` but never restores it — `exitConfigMode()` always returns to MODE_NORMAL regardless of prior state (dead variable, wrong restore).
**Fix:** Implement frames per datasheet: write `{0xC0, addh, addl, sped, chan, option}` in MODE_SLEEP; read reply `{0xC1, addh, addl, sped, chan, option}` and populate the struct; use a 3-byte fixed-TX prefix; restore `previousMode` on exit; replace the VLA with a bounded stack buffer or `std::array`.

### WR-03: cancelCommand double-decrements pendingCommandCount for already-completed commands

**File:** `src/command_sender.cpp:119-134`
**Issue:** `findTrackedCommand()` matches any non-IDLE slot, including ACKED/FAILED/TIMEOUT entries that were already decremented when they completed. Calling `cancelCommand()` on such a slot decrements `pendingCommandCount` a second time; as a `uint8_t` it underflows to 255, making `hasPendingCommands()` permanently true and corrupting status reporting (and `printStatus`) until reboot.
**Fix:**
```cpp
TrackedCommand* cmd = findTrackedCommand(sequenceNumber);
if (!cmd || cmd->state == CommandState::ACKED ||
    cmd->state == CommandState::FAILED || cmd->state == CommandState::TIMEOUT) {
    return false; // nothing active to cancel
}
cmd->state = CommandState::FAILED;
pendingCommandCount--;
```

### WR-04: Retry-delay logic is dead code; failed retries re-fire immediately with no pacing

**File:** `src/command_sender.cpp:196-200, 350-362`
**Issue:** The "Check for retry delay" block (lines 196-200) is a no-op — its only action, `continue`, is the last statement of the loop body, and the timeout branch above it (line 176) runs first without consulting `lastRetryTime`. Consequently `retryDelayMs` is never enforced. Worse, `retryCommand()` only updates `sendTime` when the transmit **succeeds**; when it fails, `sendTime` stays stale, so the very next `process()` pass sees the timeout condition still true and retries again immediately. With each failed attempt blocking ~1 s in the driver, a bad link burns through all retries in rapid succession and races past the intended pacing.
**Fix:** Honor `lastRetryTime` in the timeout branch before retrying (`if (currentTime - cmd->lastRetryTime < retryDelayMs) continue;` placed before the retry decision), and update `sendTime`/`lastRetryTime` on both success and failure of the retry transmit.

### WR-05: STATUS response type is destroyed end-to-end; sender would treat a real STATUS as failure

**File:** `src/command_handler.cpp:93-106`, `src/command_sender.cpp:259-278`
**Issue:** Two compounding defects: (1) `CommandHandler::process()` wraps every successful result in `createACK(...)`, discarding `result.responseType` — `handleGetStatus()` carefully sets `ResponseType::STATUS` (line 554) but it goes on the wire as ACK, so the protocol's STATUS type can never be observed. (2) `CommandSender::handleResponse()` treats every non-ACK response (including STATUS) as FAILED. Today GET_STATUS "works" only because of defect (1) masking defect (2); fixing either side alone breaks status queries, and no future status/diagnostics extension can use the typed path.
**Fix:** Send the actual result type: `response = createResponsePacket(result.responseType, seq, result.responseData, result.responseLength);` and in `handleResponse()`, treat `ACK` and `STATUS` as success (STATUS = success-with-payload), NACK variants as failure.

### WR-06: Start-byte resync flaw — a doubled `0xAA` loses the packet start

**File:** `src/command_handler.cpp:600-609`, `src/command_sender.cpp:209-218`
**Issue:** When hunting for the start sequence with `receiveIndex == 1` (already saw `0xAA`), any byte other than `0x55` — including another `0xAA` — falls into the `else` branch that resets `receiveIndex = 0` and **discards the current byte**. A stream `AA AA 55 ...` (e.g., noise byte before a genuine preamble, or misaligned stream after a dropped packet) loses the real start: the second `AA` is consumed, the following `55` no longer matches at index 0, and the packet is missed entirely.
**Fix:** Re-anchor instead of discarding:
```cpp
} else {
    receiveIndex = (byte == CMD_START_BYTE1) ? 1 : 0;
}
```

### WR-07: Web handlers validate after integer truncation — crafted POST values bypass range checks

**File:** `src/main_basestation.cpp:527-531, 552-554, 577-579`
**Issue:** `handleSetQuality()` does `uint8_t quality = server.arg("quality").toInt();` and then checks `quality > 63`. `toInt()` returns `long`; the cast happens first. A crafted POST of `quality=300` truncates to 44 and passes; `quality=256` becomes 0 (best-quality JPEG — the exact opposite of intent). Same pattern in brightness/contrast: `brightness=258` truncates to 2 and is accepted. The HTML `max` attributes constrain only the form UI, not the endpoint. On an unauthenticated AP-served control panel, this is the input-validation backdoor for the command path.
**Fix:** Validate in the wide type before narrowing:
```cpp
long q = server.arg("quality").toInt();
if (q < 0 || q > 63) { sendResponse(400, "Error", "Invalid quality value (0-63)"); return; }
uint8_t quality = static_cast<uint8_t>(q);
```

### WR-08: A second command received before the first is processed overwrites it silently

**File:** `src/command_handler.cpp:619-627, 82`
**Issue:** The handler keeps exactly one `PendingCommand`. `processIncomingByte()` sets `hasCommand = true` unconditionally on a valid packet; if a command is already pending (hasCommand true, not yet executed in this loop pass), the new packet overwrites `pendingCommand.packet` and the first command vanishes — no execution, no NACK, no statistics. The base station will retry it, but balloon-side behavior under quick successive commands (e.g., user clicking two controls) is lose-commands-silently.
**Fix:** When `hasCommand` is already true, either queue a second slot or reply NACK_BUSY for the new sequence instead of overwriting:
```cpp
if (!hasCommand) {
    pendingCommand.packet = cmd;
    pendingCommand.receivedTime = millis();
    hasCommand = true;
} else {
    // reject with NACK_BUSY for cmd.sequenceNumber
}
```

### WR-09: Deserializers never validate the packet type byte; serializeResponse length handling is asymmetric

**File:** `src/command_protocol.cpp:113-114, 151-176, 210-217`
**Issue:** `deserializeCommand()` accepts any `buffer[2]` (never checked against `PACKET_TYPE_COMMAND` 0x10) and `deserializeResponse()` likewise ignores it (never checked against 0x11). A response replayed into a command parser — or any future second protocol on the same link — passes all checks. Related asymmetry: `serializeResponse()` writes `resp.dataLength` as 16-bit into the header (line 162) but only 8-bit in the body (line 170), and when `dataLength > CMD_MAX_RESPONSE_DATA` it silently skips the payload while the header still advertises the larger length — the receiver then zeroes the length instead of the sender rejecting the packet.
**Fix:** In `deserializeCommand`, require `static_cast<PacketType>(buffer[2]) == PACKET_TYPE_COMMAND` (and `== 0x11` in `deserializeResponse`). In `serializeResponse()`, return `false` when `resp.dataLength > CMD_MAX_RESPONSE_DATA`, and keep header/body length fields the same width.

## Info

### IN-01: Hardcoded E32 pin literals duplicate sensor_pins.h macros

**File:** `src/main_balloon.cpp:380`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` hardcodes values that already exist as `LORA_RX_PIN/LORA_TX_PIN/LORA_M0_PIN/LORA_M1_PIN/LORA_AUX_PIN` in the included `sensor_pins.h`. Values currently match; any pin change in the header silently diverges from the runtime wiring.
**Fix:** Use the macros (and a `LORA_BAUD_RATE` define, which `sensor_pins.h` lacks — add one).

### IN-02: Hardcoded WiFi credential and unauthenticated control endpoints

**File:** `src/main_basestation.cpp:38-41, 391-397`
**Issue:** AP SSID/password are hardcoded (`"balloontrack"`), and all control endpoints (`/capture`, `/set-*`) accept commands with no authentication. Defensible for a field device on its own AP, but anyone with the (source-published) password can drive the camera. Combined with WR-07, validation is the only protection on these endpoints.
**Fix:** Acceptable for Phase 1 if documented; consider a device-unique generated password at first boot.

### IN-03: /status reports "connected": true unconditionally

**File:** `src/main_basestation.cpp:598`
**Issue:** The JSON always reports connected=true, so the UI LED is permanently green even with the balloon link dead — the one indicator that would tell the operator commands are going nowhere never fires. The sender already tracks timeouts that could feed this field.
**Fix:** Report link health from real signal, e.g. `millis() - lastAckTime < 15000` or "no timeouts in the last N commands".

### IN-04: Dead code across the new modules

**File:** `src/command_sender.cpp:295-308` (`findOldestCommand` never called), `src/main_basestation.cpp:633-642` (`updateLED` never called; the blink lives in `updateStatus`), `src/main_basestation.cpp:63-64,509-510` (`lastCommandTime`/`lastCommandSequence` written, never read), `include/command_protocol.h:75,91` (`PacketHeader header` member never populated or serialized — hand-rolled 7-byte header is used instead), `src/e32_lora.cpp:474-482` (`calculateConfigCRC` never called), `src/e32_lora.cpp:427` (`previousMode` saved, never used — see WR-02).
**Fix:** Remove or wire up; the phantom `PacketHeader` field especially misleads readers into thinking struct layout defines the wire format.

### IN-05: Duplicate PacketType enumerator values invite aliasing (pre-existing)

**File:** `include/common_types.h:26-49`
**Issue:** `GPS = TELEMETRY (0x02)`, `CAMERA_THUMB = GPS_DATA (0x03)`, `CAMERA_FULL = CAMERA_DATA (0x04)`, `ACK = COMMAND_ACK (0x06)`, `NACK = STATUS (0x07)`, `PING = DEBUG (0x08)`. Any `switch` over `PacketType` cannot distinguish them and will not compile if both aliases are cased. The newly added `COMMAND (0x10)` / `RESPONSE (0x11)` do not collide — good — but the enum remains a trap. Related: the header's "Pin Validation" comment in `sensor_pins.h` claims no conflicts while listing camera GPIO 4 which is also `BATTERY_SENSE_PIN` (see IN-08).
**Fix:** Remove the alias block or move it to a separate legacy enum namespace.

### IN-06: Per-byte 100 µs delay in E32LoRa::read

**File:** `src/e32_lora.cpp:246`
**Issue:** `delayMicroseconds(100)` inside the read loop adds ~25 ms of blocking for a 256-byte burst, with no stated purpose at 9600 baud (UART FIFO already buffers). Note this bulk-read overload is currently unused — the mains use the single-byte `read()` — but it will block when adopted.
**Fix:** Delete the delay; rely on `available()`/FIFO.

### IN-07: Init failures are undetectable — communicationActive set regardless, E32 begin cannot fail on missing module

**File:** `src/main_balloon.cpp:378-391`, `src/e32_lora.cpp:74-90`
**Issue:** `E32LoRa::begin()` returns true whenever a serial pointer exists; AUX-low only logs a warning, so a completely absent module "initializes successfully". `initializeSubsystems()` then sets `appState.communicationActive = true` unconditionally, even on the CmdHandler failure path. Downstream, nothing can distinguish a configured link from a dead one.
**Fix:** Propagate AUX-check failure (or a later readConfig probe) into the return value, and gate `communicationActive` on both begins succeeding.

### IN-08: GPIO 4 double-booked between battery ADC and camera I2C data (pre-existing, cross-file)

**File:** `src/main_balloon.cpp:577` with `include/sensor_pins.h:46` and `include/camera_pins.h:301`
**Issue:** `BATTERY_SENSE_PIN = 4` collides with `SIOD_GPIO_NUM = 4` (camera SCCB SDA for `CAMERA_MODEL_ESP32S3_EYE`). `checkHardwareStatus()` calls `analogRead(4)` before camera init, and any future periodic battery sampling would corrupt camera I2C. The "No conflicts detected" comment in `sensor_pins.h` omits pin 4 from its sensor list. Not introduced by this phase, but it will bite during Phase 1 hardware tests.
**Fix:** Move battery sense to a truly free GPIO (e.g., 3 or 41) or drop the boot-time ADC read.

---

_Reviewed: 2026-08-17T23:54:09Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

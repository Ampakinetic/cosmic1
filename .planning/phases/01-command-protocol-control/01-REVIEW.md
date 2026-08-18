---
phase: 01-command-protocol-control
reviewed: 2026-08-18T12:00:00Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - include/auto_capture.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
  - src/auto_capture.cpp
  - src/camera_manager.cpp
  - src/camera_manager.h
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
findings:
  critical: 3
  warning: 11
  info: 8
  total: 22
status: issues_found
---

# Phase 1: Code Review Report (Re-review after gap closure)

**Reviewed:** 2026-08-18T12:00:00Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Re-review of the Phase 1 command-protocol implementation after execution of fix plans 01-02..01-05. The five prior Critical findings are verified CLOSED in the current code:

- **CR-01 (seq byte / CRC)** — closed. `serializeCommand` now writes the sequence low byte at header offset 3 before CRC computation (`src/command_protocol.cpp:61`), and the regression harness sweep proves it.
- **CR-02 (240-byte LoRa limit)** — closed. Serialize rejects packets over `CMD_MAX_PACKET_SIZE` and payloads over 224 (`src/command_protocol.cpp:53-55`).
- **CR-03 (embedded 0x0D 0x0A)** — closed. Both receivers use length-driven framing with the end marker tested only at the announced length (`src/command_handler.cpp:644-682`, `src/command_sender.cpp:281-316`).
- **CR-04 (back-to-back retry loop)** — closed. Exponential backoff paces every retransmit attempt (`src/command_sender.cpp:199-209`).
- **CR-05 (legacy 30 s capture timer)** — closed. No capture timer remains in `main_balloon.cpp`; `AutoCapture` is the sole capture scheduler (`src/main_balloon.cpp:724-728`).

However, the adversarial re-review found **3 new Critical defects** and several Warnings. The most serious: every successful balloon response goes onto the wire with the wrong packet-type byte (0x00 instead of 0x11), `GET_STATUS` misreports the camera resolution due to a raw numeric cast between two differently-numbered enums, and a normal ACK-then-retry race corrupts the sender's pending-command counter (uint8 underflow). Cross-file facts were verified against the pinned `esp32-camera` library in `.pio/libdeps/esp32-s3-balloon` (real `framesize_t`: QQVGA=1, QVGA=5, VGA=8, UXGA=13, QXGA=17/2048x1536) and against `src/stubs.cpp` (LoRaComm stub does not touch Serial2 — no UART conflict with the E32 driver).

## Critical Issues

### CR-01: Success responses are serialized with packet type 0x00 (HEARTBEAT-style) instead of 0x11 (RESPONSE)

**File:** `src/command_protocol.cpp:371-387` (defect), consumed at `src/command_handler.cpp:98-103`, written to the wire at `src/command_protocol.cpp:159`
**Issue:** The free function `createResponsePacket()` never assigns `packet.type`. `ResponsePacket packet{}` zero-initializes it, so `type == 0x00` — not the documented `0x11 RESPONSE` (`include/command_protocol.h:92`, `include/common_types.h:38`). `CommandHandler::process()` builds **all successful responses** (every ACK and every STATUS) through `createResponsePacket()` (`src/command_handler.cpp:98`), and `serializeResponse()` emits `resp.type` verbatim (`src/command_protocol.cpp:159`). Only NACKs (`createNACK`, `src/command_protocol.cpp:256`) and the unused `createACK`/`createStatus` helpers set 0x11. Net effect: on the wire, ACK/STATUS packets carry header type 0x00 while NACK packets carry 0x11 — the protocol field is wrong and inconsistent for the entire success path. It works today only because `CommandSender` never checks the type byte; any type-filtering consumer (sniffer, telemetry router, Phase 2 image pipeline sharing the channel) will misroute or drop every successful response. The regression harness masks this because its JS mirror hardcodes 0x11 (`scripts/verify_protocol_roundtrip.mjs:139`) instead of modeling `createResponsePacket`.
**Fix:**
```cpp
// src/command_protocol.cpp:371
ResponsePacket createResponsePacket(ResponseType type, uint16_t refSequence, const void* data, size_t dataLen) {
    ResponsePacket packet{};
    packet.type = static_cast<PacketType>(0x11); // RESPONSE — match createACK/createNACK
    packet.responseType = type;
    // ... rest unchanged
```
For symmetry also set `packet.type = PACKET_TYPE_COMMAND;` in `createCommandPacket()` (currently only correct because `serializeCommand` hardcodes the type byte), and add a harness clause asserting byte 2 of a serialized response built the way `CommandHandler` builds it equals 0x11.

### CR-02: GET_STATUS reports the wrong resolution — raw cast between differently-numbered enums

**File:** `src/command_handler.cpp:579`
**Issue:** `status.currentResolution = static_cast<FrameSize>(camera->getFrameSize());` casts the real `framesize_t` value to the project's `FrameSize` enum, but the two enums are numbered differently despite the header comment "matching esp_camera.h framesize_t" (`include/command_protocol.h:57-68`). Verified against the pinned library (`.pio/libdeps/esp32-s3-balloon/esp32-camera/driver/include/sensor.h:85-110`): real QQVGA=1, QVGA=5, VGA=8, UXGA=13; project QQVGA=5, QVGA=6, VGA=9, UXGA=13. The balloon boots at `BALLOON_CAMERA_FRAMESIZE = FRAMESIZE_QVGA` (real 5, `include/balloon_config.h:40`), so `GET_STATUS` reports `FrameSize(5)` which any consumer interpreting the project enum reads as **QQVGA 160x120 while the camera is actually at QVGA 320x240**. A camera at real VGA (8) reports as project-QXGA (8). The SET direction is unaffected only because `framesizeFromInt` maps by enum *name* (`src/command_handler.cpp:703-735`).
**Fix:** Add a reverse mapping and use it:
```cpp
static FrameSize frameSizeFromEsp(framesize_t fs) {
    switch (fs) {
        case FRAMESIZE_QQVGA: return FrameSize::FRAMESIZE_QQVGA;
        case FRAMESIZE_QVGA:  return FrameSize::FRAMESIZE_QVGA;
        case FRAMESIZE_HQVGA: return FrameSize::FRAMESIZE_HQVGA;
        case FRAMESIZE_VGA:   return FrameSize::FRAMESIZE_VGA;
        case FRAMESIZE_SVGA:  return FrameSize::FRAMESIZE_SVGA;
        case FRAMESIZE_XGA:   return FrameSize::FRAMESIZE_XGA;
        case FRAMESIZE_SXGA:  return FrameSize::FRAMESIZE_SXGA;
        case FRAMESIZE_UXGA:  return FrameSize::FRAMESIZE_UXGA;
        default:              return FrameSize::FRAMESIZE_QVGA; // define/add an UNKNOWN if preferred
    }
}
// in handleGetStatus():
status.currentResolution = frameSizeFromEsp(camera->getFrameSize());
```
Also fix the false "matching esp_camera.h framesize_t" comment (see WR-01).

### CR-03: Duplicate/late response double-decrements `pendingCommandCount` (uint8 underflow) — triggered by the normal retry mechanism

**File:** `src/command_sender.cpp:319-357` (`handleResponse`), `src/command_sender.cpp:363-371` (`findTrackedCommand`)
**Issue:** `findTrackedCommand()` matches any non-IDLE slot, including slots already in a terminal state (ACKED/FAILED/TIMEOUT). `handleResponse()` has no terminal-state guard, so a second response for the same sequence re-runs the transition: `pendingCommandCount--` executes a second time. This is not an exotic path — it is the designed retry flow: the ACK-timeout window for CAPTURE_NOW is 2000 ms; when ACK latency approaches that edge, the sender fires a retry, the balloon re-executes and sends a second ACK with the same `refSequence`, and the late first ACK plus the second ACK both process. First ACK: count 1→0. Second ACK: count 0→255. After that `hasPendingCommands()` returns true forever (UI shows pending=1 permanently) and slot-accounting/statistics are corrupt. The same double-decrement occurs for any duplicated NACK, and after the 16-bit sequence wrap an old terminal slot with the reused sequence number can shadow a live command's response.
**Fix:**
```cpp
void CommandSender::handleResponse(const ResponsePacket& response) {
    TrackedCommand* cmd = findTrackedCommand(response.refSequence);
    if (!cmd) { /* unknown seq log */ return; }

    // A terminal slot must never be transitioned (or counted) a second time
    if (cmd->state == CommandState::ACKED || cmd->state == CommandState::FAILED ||
        cmd->state == CommandState::TIMEOUT) {
        return; // duplicate / late response
    }
    // ... rest unchanged
```
Optionally restrict `findTrackedCommand` to PENDING/SENT for response matching.

## Warnings

### WR-01: FrameSize enum mislabeled; "QXGA 400x296" option targets an unsupported 2048x1536 size and always fails

**File:** `include/command_protocol.h:57-68`; `src/main_basestation.cpp:612-622`; `src/command_handler.cpp:713-716`
**Issue:** The comment claims values match `esp_camera.h framesize_t`; they do not (see CR-02). Value 8 is named `FRAMESIZE_QXGA` and commented "400x296" — 400x296 is really `FRAMESIZE_CIF` (real 6), while real `FRAMESIZE_QXGA` is 2048x1536 (3 MP sensors only). `framesizeFromInt` maps project-8 to real QXGA, which the OV2640 on the ESP32-S3-EYE cannot produce, so the UI's "QXGA 400x296" option (base station `<option value="8">`, `src/main_balasestation.cpp:616`) always NACKs with a misleading "Set resolution failed". Real CIF/HVGA/HD sizes are unreachable.
**Fix:** Rename value 8 to `FRAMESIZE_CIF` mapped to real `FRAMESIZE_CIF` (400x296) in both `framesizeFromInt` and the base-station `<option>` label, correct the enum comment to state that these are protocol-internal codes mapped by name, and keep the CR-02 reverse mapping authoritative.

### WR-02: CommandHandler has a single pending-command slot — a second command arriving in the same loop pass silently overwrites the first

**File:** `src/command_handler.cpp:676-679` (overwrite), `src/command_handler.cpp:83` (single-slot execution)
**Issue:** `processIncomingByte` sets `pendingCommand.packet = cmd; hasCommand = true;` for each framed command. If two commands are already in the UART buffer when `process()` runs (10 Hz loop; two quick UI clicks, or a retry landing behind a fresh command), both are framed in the read loop and only the **last** is executed. The first is dropped with no response, no NACK, and no `commandsReceived` accounting; the base station must wait out the full ACK timeout and retry, doubling channel occupancy and delaying the command by seconds.
**Fix:** Either queue commands (small ring of `PendingCommand`) or, minimally, skip storing a new command while `hasCommand` is true and let the framing layer drop it — but then at least count it (`commandsReceived++`, debug log) so the loss is observable. A one-deep queue executed at one command per `process()` call is sufficient at 10 Hz.

### WR-03: No duplicate-command suppression on the balloon — retried commands re-execute non-idempotent actions

**File:** `src/command_handler.cpp:147-186` (dispatch), `src/command_handler.cpp:193-226` (CAPTURE_NOW)
**Issue:** The handler does not track recently-seen sequence numbers. When the base station retries (ACK lost or slow), the balloon executes the command again. For `SET_*` this is idempotent, but each retried `CAPTURE_NOW` captures another frame and consumes another image ID from the shared sequence (`AutoCap().allocateImageId()`), so one user click can silently produce 2-4 captures and gaps in the image-ID sequence that Phase 2 intends to use for durable image identification.
**Fix:** Keep a small cache of the last N executed sequence numbers (e.g., last 8) in `CommandHandler`; on a hit, re-send the stored response instead of re-executing. Sequence numbers are already 16-bit and monotonically issued, so a tiny ring buffer suffices.

### WR-04: Blocking calls in the single-threaded event loop violate the project's non-blocking constraint

**File:** `src/e32_lora.cpp:158-202` (transmit: `waitForAuxHigh(1000)` + `waitForAuxLow(1000)` + `waitForAuxHigh(5000)`), `src/e32_lora.cpp:104-131` (`setMode` `delay(50)`), `src/command_handler.cpp:201` and `src/auto_capture.cpp:98` (`esp_camera_fb_get` inside `captureImage`), `src/main_balloon.cpp:724-728`
**Issue:** Every response transmission and every capture runs synchronously inside `loop()`. Worst case a single transmit blocks up to ~7 s on AUX timeouts, and a high-resolution capture can block for a large fraction of a second or more. During that time sensor updates, telemetry cadence (5 s), and further command reception stall, and incoming UART bytes accumulate toward the RX FIFO limit.
**Fix:** At minimum bound the transmit handshake (e.g., 500 ms total) and treat AUX-timeout as an asynchronous state to re-check in `process()`; longer term, make `transmit` a non-blocking state machine (write → poll AUX in `process()`). Document the accepted capture-blocking window or move capture to a task if telemetry timing must hold during captures.

### WR-05: Verification harness diverges from the real ACK serialization path — masks CR-01

**File:** `scripts/verify_protocol_roundtrip.mjs:126-160` (esp. line 139)
**Issue:** The script's own header says it MUST mirror `src/command_protocol.cpp`, and `serializeResponse` hardcodes `PACKET_TYPE_RESPONSE` at the type-byte position. The real ACK path goes through `createResponsePacket` → `serializeResponse(resp.type ...)`, which emits 0x00 today (CR-01). The harness therefore passes while the firmware violates the wire format. It also never models `createCommandPacket`/`createResponsePacket`, so the whole struct-construction layer is untested.
**Fix:** After fixing CR-01, add clauses: (1) transcribe `createResponsePacket` faithfully (type from the zero-initialized struct) and assert byte 2 of the serialized ACK equals 0x11; (2) assert the NACK path and ACK path produce the same type byte. This keeps the mirror honest.

### WR-06: ACK-edge detection truncates the 32-bit ACK counter to 16 bits

**File:** `src/main_basestation.cpp:503-507`
**Issue:** `if (static_cast<uint16_t>(acked) > appState.ackedAtLastPoll)` compares truncated 16-bit views of a monotonically increasing 32-bit counter. At 65536-ACK boundaries an edge is missed (`lastAckTime` not updated), so the link LED can flip to "Unknown"/"No link" despite healthy traffic. It is also needlessly weaker than comparing the full value.
**Fix:** Store the full `uint32_t` in `ackedAtLastPoll` and test `acked != appState.ackedAtLastPoll` (any change, not just increase — the counter never legitimately decreases).

### WR-07: E32 configuration API is non-functional and partially a silent no-op

**File:** `src/e32_lora.cpp:290-314` (`writeConfig` — no 0xC0 command byte, no CRC byte, drops `option`/`channel`/`transmissionType`), `src/e32_lora.cpp:335-344` (`setChannel` validates then returns true having done nothing), `src/e32_lora.cpp:264-288` (`readConfig` reads 6 bytes and discards them), `src/e32_lora.cpp:316-333` (`setParameters`/`setAddress` pass a partially uninitialized `E32Config`)
**Issue:** None of these are on the Phase 1 hot path, but they are public API of the reviewed driver: `writeConfig` emits a byte sequence the real E32 will not interpret as a configuration command (the module expects `0xC0/0xC2` + registers + CRC), `setChannel` reports success without effect, and the config struct's `channel`, `optionBits`, `transmissionType` fields are never initialized in the setters. Any future caller gets silent misbehavior.
**Fix:** Either implement against the E32 datasheet (command prefix + full register set + XOR CRC, read-modify-write for single-field setters) or mark the methods unimplemented and return false, and value-initialize `E32Config config{};` in the setters.

### WR-08: Hardcoded WiFi AP credentials; command endpoints unauthenticated

**File:** `src/main_basestation.cpp:38-41`
**Issue:** SSID and a weak static WPA password are compiled into the firmware in source control; any client that joins the AP can trigger camera captures and reconfigure the balloon camera with no further authentication. Acceptable for a bench prototype, not for field use.
**Fix:** Move credentials to build flags or NVS/Preferences with a per-device generated password, and require at least a shared token on the POST endpoints before flight deployments.

### WR-09: CAPTURE_NOW ACK window (2000 ms) can be shorter than worst-case capture time

**File:** `include/command_protocol.h:181`; `src/command_sender.cpp:242-258`
**Issue:** The D-05 "TRIGGER" window assumes fast capture, but `esp_camera_fb_get` at SVGA+ resolutions, in low light, or while auto-exposure re-converges can exceed 2 s (made worse by WR-04's blocking transmit ahead of it). A slow-but-successful capture then looks like a timeout, the sender retries, and the balloon captures again (compounding WR-03).
**Fix:** Raise `CMD_ACK_TIMEOUT_TRIGGER_MS` to 5000 ms to match the SETTINGS class, or have the balloon ACK receipt immediately and report capture completion via a follow-up STATUS (protocol change — defer if undesirable).

### WR-10: Auto-capture UI chip latches the wrong interval and breaks at sequence wrap

**File:** `src/main_basestation.cpp:1058-1071`
**Issue:** The latch stores `appState.autoCaptureIntervalAckSec = appState.autoCaptureIntervalSec` — the interval of the **latest issued** enable command, not the one that actually ACKed. Issuing enable(10 s) then enable(60 s) quickly shows "ON · every 60s" even if the ACKed command was 10 s. The guard `entries[i].sequenceNumber > appState.autoCaptureAckSeq` also stops matching after the 16-bit sequence wraps (65535 → 1), freezing the chip state.
**Fix:** Carry the interval inside the tracked entry (extend `CommandQueueEntry` or store a map seq→interval when issuing) and latch from the ACKed entry's own value; use a wrap-safe comparison (e.g., signed 16-bit delta) or a generation counter instead of `>`.

### WR-11: Thumbnail allocation estimate is too small — thumbnail creation frequently fails

**File:** `src/camera_manager.cpp:290-300` and `319-334` (estimate `15 * 200 + 1000 = 4000` bytes for QQVGA at quality 15)
**Issue:** A QQVGA JPEG at quality 15 (high quality) is commonly 4-10 KB; when `fb->len > estimatedSize` the function frees everything and returns false. Memory handling on the failure path is correct (no overflow, no leak), but the feature fails most of the time. Dormant in Phase 1 (no thumbnail path is exercised by the command protocol) yet it is live API in a reviewed file.
**Fix:** Allocate with headroom (e.g., `estimateImageSize(FRAMESIZE_QQVGA, 15)` → use a fixed 16 KB in PSRAM, or re-`malloc` to `fb->len` when it exceeds the estimate instead of failing).

## Info

### IN-01: Dead method + comment/code mismatch in slot eviction

**File:** `src/command_sender.cpp:373-386` (`findOldestCommand` never called), `src/command_sender.cpp:396-405`
**Issue:** `findOldestCommand` is declared, defined, and never used. The eviction comment says "evict oldest completed command" but the loop evicts the *first* terminal slot in index order — which can be the slot backing the UI's pinned last-command state, blanking it arbitrarily.
**Fix:** Either delete `findOldestCommand` or actually use it for eviction; prefer evicting the oldest terminal slot.

### IN-02: Dead functions in base station

**File:** `src/main_basestation.cpp:1110-1112` (`sendHTML`), `src/main_basestation.cpp:1114-1123` (`updateLED`)
**Issue:** Neither is called; physical LED blinking is done inside `updateStatus()`. The IN-03 "LED truth" design is only reflected in the web LED, while the physical LED blinks unconditionally every 5 s regardless of link state.
**Fix:** Delete both, or wire `updateLED()` into `loop()` driven by the same `connected/linkText` truth the web UI uses.

### IN-03: Hardcoded E32 pin numbers duplicate `sensor_pins.h` macros

**File:** `src/main_balloon.cpp:375`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` inlines values that exist as `LORA_RX_PIN/LORA_TX_PIN/LORA_M0_PIN/LORA_M1_PIN/LORA_AUX_PIN` (`include/sensor_pins.h:33-37`). They match today; a pin change in the header silently diverges from the driver wiring.
**Fix:** Pass the macros (and `LORA_BAUD_RATE`).

### IN-04: Interval bounds duplicated as literals

**File:** `src/command_handler.cpp:529`
**Issue:** `intervalMs < 1000 || intervalMs > 3600000` re-states `AUTO_CAPTURE_MIN_INTERVAL_MS`/`AUTO_CAPTURE_MAX_INTERVAL_MS` by literal even though `auto_capture.h` is included and its comment explicitly describes the mirroring. Divergence risk.
**Fix:** Use the constants.

### IN-05: Duplicate-valued compat aliases in shared enums

**File:** `include/common_types.h:26-64`
**Issue:** `PacketType` has `GPS == TELEMETRY == 0x02`, `CAMERA_THUMB == GPS_DATA == 0x03`, `ACK == COMMAND_ACK == 0x06`, etc.; `PacketPriority` similarly collides (`EMERGENCY == PRIORITY_NORMAL == 1`). Switches on these enums can silently take the wrong case.
**Fix:** Drop the "compatibility" aliases or move them to clearly-typed separate enums; at minimum document which value is authoritative on the wire.

### IN-06: Start-byte hunt cannot resync on 0xAA 0xAA 0x55; duplicated `validatePacket` implementations

**File:** `src/command_handler.cpp:630-640`; `src/command_sender.cpp:268-278`
**Issue:** While expecting 0x55 at `receiveIndex == 1`, a 0xAA resets to 0 instead of restarting the hunt with the current byte, so a preamble byte of 0xAA immediately before a real start pair loses frame sync (CRC makes this safe but lossy). The two `validatePacket` methods are identical copies.
**Fix:** In the else branch, check `byte == CMD_START_BYTE1` and keep `receiveIndex = 1` (retain the byte); hoist the shared validator into `CommandProtocol`.

### IN-07: `lastReceiveTime` never updated on the single-byte read path

**File:** `include/e32_lora.h:91`
**Issue:** The `uint8_t read()` overload (the one actually used by both protocol modules) does not touch `lastReceiveTime`, so `printStatus()` reports a bogus "Last RX" forever.
**Fix:** Update `lastReceiveTime = millis()` in the single-byte overload, or drop the field if unused.

### IN-08: Dead protocol declarations

**File:** `include/command_protocol.h:107-157`; `src/command_handler.cpp:677`
**Issue:** `PayloadSetResolution` … `PayloadAutoCaptureEnable`, `ResponseCaptureData` are never used (handlers read raw payload bytes; `ResponseCaptureData` is not what CAPTURE_NOW actually returns — it returns a raw 2-byte big-endian imageId). `pendingCommand.receivedTime` is written and never read. The dead structs actively misdocument the wire format (e.g., they imply native-endian struct memcpy, which `handleAutoCaptureEnable`'s big-endian `readUint32` explicitly avoids).
**Fix:** Delete the unused structs and the unused field, or rewrite them as byte-layout documentation matching the real encoding.

---

_Reviewed: 2026-08-18T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

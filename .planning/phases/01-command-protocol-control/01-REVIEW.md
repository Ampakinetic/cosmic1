---
phase: 01-command-protocol-control
reviewed: 2026-08-18T11:06:36Z
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
  critical: 1
  warning: 11
  info: 8
  total: 20
status: issues_found
---

# Phase 1: Code Review Report (Second re-review after gap plan 01-06)

**Reviewed:** 2026-08-18T11:06:36Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Second re-review of the Phase 1 command-protocol implementation. This report supersedes the prior report at commit `ca682b4` (3 Critical / 11 Warning / 8 Info) and verifies the current state of the code after gap plan 01-06 (commits `75b8514`, `36674ff`, `56704e2`).

**All three prior Critical findings are verified CLOSED in the current code:**

- **CR-01 (response packet type 0x00 on the success path)** — closed. `createResponsePacket()` now assigns `packet.type = PACKET_TYPE_RESPONSE` (`src/command_protocol.cpp:374`) and `createCommandPacket()` sets `PACKET_TYPE_COMMAND` (`src/command_protocol.cpp:355`) for symmetry. The harness clause (f) added in `75b8514` faithfully mirrors the real factory (zero-initialized struct, `defective` variant proving teeth) and asserts byte 2 == 0x11 on both ACK and NACK paths (`scripts/verify_protocol_roundtrip.mjs:132-155, 374-396`). I executed the harness against the committed code: all 15 clauses pass.
- **CR-02 (GET_STATUS misreported resolution via numeric cast)** — closed. `handleGetStatus` now uses the name-based reverse mapping `frameSizeFromEsp()` (`src/command_handler.cpp:579, 741-766`), which matches real `framesize_t` constants to protocol codes by name. The enum value 8 was renamed `FRAMESIZE_CIF` with a truthful header comment stating the numbering deliberately differs from `framesize_t` (`include/command_protocol.h:58-72`), the forward map translates CIF→real CIF (`src/command_handler.cpp:713-716`), and the UI option label reads "CIF 400x296" (`src/main_basestation.cpp:617`). This also closes prior WR-01.
- **CR-03 (duplicate/late response double-decremented `pendingCommandCount`)** — closed. `handleResponse` returns early for any slot in a terminal state (`src/command_sender.cpp:336-338`), so a late first ACK arriving after a retry can no longer underflow the uint8 counter or corrupt statistics.

Prior **WR-05** (harness modeled the wrong construction path) is also closed by the same clause (f).

The remaining findings: one **new Critical** (a genuine double-free on the `createThumbnail` failure paths in `camera_manager.cpp` — dormant in the Phase 1 command flow but live public API in the balloon build, and the failure path is the *likely* one per WR-11), **9 carried-forward Warnings** (WR-02/03/04/06/07/08/09/10/11, all re-verified against current line numbers), **2 new Warnings** (WR-12: receive paths never validate the packet-type byte — the missing counterpart of the now-enforced type conformance; WR-13: the balloon's camera health check reports failure unconditionally when the camera is healthy), and the **8 prior Infos** re-verified unchanged. The three 01-06 commits introduced no defects in the code they touched; CR-04 and WR-12/WR-13 are pre-existing defects newly surfaced by this round's adversarial pass.

## Narrative Findings (AI reviewer)

### Closure verification detail (prior CR-02 fix)

The `frameSizeFromEsp` default branch returns `FrameSize::FRAMESIZE_QVGA` for real sizes with no protocol code (96x96, QCIF, 240x240, HVGA, HD). This is acceptable: no protocol or code path can place the camera in those sizes (boot default and every internal setter use name-mapped sizes), and the alternative (a numeric cast) was the original defect. Not counted as a finding.

## Critical Issues

### CR-04: `createThumbnail` failure paths leave a dangling pointer — double-free on the next cleanup

**File:** `src/camera_manager.cpp:311-317` and `src/camera_manager.cpp:328-334`
**Issue:** `createThumbnail()` assigns `thumbnail.buffer = malloc(...)` (line 294), where `thumbnail` is a reference to the member `currentThumbnail`. Both failure exits free the buffer **without nulling it**:

```cpp
// path 1 (line 311-317): frame-buffer grab failed
camera_fb_t* fb = esp_camera_fb_get();
if (!fb) {
    free(thumbnail.buffer);      // currentThumbnail.buffer now DANGLING
    setFrameSize(originalSize);
    setQuality(originalQuality);
    return false;
}

// path 2 (line 328-334): fb->len > estimatedSize (the common case — see WR-11)
} else {
    free(thumbnail.buffer);      // DANGLING again
    esp_camera_fb_return(fb);
    ...
```

After either path, `currentThumbnail.buffer` is a freed pointer with `valid == false` (set earlier by `freeCurrentThumbnail()`), but every consumer tests `if (currentThumbnail.buffer)`, not `valid`. The next call to `freeCurrentThumbnail()` — via a subsequent `captureThumbnail()`, `captureBoth()`, `releaseImageBuffers()` from `end()` (destructor, `enableCamera(false)` on the low-battery path in `src/main_balloon.cpp:705`) — executes `free()` on the dangling pointer: **double free → heap corruption/crash**. The path-2 trigger is probable whenever the API is used, because the 4000-byte estimate is routinely smaller than a QQVGA JPEG at quality 15 (WR-11).

Dormancy note: no Phase 1 caller invokes `captureThumbnail()`/`captureBoth()` (verified by grep — only `camera_manager.cpp` itself references them), so this does not fire in the current command flow. It is nonetheless a crash-class memory defect in live public API of a reviewed, built file, and Phase 2 image transmission will call exactly this API.
**Fix:**
```cpp
// In BOTH failure paths, null the member before returning:
free(thumbnail.buffer);
thumbnail.buffer = nullptr;   // prevent double-free via freeCurrentThumbnail()
thumbnail.valid = false;
```
(Also recommended: have `freeCurrentThumbnail()` defensively null `length`/`width`/`height`, and address WR-11 so path 2 stops being the common case.)

## Warnings

### WR-02 (carried): CommandHandler has a single pending-command slot — a second command framed in the same loop pass silently overwrites the first

**File:** `src/command_handler.cpp:676-679` (overwrite), `src/command_handler.cpp:83` (single-slot execution)
**Issue:** `processIncomingByte` stores `pendingCommand.packet = cmd; hasCommand = true;` for every framed command. If two commands are already in the UART buffer when `process()` runs (10 Hz loop, two quick UI clicks, or a retry landing behind a fresh command), both are framed in the read loop and only the last executes. The first is dropped with no response, no NACK, and no `commandsReceived` accounting; the base station waits out the full ACK timeout and retries.
**Fix:** Queue commands (a small ring of `PendingCommand`, one executed per `process()` call), or minimally skip storing while `hasCommand` is true and count/log the dropped command so the loss is observable.

### WR-03 (carried): No duplicate-command suppression on the balloon — retried commands re-execute non-idempotent actions

**File:** `src/command_handler.cpp:147-186` (dispatch), `src/command_handler.cpp:207` (`AutoCap().allocateImageId()`)
**Issue:** The handler does not track recently-seen sequence numbers. When the base station retries (ACK lost or slow, and see WR-09), the balloon executes the command again. Each retried `CAPTURE_NOW` captures another frame and consumes another image ID from the shared sequence, so one user click can silently produce 2-4 captures and gaps in the ID sequence Phase 2 intends to use for durable image identification.
**Fix:** Keep a small ring of the last N executed sequence numbers with their stored responses; on a hit, re-send the stored response instead of re-executing.

### WR-04 (carried): Blocking calls in the single-threaded event loop violate the project's non-blocking constraint

**File:** `src/e32_lora.cpp:158-202` (`transmit`: `waitForAuxHigh(1000)` + `waitForAuxLow(1000)` + `waitForAuxHigh(5000)`), `src/e32_lora.cpp:113` (`setMode` `delay(50)`), `src/command_handler.cpp:201` and `src/auto_capture.cpp:98` (`esp_camera_fb_get` via `captureImage`), called from `src/main_balloon.cpp:724-728`
**Issue:** Every response transmission and every capture runs synchronously inside `loop()`. Worst case a single transmit blocks up to ~7 s on AUX timeouts; a capture can block for a large fraction of a second or more. Sensor updates, the 5 s telemetry cadence, and further command reception stall, and incoming UART bytes accumulate toward the RX FIFO limit.
**Fix:** Bound the transmit handshake (e.g., 500 ms total) and treat AUX-timeout as asynchronous state re-checked in `process()`; longer term make `transmit` a non-blocking state machine (write → poll AUX in `process()`). Document the accepted capture-blocking window or move capture to a task.

### WR-06 (carried): ACK-edge detection truncates the 32-bit ACK counter to 16 bits

**File:** `src/main_basestation.cpp:503-507` (cast), `src/main_basestation.cpp:72` (`ackedAtLastPoll` declared `uint16_t`)
**Issue:** `if (static_cast<uint16_t>(acked) > appState.ackedAtLastPoll)` compares truncated 16-bit views of a monotonically increasing 32-bit counter. At 65536-ACK boundaries an edge is missed (`lastAckTime` not updated), so the link LED can flip to "Unknown"/"No link" despite healthy traffic.
**Fix:** Store the full `uint32_t` in `ackedAtLastPoll` and test `acked != appState.ackedAtLastPoll` (the counter never legitimately decreases).

### WR-07 (carried): E32 configuration API is non-functional and includes uninitialized-struct reads

**File:** `src/e32_lora.cpp:290-314` (`writeConfig` — no 0xC0/0xC2 command byte, no CRC byte, drops `option`/`channel`/`transmissionType`), `src/e32_lora.cpp:335-344` (`setChannel` validates then returns true having done nothing), `src/e32_lora.cpp:264-288` (`readConfig` reads 6 bytes and discards them), `src/e32_lora.cpp:316-333` (`setParameters`/`setAddress` build a partially uninitialized `E32Config`)
**Issue:** None of these are on the Phase 1 hot path, but they are public API of the reviewed driver. `writeConfig` emits a byte sequence the real E32 will not interpret as a configuration command; `setChannel` reports success without effect; `setAddress` passes uninitialized `config.uartSpeed`/`config.airDataRate` into `writeConfig` (indeterminate values read at `src/e32_lora.cpp:302-303`).
**Fix:** Implement against the E32 datasheet (command prefix + full register set + XOR CRC, read-modify-write for single-field setters), or mark the methods unimplemented and return false; at minimum value-initialize `E32Config config{};` in the setters.

### WR-08 (carried): Hardcoded WiFi AP credentials; command endpoints unauthenticated

**File:** `src/main_basestation.cpp:38-41`
**Issue:** SSID and a weak static WPA password are compiled into the firmware in source control; any client that joins the AP can trigger camera captures and reconfigure the balloon camera with no further authentication. Acceptable for a bench prototype, not for field use.
**Fix:** Move credentials to build flags or NVS/Preferences with a per-device generated password, and require at least a shared token on the POST endpoints before flight deployments.

### WR-09 (carried): CAPTURE_NOW ACK window (2000 ms) can be shorter than worst-case capture time

**File:** `include/command_protocol.h:185` (`CMD_ACK_TIMEOUT_TRIGGER_MS = 2000`), `src/command_sender.cpp:242-258`
**Issue:** The D-05 "TRIGGER" window assumes fast capture, but `esp_camera_fb_get` at SVGA+ resolutions, in low light, or while auto-exposure re-converges can exceed 2 s (made worse by WR-04's blocking transmit ahead of it). A slow-but-successful capture looks like a timeout, the sender retries, and the balloon captures again (compounding WR-03).
**Fix:** Raise `CMD_ACK_TIMEOUT_TRIGGER_MS` to 5000 ms to match the SETTINGS class, or ACK receipt immediately and report capture completion via a follow-up STATUS (protocol change — defer if undesirable).

### WR-10 (carried): Auto-capture UI chip latches the wrong interval and breaks at sequence wrap

**File:** `src/main_basestation.cpp:1058-1069`
**Issue:** The latch stores `appState.autoCaptureIntervalAckSec = appState.autoCaptureIntervalSec` — the interval of the **latest issued** enable command (`src/main_basestation.cpp:945`), not the one that actually ACKed. Issuing enable(10 s) then enable(60 s) quickly shows "ON · every 60s" even if the ACKed command was 10 s. The guard `entries[i].sequenceNumber > appState.autoCaptureAckSeq` also stops matching after the 16-bit sequence wraps (65535 → 1), freezing the chip state.
**Fix:** Carry the interval inside the tracked entry (extend `CommandQueueEntry` or keep a seq→interval map when issuing) and latch from the ACKed entry's own value; use a wrap-safe comparison (signed 16-bit delta) or a generation counter instead of `>`.

### WR-11 (carried): Thumbnail allocation estimate is too small — thumbnail creation frequently fails (and feeds CR-04)

**File:** `src/camera_manager.cpp:291` (`estimateImageSize(FRAMESIZE_QQVGA, 15)` = 15·200+1000 = 4000 bytes), `src/camera_manager.cpp:320-334` (oversize → fail)
**Issue:** A QQVGA JPEG at quality 15 is commonly 4-10 KB; when `fb->len > estimatedSize` the function frees everything and returns false — through the dangling-pointer path that constitutes CR-04. Dormant in Phase 1 (no thumbnail caller) yet live API in a reviewed file.
**Fix:** Allocate with headroom (fixed 16 KB in PSRAM), or `realloc` to `fb->len` when it exceeds the estimate instead of failing.

### WR-12 (new): Receive paths never validate the packet-type byte — any CRC-valid packet is executed regardless of direction

**File:** `src/command_handler.cpp:673-680` and `src/command_sender.cpp:309-313` (receive paths), `src/command_protocol.cpp:113` and `src/command_protocol.cpp:210` (deserializers parse `type` but never check it)
**Issue:** The 01-06 fix made the type byte truthful on the wire (0x10 command / 0x11 response), but neither receiver enforces it. `deserializeCommand` validates only start bytes, end bytes, and CRC; `CommandHandler` would frame and *execute* a RESPONSE packet that arrives on its channel — interpreting the responseType byte as a `CameraCommand` (e.g., ACK 0x00 → unknown → NACK_INVALID sent in reply), and symmetrically `CommandSender` would treat a command packet's cmd byte as a `ResponseType`. With a single balloon/base pair this is latent, but a second station, a misconfigured unit, or Phase 2 traffic sharing the channel turns it into cross-direction command execution. The type check is the natural receive-side counterpart of the CR-01 fix and costs one comparison.
**Fix:** After framing, reject on mismatch — in `CommandHandler::processIncomingByte` (or `deserializeCommand`): `if (cmd.type != PACKET_TYPE_COMMAND) return false;`, and in `CommandSender::processIncomingByte` (or `deserializeResponse`): `if (resp.type != PACKET_TYPE_RESPONSE) return false;`. Add a harness clause feeding a valid-CRC response packet to the command-flavor receiver and asserting it produces no command.

### WR-13 (new): Balloon boot check unconditionally reports "Camera system health check failed" when the camera is healthy

**File:** `src/main_balloon.cpp:465-469`
**Issue:** The actual health-check call is commented out, leaving `if (appState.cameraActive) { // && !Camera().performHealthCheck()) {` — i.e., the condition is just "camera is active". Every boot with a working camera logs `SYS_WARNING("Camera system health check failed")` and clears `allPassed`, polluting diagnostics and masking genuine check failures (sensor/diagnostics warnings in the same report become indistinguishable). `performSystemChecks` still returns true, so behavior is unaffected — the defect is the false diagnostic.
**Fix:** Restore a real check (e.g., `if (appState.cameraActive && !Camera().isReady())`) or remove the branch entirely until `performHealthCheck` exists.

## Info

### IN-01 (carried): Dead method + comment/code mismatch in slot eviction

**File:** `src/command_sender.cpp:383-396` (`findOldestCommand` defined, never called — verified by grep), `src/command_sender.cpp:406-415`
**Issue:** The eviction comment says "evict oldest completed command" but the loop takes the first terminal slot in index order — which can be the slot backing the UI's pinned last-command state, blanking it arbitrarily.
**Fix:** Delete `findOldestCommand` or use it for eviction (prefer evicting the oldest terminal slot).

### IN-02 (carried): Dead functions in base station

**File:** `src/main_basestation.cpp:1110-1112` (`sendHTML`), `src/main_basestation.cpp:1114-1123` (`updateLED`)
**Issue:** Neither is called (verified by grep); the physical LED blinks inside `updateStatus()` every 5 s regardless of link state, so the IN-03 "LED truth" design exists only in the web UI.
**Fix:** Delete both, or wire `updateLED()` into `loop()` driven by the same `connected/linkText` truth the web UI uses.

### IN-03 (carried): Hardcoded E32 pin numbers duplicate `sensor_pins.h` macros

**File:** `src/main_balloon.cpp:375`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` inlines values that exist as `LORA_RX_PIN/LORA_TX_PIN/LORA_M0_PIN/LORA_M1_PIN/LORA_AUX_PIN` (`include/sensor_pins.h:33-37`, verified to match today). A pin change in the header silently diverges from the driver wiring.
**Fix:** Pass the macros (and `LORA_BAUD_RATE`).

### IN-04 (carried): Interval bounds duplicated as literals

**File:** `src/command_handler.cpp:529`
**Issue:** `intervalMs < 1000 || intervalMs > 3600000` re-states `AUTO_CAPTURE_MIN_INTERVAL_MS`/`AUTO_CAPTURE_MAX_INTERVAL_MS` by literal even though `auto_capture.h` is included and its comment describes the mirroring. Divergence risk.
**Fix:** Use the constants.

### IN-05 (carried): Duplicate-valued compat aliases in shared enums

**File:** `include/common_types.h:26-64`
**Issue:** `PacketType` has `GPS == TELEMETRY == 0x02`, `CAMERA_THUMB == GPS_DATA == 0x03`, `ACK == COMMAND_ACK == 0x06`, etc.; `PacketPriority` similarly collides (`EMERGENCY == PRIORITY_NORMAL == 1`). Switches on these enums can silently take the wrong case.
**Fix:** Drop the compatibility aliases or move them to clearly-typed separate enums; at minimum document which value is authoritative on the wire.

### IN-06 (carried): Start-byte hunt cannot resync on 0xAA 0xAA 0x55; duplicated `validatePacket` implementations

**File:** `src/command_handler.cpp:633-640`; `src/command_sender.cpp:270-277`; `src/command_handler.cpp:691-697` vs `src/command_sender.cpp:460-466`
**Issue:** While expecting 0x55 at `receiveIndex == 1`, a 0xAA resets to 0 instead of restarting the hunt with the current byte retained, so a preamble byte of 0xAA immediately before a real start pair loses frame sync (CRC makes this safe but lossy). The two `validatePacket` methods are identical copies.
**Fix:** In the else branch, check `byte == CMD_START_BYTE1` and keep `receiveIndex = 1` (retain the byte); hoist the shared validator into `CommandProtocol`.

### IN-07 (carried): `lastReceiveTime` never updated on the single-byte read path

**File:** `include/e32_lora.h:91`
**Issue:** The `uint8_t read()` overload (the one both protocol modules actually use) does not touch `lastReceiveTime`, so `printStatus()` reports a bogus "Last RX" forever.
**Fix:** Update `lastReceiveTime = millis()` in the single-byte overload, or drop the field if unused.

### IN-08 (carried): Dead protocol declarations misdocument the wire format; `ResponseStatusData` uses native-endian memcpy against the protocol's big-endian convention

**File:** `include/command_protocol.h:110-173` (`PayloadSetResolution` … `PayloadAutoCaptureEnable`, `ResponseCaptureData` — all unused, verified by grep); `src/command_handler.cpp:677` (`pendingCommand.receivedTime` written, never read); `src/command_protocol.cpp:237-286` (`createACK`/`createStatus` never called); `src/command_handler.cpp:586-587`
**Issue:** The dead payload structs imply native-endian struct memcpy, which the handlers explicitly avoid (`handleAutoCaptureEnable` uses big-endian `readUint32`). `handleGetStatus` *does* memcpy `ResponseStatusData` (with padding) little-endian while every other multi-byte protocol field is big-endian — inconsistent, and currently unexercised end-to-end because the base station UI never issues `GET_STATUS` (no `/get-status` route; `commandDisplayName` includes "Get Status" only for queue rows). The first consumer that parses it per the struct will read garbage on any big-endian/hostile host, and the endianness split will bite when a real parser lands.
**Fix:** Delete the unused structs, the unused field, and the dead `createACK`/`createStatus` helpers (or rewrite as byte-layout documentation); define `ResponseStatusData`'s wire encoding explicitly (fixed offsets, big-endian via `writeUint16/writeUint32`) and serialize field-by-field.

---

_Reviewed: 2026-08-18T11:06:36Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

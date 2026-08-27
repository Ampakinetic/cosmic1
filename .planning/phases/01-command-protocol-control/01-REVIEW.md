---
phase: 01-command-protocol-control
reviewed: 2026-08-27T20:39:30Z
depth: standard
files_reviewed: 23
files_reviewed_list:
  - include/auto_capture.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/image_tx_manager.h
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
  - src/auto_capture.cpp
  - src/camera_manager.h
  - src/camera_manager.cpp
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - src/sd_storage.cpp
findings:
  critical: 0
  warning: 3
  info: 11
  total: 14
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-27T20:39:30Z
**Depth:** standard
**Files Reviewed:** 23
**Status:** issues_found

## Summary

Adversarial standard-depth review of the 23 Phase 1 source files (command protocol core, LoRa driver, auto-capture, camera manager, image TX/RX managers, SD storage, both firmware mains, build config, and the protocol round-trip script), reflecting the tree as of tasks 01-20 through 01-24.

The protocol and transfer core holds up under adversarial tracing. Verified correct this cycle: endianness pairing for AUTO_CAPTURE_ENABLE (BE32) and SET_EVENT_THRESHOLDS (BE16) on both ends; WR-12 type-dispatch before body arithmetic in both framers; length-driven framing with end-marker-at-expected-position plus the inter-byte resync fix; CRC16 Modbus; the fixed `storedToSd` ownership guard (sd_storage.cpp:315-316); the second-frame drop guard (command_handler.cpp:887-893); control-byte JSON escaping (main_basestation.cpp:3137-3151); critical-battery camera disable (main_balloon.cpp:863-867); NACK_BUSY defer for IMAGE_WINDOW_REQUEST; chunk offset/length math with no underflow in `acceptChunk` and `serviceWindowChunk`; manifest geometry validation; SD filename construction from `%05u` ids only (no traversal surface); duplicate-chunk idempotency; and the class-ranked TX eviction with mid-service protection.

No Critical findings. Three Warnings: a full-buffer allocation failure drops the thumbnail too (loss of the designed degradation ladder), the still-unfixed inverted camera health check that reports every healthy camera as failed at boot, and a uint16 truncation of the ACK counter that mis-orders the link-truth LED at each 65536-ACK wrap. Eleven Info items cover stale comments contradicting the 19-byte beacon body, dead declarations/state in the camera manager, an uncanceled tracked command on re-manifest restart, an ungated per-beacon debug print, and dead scaffolding paths in main_balloon.

## Narrative Findings (AI reviewer)

## Warnings

### WR-01: Full-buffer PSRAM allocation failure drops the thumbnail push too

**File:** `src/image_tx_manager.cpp:276-283`
**Issue:** In `enqueueCapture`, when `ps_malloc` for the full-image copy fails, the function logs "dropped" and returns immediately — before the thumbnail branch at lines 294-326 ever runs. The capture therefore produces nothing on the airlink: no thumbnail push and no full announcement. This is inconsistent with the designed degradation ladder two branches below, where an oversize (unarmable) full explicitly keeps the thumbnail ("skipping full transfer (thumbnail still pushes)", lines 264-270) and a failed thumbnail allocation explicitly keeps the full ("pushing full image only", lines 302-313). PSRAM exhaustion is exactly the condition under which the thumbnail (a QQVGA ~2-8 KB copy versus the full-frame copy) had the best chance of surviving, so the early return discards the cheapest, highest-value payload precisely when memory is scarce. The base sees no manifest at all for the capture.
**Fix:** Instead of returning, fall through to the thumbnail path with the full marked unavailable:
```cpp
entry.fullBuffer = (uint8_t*)ps_malloc(img.length);
if (!entry.fullBuffer) {
    Serial.printf("ImageTx: PSRAM allocation failed for image %u full buffer (%u bytes); "
                  "full dropped, thumbnail still pushes\n",
                  imageId, static_cast<unsigned>(img.length));
    entry.fullBuffer = nullptr;
    entry.fullLength = 0;
    entry.fullTotalChunks = 0;
    // fall through to the thumbnail branch below (state lands via
    // completedThumbState -> THUMB_PUSHED when nothing full is armable)
} else {
    memcpy(entry.fullBuffer, img.buffer, img.length);
    entry.fullLength = img.length;
    entry.fullCrc32 = esp_rom_crc32_le(0, entry.fullBuffer, entry.fullLength);
    entry.fullTotalChunks = chunksForSize(entry.fullLength);
}
```
(The guard at lines 331-334 already handles the both-empty case, so no empty slot is occupied if the thumbnail also fails.)

### WR-02: Camera health check is inverted — every active camera is reported failed at boot

**File:** `src/main_balloon.cpp:573-577`
**Issue:** The real condition is commented out (`// && !Camera().performHealthCheck())`), leaving `if (appState.cameraActive)` alone. Any boot with a working, active camera logs `SYS_WARNING("Camera system health check failed")`, clears `allPassed`, and ends with "Some system checks failed - continuing with reduced functionality". The negation was lost when the method call was stubbed out: the branch now fires precisely when the camera is healthy and never when it is absent (an absent camera skips the branch entirely, so a genuinely missing camera produces no camera warning at all). Operators chasing boot warnings on every flight will learn to ignore the channel, and the inverted signal masks the real failure case. (Carried from the prior review cycle as IN-10; still unfixed.)
**Fix:**
```cpp
// Check camera system (if active) — honest absent branch, no false failure
if (appState.cameraActive) {
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr) {
        SYS_WARNING("Camera system health check failed (no sensor handle)");
        allPassed = false;
    }
} else {
    SYS_WARNING("Camera inactive - camera health check skipped");
}
```
(or simply delete the block until a real health check exists.)

### WR-03: ACK-counter truncation to uint16 mis-orders link-truth LED detection at each wrap

**File:** `src/main_basestation.cpp:2206-2210`
**Issue:** `ackedAtLastPoll` is `uint16_t` (line 81) but is compared against a truncating cast of the `uint32_t` `commandsAcked`: `if (static_cast<uint16_t>(acked) > appState.ackedAtLastPoll)`. Each time the 32-bit counter crosses a multiple of 65536 between two polls, the truncated value wraps below the previous sample, the comparison is false, and that poll's ACK arrival never refreshes `lastAckTime` — the IN-03 "LED truth" link-staleness signal then runs stale until the next ACK lands after the boundary. Impact is small in absolute terms (one missed refresh per 65536 ACKed commands, only when a wrap lands between polls), but it is a genuine type-narrowing logic defect in the LED-truth path, and the fix is free.
**Fix:** Widen the poll snapshot member to match the counter:
```cpp
// appState member (main_basestation.cpp:81)
uint32_t ackedAtLastPoll;

// poll site (lines 2206-2210)
uint32_t acked = CmdSender().getCommandsAcked();
if (acked > appState.ackedAtLastPoll) {
    appState.lastAckTime = millis();
}
appState.ackedAtLastPoll = acked;
```

## Info

### IN-01: Stale comment claims a 17-byte beacon body — the constant is 19

**File:** `src/command_sender.cpp:364`
**Issue:** `bodyLen = IMG_TELEMETRY_BEACON_BODY_SIZE;   // fixed 17-byte body` — `IMG_TELEMETRY_BEACON_BODY_SIZE` is 19 (`include/image_protocol.h:171`). The comment contradicts the code and will mislead the next person touching the beacon length checks (the 01-22 grow-from-17-to-19 change updated the constant but not this comment).
**Fix:** Update the comment to "fixed 19-byte body" (or delete it — the constant is self-describing).

### IN-02: Same stale "17" in the round-trip verification script

**File:** `scripts/verify_protocol_roundtrip.mjs:778`
**Issue:** `expectedTotal = CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4; // forced 17` — the expression correctly uses the constant (19); only the trailing comment is stale.
**Fix:** Change the comment to `// forced 19` or remove it.

### IN-03: Dead private methods and dead buffer members in CameraManager

**File:** `src/camera_manager.h:88-91` (members at 74-75)
**Issue:** `resizeImage`, `updateCameraSettings`, and `adaptiveBrightnessControl` are declared but defined nowhere in `src/camera_manager.cpp` and called nowhere — dead declarations. The `imageBuffer`/`imageBufferSize` members are initialized in the constructor, freed in `releaseImageBuffers`, and accounted in `getMemoryUsage`, but never written by any capture path, so the free/account branches are unreachable dead state (imageBuffer is always nullptr).
**Fix:** Delete the three declarations and the two members plus their dead free/account sites (or implement them if they are planned Phase 3 work — the header gives no such indication).

### IN-04: `previousMode` saved in `enterConfigMode` but never used; exit never restores entry mode

**File:** `src/e32_lora.cpp:610-622`
**Issue:** `E32Mode previousMode = currentMode;` (line 612) is written and never read — `exitConfigMode` unconditionally returns to `MODE_NORMAL`, so a caller that entered config mode from POWER_SAVE or WAKEUP is silently dropped to NORMAL. (Carried from the prior cycle's IN-03; the unused-local half is still present.)
**Fix:** Store the entry mode as a member (`E32Mode modeBeforeConfig;` set in `enterConfigMode`) and have `exitConfigMode` `setMode(modeBeforeConfig)`; or delete the dead local if NORMAL-always is the accepted contract.

### IN-05: `sendResponse` uses a hardcoded 128-byte buffer against a size-less serializer

**File:** `src/command_handler.cpp:791`
**Issue:** `uint8_t buffer[128];` is handed to `CommandProtocol::serializeResponse(response, buffer, length)`, which takes no buffer-capacity parameter (`include/command_protocol.h:246`). The call is safe today (max response packet is 65 B), but the 128 literal is unrelated to the protocol's own `CMD_MAX_PACKET_SIZE` (240) used by every other serialize call site in the file pair — a future response type grown past 128 would overflow the stack buffer with no API to catch it.
**Fix:** At minimum use the shared constant: `uint8_t buffer[CMD_MAX_PACKET_SIZE];`. Better: add a capacity parameter to `serializeResponse` (and siblings) and bounds-check internally.

### IN-06: Camera init partial failure leaves the driver initialized and wedges retries

**File:** `src/camera_manager.cpp:116-122`
**Issue:** If `esp_camera_init` succeeds but `esp_camera_sensor_get()` returns null, `initCamera` returns false without `esp_camera_deinit()`. `initialized` stays false, so the only deinit site — `end()` at lines 86-93, gated on `initialized` — never runs, and a retrying `begin()`/`reinitialize()` calls `esp_camera_init` again on an already-initialized driver, which fails, leaving the camera dead until reboot. (Carried from the prior cycle as IN-09; still unfixed. Reachability is low — `sensor_get` failing after a successful init is rare — hence Info.)
**Fix:** In the `!s` branch call `esp_camera_deinit();` before `return false;`.

### IN-07: Balloon E32 `begin()` call bypasses `sensor_pins.h` with magic pin literals

**File:** `src/main_balloon.cpp:473-474`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` duplicates the pin map as bare literals — 48/14/19/20/21 are `LORA_RX_PIN`/`LORA_TX_PIN`/`LORA_M0_PIN`/`LORA_M1_PIN`/`LORA_AUX_PIN` from `include/sensor_pins.h:54-58` (values verified to match today). Meanwhile `initializeBoard()` (lines 605-610) drives M0/M1/AUX through the named macros, so the same physical pins are configured through two independent sources that can drift apart silently. This project's own GPIO-35 bootloop incident (documented in sensor_pins.h) is exactly the failure class a duplicated pin list invites. (Carried from the prior cycle as IN-11; still unfixed.)
**Fix:** `E32LoRaModule().begin(loraSerial, LORA_RX_PIN, LORA_TX_PIN, LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN, 9600);` (and a named baud constant if one exists).

### IN-08: Legacy periodic packets are created into a queue nothing drains

**File:** `src/main_balloon.cpp:282-292 and 956-1027` (send loop commented out at 805-812)
**Issue:** `sendTelemetryData`/`sendHeartbeatPacket`/`sendStatusReport` run every 5/30/60 s and call `PacketMgr().create*Packet(...)`, which enqueues into PacketHandler's bounded 16-slot queue (oldest-evicting, verified in `src/packet_handler.cpp:351-401`) — but the only transmit/drain loop (`processCommunications`) is fully commented out, and Phase 2 replaced this traffic with the 0x14 telemetry beacon in `ImageTx()`. Net effect: continuous dead work that silently discards every packet via oldest-eviction while logging "Telemetry packet created" as if it meant something. Memory-safe (bounded queue), but misleading and wasteful.
**Fix:** Delete the three periodic send functions and their `shouldSend*` gates (and the timing constants), or wire `getBufferedPacket` draining if the legacy path is ever meant to return.

### IN-09: Dead event-handler scaffolding and never-invoked utilities in main_balloon

**File:** `src/main_balloon.cpp:1096-1179` (plus `handleSystemError` 1045-1057, `checkSystemHealth` 1072-1090, `configureSystem` 506-549)
**Issue:** `onSystemEvent` has an empty body over a fully commented-out dispatch; `onEmergencyTriggered` (whose camera-disable content is now duplicated for real at lines 863-867/874-878), `onModeChanged`, and `onFlightPhaseChanged` are defined but never registered or called; `handleSystemError` and `checkSystemHealth` have no call sites; `configureSystem` is a wall of commented-out configuration that always returns true. Roughly 150 lines of scaffolding that reads as live behavior but never runs.
**Fix:** Delete the dead handlers and utilities (the real emergency behavior already lives in `processPowerManagement`), and reduce `configureSystem` to its actual content or remove the call.

### IN-10: Re-manifest restart orphans an in-flight tracked window request

**File:** `src/image_rx_manager.cpp:549-565`
**Issue:** The same-(id,kind) re-manifest path does `*t = ImageRxTransfer{}` (line 563), which silently discards the slot's `windowRequestSeq` without `CmdSender().cancelCommand(...)` — the WR-04 cancel discipline is applied at `finalizeTransfer`/`finalizeIncomplete` (lines 812-814, 843-845) but not at restart. A window request still PENDING/SENT for the old incarnation keeps occupying one of the 5 command-table slots until its own D-05/D-07 terminal state, and if it later ACKs, the balloon arms a window whose chunks then land in the restarted slot (benign — geometry is identical — but untracked). Bounded by the command timeout budget, hence Info.
**Fix:** Before the reset, mirror the finalize sites:
```cpp
if (t->windowRequestSeq != 0) {
    CmdSender().cancelCommand(t->windowRequestSeq);
}
releaseSlotWork(*t);
bool wasPull = t->pullActive;
*t = ImageRxTransfer{};
t->pullActive = wasPull;
```

### IN-11: Ungated per-beacon debug print on the base's UART0

**File:** `src/image_rx_manager.cpp:357`
**Issue:** `Serial0.printf("[BCNRX] seq=%u accepted\n", (unsigned)b.seq);` runs for every accepted telemetry beacon frame unconditionally — it sits outside the `if (DEBUG_IMAGE_RX)` block that gates the very next statement (lines 366-373). At the 5 s beacon cadence this is a permanent one-line-per-5-s debug artifact on the base's bridge serial, unlike every neighboring diagnostic which is flag-gated.
**Fix:** Move the print inside the `if (DEBUG_IMAGE_RX)` block (or delete it now that the bench session it served — "balloon-no-data-oled-blank" — is resolved).

---

_Reviewed: 2026-08-27T20:39:30Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

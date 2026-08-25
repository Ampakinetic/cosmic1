---
phase: 01-command-protocol-control
reviewed: 2026-08-25T02:08:01Z
depth: standard
files_reviewed: 22
files_reviewed_list:
  - include/auto_capture.h
  - include/command_handler.h
  - include/command_sender.h
  - include/command_protocol.h
  - include/e32_lora.h
  - include/image_protocol.h
  - include/image_tx_manager.h
  - include/image_rx_manager.h
  - include/sensor_pins.h
  - src/auto_capture.cpp
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/camera_manager.cpp
  - src/camera_manager.h
  - src/e32_lora.cpp
  - src/image_tx_manager.cpp
  - src/image_rx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
findings:
  critical: 1
  warning: 2
  info: 9
  total: 12
status: issues_found
---

# Phase 01: Code Review Report (Round #8)

**Reviewed:** 2026-08-25T02:08:01Z
**Depth:** standard
**Files Reviewed:** 22
**Diff Base:** da84250 (round-#7 fix commits 70543c5..02142e1 verified in this pass)
**Status:** issues_found

## Summary

Adversarial standard-depth review of the full Phase 01/02 chain: balloon TX
(auto capture, command handler/protocol, camera, E32, image TX), base RX
(command sender, image RX, base main/web server), protocol headers, build
config, and the wire harness. All prior-round (round #7) fixes were
re-verified at root and are confirmed FIXED — the fixer must NOT re-apply
them (list below). This round found one new Critical defect in the command
dispatch gate, one new Warning in the chunk-push failure path, and carried
forward the remaining prior findings (WR-03, IN-02..IN-07), all re-confirmed
present at the cited lines.

## Prior-Round Fixes Verified FIXED (do not re-apply)

| Prior ID | Root-cause fix verified | Evidence |
|---|---|---|
| CR-01 | imageKind byte in `ImageChunkBody`; stamped at both TX sites; validated at deserialize; kind-exact slot routing | include/image_protocol.h:166-171; src/image_tx_manager.cpp:580, :860; src/command_protocol.cpp:464-467; src/image_rx_manager.cpp:314-339 |
| CR-02 | `announceFullManifest` success-gated, bounded by `IMG_MANIFEST_MAX_ATTEMPTS`, park-and-free keeps thumbBuffer | src/image_tx_manager.cpp:623-684 |
| CR-03 | `THUMB_MAX_BYTES` (8192) clamp + stale-frame drain before capture | src/camera_manager.cpp:12, :357-366, :393-407 |
| WR-01 | `pushThumbManifest` success-gated | src/image_tx_manager.cpp:538-564 |
| WR-02 | two-pass sensor-overflow scan before capture | src/camera_manager.cpp:346-368 |
| WR-04 | defer-aware `windowRequestSeq` accounting + cancel-before-next + post-terminal cancels | src/image_rx_manager.cpp:134-138, :160-166, :235-243, :806-810, :838-841 |
| WR-05 | NACK_BUSY deferral scoped to IMAGE_WINDOW_REQUEST only | src/command_sender.cpp:427-439 |
| IN-01 | retry-ordinal labels ("retry %d/%d") | src/command_sender.cpp:561-566 |

## Critical Issues

### CR-04: Camera-ready gate in executeCommand blocks image window service and status while the radio and ImageTx remain alive

**File:** `src/command_handler.cpp:139-145` (with `src/main_balloon.cpp:858-867`, `src/camera_manager.h:134`, `src/camera_manager.cpp:87-91`)
**Issue:** `executeCommand` rejects EVERY command with `NACK_BUSY "Camera not ready"` when `camera->isReady()` is false (`isReady()` is just `initialized`). On low battery (`batteryVoltage < BATTERY_LOW_THRESHOLD`), `main_balloon.cpp:864` calls `Camera().enableCamera(false)` → `end()` → `esp_camera_deinit()` + `initialized = false`. From that moment:

1. `IMAGE_WINDOW_REQUEST` (command_handler.cpp:182-183) is refused — yet window service needs only the ImageTx PSRAM buffers and the E32 radio, both still fully operational (`ImageTx().process()` keeps running at main_balloon.cpp:895 and keeps transmitting 0x14 beacons). Every full image already announced becomes unretrievable for the entire low-battery window — exactly the final images an operator needs to recover before power is lost. The stranded data is then destroyed by power-off or TTL eviction (IMG_ENTRY_TTL_MS). NACK_BUSY tells the base "retry later," so each pull burns its full retry budget before terminal-failing.
2. `GET_STATUS` (line 179-180) is also refused, so the base's periodic status poll (src/main_basestation.cpp:1990-1995) fails permanently and the dashboard's last-command row latches a perpetual failure while the balloon is still beaconing.

This is a data-loss-risk behavior gap, not just degradation: the refusal reason (camera) is unrelated to the resource the command needs.
**Fix:** Remove the global gate and apply `isReady()` checks only where camera hardware is actually touched:

```cpp
// executeCommand — replace the blanket gate with dispatch, then guard
// inside the camera-touching handlers:
case CameraCommand::CAPTURE_NOW:
case CameraCommand::SET_RESOLUTION:
case CameraCommand::SET_QUALITY:
    /* ... */ {
    if (!camera->isReady()) {
        result.responseType = ResponseType::NACK_BUSY;
        strncpy(result.message, "Camera not ready", sizeof(result.message) - 1);
        commandsFailed++;
        return result;
    }
    // ... existing handler body
}
// IMAGE_WINDOW_REQUEST, GET_STATUS, SET_EVENT_THRESHOLDS,
// AUTO_CAPTURE_ENABLE/DISABLE: no camera gate (AutoCap/ImageTx/cached
// settings only).
```

## Warnings

### WR-03 (remaining from round #7): Manual capture never advances the auto-capture baseline — interval capture can fire moments after a manual one

**File:** `src/command_handler.cpp:200-237` with `src/auto_capture.cpp:175-178`
**Issue:** `handleCaptureNow` captures and allocates an image id but never touches `AutoCap`'s `lastCaptureTime`. `AutoCapture::process` fires when `millis() - lastCaptureTime >= intervalMs`, so a manual capture does not reset the interval baseline. If the operator triggers a manual capture just before an interval deadline, the balloon captures twice in quick succession (duplicate frame, doubled TX queue pressure); there is no suppression path. Re-confirmed present this round.
**Fix:** Add `void markCaptureBaseline() { lastCaptureTime = millis(); }` to AutoCapture and call it from `handleCaptureNow` after a successful `captureImage()` (before enqueueing the ImageTx entry), so D-27/D-28 spacing semantics treat manual captures as baseline-advancing captures.

### WR-08 (new): Chunk cursors advance on failed transmit; a failed tail chunk can mark an entry SERVED before every byte was offered

**File:** `src/image_tx_manager.cpp:601` (`pushThumbChunk`) and `src/image_tx_manager.cpp:881-895` (`serviceWindowChunk`)
**Issue:** Both push paths execute `serializeChunk(...) && lora->transmit(...)` and then advance the cursor unconditionally (`entry.nextThumbChunk++` / `entry.windowNextIndex++`) even when `ok == false`. Each failed transmit becomes a permanent hole for that pass whose only recovery is a base-side stall timeout (IMG_WINDOW_STALL_MS) plus a re-request round trip — a burst of E32 transmit failures converts one push into many multi-second heal cycles. Worse, in `serviceWindowChunk`, when the FAILED chunk is the last of a tail-reaching window (`windowNextIndex >= windowStart + windowCount`), the entry is marked `windowArmed = false` and `SERVED` (lines 883-895) — a preferred eviction candidate — despite the final chunk never leaving the balloon. Under queue pressure the subsequent heal re-request can hit `UNKNOWN_IMAGE` after eviction and the transfer terminal-fails INCOMPLETE, even though the data was resident the whole time.
**Fix:** On `!ok`, do not advance the cursor; retry the same index on the next `process()` pass with a small same-index bound (e.g., 3 attempts) before skipping, and only evaluate the SERVED transition on a successful final-chunk transmit:

```cpp
if (ok) {
    entry.windowNextIndex++;
    if (entry.windowNextIndex >= entry.windowStart + entry.windowCount) {
        entry.windowArmed = false;
        if (/* tail window reached */)
            entry.state = ImageTxEntryState::SERVED;
    }
} else {
    if (++entry.windowFailCount >= 3) {
        entry.windowFailCount = 0;
        entry.windowNextIndex++;   // skip after bounded retries — heal path owns it
    }
    entry.lastActivityMs = millis();
    return false;
}
```

## Info

### IN-08 (new): Re-manifest restart clears window context without cancelling the still-tracked window request

**File:** `src/image_rx_manager.cpp:549-565`
**Issue:** `startTransfer`'s same-(id,kind) restart does `*t = ImageRxTransfer{}`, zeroing `windowRequestSeq`/`windowActive`, while the previously issued `IMAGE_WINDOW_REQUEST` may still be PENDING/SENT in CmdSender. A stale retry can later arm a pre-restart span concurrently with the fresh request the window driver will issue. Harmless today (last-arm-wins in `handleWindowRequest`, and `acceptChunk` validates id/kind/index so stale-span chunks still count), but the accounting mismatch is avoidable.
**Fix:** On restart, cancel the outstanding tracked request the same way finalize does (the existing cancel path used at image_rx_manager.cpp:806-810/838-841) before zeroing the slot.

### IN-02 (remaining): Camera health check always reports failure when the camera is active

**File:** `src/main_balloon.cpp:573-577`
**Issue:** The check body is commented out (`// && !Camera().performHealthCheck()`), so whenever `appState.cameraActive` is true the log prints "Camera system health check failed" and `allPassed = false` unconditionally — a healthy camera is reported as failed on every health pass.
**Fix:** Either implement a real check or remove the camera branch (and the misleading warning) until one exists.

### IN-03 (remaining): Dead store `previousMode`

**File:** `src/e32_lora.cpp:612`
**Issue:** `previousMode` is assigned and never read.
**Fix:** Delete the variable, or use it to report mode-transition failures in the log.

### IN-04 (remaining): `currentMode` committed before AUX verifies the mode actually took

**File:** `src/e32_lora.cpp:158`
**Issue:** The cached mode is updated before AUX confirmation, so a failed mode switch leaves the cache claiming the requested mode while the module is elsewhere.
**Fix:** Set `currentMode` only after `waitForAux()` succeeds (mirror the pattern used at the failure branch).

### IN-05 (remaining): `transmitToAddress` unused, VLA, no initialized guard

**File:** `src/e32_lora.cpp:272-290`
**Issue:** Unused public method that builds a stack VLA from a runtime size and transmits without checking `initialized`.
**Fix:** Remove it, or guard with `initialized` and replace the VLA with a fixed `CMD_MAX_PACKET_SIZE`-bounded buffer.

### IN-06 (remaining): Dead code `findOldestCommand`

**File:** `src/command_sender.cpp:493-506`
**Issue:** Never called; duplicates slot-recycling logic that `findFreeSlot` already owns.
**Fix:** Delete it.

### IN-07 (remaining): Stale "fixed 17-byte body" comment

**File:** `src/command_sender.cpp:329`
**Issue:** The wire body is 19 bytes; the comment says 17.
**Fix:** Update the comment (or derive the constant so comment and code cannot drift).

### IN-09 (out-of-plan change, no defect found): GPS pin/baud change in sensor_pins.h

**File:** `include/sensor_pins.h:35-38`
**Issue:** `GPS_TX_PIN 35` and `GPS_BAUD_RATE 38400` changed after the plan's file scope was cut (operator-requested, per handoff). Verified internally consistent: `src/sensor_manager.cpp:137` and `src/main_balloon.cpp:615` consume the same macros, and the values match the hardware-proven config documented in `src/test_lora_balloon.cpp:42-44`. Note only: `src/test_minimal_hardware.cpp:17-18` still hardcodes the obsolete 45/46 pins — harmless while that test sketch stays out of the default build environment (platformio.ini default envs unchanged).
**Fix:** None required; optionally update the minimal-hardware test sketch pins the next time it is used on hardware.

---

_Reviewed: 2026-08-25T02:08:01Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

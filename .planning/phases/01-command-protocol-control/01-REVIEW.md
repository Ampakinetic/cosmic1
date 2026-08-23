---
phase: 01-command-protocol-control
reviewed: 2026-08-23T02:07:31Z
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
  critical: 0
  warning: 6
  info: 10
  total: 16
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-23T02:07:31Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Adversarial review of the Phase 1 command-protocol implementation at standard depth: full reads of all 17 in-scope files plus cross-referencing of `balloon_config.h`, `image_protocol.h`, `sensor_pins.h`, `camera_pins.h`, `base_station_config.h`, `sd_storage.cpp`, and `image_tx_manager.cpp` for boundary verification.

The core protocol paths are sound. Verified clean (do not re-litigate): length-driven framing with type-dispatch bounds in both receivers (max expected frame 216 bytes vs 256/240-byte buffers — no overflow reachable from the wire); CRC-validated serialize/deserialize round-trips for command/response/chunk/manifest/beacon; the in-page `section#capture` delegated submit handler (defaultPrevented guard, id skip, field names match server argument parsing); ImageTxManager memcpy-copies image bytes at enqueue (no dangling pointer from `freeCurrentImage`); the E32 0xC1/0xC0 register frames with echo verification, `drainConfigRx`, and the `ensureLinkConfig` fail-open path; the SPI-to-SD_MMC 1-bit pin transition with the documented STATUS_LED/SD-CLK GPIO39 ownership order; strict numeric id parsing in `handleImage`/`handleGalleryDetail` (no path traversal); XSS avoided via `textContent`/`createElement` throughout the new UI script; pendingCommandCount underflow guards; STATUS struct size consistency (32 bytes, dataLength-gated).

What remains: six warnings, dominated by inert safety logic on the balloon (dummy battery data keeps the emergency and low-power paths permanently unreachable, an inverted camera health check, and a real GPIO4 double-assignment between the battery ADC and camera SCCB SDA), plus a build-config gap and two latent logic defects. Ten info items cover dead code, stale comments, and sequence-wraparound edges. Known residual G-01-5 (thumbnail loss during push bursts) is already routed elsewhere and is intentionally not reported here.

## Narrative Findings (AI reviewer)

No Critical issues found. The wired protocol surface was probed hardest (frame bounds, CRC bypass, type confusion, echo forgery, buffer lifetimes) and held up; the defects below are in surrounding system logic and configuration.

## Warnings

### WR-01: Dummy battery data permanently disables emergency and low-power safety logic

**File:** `src/main_balloon.cpp:726-727` (also `804-805`, `915-918`)
**Issue:** `updateSystemState()` (line 727) and `processPowerManagement()` (line 805) both construct `PowerData powerData = {3.7f, 0.1f, 85, millis(), true};` instead of reading the real power manager. Because percentage is pinned at 85, `processPowerManagement()`'s battery-critical `triggerEmergency()` branch and the low-battery camera-disable branch can never execute — a live-looking safety feature that silently does nothing on a real flight while the battery drains. Line 916-918 repeats the pattern in `sendTelemetryData()` (called from the loop at line 283), queueing telemetry packets with a hardcoded 3.7 V / 85%. Real voltage demonstrably exists elsewhere (`PowerMgr().getBatteryVoltage()` feeds the Phase 1 telemetry beacon and OLED), so the data is available and simply not wired in.
**Fix:**
```cpp
// in updateSystemState() and processPowerManagement():
PowerData powerData = PowerMgr().getData();   // or getPowerData() per power_manager.h

// in sendTelemetryData():
telemetryData.batteryVoltage    = PowerMgr().getBatteryVoltage();
telemetryData.batteryPercentage = PowerMgr().getBatteryPercentage();
```
If the dummy is intentionally deferred, gate the dead branches behind a `// TODO(phase-2)` comment and make `performSystemChecks()` warn at boot that power protection is INERT, so operators do not believe battery protection is active.

### WR-02: Inverted camera health check flags a healthy camera as failed

**File:** `src/main_balloon.cpp:574-577`
**Issue:** In `performSystemChecks()`:
```cpp
if (appState.cameraActive) {
    SYS_WARNING("Camera system health check failed");
    allPassed = false;
}
```
The real check is commented out and the condition is inverted: the warning fires precisely when the camera is healthy (`cameraActive == true`) and stays silent when it is broken. Every boot with a working camera logs a spurious "Camera system health check failed" and sets `allPassed = false`. No functional damage follows today (the function returns `true` regardless and the caller at line 231 proceeds), but the diagnostic is exactly backwards and `allPassed` is dead-weight, which will mask real failures the day the return value is honored.
**Fix:** Restore a substantive check (or drop the branch):
```cpp
if (appState.cameraActive && !Camera().isInitialized()) {
    SYS_WARNING("Camera system health check failed");
    allPassed = false;
}
```
and `return allPassed;` at the end instead of the unconditional `return true`.

### WR-03: GPIO4 double-assigned — battery ADC vs camera SCCB SDA

**File:** `include/sensor_pins.h:52` (conflict with `src/camera_pins.h:301`, used at `src/main_balloon.cpp:686` and `src/power_manager.cpp:317`)
**Issue:** `sensor_pins.h:52` defines `BATTERY_SENSE_PIN 4`. The balloon build uses `CAMERA_MODEL_ESP32S3_EYE`, whose pin map sets `SIOD_GPIO_NUM 4` (camera_pins.h:301) — the SCCB/I2C SDA line to the camera sensor. `main_balloon.cpp:686` (`checkHardwareStatus`) and `power_manager.cpp:317` both call `analogRead(BATTERY_SENSE_PIN)`, re-configuring GPIO4 as an ADC input on the same pad the camera driver owns as I2C SDA. Any SCCB transaction after a battery read — and the thumbnail path calls `setFrameSize()`/restore on every capture — races the 1 Hz ADC read. Worse, the header's own "No conflicts detected" comment (sensor_pins.h:83-86) lists camera pin 4 in the camera set but omits pin 4 from the sensor list, so the documented invariant is false in the very file that asserts it. It may appear to work on the bench (pin re-attach behavior is core-version dependent), but this is an undocumented hardware collision.
**Fix:** Move the battery sense to a genuinely free pin (e.g. one of the unused GPIOs in the sensor_pins.h comment inventory) and update both the `#define` and the conflict-list comment; or, if GPIO4 must stay, disable the camera SIOD override via a custom camera pin struct and route SCCB to dedicated I2C pins. At minimum correct the sensor_pins.h comment so the conflict is documented rather than denied.

### WR-04: Production basestation env missing `board_build.partitions`

**File:** `platformio.ini:259-314` (`[env:esp32-s3-basestation]`)
**Issue:** Every other application env sets `board_build.partitions = partitions.csv` (devkitc-1 at line 28, balloon at line 110 — the partition file whose comment promises "3MB APP space as required"). The basestation env — the one carrying the ~57 KB HTML footer, gzipped Leaflet, gallery, and growing web surface — has no `board_build.partitions`, so it falls back to the board default partition table (1.2 MB factory app on the stock 16 MB profile). It builds today, but the env is inconsistent with its siblings and will fail with `app partition insufficient` as the web assets grow, in the env least likely to be size-tested.
**Fix:** Add to `[env:esp32-s3-basestation]`:
```ini
board_build.partitions = partitions.csv
```

### WR-05: `exitConfigMode()` ignores the saved previous mode; `previousMode` is dead

**File:** `src/e32_lora.cpp:591-609`
**Issue:** `enterConfigMode()` (line 591) saves `E32Mode previousMode = currentMode;` (line 593) and the variable is never read again. `exitConfigMode()` (line 606) has a comment claiming it restores the previous mode but unconditionally drives the pins to `MODE_NORMAL`. Today every caller happens to enter config from NORMAL, so behavior is correct by coincidence; the code and its comment disagree, and any future caller entering config from WOR-mode (fixed transmission, used by `transmitToAddress`) would be silently dropped back to transparent NORMAL — changing on-air framing behavior without any error.
**Fix:** Either thread the state through:
```cpp
bool E32LoRa::enterConfigMode() {
    previousMode = currentMode;          // member, not local
    ...
}
bool E32LoRa::exitConfigMode() {
    setPinsForMode(previousMode);
    currentMode = previousMode;          // truly restore
    ...
}
```
or delete the local and rewrite the comment to "always return to NORMAL mode" so the code tells the truth.

### WR-06: Canvas-fallback centering offsets are identically zero (dead geometry math)

**File:** `src/main_basestation.cpp:978-979`
**Issue:** In the offline Leaflet fallback (`renderCanvasFallback`):
```js
const latOff = ((h - 2 * margin) - latSpan * ((h - 2 * margin) / latSpan)) / 2;
const lonOff = ((w - 2 * margin) - lonSpan * ((w - 2 * margin) / lonSpan)) / 2;
```
`x - s * (x / s)` is identically zero for any nonzero span, so both offsets are always 0 and the "centering" at lines 982-983 does nothing. The intended formula centers the track on the non-limiting axis (e.g., when latitude span dictates the scale, the longitude extent should be centered horizontally). Effect is cosmetic — the offline plot hugs the left/top instead of centering — but this is freshly shipped code whose stated purpose does not execute.
**Fix:**
```js
const s = Math.min((h - 2 * margin) / latSpan, (w - 2 * margin) / lonSpan);
const latOff = ((h - 2 * margin) - latSpan * s) / 2;
const lonOff = ((w - 2 * margin) - lonSpan * s) / 2;
```
i.e. compute both offsets against the single chosen scale `s`, not each against its own axis ratio.

## Info

### IN-01: Dead code batch — uncalled functions and undefined declarations

**File:** `src/main_balloon.cpp:1011`, `src/command_sender.cpp:466`, `src/e32_lora.cpp:253-257`, `src/camera_manager.cpp:232,578,590`, `src/camera_manager.h:82-85,147`
**Issue:** None of these are reachable: `checkSystemHealth()` (main_balloon.cpp:1011, never called), `CommandSender::findOldestCommand()` (command_sender.cpp:466), `E32LoRa::transmitToAddress()` (e32_lora.cpp:253 — also builds a stack VLA `uint8_t buffer[length + 4]` at line 257 with no length bound, a stack-overflow hazard if ever revived), `CameraManager::captureBoth()`/`optimizeForBandwidth()`/`optimizeForQuality()` (camera_manager.cpp:232,578,590). Additionally `camera_manager.h:82-85,147` declares `resizeImage`, `updateCameraSettings`, `adaptiveBrightnessControl`, and `updateForConditions` with no definitions in camera_manager.cpp (no linker error only because they are never called). Also `processCommunications()` (main_balloon.cpp:762) is a fully commented-out body invoked every loop pass.
**Fix:** Delete the dead functions and phantom declarations, or mark them `// TODO(phase-2)`. If `transmitToAddress` is kept for WOR-mode later, replace its VLA with a heap/`std::vector` buffer bounded by a max-frame constant.

### IN-02: Stale "17-byte" comments vs actual 19-byte beacon body

**File:** `src/command_sender.cpp:329`, `scripts/verify_protocol_roundtrip.mjs:763`
**Issue:** `command_sender.cpp:329` reads `bodyLen = IMG_TELEMETRY_BEACON_BODY_SIZE;   // fixed 17-byte body` while the constant is 19 (`include/image_protocol.h:107`); the mirror comment at verify_protocol_roundtrip.mjs:763 says `// forced 17`. The code paths are correct (both use the constant); only the comments lie — the exact kind of drift that misleads the next protocol revision. (The "legacy 17-byte" references at mjs:938-961 are intentional legacy-rejection tests and are fine.)
**Fix:** Update both comments to "19-byte body".

### IN-03: Hardcoded E32 pin literals duplicate sensor_pins.h macros

**File:** `src/main_balloon.cpp:474`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` spells pins 48/14/19/20/21 as magic numbers that exactly duplicate `LORA_AUX_PIN`/`LORA_RX_PIN`/`LORA_TX_PIN`/`LORA_M0_PIN`/`LORA_M1_PIN` from sensor_pins.h. A pin repin in the header silently diverges from this call site.
**Fix:** `E32LoRaModule().begin(loraSerial, LORA_RX_PIN, LORA_TX_PIN, LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN, 9600);`

### IN-04: Raw uint16 comparisons break at sequence wraparound

**File:** `src/main_basestation.cpp:2190-2193` and `src/main_basestation.cpp:3182-3183`
**Issue:** Both the RX-LED edge detector (`acked > appState.ackedAtLastPoll`) and the auto-capture ACK latch (`entries[i].sequenceNumber > appState.autoCaptureAckSeq`) use plain `>` on uint16 sequence numbers. After the 65535→1 wrap (roughly 18 hours at one command per second), a genuinely new ACK no longer compares greater, so the RX LED skips a beat and the auto-capture status chip can go stale until the counter re-passes the latch value.
**Fix:** Use signed serial-difference comparison: `static_cast<int16_t>(acked - appState.ackedAtLastPoll) > 0` (and likewise at line 3182), which is wrap-safe for gaps under 32768.

### IN-05: `jsonEscape` does not escape control characters

**File:** `src/main_basestation.cpp:3117` (used at 3312, 3315)
**Issue:** `jsonEscape` handles only `"` and `\`. A configured SSID containing a control character (< 0x20) or newline would be emitted raw into `/api/state`, producing invalid JSON, failing the whole `JSON.parse` on the client, and tripping the poll-failure backoff (stale badge) until the SSID is changed. Low practical likelihood (operator-entered SSID), but the failure is disproportionate.
**Fix:** Add a default branch for `c < 0x20` emitting `\u00XX`, or at minimum `\n`/`\r`/`\t` escapes.

### IN-06: Asymmetric LoRa UART RX buffering — balloon keeps default 256 bytes

**File:** `src/main_balloon.cpp:474` vs `src/main_basestation.cpp:2088`
**Issue:** The base enlarged its LoRa serial RX buffer to 1024 (`LoRaSerial.setRxBufferSize(1024)`); the balloon uses `Serial2` with the 256-byte default. Balloon-bound frames are small (commands ≤ 216 bytes) and currently paced, so no loss is expected today — but the hardening was applied on only one side of the link and the balloon also receives image-window requests mid-burst.
**Fix:** `Serial2.setRxBufferSize(1024);` before `E32LoRaModule().begin(...)` in `initializeBoard()`.

### IN-07: Retried commands re-execute on the balloon (at-least-once, no sequence dedupe)

**File:** `src/command_handler.cpp` (executeCommand dispatch; no duplicate-sequence guard)
**Issue:** The retry design resends the same command with the same sequence number. The base dedupes balloon ACKs (terminal-state guard), but the balloon has no last-seen-sequence check, so a CAPTURE_NOW whose ACK was lost executes twice (extra capture; harmless-but-wasteful for idempotent SET_* commands). This is a conscious at-least-once tradeoff, but it is undocumented.
**Fix:** Record the last executed `(sequenceNumber)` per source in `CommandHandler` and skip re-execution when an identical seq/cmdType pair arrives within the retry window (still re-ACK it), or document the at-least-once semantics at the dispatch site.

### IN-08: Blocking AUX waits run on the single-threaded loop

**File:** `src/e32_lora.cpp` (`transmit()`: waitForAuxHigh(1000) + waitForAuxLow(1000) + waitForAuxHigh(5000))
**Issue:** Worst case a single `transmit()` blocks the loop ~7 s (AULT fault), during which the base's `server.handleClient()` does not run and the 5 s UI poll fails. Healthy-AUX timings are short and UAT passed, but this sits in tension with the project's "single-threaded event loop (no blocking)" constraint; under AUX faults the web UI and command processing stall together.
**Fix:** Not a Phase 1 fix — record as a constraint note. If it bites, convert the AUX waits to a non-blocking state machine driven from `loop()` (poll `isAuxHigh()` with a millis deadline) so the web server keeps breathing during radio waits.

### IN-09: `handleApiState` mutates `appState` inside a GET handler; latch can miss evicted entries

**File:** `src/main_basestation.cpp:3175-3185`
**Issue:** The `/api/state` serializer has a write side effect (auto-capture ACK latch update at 3182-3183). If the ACKED queue entry is evicted (5-slot ring reuses terminal slots when new commands arrive) between the ACK and the next poll, the latch never observes the transition and the chip shows a stale "pending" until the next enable/disable. Side-effectful serialization also makes the poll endpoint order-dependent.
**Fix:** Move the latch update into `processLoRa`/`handleResponse` at ACK time (where the transition is authoritative), leaving `/api/state` a pure read.

### IN-10: Hardcoded weak default AP credential (cross-file context)

**File:** `include/base_station_config.h:48-49`
**Issue:** `WIFI_AP_PASSWORD "balloon123"` is a checked-in, guessable default for the base station AP, and the web console has no authentication — anyone who joins the AP can send camera commands. Acceptable for a field-device MVP on a dedicated bench network, but the default should not survive a real flight.
**Fix:** Document operator action ("change WIFI_AP_PASSWORD before field use") in the phase runbook, or derive a per-device suffix printed on the OLED/boot serial.

---

_Reviewed: 2026-08-23T02:07:31Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

---
phase: 01-command-protocol-control
reviewed: 2026-08-28T00:00:00Z
depth: standard
files_reviewed: 43
files_reviewed_list:
  - include/auto_capture.h
  - include/base_station_config.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/image_tx_manager.h
  - include/sd_storage.h
  - include/sensor_pins.h
  - include/status_display.h
  - src/alert_engine.cpp
  - src/alert_engine.h
  - src/auto_capture.cpp
  - src/camera_manager.cpp
  - src/camera_manager.h
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - src/sd_storage.cpp
  - src/sensor_manager.cpp
  - src/status_display.cpp
  - src/stubs.cpp
  - src/test_lora_balloon.cpp
  - src/trajectory_buffer.cpp
  - src/trajectory_buffer.h
  - src/web_assets.h
  - src/wifi_manager.cpp
  - src/wifi_manager.h
  - platformio.ini
  - scripts/embed_web_assets.mjs
  - scripts/verify_protocol_roundtrip.mjs
  - vendor/leaflet.css
  - vendor/leaflet.js
  - vendor/LICENSE
  - vendor/provenance.json
findings:
  critical: 0
  warning: 5
  info: 13
  total: 18
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-28
**Depth:** standard
**Files Reviewed:** 43
**Status:** issues_found

## Summary

Adversarial review of the Phase 1 command-protocol path (command_protocol, command_sender, command_handler, e32_lora, auto_capture) and the image-transfer managers (image_tx_manager, image_rx_manager, sd_storage), plus both mains, the web layer, build config, scripts, and vendored assets.

Overall the protocol core is solid: CRC16 framing, big-endian payload codecs used consistently on both ends (base `handleAutoCaptureEnable`/`handleSetEventThresholds` encode with `writeUint32`/`writeUint16`, balloon decodes with `readUint32`/`readUint16` — the suspected endianness mismatch was checked and does NOT exist), WR-12 type-dispatch before body arithmetic, bounded payload validation, and the sequence-sweep regression harness all check out. Vendor Leaflet hashes verify against `provenance.json`.

No Critical findings. Five Warnings: a false `storedToSd=true` sidecar written for preempted transfers, silent drop of back-to-back commands on the balloon, no inter-byte resync timeout in either framer, an SSID-control-character gap that can break the whole dashboard JSON, and an unreachable emergency camera-disable path. Thirteen Info items cover dead/incorrect driver code, sequence-wrap collision, duplicate enum values, a malformed macro, stale credentials, and related maintainability defects.

## Warnings

### WR-01: Sidecar records `storedToSd=true` from the wrong transfer's byte counter

**File:** `src/sd_storage.cpp:296-313`
**Issue:** `finalizeImage` reads `*persistedBytes` (line 313) unconditionally, but the kind handle that counter belongs to is only closed/reset when `*handleId == meta.imageId` (line 296). When `finalizeImage` runs for a transfer whose id does NOT own the kind handle — the real case is `ImageRxManager::finalizeIncomplete` on slot-pressure eviction (`src/image_rx_manager.cpp:480-488`), which finalizes the OLDEST transfer while the single per-kind write handle already serves a newer image — the sidecar's `storedToSd` is computed from the other image's persisted byte count. A partially-received image with zero persisted bytes then gets a sidecar (and `/gallery/{id}` detail) claiming it was stored to SD.
**Fix:**
```cpp
// src/sd_storage.cpp, inside finalizeImage (replace line 313)
SdImageMetadata m = meta;
m.storedToSd = (*handleId == meta.imageId) && (*persistedBytes > 0);
```
(Keep the handle-close block at 296-299 unchanged; `*handleId` still names the last image the counter accounted for, which is exactly the guard needed.)

### WR-02: A second command completing in the same drain silently overwrites the pending command

**File:** `src/command_handler.cpp:872-874` (with `process()` at 78-84)
**Issue:** `processIncomingByte` assigns `pendingCommand.packet = cmd; hasCommand = true;` for every validated frame during the drain loop, but `process()` executes only one command per pass (after the drain). If two complete COMMAND frames arrive within one 100 ms balloon loop window (two ~20-byte frames at 9600 baud fit easily), the first is overwritten before execution and never runs — no NACK is sent for it. The base sender eventually recovers via its ACK timeout and retry, but each occurrence burns a full timeout window (2-15 s) and re-executes the command on retry, degrading the queue UI and double-triggering captures on the retry path.
**Fix:** Refuse the second frame instead of overwriting, so the base retries it as a fresh command:
```cpp
if (CommandProtocol::deserializeCommand(receiveBuffer, expectedTotal, cmd)) {
    if (!hasCommand) {
        pendingCommand.packet = cmd;
        pendingCommand.receivedTime = millis();
        hasCommand = true;
    }
    // else: previous command not yet executed — drop the new frame; the
    // sender's timeout/retry will re-deliver it. (Better: send NACK_BUSY
    // for the new sequence here.)
}
```

### WR-03: No inter-byte resync timeout in either length-driven framer

**File:** `src/command_sender.cpp:302-421` and `src/command_handler.cpp:814-879`
**Issue:** Once `inPacket` latches, both framers accumulate indefinitely toward `expectedTotal` — there is no inter-byte timeout. A frame truncated by RF noise (or a corrupted `bodyLen` field that passes the bound check but announces more bytes than will ever arrive) wedges the parser mid-frame; every subsequent good frame's bytes are consumed as payload of the phantom frame until enough bytes accumulate that the end-marker/CRC check fails and `resetReceiveState()` runs. Recovery is eventual but arbitrary later frames are destroyed in the process — on a 5 s beacon cadence this manifests as lost beacons/ACKs after any truncation event.
**Fix:** Stamp the last-byte time and reset on a gap, e.g. in both `processIncomingByte` implementations:
```cpp
// member: uint32_t lastFrameByteMs = 0;
// top of processIncomingByte:
if (inPacket && millis() - lastFrameByteMs > 200) {  // inter-byte gap
    resetReceiveState();                              // bytes cannot be >200ms apart at 9600 baud
}
lastFrameByteMs = millis();
```

### WR-04: `jsonEscape` leaves control characters unescaped — one weird SSID breaks the entire dashboard poll

**File:** `src/main_basestation.cpp:3134-3145` (used at 3329/3332)
**Issue:** `jsonEscape` escapes only `"` and `\`. WiFi SSIDs are arbitrary byte strings (1-32 bytes, no UTF-8 or printable constraint enforced by `handleWifiSwitch` beyond length); a station SSID containing a control byte (<0x20) or invalid UTF-8 is embedded raw into the `/api/state` JSON. Per RFC 8259 a raw control character makes the JSON invalid — `JSON.parse` in the browser throws, the poll handler fails, and the entire dashboard freezes (tiles, alerts, map) for as long as the base is joined to that network. The WiFi-STA path accepts such SSIDs (only length is validated), so the state is reachable.
**Fix:** Escape control bytes and drop non-ASCII bytes that cannot be rendered safely:
```cpp
static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[7];
            snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned char>(c));
            out += buf;
        } else if (static_cast<unsigned char>(c) < 0x80) {
            out += c;
        }
        // bytes >= 0x80: drop (or UTF-8-sanitize) rather than emit invalid UTF-8
    }
    return out;
}
```

### WR-05: Emergency camera-disable path is unreachable — critical battery never disables the camera

**File:** `src/main_balloon.cpp:1104-1118` (dispatcher at 1085-1102 commented out)
**Issue:** `onEmergencyTriggered` contains the emergency camera power-down, but its only caller (`onSystemEvent`) has its entire dispatch body commented out and `onSystemEvent` itself is never registered anywhere. Meanwhile `processPowerManagement` (lines 849-868) disables the camera only on the LOW threshold; the CRITICAL branch calls `SysState().triggerEmergency(...)` and stops. Net effect: at critical battery the balloon enters emergency mode but the camera (the biggest non-radio draw) keeps running — the opposite of the handler's documented intent.
**Fix:** Either delete the dead handlers or invoke the camera-disable directly in the critical branch:
```cpp
if (powerData.batteryVoltage < BATTERY_CRITICAL_THRESHOLD) {
    if (!SysState().isEmergencyActive()) {
        SysState().triggerEmergency("Critical battery level");
        if (appState.cameraActive) {
            Camera().enableCamera(false);
            appState.cameraActive = false;
        }
    }
}
```

## Info

### IN-01: `transmitToAddress` is dead code with a wrong E32 wire format and an unbounded VLA

**File:** `src/e32_lora.cpp:272-290`
**Issue:** No callers exist anywhere in `src/`. The implementation writes FOUR address bytes (two per `uint16`), but E32 ADDH/ADDL are one byte each — directed (fixed) transmission takes exactly ADDH, ADDL (+ optional CHAN) before payload, so the first two payload bytes would be eaten as address. It also uses a VLA (`uint8_t buffer[length + 4]`) with no length bound.
**Fix:** Delete the method (and its declaration in `include/e32_lora.h:132-133`), or rewrite as 2 address bytes with `length` bounded and a fixed buffer.

### IN-02: `uint8_t read()` inline accessor dereferences `serial` without an init guard

**File:** `include/e32_lora.h:138`
**Issue:** Every sibling (`available()`, `read(buffer, len)`) checks `initialized` first; this one calls `serial->read()` directly — null-pointer crash if invoked before `begin()` (both real callers only call it after successful `begin`, so latent).
**Fix:** `uint8_t read() { return initialized ? serial->read() : 0; }`

### IN-03: E32 mode bookkeeping desyncs — `currentMode` set before verification; `exitConfigMode` never restores

**File:** `src/e32_lora.cpp:153-158` and `610-627`
**Issue:** `setMode` assigns `currentMode = mode` (line 158) before the AUX verification that can fail (line 166-171), so a failed transition leaves the recorded mode disagreeing with pin state. `enterConfigMode` saves `previousMode` (line 612) that is never used, and `exitConfigMode` unconditionally returns to NORMAL — a caller that entered from POWER_SAVE/WAKEUP is silently dropped to NORMAL.
**Fix:** Set `currentMode` only after verification succeeds; make `exitConfigMode` restore the saved mode (store it as a member).

### IN-04: Sequence-number wraparound lets a stale terminal slot swallow a live response

**File:** `src/command_sender.cpp:513-521` (with 538-558 and 440-442)
**Issue:** Terminal slots (ACKED/FAILED/TIMEOUT) retain their sequence numbers until evicted, and `nextSequenceNumber` wraps at 65535. After a wrap, a stale terminal slot holding the same seq can sit earlier in the table than the live slot; `findTrackedCommand` returns the first match, and the terminal-state guard at line 440 discards the live command's response — the live command then false-times-out. Long horizon (needs 65535 commands), but the GET_STATUS poll plus window pulls make it reachable on multi-day flights.
**Fix:** In `findTrackedCommand`, prefer non-terminal matches; or in `sendCommand`, evict any existing slot with the allocated sequence before arming the new one.

### IN-05: Dead negative check in WB-mode validation

**File:** `src/command_handler.cpp:543-544`
**Issue:** `wbModeValue` derives from a `uint8_t` payload byte cast through the enum; `wbModeValue < 0` can never be true. The `> 4` half still catches garbage, so behavior is correct — the dead arm is just misleading.
**Fix:** Drop the `< 0` comparison (or validate `cmd.payload[0] > 4` directly before the enum cast).

### IN-06: `PacketType` / `PacketPriority` enumerators carry duplicate values

**File:** `include/common_types.h:26-49` and `52-64`
**Issue:** `GPS = 0x02` duplicates `TELEMETRY`, `CAMERA_THUMB/CAMERA_FULL` duplicate `GPS_DATA/CAMERA_DATA`, `ACK/NACK/PING` duplicate `COMMAND_ACK/STATUS/DEBUG`, and in `PacketPriority` `EMERGENCY = 1` equals `PRIORITY_NORMAL` (so any emergency-priority packet routed through `packet_handler.cpp`'s drop logic at 746-747 is treated as merely Normal). Legal C++, but switch/case dispatch can never distinguish them.
**Fix:** Re-number the legacy aliases to unused values, or delete them if truly compat-dead.

### IN-07: Malformed macro — `#define BACKUP retention_DAYS 7`

**File:** `include/base_station_config.h:177`
**Issue:** The space makes the macro name `BACKUP` with replacement text `retention_DAYS 7`; the intended `BACKUP_RETENTION_DAYS` constant does not exist. Any future use of `BACKUP` expands to garbage that happens to compile in expression contexts only by accident.
**Fix:** `#define BACKUP_RETENTION_DAYS 7`

### IN-08: Stale AP password contradicts the live one

**File:** `include/base_station_config.h:50` vs `src/wifi_manager.cpp:17`
**Issue:** `base_station_config.h` declares `WIFI_AP_PASSWORD "balloon123"` but nothing uses it — the AP actually runs on wifi_manager.cpp's static `"balloontrack"`. During incident response (operator cannot join the AP), the header actively misleads. The hardcoded fallback credential itself is a documented design choice (D-40), so Info, not Warning.
**Fix:** Delete the dead define from `base_station_config.h` (single source of truth in `wifi_manager.cpp`).

### IN-09: Camera init partial failure leaks the driver and wedges retries

**File:** `src/camera_manager.cpp:116-122`
**Issue:** If `esp_camera_init` succeeds but `esp_camera_sensor_get()` returns null, `initCamera` returns false without `esp_camera_deinit()`. `initialized` stays false, so a later `end()` skips deinit (line 87-90 gates on `initialized`), and a retrying `begin()` calls `esp_camera_init` on an already-initialized camera — which fails, leaving the camera dead until reboot.
**Fix:** In the `!s` branch, call `esp_camera_deinit()` before returning false.

### IN-10: Camera "health check" always fails when the camera is up

**File:** `src/main_balloon.cpp:573-577`
**Issue:** The actual check is commented out (`// && !Camera().performHealthCheck()`), so `performSystemChecks` unconditionally logs "Camera system health check failed" and clears `allPassed` whenever `cameraActive` is true — a misleading boot warning on every healthy flight boot.
**Fix:** Restore the real check or drop the block until the method exists.

### IN-11: Balloon E32 `begin()` bypasses `sensor_pins.h` with hardcoded pin literals

**File:** `src/main_balloon.cpp:474`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` duplicates the pin map as magic numbers instead of `LORA_RX_PIN`/`LORA_TX_PIN`/etc. The current values happen to match the header, but this project's own GPIO-35 bootloop incident (documented in `include/sensor_pins.h:35-46`) is exactly the class of bug a duplicated pin list invites.
**Fix:** Use the `sensor_pins.h` macros (and a named baud constant) at the call site.

### IN-12: Auto-capture chip can latch the wrong interval

**File:** `src/main_basestation.cpp:3195-3204`
**Issue:** On ACK, `autoCaptureIntervalAckSec` is assigned `appState.autoCaptureIntervalSec` — the LAST SUBMITTED interval, not the interval that rode the ACKed sequence. Two rapid `/auto-capture` POSTs with different intervals briefly display the second interval against the first command's ACK. Self-corrects when the second ACK lands.
**Fix:** Store the interval alongside the sequence when queuing (e.g., record it in `handleAutoCaptureEnable` keyed to the returned seq) and latch that value on ACK.

### IN-13: Asset-hash verification silently skipped when provenance lacks an entry

**File:** `scripts/embed_web_assets.mjs:103-109`
**Issue:** `if (provenance.files[name] && ...)` — if `provenance.json` is missing the entry for a vendored file, no hash comparison runs and the file is embedded unverified (the check is only toothful when the entry exists). Current committed provenance covers both files (verified: hashes MATCH), so this is a hardening gap, not an active breach.
**Fix:** Fail when the entry is absent: `if (!provenance.files[name] || provenance.files[name].sha256 !== h) throw ...`

---

_Reviewed: 2026-08-28_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

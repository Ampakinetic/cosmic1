---
phase: 01-command-protocol-control
reviewed: 2026-08-24T00:00:00Z
depth: standard
files_reviewed: 27
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
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
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
  - src/stubs.cpp
findings:
  critical: 0
  warning: 8
  info: 17
  total: 25
status: issues_found
---

# Phase 1: Code Review Report

**Reviewed:** 2026-08-24
**Depth:** standard
**Files Reviewed:** 27
**Status:** issues_found

## Summary

Adversarial re-review of the Phase 1 command-protocol implementation at standard depth: full reads of all 27 in-scope files (balloon and base firmware, shared protocol, SD persistence, wire-format harness, build config), with every finding from the prior report (commit 5366a93) re-verified against the current tree rather than copied forward.

**Prior findings verified FIXED since 5366a93 (not re-reported):**
- Prior WR-01 (dummy battery data): `main_balloon.cpp:706-709` now reads `PowerMgr()` with a validity gate (commit c9770b1), applied at both `processPowerManagement()` and `sendTelemetryData()` — the emergency/low-power branches are reachable.
- E32 AUX-low miss no longer booked FAILED (commit f265556) — verified at `e32_lora.cpp:247-252`.
- NVS image-ID persistence (commit 678d4f1) — verified at `auto_capture.cpp:74-83` (restore) and `309-311` (persist-before-handout).
- STATUS_LED remapped to GPIO 41 with edge-gated writes (commit 91bee03) — verified at `main_basestation.cpp:47` and `updateLED()` `main_basestation.cpp:3682-3707`.
- QQVGA 160x120 frame-size guard (commit c67e1a5) — verified at `camera_manager.cpp:349-361`.
- `CDC_ON_BOOT=0` in both production envs (commit 3fc35a7) — verified `platformio.ini:109` and `:305`.
- SD transport on SD_MMC 1-bit with datasheet-verified pins CLK=39/CMD=38/D0=40 (commit 794df00) — verified `sd_storage.cpp:55-79` (`setPins` before `begin`, correct order), config comments in `base_station_config.h:29-44`.

**Verified clean (probed again, holds):** length-driven framing with type-dispatch bounds in both receivers (max 216-byte expected frame vs 256/240-byte buffers — no wire-reachable overflow); CRC round-trips for all five packet types; strict numeric id parsing in `handleImage`/`handleGalleryDetail` (no path traversal — names built only from `%05u` ids); XSS avoided via `textContent`/`createElement`; the SD sidecar parser's bounded reads and clamps; the `degrade()`-but-keep-serving read path; `jsonValuePos`/`jsonStringAt` are NUL-safe against the 1024-byte cap.

**What remains:** 8 warnings — five persisted unaddressed from the prior review (inverted camera health check, GPIO4 double-assignment, missing basestation partition table, dead `previousMode` in the E32 config path, zero-valued canvas centering math), one new latent serializer defect (`serializeResponse` silently drops an oversize payload while its header advertises it), and the two already-routed known-open defects G-01-7 (concurrent-transfer eviction starvation) and G-01-8 (SET_RESOLUTION frame-buffer realloc), recorded here for completeness per routing, not re-litigated. 17 info items cover the ten prior info findings that persist (dead code, stale comments, sequence wraparound, blocking AUX waits, default AP credential, and others) plus seven new ones.

## Narrative Findings (AI reviewer)

No Critical issues found. The wire-protocol surface was re-probed hardest (frame bounds, CRC bypass, type confusion, sidecar parsing, path construction) and held; the defects below are in surrounding system logic, configuration, and latent internal-API edges.

## Warnings

### WR-01: Inverted camera health check flags a healthy camera as failed (persists from prior WR-02)

**File:** `src/main_balloon.cpp:574-577`
**Issue:** In `performSystemChecks()`:
```cpp
if (appState.cameraActive) { // && !Camera().performHealthCheck()) {
    SYS_WARNING("Camera system health check failed");
    allPassed = false;
}
```
The condition is inverted: the warning fires precisely when the camera is healthy (`cameraActive == true`) and stays silent when it is broken. Every boot with a working camera logs a spurious failure, `allPassed` is set false, and the function still returns `true` unconditionally (line 591) — the diagnostic is backwards and the return value masks it. Unchanged since the prior review.
**Fix:** Restore a substantive check or delete the branch:
```cpp
if (appState.cameraActive && !Camera().isInitialized()) {
    SYS_WARNING("Camera system health check failed");
    allPassed = false;
}
...
return allPassed;
```

### WR-02: GPIO4 double-assigned — battery ADC vs camera SCCB SDA (persists from prior WR-03)

**File:** `include/sensor_pins.h:52` (conflict with `src/camera_pins.h` SIOD for ESP32S3_EYE, used at `src/main_balloon.cpp:686` area and `src/power_manager.cpp`)
**Issue:** `BATTERY_SENSE_PIN 4` collides with the camera model's SCCB/I2C SDA pin. `analogRead(BATTERY_SENSE_PIN)` reconfigures GPIO4 as an ADC input on the same pad the camera driver owns as I2C SDA, racing every SCCB transaction that follows a battery read. The header's own "No conflicts detected" comment (sensor_pins.h:83-86) still lists camera pin 4 but omits pin 4 from the sensor list — the asserted invariant is false in the file that asserts it. Unchanged since the prior review. (The new battery-validity gating means real `analogRead`s now actually happen, making the collision live rather than inert.)
**Fix:** Move battery sense to a genuinely free pin and update the `#define` plus the conflict-list comment; or document the collision in the comment instead of denying it.

### WR-03: Production basestation env missing `board_build.partitions` (persists from prior WR-04)

**File:** `platformio.ini:262-318` (`[env:esp32-s3-basestation]`)
**Issue:** Every other application env sets `board_build.partitions = partitions.csv` (devkitc-1 at line 28, balloon at line 113). The basestation env — the one carrying the ~57 KB streamed HTML footer, gzipped Leaflet, gallery index, and the growing web surface — falls back to the board-default partition table and will fail with `app partition insufficient` as web assets grow, in the env least likely to be size-tested. Unchanged since the prior review.
**Fix:** Add to `[env:esp32-s3-basestation]`:
```ini
board_build.partitions = partitions.csv
```

### WR-04: `exitConfigMode()` ignores the saved previous mode; `previousMode` is dead (persists from prior WR-05)

**File:** `src/e32_lora.cpp:610-628`
**Issue:** `enterConfigMode()` saves `E32Mode previousMode = currentMode;` (line 612) into a local that is never read; `exitConfigMode()` (line 625-628) claims "Return to previous mode" but unconditionally drives `MODE_NORMAL`. Correct today only because every caller enters config from NORMAL; any future WOR-mode caller (fixed transmission, the stated purpose of `transmitToAddress`) would be silently dropped back to transparent mode, changing on-air framing with no error.
**Fix:** Store `previousMode` as a member, and in `exitConfigMode()` call `setMode(previousMode)`; or delete the local and correct the comment to "always return to NORMAL mode".

### WR-05: Canvas-fallback centering offsets are identically zero (persists from prior WR-06)

**File:** `src/main_basestation.cpp:985-986`
**Issue:** In the offline Leaflet fallback:
```js
const latOff = ((h - 2 * margin) - latSpan * ((h - 2 * margin) / latSpan)) / 2;
const lonOff = ((w - 2 * margin) - lonSpan * ((w - 2 * margin) / lonSpan)) / 2;
const s = Math.min((w - 2 * margin) / lonSpan, (h - 2 * margin) / latSpan);
```
`x - s * (x / s)` is identically zero, so both offsets are always 0 and the "centering" applied at lines 989-990 does nothing — the offline plot hugs the edges instead of centering on the non-limiting axis. A single-scale `s` now exists (line 987, apparently from a partial fix attempt) but the offsets still use per-axis ratios. Cosmetic but shipped code whose stated purpose does not execute.
**Fix:** Compute both offsets against the single chosen scale:
```js
const latOff = ((h - 2 * margin) - latSpan * s) / 2;
const lonOff = ((w - 2 * margin) - lonSpan * s) / 2;
```

### WR-06: `serializeResponse` silently drops an oversize payload while the header advertises it (new)

**File:** `src/command_protocol.cpp:148-192` (defect at 176-179)
**Issue:** `serializeResponse` writes `resp.dataLength` into the header (line 165) and the body-length byte (line 173), then gates the actual payload copy:
```cpp
if (resp.dataLength > 0 && resp.dataLength <= CMD_MAX_RESPONSE_DATA) {
    memcpy(buffer + offset, resp.data, resp.dataLength);
```
A `dataLength > CMD_MAX_RESPONSE_DATA` (50) skips the payload entirely yet still returns `true` with a packet whose header/CRC advertise the missing bytes — the receiver's length-driven framing then consumes the following stream bytes as payload. This is the exact defect class fixed for `serializeCommand` (which now rejects `payloadLength > CMD_MAX_PAYLOAD_SIZE`; regression clause (b) in the harness). Latent today only because `createResponsePacket` clamps to 50 and internal callers are bounded — but the serializer is a public protocol API, and the command-side fix was deliberately asymmetrical.
**Fix:** Reject like `serializeCommand` does:
```cpp
if (resp.dataLength > CMD_MAX_RESPONSE_DATA) {
    return false;   // payload bound — header must never advertise absent bytes
}
```
(Mirrors the WR-04-class fix; the harness's `serializeResponse` transcription at `scripts/verify_protocol_roundtrip.mjs:192-226` should grow the matching rejection clause.)

### WR-07: FULL window request evicts older entries mid-flight — concurrent-transfer starvation (known-open, routed G-01-7)

**File:** `src/image_tx_manager.cpp:693` (policy at 798-806)
**Issue:** On any FULL window request, `evictEntriesOlderThan(*target)` frees EVERY entry whose `enqueueSeq` is older than the target — including entries with an in-flight push or an armed, partially-served window. A concurrent thumbnail push or a second armed pull older than the newest FULL request is killed mid-stream, so back-to-back gallery pulls starve each other. Recorded here as still-present per its routing (gap G-01-7); closure is owned elsewhere and is not re-litigated.
**Fix:** Routed — track under G-01-7. Direction from the routing notes: restrict eviction to terminal/complete entries (or entries with no armed window), never an in-flight or armed-but-incomplete one.

### WR-08: `setFrameSize` changes the sensor framesize without frame-buffer reallocation (known-open, routed G-01-8)

**File:** `src/camera_manager.cpp:398-416`
**Issue:** `setFrameSize` calls `s->set_framesize(s, size)` and updates the stored config, but never re-initializes the camera or reallocates the DMA frame buffers that were sized at `begin()` for the original framesize. A SET_RESOLUTION command at runtime therefore produces frames that do not match the new sensor output geometry (distorted/truncated captures) until reboot. Recorded as still-present per its routing (gap G-01-8).
**Fix:** Routed — track under G-01-8. Direction: on framesize change, deinit and re-init `esp_camera` with the new `frame_size` (PSRAM budget permitting), or reject runtime changes to larger sizes than the init-time allocation.

## Info

### IN-01: Dead code batch — uncalled functions and phantom declarations (persists from prior IN-01)

**File:** `src/main_balloon.cpp:1061` (`checkSystemHealth`), `src/command_sender.cpp:466` (`findOldestCommand`), `src/e32_lora.cpp:272-290` (`transmitToAddress`), `src/camera_manager.cpp:232,601,613` (`captureBoth`/`optimizeForBandwidth`/`optimizeForQuality`), `src/camera_manager.h:100,148-149` (declarations)
**Issue:** None of these are reachable. `transmitToAddress` additionally builds an unbounded stack VLA `uint8_t buffer[length + 4]` (e32_lora.cpp:276) with no length check — a stack-overflow hazard if ever revived with wire-length input. `processCommunications()` in main_balloon.cpp remains a commented-out body invoked every loop pass.
**Fix:** Delete or mark `// TODO(phase-2)`. If `transmitToAddress` is kept, replace the VLA with a heap buffer bounded by the max-frame constant.

### IN-02: Stale "17-byte" comments vs actual 19-byte beacon body (persists from prior IN-02)

**File:** `src/command_sender.cpp:329`, `scripts/verify_protocol_roundtrip.mjs:763`
**Issue:** `// fixed 17-byte body` and the mirror comment `// forced 17` both contradict `IMG_TELEMETRY_BEACON_BODY_SIZE = 19`. Code paths are correct (both use the constant); only the comments lie — the exact drift that misleads the next protocol revision.
**Fix:** Update both comments to "19-byte body".

### IN-03: Hardcoded E32 pin literals duplicate sensor_pins.h macros (persists from prior IN-03)

**File:** `src/main_balloon.cpp:474`
**Issue:** `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` spells pins 48/14/19/20/21 as magic numbers that exactly duplicate `LORA_RX_PIN`/`LORA_TX_PIN`/`LORA_M0_PIN`/`LORA_M1_PIN`/`LORA_AUX_PIN` from sensor_pins.h. A header repin silently diverges from this call site.
**Fix:** Pass the macros (the base station already does at `main_basestation.cpp:2097`).

### IN-04: Raw uint16 comparisons break at sequence wraparound (persists from prior IN-04)

**File:** `src/main_basestation.cpp:2196-2200` and `3185-3194`
**Issue:** The RX-LED edge detector (`acked > appState.ackedAtLastPoll`) and the auto-capture ACK latch (`entries[i].sequenceNumber > appState.autoCaptureAckSeq`) use plain `>` on uint16 sequence numbers. After the 65535→1 wrap, a genuinely new ACK no longer compares greater, so the LED skips beats and the auto-capture chip can go stale until the counter re-passes the latch value.
**Fix:** Use signed serial-difference comparison: `static_cast<int16_t>(acked - appState.ackedAtLastPoll) > 0` (and likewise at 3189).

### IN-05: `jsonEscape` does not escape control characters (persists from prior IN-05)

**File:** `src/main_basestation.cpp:3124-3135` (used at 3319, 3322)
**Issue:** `jsonEscape` handles only `"` and `\`. An SSID containing a control character (< 0x20) or newline is emitted raw into `/api/state`, producing invalid JSON that fails the client's `JSON.parse` for the whole payload and trips the poll-failure backoff until the SSID changes.
**Fix:** Add a default branch for `c < 0x20` emitting `\u00XX` (at minimum `\n`/`\r`/`\t`).

### IN-06: Asymmetric LoRa UART RX buffering — balloon keeps default 256 bytes (persists from prior IN-06)

**File:** `src/main_balloon.cpp:472-474` vs `src/main_basestation.cpp:2095`
**Issue:** The base enlarged its LoRa serial RX buffer to 1024; the balloon still uses `Serial2` with the 256-byte default. Balloon-bound frames are small (commands ≤ 216 bytes), but the balloon also receives image-window requests mid-burst and the hardening was applied on one side only.
**Fix:** `Serial2.setRxBufferSize(1024);` before `E32LoRaModule().begin(...)`.

### IN-07: Retried commands re-execute on the balloon — no sequence dedupe (persists from prior IN-07)

**File:** `src/command_handler.cpp` (dispatch via `pendingCommand`, 824-830; no last-seen-sequence guard)
**Issue:** The retry design resends the same command with the same sequence number; the balloon has no duplicate-sequence check, so a CAPTURE_NOW whose ACK was lost executes twice. A conscious at-least-once tradeoff, still undocumented at the dispatch site.
**Fix:** Record the last executed sequence per source and skip re-execution (still re-ACK), or document the at-least-once semantics at the dispatch site.

### IN-08: Blocking AUX waits run on the single-threaded loop (persists from prior IN-08)

**File:** `src/e32_lora.cpp` (`transmit()`: waitForAuxLow(1000) at 247 + waitForAuxHigh(5000) at 255)
**Issue:** Worst case a single `transmit()` blocks the loop up to ~6 s under AUX faults (previously ~7 s; the tolerated AUX-low miss at 247-252 reduced but did not remove this), during which the base's `server.handleClient()` and command processing stall. Sits in tension with the project's "single-threaded event loop (no blocking)" constraint.
**Fix:** Constraint note, not a Phase 1 fix: convert the AUX waits to a non-blocking millis-deadline state machine driven from `loop()` if it bites.

### IN-09: `handleApiState` mutates `appState` inside a GET handler; latch can miss evicted entries (persists from prior IN-09)

**File:** `src/main_basestation.cpp:3183-3194`
**Issue:** The `/api/state` serializer still has a write side effect (auto-capture ACK latch update). If the ACKED queue entry is evicted from the 5-slot ring between the ACK and the next poll, the latch never observes the transition and the chip shows stale state. Side-effectful serialization also makes the poll endpoint order-dependent.
**Fix:** Move the latch update into the ACK-handling path (`processLoRa`/`handleResponse`), leaving `/api/state` a pure read.

### IN-10: Hardcoded weak default AP credential (persists from prior IN-10)

**File:** `include/base_station_config.h:49-50`
**Issue:** `WIFI_AP_PASSWORD "balloon123"` remains a checked-in, guessable default and the web console has no authentication — anyone who joins the AP can send camera commands. Acceptable for the bench MVP; should not survive a real flight.
**Fix:** Document operator action in the phase runbook, or derive a per-device suffix printed at boot.

### IN-11: Second command in the same drain overwrites `pendingCommand`; the first is dropped (new)

**File:** `src/command_handler.cpp:824-830`
**Issue:** `processIncomingByte` sets `pendingCommand.packet = cmd; hasCommand = true;` without checking `hasCommand` first. If two complete command frames arrive in one UART drain before the loop calls `process()`, the second overwrites the first — the first command is never executed or ACKed. Self-healing via the base's retry machinery (the base sees no ACK and resends), so impact is added latency, not loss.
**Fix:** If `hasCommand` is already true, either process the pending command first or queue the new packet; at minimum log the overwrite.

### IN-12: `allocateSlot(uint8_t kind)` never uses `kind` (new)

**File:** `src/image_rx_manager.cpp:343-395`
**Issue:** The `kind` parameter is dead — slot selection (free slot, oldest terminal, oldest non-pull) never consults it. Misleading signature suggests kind-aware placement that does not exist.
**Fix:** Drop the parameter (and update the two call sites) or implement the kind-aware preference the signature implies.

### IN-13: Re-manifest of an ACTIVE pull clears window state without re-issuing the window request (new)

**File:** `src/image_rx_manager.cpp:456-466`
**Issue:** When a re-manifest arrives for an id whose slot has `pullActive`, the slot is zeroed (`*t = ImageRxTransfer{}` with `pullActive` preserved) — clearing the armed-window bookkeeping — but no window request is issued here. Recovery waits on the transfer-age/stall timeout (~8 s of silence) before the pull machinery re-requests, an avoidable stall on every duplicate manifest for an active pull.
**Fix:** After resetting an active pull's slot, immediately re-issue the window request (or re-arm it in the pull state machine) instead of waiting for the stall timer.

### IN-14: Retry-count asymmetry between transmit-failure and timeout paths (new)

**File:** `src/command_sender.cpp:236-248` vs `252-269`
**Issue:** The transmit-failure path books FAILED once `retryCount >= maxRetries` (3) — 3 total attempts. The ACK-timeout path retries while `retryCount < maxRetries` via `retryCommand()` — initial + 3 retries = 4 total attempts. Two different "max retries" semantics for the same configured value; operators reading "Failed after 3 transmit attempts" vs "timeout after 3 retries" see inconsistent totals.
**Fix:** Align the semantics (e.g., book FAILED at `retryCount > maxRetries`, or count both paths the same way) and state the intended total in the log strings.

### IN-15: Typo'd macro `BACKUP retention_DAYS` (new)

**File:** `include/base_station_config.h:177`
**Issue:** `#define BACKUP retention_DAYS 7` — the space makes this define the macro `BACKUP` (expanding to `retention_DAYS`), not the intended `BACKUP_RETENTION_DAYS`. The intended constant does not exist; any future use of `BACKUP_RETENTION_DAYS` fails to compile, and `BACKUP` is a collision hazard.
**Fix:** `#define BACKUP_RETENTION_DAYS 7` (the value is currently unreferenced, so the fix is free).

### IN-16: `storedToSd` accounting is wrong across handle flips (new)

**File:** `src/sd_storage.cpp:234` and `312-313`
**Issue:** `writeChunk`'s reopen path resets `*persistedBytes = 0` on every id flip (one handle per kind), and `finalizeImage` computes `m.storedToSd = *persistedBytes > 0` from whatever the kind's single handle currently tracks. If two ids of the same kind interleave (the documented heal-vs-push path), a finalize can read the OTHER transfer's byte count — marking an unpersisted image `storedToSd: true` (or vice versa). The code comments acknowledge "accounting restarts at each flip (conservative)", but the value written into the sidecar can still be wrong, not just conservative.
**Fix:** Track persisted bytes per (id, kind) in a small map (or in the caller's transfer slot), so `storedToSd` reflects the finalized image's own writes.

### IN-17: Debug-session UART0 prints compiled into the release build (new)

**File:** `src/main_basestation.cpp:2037-2043` (`[NET]`), `2211` (`[API] GET /`), `2599` (`[PAGE]`), `3356` (`[API] /api/state`), `3641` (`[HTTP] ... -> 404`)
**Issue:** The balloon-no-data debug-session diagnostics print unconditionally on UART0 in the release build: a line per `/api/state` poll (~40 chars ≈ 3.5 ms blocking at 115200 baud) and a line per 404. Useful on the bench; noise and small per-request blocking in production.
**Fix:** Gate behind the existing `DEBUG_*` pattern (`#ifdef DEBUG` / `DEBUG_WEB_SERVER`) so release builds stay silent.

---

_Reviewed: 2026-08-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

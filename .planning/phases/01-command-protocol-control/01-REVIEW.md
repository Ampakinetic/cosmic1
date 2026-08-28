---
phase: 01-command-protocol-control
reviewed: 2026-08-28T00:00:00Z
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
findings:
  critical: 0
  warning: 2
  info: 7
  total: 9
status: issues_found
---

# Phase 01: Code Review Report

**Reviewed:** 2026-08-28
**Depth:** standard
**Files Reviewed:** 23
**Status:** issues_found

## Summary

Full standard-depth review of the Phase 1 command-protocol surface across both firmware targets: the LoRa E32 driver, command/response protocol codec, balloon-side command handler, base-side command sender, image TX/RX managers, auto-capture, camera manager, SD storage, and both main loops, plus the web handlers and embedded dashboard JS.

The protocol core is in strong shape. Verified-sound behaviors this review confirmed and does not re-flag: wrap-safe millis() subtraction everywhere; length-driven framing with CRC16 and interbyte resync on both TX and RX framer paths (handler and sender); type dispatch before body arithmetic; chunk kind-byte validation; duplicate/terminal-response guard with NACK_BUSY window deferral; bounded manifest/chunk retry ladders with reset-on-success; PSRAM buffer ownership (enqueue-time copies, dangling-pointer fixes in thumbnails); eviction class accounting with freeEntry resets; the static_assert pairing of MAX_IMAGE_SIZE/IMG_MAX_IMAGE_SIZE; strict WR-07 range-before-narrow in every web handler; strictly numeric id parsing before any SD path construction (T-03-10) with a per-character gallery-name parser; the bounded hostile-sidecar reader (clamp, never wrap); password never echoed in any response, log, or DOM; all DOM writes via textContent/createElement (no innerHTML anywhere); all boot-degradation paths guarded (`E32LoRa::transmit` checks `!initialized`, `CommandSender` guards at :122/:189, `CommandHandler::process` guards `!initialized || !lora`); and the documented G-01-10 instrumentation ([MEM]/[IDLE0]/[I2C]/B2) carrying explicit REMOVAL CONDITIONs — treated as deliberate instrumentation, not debug leftovers.

No Critical issues were found. Two Warnings: a latent framing-corruption gap in `serializeResponse` and a fault-containment comment in the balloon loop that documents call sites which do not exist. The remaining seven findings are dead code, concentrated in the legacy scaffolding regions of main_balloon.cpp and two unreferenced driver/sender functions (one of which, `findOldestCommand`, is a recurrence of 02-REVIEW.md IN-01).

## Warnings

### WR-01: serializeResponse silently emits a corrupt frame for dataLength above CMD_MAX_RESPONSE_DATA

**File:** `src/command_protocol.cpp:154-179`
**Issue:** `serializeResponse` bounds only `CMD_MAX_PACKET_SIZE` (:156). The payload copy is gated by `if (resp.dataLength > 0 && resp.dataLength <= CMD_MAX_RESPONSE_DATA)` (:176), but there is no early rejection for `dataLength > CMD_MAX_RESPONSE_DATA`. A caller that passes, say, `dataLength = 100` gets `return true` with a frame whose header length field (:165) and body length byte (:173) both advertise 100 data bytes while zero data bytes are actually written (:176 guard skips the memcpy). Every receiver will fail deserialization/CRC on this frame, and the sender reports success. `serializeCommand` (:57 region) rejects out-of-range data up front — the two serializers are asymmetric. Not reachable with today's factories (all of them clamp data), which is why this is a Warning and not Critical, but the function's contract ("return true = a well-formed frame was produced") is broken for any future response type with a larger payload.
**Fix:**
```cpp
size_t packetLength = CMD_HEADER_SIZE + 4 + resp.dataLength + 4;

if (resp.dataLength > CMD_MAX_RESPONSE_DATA || packetLength > CMD_MAX_PACKET_SIZE) {
    return false;   // reject before writing anything — mirrors serializeCommand
}
```
and then drop the `resp.dataLength <= CMD_MAX_RESPONSE_DATA` term from the guard at :176 (keeping `> 0`).

### WR-02: loop() comment cites handleSystemError() call sites that do not exist; the documented error-escalation path is dead

**File:** `src/main_balloon.cpp:296-301` (comment), `src/main_balloon.cpp:1158-1170` (function)
**Issue:** The WR-09 rationale in loop() states fault containment is "the watchdog plus the handleSystemError() call sites at real error paths." A project-wide search finds zero call sites for `handleSystemError` — it is declared (:179) and defined (:1158) but never invoked. Its escalation logic (`errorCount > 10` -> `SysState().triggerEmergency("Too many system errors")`, :1167-1169) therefore does not exist in practice. During the active G-01-10 crash investigation this is more than dead code: the comment leads a maintainer to believe an error-counting emergency brake is armed when it is not, and SYS_ERROR() calls across the subsystems never feed it.
**Fix:** Either (a) wire `handleSystemError()` into the real failure paths (e.g. where SYS_ERROR is emitted for recoverable subsystem faults) so the documented containment exists, or (b) delete the function and correct the loop() comment to name only the watchdog as the containment mechanism. Option (b) is acceptable if the emergency brake is intentionally deferred; option (a) matches what the comment promises.

## Info

### IN-01: transmitToAddress is dead code containing latent hazards

**File:** `src/e32_lora.cpp:304-322` (declaration `include/e32_lora.h:138`)
**Issue:** Zero call sites (verified by project-wide search). The body allocates a VLA `uint8_t buffer[length + 4]` (:308) before any validation, `memcpy`s from `data` without a null check (:319), and never checks `initialized` itself (the guard lives inside `transmit()`). If ever called with a large or attacker-chosen `length`, this is a stack overflow; with `data == nullptr`, a null deref.
**Fix:** Delete the function (the protocol uses `transmit()` exclusively), or if it is retained for future directed-address use, validate `data`/`length`, add the `initialized` guard, and replace the VLA with a bounded static or heap path.

### IN-02: enterConfigMode saves previousMode but never restores it; exitConfigMode hardcodes MODE_NORMAL

**File:** `src/e32_lora.cpp:642-660`
**Issue:** `enterConfigMode` declares `E32Mode previousMode = currentMode;` (:644) which is never read — a dead variable with misleading "save current mode" intent. `exitConfigMode` (:657-660) hardcodes `MODE_NORMAL` instead of restoring. Benign today because configuration only runs during `begin()` while in normal mode, but the code implies a restore semantics it does not have.
**Fix:** Remove the unused `previousMode` local (and its comment), or make `exitConfigMode` restore the saved member if config mode is ever entered at runtime.

### IN-03: findOldestCommand is dead code — recurrence of 02-REVIEW.md IN-01

**File:** `src/command_sender.cpp:534` (declaration `include/command_sender.h:161`)
**Issue:** Zero call sites (verified by project-wide search). This same dead function was flagged as IN-01 in the Phase 2 review and still survives. Dead code in a safety-relevant manager invites drift: future edits to queue-eviction logic may assume it is load-bearing.
**Fix:** Delete both the definition and the header declaration.

### IN-04: Dead event/error-handler family in main_balloon.cpp

**File:** `src/main_balloon.cpp:1209-1292`, `1185-1203`
**Issue:** `onSystemEvent` (:1209, body entirely commented out), `onEmergencyTriggered` (:1228), `onModeChanged` (:1244), `onFlightPhaseChanged` (:1270), and `checkSystemHealth` (:1185) are all declared and defined but never called (the dispatcher in onSystemEvent is documented dead in WINDOWS entry 14, and the camera-disable concern it once covered is handled inline per WR-05 at :971-980). `checkSystemHealth`'s low-memory/loop-time checks likewise never run. The emergency camera-disable itself is correctly implemented inline in `processPowerManagement`, so nothing behavioral is missing — but five dead handlers plus a never-called health check obscure which safety logic is actually armed.
**Fix:** Delete the handler family and `checkSystemHealth`, or register `onModeChanged`/`onFlightPhaseChanged` with SysState if the phase-based behavior adjustments are still planned. (The `handleSystemError` piece is tracked separately as WR-02.)

### IN-05: processCommunications is an empty shell; legacy packet path enqueues packets nobody drains

**File:** `src/main_balloon.cpp:895-925` (shell), `1069-1140` (producers)
**Issue:** `processCommunications()` is called every loop pass but its entire body is commented out. Meanwhile `sendTelemetryData`/`sendHeartbeatPacket`/`sendStatusReport` still run on their 5 s/30 s/60 s timers and enqueue packets into PacketMgr's ring buffer (bounded — queue head/tail in `src/packet_handler.h:168-170`, so no memory growth), and nothing ever dequeues or transmits them. The result is pure dead-end work plus SYS_WARNING spam once the ring fills. Pre-existing legacy scaffolding outside the Phase 1 protocol, so Info — but it should not survive into flight firmware.
**Fix:** Gate the three `send*` producers (or the whole legacy PacketMgr path) behind a build flag / remove them, or implement the drain side.

### IN-06: processCommands declared but never defined

**File:** `src/main_basestation.cpp:143`
**Issue:** `void processCommands();` is declared in the function-declarations block but has no definition and no call site (the loop calls `CmdSender().process()` / `ImageRx().process()` directly). Harmless dead declaration that misstates the file's structure.
**Fix:** Delete the declaration.

### IN-07: validateImageBuffer has a redundant second length check

**File:** `src/camera_manager.cpp:856`
**Issue:** The JPEG-footer check re-tests `length < 2`, already guaranteed false by the header check at :851 (any `length < 2` returned there). No behavior difference — pure redundancy in a validation helper readers rely on for exactness.
**Fix:** Drop the redundant `length < 2 ||` term at :856.

---

_Reviewed: 2026-08-28T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

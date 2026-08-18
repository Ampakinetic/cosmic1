---
phase: 01-command-protocol-control
reviewed: 2026-08-18T03:37:35Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - .claude/CLAUDE.md
  - .vscode/extensions.json
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
  warning: 6
  info: 12
  total: 19
status: issues_found
---

# Phase 1: Code Review Report (re-review after gap closure)

**Reviewed:** 2026-08-18T03:37:35Z
**Depth:** standard
**Files Reviewed:** 18 (plus cross-reference: `src/stubs.cpp`, `include/sensor_pins.h`, phase plans 01-02..01-04, REQUIREMENTS.md)
**Status:** issues_found
**Supersedes:** 2026-08-17 review (21 findings)

## Summary

Re-reviewed the Phase 1 command-protocol stack after the gap-closure plans (01-02 protocol fixes, 01-03 UI completion, 01-04 auto-capture). The protocol core is now sound: **all four prior Critical findings are verified fixed in the current code** — the sequence byte is written before the CRC is computed (`command_protocol.cpp:61`, CR-01), the sender's transmit buffer is sized to the shared 240-byte constant (`command_sender.cpp:415`, CR-02), both receivers frame by the header-announced length so embedded `0D 0A` survives (CR-03), and PENDING transmit failures terminate in FAILED with retry accounting (CR-04, `command_sender.cpp:221-238`). WR-03 (cancel guard), WR-04 (backoff pacing above both state branches), WR-05 (STATUS typed end-to-end), WR-07 (validate-before-narrowing in every web handler), and IN-03 (computed link state) are also closed, and the new wire-format regression harness (`scripts/verify_protocol_roundtrip.mjs`) passes 10/10 when re-run for this review. `stubs.cpp` was verified to supply a real `CameraManager` instance, so the command handler drives the real camera.

One new Critical and one new Warning remain, plus four carried Warnings that were deferred rather than fixed.

**The new Critical (CR-05):** the balloon still runs its legacy 30-second capture timer (`main_balloon.cpp:667`) alongside the new commanded auto-capture, with its own independent image-ID counter (`main_balloon.cpp:676-677`). `AUTO_CAPTURE_DISABLE` therefore does not stop automatic capture — within 30 s the legacy path fires again — and the "single shared image-ID sequence" invariant that plan 01-04 explicitly built (auto_capture.h:39-41, used by CAPTURE_NOW and GET_STATUS) is violated by a second sequence that will collide with it from ID 1.

**The new Warning (WR-10):** `serializeCommand` accepts payload lengths 201-224 and `serializeResponse` accepts data lengths 51-225 — windows that fall between the packet-size check and the payload-copy guard. The serializer returns `true` and emits a 16-byte packet whose header declares a body of 201-224 bytes (verified empirically); the receiver then waits forever for bytes that were never sent. Current callers clamp (200/50) so it is unreachable today, but the regression harness's clause (b) actively certifies the malformed 224-byte case as "the valid boundary" — the test institutionalizes the defect.

Carried unfixed from the previous review: blocking E32 transmit up to ~7 s inside both event loops (WR-01), the datasheet-violating E32 config API (WR-02, uncalled), the `AA AA` start-byte resync loss (WR-06), silent overwrite of a pending command by a second command in the same read burst (WR-08), and the unchecked packet-type byte / response length-width asymmetry (WR-09).

Accepted gaps (per commit 0b52b9e: D-13/D-15 UI restructure deferred to Phase 3; hardware UAT items in 01-VERIFICATION.md) were not reported as findings.

## Critical Issues

### CR-05: Legacy 30-second capture timer still runs beside commanded auto-capture — disable doesn't disable, and a second image-ID sequence collides with the shared one

**File:** `src/main_balloon.cpp:660-692` (with `include/auto_capture.h:39-41`, `src/command_handler.cpp:207`)
**Issue:** `processCamera()` retains the pre-Phase-1 behavior: `Camera().isTimeToCapture(30000)` captures an image every 30 s and assigns it an ID from its own `static uint16_t nextImageId` counter (lines 676-677). This runs in the same loop as `AutoCap().process()` and is gated only on `appState.cameraActive`, not on any commanded state. Consequences:

1. **CTRL-03/CTRL-04 semantics broken:** after `AUTO_CAPTURE_DISABLE` is ACKed and the UI chip shows OFF, the balloon keeps capturing automatically within 30 s. (Note the interaction: any capture resets `CameraManager::lastCaptureTime`, so while commanded auto-capture runs at ≤30 s intervals the legacy timer never fires — the legacy captures manifest *exactly* when the user has disabled auto-capture.)
2. **Shared image-ID invariant violated:** plan 01-04 deliberately made `AutoCapture::allocateImageId()` the single authority shared by CAPTURE_NOW and interval captures ("manual and automatic captures share it, so GET_STATUS lastImageId is truthful across both modes", 01-04-SUMMARY). The legacy counter starts at 1 and counts independently, so `GET_STATUS`'s `lastImageId` can name a different image than the one last captured, and Phase 2 image sequencing inherits colliding IDs.
3. Each interleaved legacy capture calls `captureImage()`, which frees and replaces the current image buffer — a legacy capture between a commanded capture and any future use of that image (Phase 2 transmission) silently destroys it.

Phase 1 does not transmit images, so today's visible impact is bookkeeping and camera churn — but the commanded-control contract this phase exists to deliver ("user disables auto-capture → no automatic captures") does not hold.
**Fix:** Remove the legacy capture block from `processCamera()` (the telemetry packet it builds goes to the stubbed `LoRaComm` path and is never transmitted), or gate it behind an explicit debug flag. If any periodic capture is kept, route its ID through `AutoCap().allocateImageId()` and make `AutoCap` the only capture trigger.

## Warnings

### WR-10: Length-validation window (commands 201-224, responses 51-225) emits header/body-mismatch packets that the serializer reports as success — and the regression harness certifies the malformed boundary as valid

**File:** `src/command_protocol.cpp:50-54,75,150-152,172` (with `scripts/verify_protocol_roundtrip.mjs:271-274`)
**Issue:** `serializeCommand()` guards with `cmd.payloadLength > CMD_MAX_PACKET_SIZE - 16` (=224) but only copies the payload when `cmd.payloadLength <= CMD_MAX_PAYLOAD_SIZE` (=200). For any payload of 201-224 bytes the size check passes, the payload copy is silently skipped, and the function returns `true` with a 16-byte packet whose header declares `bodyLen = 201..224`. Verified by running the transcribed serializer: `payload 201 -> packet 16 bytes, header declares bodyLen 201`. The receiver frames to `7 + 5 + bodyLen + 4` bytes that never arrive and silently drops the command. `serializeResponse()` has the same hole for `dataLength` 51-225 (only a packet-length check at line 152; copy guard is 50 at line 172). Unreachable via current callers — `createCommandPacket`/`createResponsePacket`/`createACK`/`createNACK`/`createStatus` all clamp — so this is a defense gap for future callers, not a live failure. However, harness clause (b) asserts `serializeCommand accepts the 224-byte payload boundary (240-byte packet)` — the packet it blesses is actually the malformed 16-byte one, and the label's "240-byte packet" claim is false. The test locks in the defect instead of guarding against it.
**Fix:** In `serializeCommand`, reject `cmd.payloadLength > CMD_MAX_PAYLOAD_SIZE` (which subsumes the 240-byte packet bound: 7+5+200+4 = 216 ≤ 240); in `serializeResponse`, reject `resp.dataLength > CMD_MAX_RESPONSE_DATA`. Return `false` rather than skipping the copy. Update harness clause (b) to test the real boundaries (200 accepted / 201 rejected) and add a shape assertion that the accepted packet's length equals `16 + payloadLength`.

### WR-01 (carried, unfixed): E32 transmit blocks up to 7 seconds inside both main loops; AUX-low race can false-fail short transmissions

**File:** `src/e32_lora.cpp:158-202`
**Issue:** Unchanged since the last review: `transmit()` performs `waitForAuxHigh(1000)` + `waitForAuxLow(1000)` + `waitForAuxHigh(5000)` — up to 7 s of `delay(10)` polling. On the base station this runs inside `CmdSender().process()` from `loop()` (`main_basestation.cpp:404`), freezing `server.handleClient()` during every send and retry (against the 5-second UI responsiveness constraint); on the balloon it runs inside `CmdHandler().process()` (`command_handler.cpp:616` sends each response), stalling the 10 Hz loop and the watchdog feed between `Debug.feedWatchdog()` calls. The AUX-low race also stands: `serial->flush()` returns after the UART drains, by which time a short packet's air time may be complete and AUX back high — `waitForAuxLow` then times out and a successful transmission is reported as failure, driving the (now correctly bounded) retry path.
**Fix:** As previously recommended: non-blocking AUX state machine, or at minimum drop the `waitForAuxLow` step and treat "AUX never observed low" as success when the write completed.

### WR-02 (carried, unfixed): E32 configuration API contradicts the E32-900T30D datasheet

**File:** `src/e32_lora.cpp:204-222, 264-344, 425-443`
**Issue:** Unchanged and still uncalled: `writeConfig()` sends 6 raw bytes with no `0xC0` command prefix and a wrong address layout (E32 frame is `C0 ADDH ADDL SPED CHAN OPTION`, addresses are 1 byte each); `setParameters()` passes a partially-initialized `E32Config`; `readConfig()` sends `C1 C1 C1` but never parses the reply into `config`; `transmitToAddress()` uses a 4-byte prefix (E32 fixed TX uses 3: `ADDH ADDL CHAN`) plus an unbounded stack VLA; `enterConfigMode()` still saves `previousMode` without restoring it. Any future hardware bring-up that calls these writes garbage to the module.
**Fix:** Implement the datasheet frames as previously specified, or delete the config API until it is needed so it cannot be trusted by mistake.

### WR-06 (carried, unfixed): Start-byte resync flaw — a doubled `0xAA` loses the packet start (both receivers)

**File:** `src/command_handler.cpp:633-640`, `src/command_sender.cpp:270-277`
**Issue:** Unchanged by the CR-03 rewrite: while hunting for the start pair with `receiveIndex == 1` (already saw `0xAA`), any byte other than `0x55` — including another `0xAA` — falls into the `else` branch that resets `receiveIndex = 0` and discards the current byte. A stream `AA AA 55 ...` (noise byte ahead of a genuine preamble, or a misaligned stream after a dropped packet) consumes the real start marker and loses the packet. The length-driven framing does not help here because framing never begins.
**Fix:** Re-anchor instead of discarding: `receiveIndex = (byte == CMD_START_BYTE1) ? 1 : 0;`. The mjs harness's `makeLengthDrivenReceiver` transcribes this behavior too — update it in the same change and add an `AA AA 55` resync clause.

### WR-08 (carried, unfixed): A second command arriving in the same read burst silently overwrites the pending one

**File:** `src/command_handler.cpp:671-679` (single-slot `pendingCommand`, `include/command_handler.h:69`)
**Issue:** Still present after the framing rewrite: `process()` drains all available bytes before executing, so two commands that arrive back-to-back (or a base-station retry landing while the first is queued) both pass through `processIncomingByte`, and the second unconditionally overwrites `pendingCommand.packet` (line 676). The first command is never executed and never NACKed; only the retry machinery on the base station recovers it, after a full D-05 timeout window.
**Fix:** When `hasCommand` is already true, either queue the newcomer or reply `NACK_BUSY` for the new sequence number instead of overwriting.

### WR-09 (carried, unfixed): Packet type byte never validated on either side; response length fields are asymmetric

**File:** `src/command_protocol.cpp:113, 161-169, 210-217`; framing in `src/command_handler.cpp:655`, `src/command_sender.cpp:291`
**Issue:** Unchanged: `deserializeCommand()` stores `buffer[2]` into `cmd.type` without checking it against `PACKET_TYPE_COMMAND` (0x10), `deserializeResponse()` likewise ignores it, and the framing layers assume a body overhead (5 vs 4) from the packet's direction rather than its type byte. A command packet reaching the base station's response-flavored receiver (second base station, echo, future relay) is misframed by one byte rather than rejected. `serializeResponse()` also still writes `dataLength` 16-bit into the header (line 161) but 8-bit in the body (line 169) — the deserializer reads only the body byte, so the two fields can disagree for dataLength > 255 (currently impossible, but the asymmetry invites drift).
**Fix:** Check `buffer[2]` against the expected type in framing (before the body-overhead assumption) and in both deserializers; make both response length fields the same width.

## Info

### IN-01 (carried): Hardcoded E32 pin literals duplicate sensor_pins.h macros

**File:** `src/main_balloon.cpp:381` — `E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)` still hardcodes values that exist as `LORA_RX_PIN/LORA_TX_PIN/LORA_M0_PIN/LORA_M1_PIN/LORA_AUX_PIN` in `include/sensor_pins.h:33-37`. Values currently match. `main_basestation.cpp:24-29` defines its own matching set (acceptable — it does not include sensor_pins.h). **Fix:** Use the macros in main_balloon.cpp; add a `LORA_BAUD_RATE` define.

### IN-02 (carried): Hardcoded WiFi credential and unauthenticated control endpoints

**File:** `src/main_basestation.cpp:38-41` — AP SSID/password hardcoded (`"balloontrack"`), all control endpoints accept commands with no authentication. Defensible for a field device on its own AP; now that WR-07's range checks are fixed, validation is the only protection. **Fix:** Acceptable for Phase 1 if documented; consider a device-unique generated password.

### IN-04 (carried, partially fixed): Dead code across the modules

**Fixed since last review:** `lastCommandSequence` is now read (processLoRa/handleStatus); `lastStatus` is still write-only. **Still dead:** `findOldestCommand` (`command_sender.cpp:373-386`, never called), `updateLED` (`main_basestation.cpp:1114-1123`, never called — the blink lives in `updateStatus`), `lastCommandTime` (`main_basestation.cpp:62`, written at :700, never read), `lastStatus` (`:375,383` written, never read), `sendHTML` (`:1110-1112`), the phantom `PacketHeader header` members in `CommandPacket`/`ResponsePacket` (`include/command_protocol.h:75,91` — hand-rolled serialization ignores them), `calculateConfigCRC` (`src/e32_lora.cpp:474-482`), `previousMode` (`e32_lora.cpp:427`). **Fix:** Remove or wire up; the phantom header fields especially mislead readers about the wire format.

### IN-05 (carried, pre-existing): Duplicate PacketType enumerator values; protocol constant duplicates the new enum entry

**File:** `include/common_types.h:41-48` — `GPS=TELEMETRY (0x02)`, `CAMERA_THUMB=GPS_DATA (0x03)`, `CAMERA_FULL=CAMERA_DATA (0x04)`, `ACK=COMMAND_ACK (0x06)`, `NACK=STATUS (0x07)`, `PING=DEBUG (0x08)` remain aliased. Related new drift: `include/command_protocol.h:14` defines `PACKET_TYPE_COMMAND = static_cast<PacketType>(0x10)` while the enum already carries `COMMAND = 0x10` — two definitions of the same value in different headers. **Fix:** Remove the alias block; use the enum value for `PACKET_TYPE_COMMAND`.

### IN-06 (carried): Per-byte 100 µs delay in E32LoRa::read

**File:** `src/e32_lora.cpp:246` — `delayMicroseconds(100)` per byte in the (still unused) bulk-read overload; ~25 ms of blocking for a 256-byte burst with no stated purpose. **Fix:** Delete the delay.

### IN-07 (carried): Init failures are undetectable

**File:** `src/main_balloon.cpp:380-398`, `src/e32_lora.cpp:74-80` — `E32LoRa::begin()` still returns true whenever a serial pointer exists (AUX-low only logs a warning), and `initializeSubsystems()` sets `appState.communicationActive = true` unconditionally (line 398) even when the E32/CmdHandler/AutoCap begin calls above it failed. Nothing downstream can distinguish a configured link from a dead one. **Fix:** Propagate AUX failure into begin()'s return; gate `communicationActive` on the Phase-1 begins succeeding.

### IN-08 (carried, pre-existing): GPIO 4 double-booked between battery ADC and camera I2C data

**File:** `include/sensor_pins.h:46` (`BATTERY_SENSE_PIN = 4`) vs `include/camera_pins.h` (`SIOD_GPIO_NUM = 4` for CAMERA_MODEL_ESP32S3_EYE). `checkHardwareStatus()` calls `analogRead(4)` before camera init; any periodic battery sampling would corrupt camera I2C. Not introduced by this phase, but it will bite during hardware tests. **Fix:** Move battery sense to a free GPIO or drop the boot-time ADC read.

### IN-09 (new): GET_STATUS is implemented balloon-side but has no caller anywhere — the truthful-status work is unreachable end-to-end

**File:** `src/command_handler.cpp:572-595`, `src/main_basestation.cpp` (no endpoint calls `sendCommand(CameraCommand::GET_STATUS)`)
**Issue:** Plan 01-04 invested in making GET_STATUS truthful (tracked image ID, auto-capture state, live settings) and 01-03 added the "Get Status" display name for queue rows, but no base-station code path ever sends GET_STATUS and the STATUS response payload is never parsed by the sender (it only checks the response type). The feature cannot be exercised in UAT and the STATUS wire format (including the struct issues in IN-10) ships unvalidated. Consistent with the plans (no acceptance criterion requires a UI trigger), so recorded as info, not a gap violation. **Fix:** Either add a minimal trigger (a "Refresh status" button or a periodic GET_STATUS poll feeding the UI) before Phase 2 builds on the payload, or mark the command reserved in the protocol header.

### IN-10 (new): ResponseStatusData is memcpy'd as a raw struct — padding bytes and native endianness on the wire

**File:** `src/command_handler.cpp:586` (`memcpy(result.responseData, &status, sizeof(ResponseStatusData))`), `include/command_protocol.h:160-169`
**Issue:** `ResponseStatusData` serializes by raw struct copy: `sizeof` is 32 on ESP32 (29 field bytes + alignment padding; the padding's content is unspecified even after `{}` initialization), the CRC therefore covers bytes nobody set, and all multi-byte fields travel native little-endian — inconsistent with the protocol's explicit big-endian discipline (`writeUint16`/`writeUint32`, correctly used for the auto-capture interval payload per the D-09 comment at `main_basestation.cpp:933-936`). Harmless today because the base station never parses the payload, but any future reader using `readUint16/readUint32` gets garbage, and the two sides must share struct layout exactly. Related doc drift: the `PayloadSet*` structs (`command_protocol.h:107-151`) declare 4-byte `reserved`-padded layouts while the actual wire payloads are 1 byte (handlers read `payload[0]` only) — the structs describe a format that is not the one sent. **Fix:** Serialize status fields explicitly (big-endian, fixed offsets) like the auto-capture payload; delete or correct the unused payload structs.

### IN-11 (new): Base-station display nits — chip-latch races under slot churn, physical LED not link-truthful

**File:** `src/main_basestation.cpp:1058-1071, 510-521, 504-507`
**Issue:** (1) The auto-capture chip latch scans queue entries for an ACKed auto-capture command; ACKED slots are evicted by `findFreeSlot` when all 5 slots are occupied, so under rapid clicking (6+ commands within a 1 s poll) the ACKed enable/disable can be evicted before any `/status` poll sees it — the chip then shows a stale ON/OFF indefinitely. (2) The latched interval comes from `appState.autoCaptureIntervalSec`, which is set at *send* time: if a newer enable with a different interval is sent before an older enable ACKs, the older ACK latches the newer interval until the newer command resolves. (3) The physical STATUS_LED blinks on a 5 s timer regardless of link state — IN-03's LED truth was delivered for the web LED only. (4) `static_cast<uint16_t>(acked)` (line 504) truncates the 32-bit acked counter for change detection; benign except at exact 65536-delta boundaries. All narrow/transient; none misstate a command outcome. **Fix:** Latch auto-capture ACKs inside `handleResponse` (event-driven, no polling race) rather than in the status handler; derive the physical LED from the same computed link state as the web LED.

### IN-12 (new): NACK_BUSY used as a catch-all — camera-not-ready blocks GET_STATUS and AUTO_CAPTURE_DISABLE; generic capture failures typed as BUSY

**File:** `src/command_handler.cpp:139-144, 218-222, 529`
**Issue:** `executeCommand()` gates every command on `camera->isReady()` and returns `NACK_BUSY` — including GET_STATUS (reads cached settings, needs no camera) and AUTO_CAPTURE_DISABLE (timer-only). With a failed camera init, disable can never be ACKed, so the UI permanently shows the disable command as Failed while the balloon's timer state is unknowable. `handleCaptureNow()` also returns `NACK_BUSY` with message "Capture failed" for a generic capture failure — wrong type (nothing is busy). Minor: `handleAutoCaptureEnable` hardcodes `1000`/`3600000` (line 529) instead of the `AUTO_CAPTURE_MIN/MAX_INTERVAL_MS` constants the auto_capture.h comment says it mirrors. **Fix:** Apply the camera-ready gate only to commands that act on the sensor; type generic failures as NACK; use the shared interval constants.

### IN-13 (new): Retry-cadence comment vs actual behavior; harness not wired into any automated gate

**File:** `src/command_sender.cpp:196-209`, `include/command_protocol.h:185-186`, `scripts/verify_protocol_roundtrip.mjs`
**Issue:** (1) The D-07 comments describe "2000/4000/8000ms before retries 1/2/3", but the backoff guard is combined with the D-05 ACK-timeout window, so the effective spacing between attempts is `max(ackTimeout, backoff)`: for SETTINGS commands that is 5000/5000/8000 ms, not 2000/4000/8000 (only CAPTURE_NOW realizes the documented cadence, since backoff ≥ its 2000 ms timeout at every attempt). Behavior is paced and bounded — this is a documentation accuracy issue, and it means SETTINGS commands retry somewhat *later* than the documented schedule. (2) The regression harness passes (re-run for this review, 10/10) but is invoked only manually — nothing in the repo (no package.json, no CI hook, no plan verify step outside 01-02) runs it, so a future wire-format change can skip it despite the script's own "MUST be updated" contract. **Fix:** Correct the comments to state the composed cadence; add the harness to a pre-commit or CI step alongside `pio run`.

---

## Verification of prior findings (2026-08-17 review)

| Prior ID | Status in current code |
|---|---|
| CR-01 | **Fixed** — real sequence byte written at header offset 3 before CRC (`command_protocol.cpp:61`); harness sweep proves all 65535 sequences pass |
| CR-02 | **Fixed** — `transmitCommand` uses `uint8_t buffer[CMD_MAX_PACKET_SIZE]` (`command_sender.cpp:415`); shared constant in `command_protocol.h:177` |
| CR-03 | **Fixed** — both receivers frame by header-announced length, end marker tested only at the framed position (`command_handler.cpp:644-683`, `command_sender.cpp:281-317`); harness clauses (c)/(e) lock it in |
| CR-04 | **Fixed** — PENDING transmit failures count attempts, pace via backoff, and terminate in FAILED (`command_sender.cpp:221-238`) |
| WR-03 | **Fixed** — cancelCommand decrements only from PENDING/SENT (`command_sender.cpp:148-168`) |
| WR-04 | **Fixed** — backoff guard executes above both state branches; `retryCommand` restarts the window on both outcomes (`command_sender.cpp:196-209, 428-442`) — cadence nuance in IN-13 |
| WR-05 | **Fixed** — handler sends `result.responseType`; sender treats ACK and STATUS as success (`command_handler.cpp:98-110`, `command_sender.cpp:335-340`) |
| WR-06 | Open (see above) |
| WR-07 | **Fixed** — all web handlers range-check the wide `long` before narrowing (`main_basestation.cpp` handlers) |
| WR-08 | Open (see above) |
| WR-09 | Open (see above) |
| WR-01, WR-02 | Open (see above) |
| IN-03 | **Fixed** — `connected`/`linkText` computed from ACK recency and terminal outcomes (`main_basestation.cpp:1031-1054`) |
| IN-01, IN-02, IN-04, IN-05, IN-06, IN-07, IN-08 | Open (IN-04 partially fixed; see above) |

---

_Reviewed: 2026-08-18T03:37:35Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

---
phase: 01-command-protocol-control
verified: 2026-08-18T00:02:44Z
status: gaps_found
score: 1/5 must-haves verified
behavior_unverified: 0
gaps:
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2)"
    status: failed
    reason: "CR-01: serializeCommand() computes CRC16 over a 0x00 sequence placeholder (src/command_protocol.cpp:60,79) and patches the real sequence byte into the header afterward (line 88). The balloon's validateCRC() recomputes over the transmitted bytes, so every command with (seq & 0xFF) != 0 fails CRC and is silently dropped before dispatch. Deterministic, code-level failure — not hardware-dependent. Proven by exhaustive simulation of the exact algorithm: only 255 of 65535 sequence values pass; nextSequenceNumber starts at 1 (src/command_sender.cpp:24), so the first 255 commands (and 255 of every 256 thereafter) are discarded with no ACK."
    artifacts:
      - path: "src/command_protocol.cpp"
        issue: "Lines 60/79/88: CRC computed before sequence patch. Fix: write the real sequence byte before the CRC calculation and delete the post-hoc patch."
    missing:
      - "Write real sequenceNumber into buffer[3] before calculateCRC16() in serializeCommand()"
      - "Remove the post-serialization patch at src/command_protocol.cpp:88"
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) — framing integrity"
    status: failed
    reason: "CR-03: both receivers terminate a packet as soon as the last two buffered bytes equal 0x0D 0x0A with no length awareness (src/command_handler.cpp:615-627, src/command_sender.cpp:224-235). A 0x0D 0x0A pair inside the payload/data region truncates the packet, fails CRC, and discards the remainder. Deterministic triggers with real values: AUTO_CAPTURE_ENABLE intervals 0x000D0Axx (855040-855295 ms, inside the accepted range), and ResponseStatusData with UXGA(0x0D)+quality 10(0x0A) — the balloon's own ACK for a successful status query is always dropped, causing duplicate execution and false TIMEOUTs."
    artifacts:
      - path: "src/command_handler.cpp"
        issue: "End-marker framing without length awareness (lines 615-627)"
      - path: "src/command_sender.cpp"
        issue: "Same end-marker framing defect (lines 224-235)"
    missing:
      - "Length-driven framing: accumulate header + payloadLength/dataLength + 4 bytes, then validate end marker and CRC, or byte-escape 0x0D/0x0A in payload on send and unescape on receive"
  - truth: "Failed commands are retried with timeout and user is notified (SC-4)"
    status: failed
    reason: "CR-04: a PENDING command whose transmit fails stays PENDING forever — retryCount is only incremented in retryCommand(), reachable only from the SENT-timeout branch (src/command_sender.cpp:163-173 vs 350-362). Each failed attempt blocks ~1s in E32LoRa::transmit()'s AUX polling, starving server.handleClient(); five such commands permanently fill all slots and no TIMEOUT/FAILED state is ever reached. Additionally WR-04: the retry-delay check is dead code (its only action 'continue' is the last statement of the loop body, and the timeout branch above it never consults lastRetryTime), so failed retries re-fire immediately; WR-03: cancelCommand() double-decrements pendingCommandCount for already-completed commands (underflows uint8_t to 255). User notification is aggregate counters only — the /status JSON and UI show sent/acked/failed/pending totals, but no per-command failure message is ever surfaced (handleCapture returns 'Capture command sent' immediately; the retry outcome is never reported to the user)."
    artifacts:
      - path: "src/command_sender.cpp"
        issue: "PENDING transmit-failure path has no retry accounting or terminal state (lines 163-173); retry-delay dead code (196-200); cancelCommand double-decrement (119-134)"
      - path: "src/main_basestation.cpp"
        issue: "No per-command failure notification; fire-and-forget handlers"
    missing:
      - "Retry accounting and FAILED transition on the initial-send failure path (increment retryCount, cap at maxRetries, decrement pendingCommandCount)"
      - "Honor lastRetryTime/retryDelayMs in the timeout branch before retrying; update sendTime/lastRetryTime on both success and failure"
      - "Guard cancelCommand against already-completed states"
      - "Per-command outcome surfacing in the UI (e.g. poll getCommandState(seq) after submission and display ACKED/TIMEOUT/FAILED)"
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5) / CTRL-03, CTRL-04"
    status: failed
    reason: "Auto-capture is not implemented anywhere: handleAutoCaptureEnable/Disable validate the interval then ACK success without starting any timer ('Auto-capture will be implemented in Phase 1 expansion / For now, just acknowledge' — src/command_handler.cpp:510-511, 528-529). The planned src/auto_capture.cpp and include/auto_capture.h were never created; no interval timer exists anywhere in the codebase (grep for auto-capture references hits only the placeholder handlers); the web UI has no auto-capture controls. The ACK-without-action pattern falsely reports success to the operator. Manual trigger exists end-to-end in code but is functionally defeated by the CR-01 CRC defect."
    artifacts:
      - path: "src/command_handler.cpp"
        issue: "handleAutoCaptureEnable (491-523) and handleAutoCaptureDisable (525-539) are ack-only placeholders; TODO at 545-546 for state tracking"
    missing:
      - "Interval timer module (auto_capture.h/.cpp or equivalent) that fires periodic captures when enabled"
      - "Auto-capture state tracking (enabled flag, interval, last image ID) reflected in GET_STATUS"
      - "Auto-capture UI controls (enable/disable + interval input) on the base station web interface"
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3) / CTRL-02"
    status: failed
    reason: "Two layers: (1) reception is broken upstream by CR-01 (every command dropped at CRC validation before dispatch). (2) Even with reception fixed, only 4 of 7 settings actually execute: setSaturation (408-418), setExposure (442-452), and setWBMode (476-486) are placeholder handlers that ACK success without touching CameraManager ('not exposed in CameraManager yet / For now, just acknowledge'). The SUMMARY claim 'executing CAPTURE_NOW plus all 7 settings commands against CameraManager' is false for these 3. The web UI additionally exposes only 3 of 7 settings (quality/brightness/contrast) — no resolution, saturation, exposure, or WB forms. CTRL-02 ('adjust ALL camera settings remotely') is not satisfiable end-to-end."
    artifacts:
      - path: "src/command_handler.cpp"
        issue: "Saturation/exposure/WB handlers are ack-only placeholders that never call CameraManager"
      - path: "src/main_basestation.cpp"
        issue: "handleRoot() provides forms for only 3 of 7 settings (467-497); no /set-resolution, /set-saturation, /set-exposure, /set-wb routes"
    missing:
      - "CameraManager methods (or direct sensor access) for saturation, exposure, white balance, wired into the handlers"
      - "UI forms and HTTP routes for the remaining 4 settings (resolution, saturation, exposure, WB)"
  - truth: "Protocol robustness — sender stack buffer (CR-02, latent blocker on the send path)"
    status: failed
    reason: "transmitCommand() serializes into uint8_t buffer[128] (src/command_sender.cpp:337) but serializeCommand() permits packets up to 240 bytes with payloads up to CMD_MAX_PAYLOAD_SIZE=200 (include/command_protocol.h:176). A payload >= 113 bytes overflows the stack by up to 88 bytes. sendCommand() is a public API accepting arbitrary payloadSize (createCommandPacket truncates to 200, not 112), so any future caller silently corrupts the base-station loop task stack."
    artifacts:
      - path: "src/command_sender.cpp"
        issue: "128-byte stack buffer vs 240-byte protocol maximum (line 337)"
      - path: "src/command_protocol.cpp"
        issue: "serializeCommand does not reject payloadLength > CMD_MAX_PACKET_SIZE-16"
    missing:
      - "Shared CMD_MAX_PACKET_SIZE=240 constant; size both send/receive buffers to it; reject oversized payloads in serializeCommand"
deferred: []
---

# Phase 1: Command Protocol & Control — Verification Report

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-18T00:02:44Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

The phase goal — bidirectional LoRa communication for camera control — is **not achieved**. The architecture is fully built and both firmware targets compile, but the command link as written cannot function: a serialization defect (CRC computed before the sequence byte is patched) deterministically causes the balloon to discard virtually every command from the base station. This is a code-level, arithmetic-verifiable failure, not a hardware-verification question. Additionally, auto-capture (CTRL-03/CTRL-04) is entirely unimplemented, 3 of 7 camera settings are placeholder handlers that ACK without acting, and the retry mechanism has a code-verified defect where transmit failures never time out.

### MVP Mode Discrepancy

ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal ("Establish bidirectional LoRa communication for camera control") is not in user-story format — `gsd-tools query user-story.validate` returned `valid: false`. Per `references/verify-mvp-mode.md`, MVP-mode user-flow framing requires BOTH mode=mvp AND a user-story goal, so this report uses standard goal-backward verification. **Recommendation:** run `/gsd mvp-phase 1` to reformat the goal as "As a [role], I want to [capability], so that [outcome]" so future phases get User Flow Coverage verification.

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (PLAN.md has no frontmatter `must_haves`; the roadmap contract governs and cannot be reduced by the plan).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | `src/main_basestation.cpp` handleRoot() renders trigger button (456-461) + quality/brightness/contrast forms (467-497); POST routes wired to CmdSender().sendCommand() (392-396, 505-594); status bar polls /status every 1s (footer script 268-286). Note: 3 of 7 settings only — the "all settings" gap is tracked under CTRL-02/truth 3. |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✗ FAILED | CR-01 confirmed in `src/command_protocol.cpp:60,79,88`: CRC16 computed over `0x00` sequence placeholder, real sequence patched after. Balloon `validateCRC` (24-38) recomputes over transmitted bytes. Exhaustive simulation of the exact algorithm: only 255/65535 sequence values accepted (multiples of 256); sender starts at seq=1 (`command_sender.cpp:24`) so the first 255 commands are silently dropped, exhaust retries, report TIMEOUT. Secondary: CR-03 non-transparent framing truncates any packet whose payload contains `0x0D 0x0A` (`command_handler.cpp:615-627`). |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ✗ FAILED | Reception blocked by CR-01 upstream (commands rejected before dispatch). Additionally `command_handler.cpp` handlers for saturation (408-418), exposure (442-452), WB mode (476-486) are "For now, just acknowledge" placeholders that never call CameraManager — they ACK success for a no-op. Only capture/resolution/quality/brightness/contrast touch CameraManager. |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✗ FAILED | Retry state machine structurally present for ACK-timeout (SENT + 2000ms → retry ≤3 → TIMEOUT, `command_sender.cpp:176-193`) but: CR-04 — PENDING transmit-failure path never increments retryCount or reaches a terminal state (163-173; retryCommand 350-362 unreachable from PENDING), producing an unbounded ~1s-blocking retry storm when the radio is unreachable; WR-04 — retry-delay enforcement is dead code (196-200); WR-03 — cancelCommand double-decrements pendingCommandCount (119-134). User notification is aggregate counters in /status only; no per-command outcome is ever surfaced to the user. |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✗ FAILED | Auto-capture unimplemented: handlers ACK without starting a timer (`command_handler.cpp:510-511, 528-529` — "will be implemented in Phase 1 expansion / For now, just acknowledge"); planned `auto_capture.h/.cpp` never created; no interval timer anywhere in the codebase; no auto-capture UI controls. Manual trigger path exists but is defeated by CR-01. |

**Score:** 1/5 truths verified (0 present, behavior-unverified — the unexercised ACK-timeout retry path is folded into truth 4's failure rather than counted separately, since two code-verified defects already fail that truth)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/command_protocol.h` | Command/response packet structures, constants | ✓ VERIFIED | 251 lines; all command/response types, constants; substantive |
| `src/command_protocol.cpp` | CRC16, serialization, factories | ✓ VERIFIED (substantive, wired) | 388 lines; wired from sender + handler; contains CR-01 defect |
| `include/e32_lora.h` / `src/e32_lora.cpp` | E32 UART driver | ✓ VERIFIED | 157+482 lines; M0/M1 mode control, AUX monitoring, config mode; wired from both mains. WR-01: transmit blocks up to 7s (1000+1000+5000ms AUX polls) violating the no-blocking constraint; WR-02: config API contradicts E32 datasheet (no 0xC0 prefix) — unused today |
| `include/command_sender.h` / `src/command_sender.cpp` | Retry state machine, pending queue | ✓ VERIFIED (substantive, wired) | 122+432 lines; wired into basestation loop; contains CR-02/CR-04 defects |
| `include/command_handler.h` / `src/command_handler.cpp` | Command dispatch, camera execution, ACK/NACK | ⚠️ PARTIAL | 107+705 lines, wired into balloon loop; 5 of 11 handlers are placeholder ACKs (saturation, exposure, WB, auto-capture enable/disable) |
| `src/main_basestation.cpp` | WiFi AP, web server, control endpoints | ✓ VERIFIED (substantive, wired) | 642 lines; AP + WebServer + routes + loop |
| `src/main_balloon.cpp` (modified) | Integrate command handler | ✓ VERIFIED | Includes + init (380-391) + CmdHandler().process() in loop (758) |
| `include/common_types.h` (modified) | COMMAND/RESPONSE packet types | ✓ VERIFIED | COMMAND=0x10, RESPONSE=0x11 (lines 37-38) |
| `platformio.ini` (modified) | Both build targets | ✓ VERIFIED | `esp32-s3-balloon` (65) and `esp32-s3-basestation` (254) envs with src_filter separation |
| `src/auto_capture.cpp`, `include/auto_capture.h`, `src/web_server.cpp`, `src/html/camera_control.html` (planned) | Auto-capture timer, UI | ✗ MISSING | Never created — root of the SC-5 gap (UI was embedded in main_basestation.cpp instead, an acceptable substitution for web_server.cpp/html) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| main_basestation.cpp handlers | CommandSender | `CmdSender().sendCommand(...)` (508, 536, 561, 586) | ✓ WIRED | All four control endpoints reach the sender |
| CommandSender | E32LoRa | `lora->transmit/read/available` (347, 146-148) | ✓ WIRED | Send and receive paths connected |
| main_balloon.cpp | CommandHandler | `begin()` (386) + `process()` (758) | ✓ WIRED | Init and main-loop processing active |
| CommandHandler | CameraManager | `camera->captureImage/setFrameSize/setQuality/setBrightness/setContrast` | ⚠️ PARTIAL | Wired for capture + 4 settings; saturation/exposure/WB handlers never call CameraManager |
| CommandHandler | E32LoRa (response path) | `sendResponse` → `lora->transmit` (585) | ✓ WIRED | ACK/NACK responses transmitted; response serialization itself is CRC-correct (real refSequence written before CRC, line 161) |
| Web UI | /status | footer script fetch (268-286) | ✓ WIRED | 1s polling updates counters and LED |

**Logical end-to-end link (base → balloon → ACK → base):** wired in code at every hop, but **non-functional** — CR-01 prevents commands from reaching dispatch, so no ACK is ever generated for the first 255 commands. The wiring is present; the protocol logic breaks the chain.

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| main_basestation.cpp /status | commandsSent/Acked/Failed | CmdSender() counters via processLoRa() (410-412) | Yes | ✓ FLOWING |
| main_basestation.cpp /status | `connected` | Hardcoded literal `"connected":true` (598) | No | ⚠️ STATIC — LED always green even with link dead (IN-03) |
| UI settings forms | quality/brightness/contrast | `server.arg(...)` from POST | Yes | ✓ FLOWING |
| handleGetStatus response | imageId/autoCaptureEnabled/... | Hardcoded 0s + defaults (545-549) | No | ⚠️ STATIC — TODO markers at 545-546; status data is fabricated |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Both firmware targets build | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` | `2 succeeded in 00:00:41` — both SUCCESS, exit 0 (run by verifier, not trusted from SUMMARY) | ✓ PASS |
| Command round-trip survives balloon validation (seq 1..65535, exact algorithm transcribed from src/command_protocol.cpp) | host simulation: serialize with seq → validateCRC → deserialize | 255/65535 accepted (only multiples of 256); seq=1,2,3,255 all REJECTED — commands silently dropped before dispatch | ✗ FAIL |
| Retry state machine exercised by a test | none exists — no test/ directory, no host unit tests; test_*.cpp are firmware test mains excluded from both env builds | n/a | ? SKIP (no test infrastructure; runtime behavior needs hardware) |

### Probe Execution

No probes declared in PLAN.md or SUMMARY.md; no `scripts/*/tests/probe-*.sh` exists. SKIPPED.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-01 (T1-T8) | Trigger camera capture from base station web UI | ✗ BLOCKED | Code complete end-to-end (UI → sender → protocol → handler → camera->captureImage) but functionally defeated by CR-01: every command fails balloon CRC validation. Hardware UAT additionally outstanding. SUMMARY claimed complete — overstated. |
| CTRL-02 | 01-01 (E1-E4) | Adjust ALL camera settings remotely (7 settings) | ✗ BLOCKED | UI exposes 3 of 7 (quality/brightness/contrast); balloon executes 4 of 7; saturation/exposure/WB handlers are ack-only placeholders ("not exposed in CameraManager yet"). No end-to-end path exists for 3 settings. SUMMARY itself admits partial — accurate. |
| CTRL-03 | 01-01 (A1-A4) | Manual + automatic capture modes | ✗ BLOCKED | Automatic mode unimplemented (placeholder ACKs, no timer, no UI). Manual path broken by CR-01. SUMMARY admits NOT delivered — accurate. |
| CTRL-04 | 01-01 (A2) | Auto-capture fixed interval timing | ✗ FAILED | Not implemented at all — no interval timer anywhere in the codebase. SUMMARY admits NOT delivered — accurate. |
| CTRL-06 | 01-01 (T4) | Failed commands retried with timeout | ✗ BLOCKED | Retry logic present for ACK-timeout (2000ms, 3 retries, 5-slot queue) but CR-04 transmit-failure path never terminates, WR-04 pacing dead, WR-03 counter underflow; no tests; hardware verification outstanding. |
| PRI-02 | 01-01 (T4) | Camera commands use retry with timeout | ✗ BLOCKED | Same implementation and defects as CTRL-06. |

Orphaned requirements: none — REQUIREMENTS.md traceability maps exactly CTRL-01/02/03/04/06 + PRI-02 to Phase 1, matching all plans.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/command_protocol.cpp | 60, 79, 88 | CRC computed over placeholder then patched — false-success/protocol break | 🛑 Blocker | Every command with nonzero seq byte fails validation; link non-functional |
| src/command_handler.cpp | 510-511, 528-529 | Placeholder ACKs — reports success for unimplemented auto-capture | 🛑 Blocker | Operator falsely told auto-capture is active; CTRL-03/04 undelivered |
| src/command_handler.cpp | 408-418, 442-452, 476-486 | Placeholder ACKs for saturation/exposure/WB ("For now, just acknowledge") | 🛑 Blocker | False success for no-op settings; contradicts SUMMARY claim of "all 7 settings against CameraManager" |
| src/command_sender.cpp | 163-173 | No terminal state on transmit failure (CR-04) | 🛑 Blocker | Unbounded blocking retry storm when radio unreachable; event loop starved |
| src/command_sender.cpp | 337 | 128-byte stack buffer vs 240-byte max (CR-02) | 🛑 Blocker (latent) | Stack overflow for payloads ≥113 bytes via public API |
| src/command_handler.cpp | 545-546 | `TODO: Track last image ID` / `TODO: Track auto-capture state` — unreferenced | ⚠️ Warning | GET_STATUS returns fabricated static data |
| src/main_basestation.cpp | 527, 552, 577 | Integer truncation before range validation (WR-07) | ⚠️ Warning | `quality=300` truncates to 44 and passes; input-validation backdoor on unauthenticated endpoints |
| src/main_basestation.cpp | 598 | `"connected":true` hardcoded (IN-03) | ⚠️ Warning | Link-health LED permanently green |
| src/e32_lora.cpp | 158-202 | Blocking transmit up to 7s in main loop (WR-01) | ⚠️ Warning | Violates single-threaded no-blocking constraint; false transmit failures on short packets |
| src/e32_lora.cpp | 264-344, 425-443 | E32 config API contradicts datasheet (WR-02) | ⚠️ Warning | Currently uncalled; will write garbage registers on hardware bring-up |
| src/command_handler.cpp | 93-106 | STATUS response type discarded, always sent as ACK (WR-05) | ⚠️ Warning | Typed status path unusable; sender treats STATUS as failure if ever typed correctly |
| platformio.ini | 75 | TODO: Rewrite lora_comm for E32 | ℹ️ Info | Config comment, pre-existing |
| src/main_balloon.cpp | 878 | "For now, it's a placeholder" | ℹ️ Info | Pre-existing section, not phase-modified logic |

No TBD/FIXME/XXX markers found in any phase file.

### SUMMARY vs Reality Discrepancies

1. SUMMARY claims "Balloon handler executing CAPTURE_NOW plus all 7 settings commands against CameraManager" — **false**: 3 of 7 (saturation, exposure, WB) are ack-only placeholders that never call CameraManager (`command_handler.cpp:408-418, 442-452, 476-486`).
2. SUMMARY lists `requirements-completed: [CTRL-01, CTRL-06, PRI-02]` as "code-complete" — the code exists and compiles, but CR-01 deterministically prevents any command from being accepted by the balloon, so "complete" materially overstates delivery of even these three.
3. SUMMARY's admissions (CTRL-02 partial, CTRL-03/04 not delivered, hardware pending) are accurate.
4. SUMMARY's 8 task commits verified — all exist (`1f9de55..3962ccf`), matching `git log`.
5. Build claim verified independently: both targets SUCCESS.

### Human Verification Required (post-gap-closure)

These items cannot be verified without physical hardware and remain outstanding even after the code gaps are fixed. They are recorded here so the next verification cycle and UAT pick them up; they are NOT the cause of the gaps_found status.

1. **End-to-end command round trip over real radios**
   - Test: Power both ESP32-S3 boards with E32 modules linked; click Trigger Camera Capture in the web UI.
   - Expected: Balloon captures, ACK received within 2s, UI counter increments.
   - Why human: RF delivery, AUX timing, and mode switching require physical modules (WR-01/WR-02 affect this).
2. **Retry behavior over a degraded link**
   - Test: Power balloon off; issue a capture command.
   - Expected: 3 retries over ~6s, then TIMEOUT surfaced in UI.
   - Why human: Retry state transitions at runtime need two radios; no host tests exist.
3. **Camera settings take effect on the sensor**
   - Test: Send SET_QUALITY / SET_RESOLUTION from the UI; capture and inspect the image.
   - Why human: Requires camera module; visual image assessment.
4. **Auto-capture at interval (after implementation)**
   - Test: Enable auto-capture at 10s; observe captures every 10s; disable; observe stop.
   - Why human: Timing behavior across the radio link requires hardware.

### Gaps Summary

The phase built the full architecture — protocol, driver, sender with retry, handler, web UI — and both firmware targets compile cleanly (independently verified). But the goal "bidirectional LoRa communication for camera control" is not achieved:

1. **The command link is deterministically broken (CR-01).** CRC16 is computed over a zero placeholder where the sequence number belongs, then the real value is patched in afterward. The balloon's validator recomputes over the transmitted bytes and rejects. Exhaustive enumeration proves 255 of 65535 sequences pass — and the sender's counter starts at 1, so the first 255 commands (and 255 of every 256 after) are silently dropped with no ACK. Every retry fails identically. This alone fails SC-2 and SC-3 and functionally defeats CTRL-01. Fix is three lines (write real sequence before CRC, delete the patch).
2. **Even with CR-01 fixed, CR-03 kills specific real payloads** — any packet whose payload contains `0D 0A` (including plausible auto-capture intervals and the balloon's own UXGA status data) truncates at the receiver.
3. **Auto-capture (SC-5, CTRL-03/04) is entirely absent** — placeholder handlers ACK success without starting any timer; the planned timer module was never created; no UI controls exist.
4. **Three of seven camera settings are no-op ACKs** (CTRL-02), contradicting the SUMMARY's "all 7 settings" claim; the UI exposes only 3.
5. **The retry mechanism fails on the transmit-failure path (CR-04)** — PENDING commands retry forever without reaching TIMEOUT, blocking the event loop ~1s per attempt when the radio is unreachable; retry pacing is dead code; user notification of failures is aggregate counters only.
6. **Latent stack overflow (CR-02)** on the public send API (128-byte buffer vs 240-byte protocol max).

The code review's 4 Critical findings (01-REVIEW.md) were independently re-verified line-by-line in the current code — all four are present and unfixed. All identified gaps are addressable in code without hardware; hardware UAT (items in the section above) follows gap closure.

No gaps were deferred: Phase 2 covers event-based triggers (CTRL-05), which is distinct from CTRL-04's fixed interval; no later phase addresses the protocol defects or the missing interval auto-capture. The SUMMARY itself lists these as Phase 1 blockers.

---

_Verified: 2026-08-18T00:02:44Z_
_Verifier: Claude (gsd-verifier)_

---
phase: 01-command-protocol-control
verified: 2026-08-18T03:49:52Z
status: gaps_found
score: 1/5 must-haves verified
behavior_unverified: 3 # SC-2/SC-3/SC-4 present + wired; runtime behavior needs hardware UAT
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 1/5
  gaps_closed:
    - "SC-2 protocol defects: CR-01 (CRC over sequence placeholder) and CR-03 (end-marker truncation) — closed, proven by harness re-run"
    - "SC-4 retry mechanics: CR-04 terminal state, WR-03 cancel guard, WR-04 dead pacing — closed; D-05/D-07 encoded"
    - "SC-3 placeholder handlers: saturation/exposure/WB now execute against the sensor (3 of 7 -> 7 of 7)"
    - "SC-5 implementation: AutoCapture module created and wired; GET_STATUS truthful; UI auto-capture controls delivered"
    - "CR-02 latent stack overflow: shared CMD_MAX_PACKET_SIZE constant, buffers resized"
  gaps_remaining:
    - "SC-5 disable semantics: CR-05 legacy 30 s capture timer still runs beside commanded auto-capture"
  regressions: [] # nothing previously passing now fails; CR-05 is a pre-existing code path newly conflicting with the delivered commanded-control semantics
gaps:
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5) — plan 01-04 must-have clause: 'disabling stops it'"
    status: failed
    reason: "CR-05: the balloon's legacy 30-second capture timer (src/main_balloon.cpp:658-691, processCamera) still runs unconditionally in loop() (line 253), gated only on appState.cameraActive — never on commanded auto-capture state. CameraManager::captureImage() resets lastCaptureTime (camera_manager.cpp:194), so while commanded auto-capture runs at <=30 s intervals the legacy timer is masked, but the moment the user disables auto-capture (or sets an interval >30 s) the legacy path takes over within 30 s. Deterministic, code-level: (1) after an ACKed AUTO_CAPTURE_DISABLE the balloon keeps capturing automatically every 30 s — the commanded-control contract 'disable stops automatic capture' does not hold; (2) intervals >30 s are polluted by interleaved legacy captures (cadence != commanded interval); (3) the legacy static nextImageId counter (lines 676-677) is a second image-ID sequence independent of AutoCapture::allocateImageId(), so GET_STATUS lastImageId can name an image other than the last captured, and Phase 2 image sequencing inherits colliding IDs from ID 1; (4) each legacy capture frees/replaces the camera frame buffer, destroying any commanded capture awaiting future transmission."
    artifacts:
      - path: "src/main_balloon.cpp"
        issue: "processCamera() legacy 30 s timer (line 667) with independent image-ID counter (lines 676-677), ungated by commanded auto-capture state; runs beside AutoCap().process() (line 768)"
    missing:
      - "Remove the legacy capture block from processCamera() (or gate it behind an explicit debug flag); if any periodic capture is kept, route its image ID through AutoCap().allocateImageId() so AutoCapture stays the single capture/ID authority"
deferred:
  - truth: "D-13: separate camera-controls page from telemetry display"
    addressed_in: "Phase 3"
    evidence: "Plan 01-03 truth: 'deferred to Phase 3 (WEB-04 top-down layout)'; Phase 3 goal 'Full base station control panel with telemetry, maps, and gallery'; accepted in commit 0b52b9e"
  - truth: "D-15: accordion panels grouping camera settings"
    addressed_in: "Phase 3"
    evidence: "Plan 01-03 truth: 'deferred to Phase 3 (WEB-04 top-down layout)'; accepted in commit 0b52b9e"
behavior_unverified_items:
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2)"
    test: "Power both ESP32-S3 boards with E32 modules linked; issue a capture command from the web UI"
    expected: "Balloon executes, ACK arrives within the D-05 window (2 s for CAPTURE_NOW), UI queue row shows ACK Received; degraded link yields 3 paced retries then Timeout"
    why_human: "Requires physical radios; E32 AUX timing (WR-01 blocking transmit / AUX-low race) can only be evaluated on hardware — no host harness drives the sender/handler state machines over a link"
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "Send SET_RESOLUTION / SET_SATURATION / SET_EXPOSURE / SET_WB from the UI, then capture and inspect the image"
    expected: "Sensor accepts the values; visible change in captured images; NACK_BUSY if a sensor call fails"
    why_human: "Sensor acceptance of saturation/ae_level/wb_mode values and visual image assessment need the physical camera module"
  - truth: "Failed commands are retried with timeout and user is notified (SC-4)"
    test: "Power the balloon off; issue a capture command; watch the Command Queue panel"
    expected: "Row shows Sent, then Failed/Timeout after 3 paced attempts (D-05 window + D-07 backoff); LED turns red 'No link'; counters advance"
    why_human: "Retry state transitions at runtime need two radios; no host test exercises the CommandSender state machine (millis/E32-coupled)"
---

# Phase 1: Command Protocol & Control Verification Report (Gap-Closure Re-Verification)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-18T03:49:52Z
**Status:** gaps_found
**Re-verification:** Yes — after gap closure (plans 01-02, 01-03, 01-04 executed)

## Goal Achievement

The gap-closure work was real and effective: all five previously-failed defect classes are verifiably closed in the current code, proven by independent evidence (wire-format harness re-run by this verifier: 10/10; dual-target build re-run: 2 succeeded; line-level reads of every fixed site). The command protocol is now arithmetically sound end to end, all 7 camera settings execute against the sensor, the retry state machine reaches terminal states with pacing, and the UI delivers per-command outcomes, all settings forms, auto-capture controls, and a computed link LED.

One gap remains, and it is new information surfaced by the post-execution code review (01-REVIEW.md CR-05), independently confirmed by this verifier in the code: **the balloon's legacy 30-second capture timer still runs beside the new commanded auto-capture**. It deterministically breaks the disable semantics that plan 01-04's own must-have specifies ("disabling stops it"), corrupts interval fidelity above 30 s, and introduces a second, colliding image-ID sequence. SC-5 therefore remains FAILED — for a different root cause than before.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as the initial verification).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | 12 routes registered (main_basestation.cpp:461-472) incl. all 7 settings + auto-capture pair; Trigger Camera Capture button (567); resolution/WB selects with documented defaults; long-first validation in every handler (8 range-check sites, zero narrowing-before-check patterns); Command Queue panel + cmd-failed cell + 480px collapse |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | All deterministic code blockers closed and behaviorally proven: CR-01 fixed (command_protocol.cpp:61 — real sequence byte before CRC at :81; no post-hoc patch); CR-03 fixed (length-driven framing both receivers: command_handler.cpp:644-683 header+5+bodyLen+4, command_sender.cpp:281-317 header+4+bodyLen+4; bogus-length reset). Harness re-run by verifier: 10/10 PASS incl. all-65535-sequence sweep and defective-variant teeth proof. CR-02 closed (CMD_MAX_PACKET_SIZE=240, buffer[CMD_MAX_PACKET_SIZE] at command_sender.cpp:415). Remaining: physical RF round trip (incl. WR-01 AUX race) — hardware UAT item 1 |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | All 7 settings now execute against the sensor: CameraManager::setSaturation/setExposure/setWBMode real (camera_manager.cpp:426-474, setContrast pattern, sensor calls verified); handlers call them and ACK only on true return, NACK_BUSY otherwise (command_handler.cpp:412, 453, 495); WB validation rejects outside 0..4 incl. negatives; stub comments and placeholder strings gone (grep clean). Zero ack-only handlers remain. Remaining: sensor acceptance of values on physical camera — hardware UAT item 3 |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Mechanics closed: PENDING transmit-failure terminal path (command_sender.cpp:221-238 — retryCount++, FAILED + pendingCommandCount-- + commandsFailed++ at maxRetries); D-05 ackTimeoutFor (24-46) used in SENT timeout (243); D-07 backoff above state branches with clamped shift (196-209); retryCommand sets sendTime on both outcomes (428-442); cancel guard (157-159); pendingCommandCount-- exactly 5 counted sites; WR-05 STATUS-as-success (335-340) with handler typing (command_handler.cpp:98-110). Notification half delivered: getCommandQueue/getCommandRetryCount consumed by /status (main_basestation.cpp:1011, 1024), locked vocabulary via shared commandStateToString (972-989), pinned row + queue rows, computed link LED (1031-1054). Remaining: runtime retry/timeout transitions — hardware UAT item 2 |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✗ FAILED | Enable path real: AutoCapture module (auto_capture.cpp) validates 1000-3600000 ms, resets baseline (first capture one interval after enable), wraparound-safe process() with baseline-before-attempt, no delay(); wired in main_balloon.cpp (begin :393, process :768); GET_STATUS truthful for the module (command_handler.cpp:576-578). **But CR-05: legacy 30 s timer in processCamera() (main_balloon.cpp:667, called unconditionally from loop() :253) keeps capturing after an ACKed AUTO_CAPTURE_DISABLE — within 30 s of the disable, because captureImage() resets lastCaptureTime (camera_manager.cpp:194) and nothing else captures — and interleaves extra captures when the commanded interval >30 s. Its static nextImageId (:676-677) is a second ID sequence colliding with AutoCapture::allocateImageId() from ID 1. Plan 01-04's must-have clause "disabling stops it" does not hold at system level; fix is code-only (remove/gate the legacy block).** |

**Score:** 1/5 truths verified (3 present, behavior-unverified — routed to human UAT; 1 failed)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | D-13: separate camera-controls page (controls stay cards on the single page) | Phase 3 | WEB-04 top-down layout restructure; plan 01-03 deferral truth; commit 0b52b9e |
| 2 | D-15: accordion settings groups (stacked single-field forms retained) | Phase 3 | WEB-04 top-down layout restructure; commit 0b52b9e |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/command_protocol.h` | Shared packet-limit + D-05/D-07 constants | ✓ VERIFIED | CMD_MAX_PACKET_SIZE=240 (:177), CMD_ACK_TIMEOUT_{TRIGGER,SETTINGS,COMPLEX}_MS 2000/5000/10000 (:181-183), CMD_RETRY_BACKOFF_BASE_MS=2000 (:186) |
| `src/command_protocol.cpp` | CRC over real sequence byte; oversize rejection | ✓ VERIFIED | :61 real seq byte before CRC; :53 CMD_MAX_PACKET_SIZE rejection. Note WR-10 window (payload 201-224 passes size check but copy skipped at :75 — header/body mismatch; unreachable via current callers which clamp to 200) |
| `include/command_sender.h` / `src/command_sender.cpp` | Retry terminal states, framing, queue API | ✓ VERIFIED | All plan 01-02 must-have elements verified line-level (see truth 4); getCommandQueue (482-502), getCommandRetryCount (472-480), buffer[CMD_MAX_PACKET_SIZE] (415) |
| `src/command_handler.cpp` | Length-driven framing, real handlers, typed responses, truthful status | ✓ VERIFIED | Framing :644-683; all handlers execute (no placeholder/TODO/stub strings — grep clean); STATUS typing :98-110; GET_STATUS sourced from AutoCap + camera getters :576-583 |
| `include/auto_capture.h` / `src/auto_capture.cpp` | Interval timer module | ✓ VERIFIED | Full API implemented; bounds re-validated in enable(); baseline semantics correct; no delay(); single ID authority within the module |
| `src/camera_manager.h` / `.cpp` | Sensor setters for 3 settings | ✓ VERIFIED | setSaturation/setExposure/setWBMode (:426-474) mirror setContrast; getters declared |
| `src/main_basestation.cpp` | Complete UI layer | ✓ VERIFIED | 12 routes, 7 forms, auto-capture card, queue panel, computed LED, WR-07-safe validation |
| `src/main_balloon.cpp` | AutoCap wiring + handler integration | ⚠️ PARTIAL | AutoCap().begin (:393) / process (:768) wired; CmdHandler wired (:387, :765) — **but legacy 30 s capture block retained (CR-05, see gap)** |
| `platformio.ini` | Build separation | ✓ VERIFIED | -<auto_capture.cpp> in basestation filter (:277); both envs build SUCCESS (verifier-run) |
| `scripts/verify_protocol_roundtrip.mjs` | Host wire-format regression harness | ✓ VERIFIED | Exists, substantive; re-run by verifier: exit 0, 10/10 clauses |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| main_basestation POST routes | CmdSender().sendCommand | 10 command routes call sendCommand (incl. 4 new settings + auto-capture pair); writeUint32 big-endian interval (:936) | ✓ WIRED | All control endpoints reach the sender |
| main_balloon loop | AutoCapture | AutoCap().begin(&Camera()) :393 + AutoCap().process() :768, every pass | ✓ WIRED | Module fires in the live loop |
| command_handler handlers | CameraManager | camera->setSaturation/:412, setExposure/:453, setWBMode/:495 + capture/resolution/quality/brightness/contrast | ✓ WIRED | All 7 settings + capture execute against the sensor |
| command_handler auto-capture handlers | AutoCap() | AutoCap().enable(intervalMs) :536, disable() :559 | ✓ WIRED | ACK reflects the module call — but see CR-05: system-level disable contract broken by the separate legacy path |
| main_balloon loop | Legacy 30 s capture | processCamera() :253/:667, gated only on cameraActive | ✗ CONFLICTING WIRING | The root of CR-05: an ungated second capture trigger beside AutoCap |
| handleStatus | CmdSender state queries | getCommandState/getCommandRetryCount/getCommandQueue (:1011-1028) | ✓ WIRED | Per-command outcomes + queue rows + computed link flow to the 1 s poll |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| /status | sent/acked/failed/pending | CmdSender counters + hasPendingCommands | Yes | ✓ FLOWING |
| /status | lastCmd/lastSeq/lastState/lastRetry | appState + getCommandState/RetryCount | Yes | ✓ FLOWING |
| /status | connected/linkText | Computed from lastAckTime + terminal outcomes (:1031-1054) | Yes | ✓ FLOWING (hardcoded literal gone — IN-03 closed at code level) |
| /status | queue[] | getCommandQueue over 5-slot table (:1011, 1081) | Yes | ✓ FLOWING |
| GET_STATUS response | imageId/enabled/interval/settings | AutoCap() + camera getters (command_handler.cpp:576-583) | Yes, module-truthful | ⚠️ FLOWING-WITH-DIVERGENCE — lastImageId tracks only AutoCapture-allocated IDs; legacy-timer captures (CR-05) are invisible to it, so the value can diverge from the actual last capture |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite (all sequences, framing, oversize, teeth proof) | `node scripts/verify_protocol_roundtrip.mjs` (run by verifier, not trusted from SUMMARY) | 10/10 PASS, exit 0 | ✓ PASS |
| Both firmware targets compile | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (run by verifier) | 2 succeeded in 00:00:46 | ✓ PASS |
| Retry state machine exercised by a test | none exists — no host harness for CommandSender state machine (millis/E32-coupled) | n/a | ? SKIP (hardware UAT item 2) |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists. The declared `scripts/verify_protocol_roundtrip.mjs` verification command was executed above (PASS). SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-01, 01-02, 01-03 | Trigger camera capture from base station web UI | ✓ SATISFIED (code; UAT 1 outstanding) | Full path repaired end to end: UI → sender → CRC-correct protocol → handler → camera->captureImage(); shared image ID |
| CTRL-02 | 01-01, 01-03, 01-04 | Adjust ALL camera settings remotely | ✓ SATISFIED (code; UAT 3 outstanding) | 7 of 7 UI forms/routes with safe validation + 7 of 7 sensor-executing handlers; zero ack-only stubs remain |
| CTRL-03 | 01-01, 01-03, 01-04 | Manual + automatic capture modes | ✗ BLOCKED | Manual mode complete; automatic enable complete — but disable does not stop automatic capture (CR-05): the balloon continues capturing every 30 s after an ACKed disable. REQUIREMENTS.md marks this Complete — overstated |
| CTRL-04 | 01-01, 01-03, 01-04 | Automatic capture fixed interval timing | ✗ BLOCKED | Commanded interval honored only while ≤30 s (legacy timer masked); intervals >30 s polluted by interleaved legacy captures; after disable a 30 s cadence persists (CR-05). REQUIREMENTS.md marks this Complete — overstated |
| CTRL-06 | 01-01, 01-02, 01-03 | Failed commands retried with timeout | ✓ SATISFIED (code; UAT 2 outstanding) | Terminal states on all paths, D-05 windows, D-07 pacing, cancel guard, per-command outcome UI in locked vocabulary |
| PRI-02 | 01-01, 01-02 | Retry with timeout for failed transmissions | ✓ SATISFIED (code; UAT 2 outstanding) | Same evidence as CTRL-06; retry storm bounded, loop no longer starved indefinitely |

Orphaned requirements: none — REQUIREMENTS.md traceability maps exactly the six phase IDs; all appear in plan `requirements` fields.

### Plan Prohibition Verdicts (judgment-tier — autonomous, non-authoritative; human review recommended at UAT)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-02 | MUST NOT let command processing starve the telemetry/flight loop (paced retries, terminal states) | PASS (code-level) | Terminal states + backoff verified; residual ~1 s per-attempt AUX block (WR-01) deferred to hardware bring-up per plan — evaluate at UAT item 2 |
| 01-03 | MUST NOT show green/Ready LED without recent confirmed activity or after last command failed | PASS (code-level) | Computation verified (:1031-1054): red while failure newer than last ACK, green only within LINK_STALE_MS of an ACK; runtime truthfulness = UAT |
| 01-04 | MUST NOT acknowledge success for actions not executed (no ack-only stubs, no fabricated status) | **VIOLATED IN EFFECT (system level)** — `unverified-prohibition — human review recommended` | Every handler now acts (handler-level PASS), but AUTO_CAPTURE_DISABLE ACKs "disabled" while automatic capture continues via the legacy path (CR-05) — the operator is told auto-capture is off when the balloon keeps auto-capturing within 30 s. Same root cause as the SC-5 gap |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/main_balloon.cpp | 658-691 | Legacy 30 s capture timer + independent image-ID counter beside commanded auto-capture (CR-05) | 🛑 Blocker | Disable semantics broken; interval fidelity broken >30 s; ID collision; frame-buffer churn — the SC-5 gap |
| src/command_protocol.cpp | 53, 75 | WR-10: length-validation window (payload 201-224 / data 51-225) emits header/body-mismatch packets reported as success; harness clause (b) certifies the malformed 224-byte boundary as valid | ⚠️ Warning | Unreachable via current callers (all factories clamp); defense gap + test institutionalizes it |
| src/e32_lora.cpp | 158-202 | WR-01 (carried): blocking transmit up to ~7 s in both loops; AUX-low race can false-fail short sends | ⚠️ Warning | Deferred to hardware bring-up (documented in plan 01-02); risks false transmit failures at UAT |
| src/e32_lora.cpp | 204-443 | WR-02 (carried): config API contradicts E32 datasheet | ⚠️ Warning | Uncalled; fix at radio bring-up |
| src/command_handler.cpp:633-640, src/command_sender.cpp:270-277 | — | WR-06 (carried): doubled 0xAA loses packet start (resync flaw) | ⚠️ Warning | Noise-robustness gap; no resync re-anchor |
| src/command_handler.cpp | 671-679 | WR-08 (carried): second command in same read burst overwrites pending one | ⚠️ Warning | Recovered only via base-station retry after a full D-05 window |
| src/command_protocol.cpp | 113, 161-169 | WR-09 (carried): packet type byte never validated; response length-field width asymmetry | ⚠️ Warning | Misframing risk for cross-direction packets |
| src/command_handler.cpp | 118-144 | IN-12: camera-ready gate NACK_BUSYs GET_STATUS and AUTO_CAPTURE_DISABLE; generic capture failure typed BUSY | ⚠️ Warning | With camera init failed, disable can never be ACKed — compounds CR-05's disable story in the degraded case |
| src/command_handler.cpp / main_basestation.cpp | — | IN-09: GET_STATUS has no caller anywhere; IN-13: harness not wired into an automated gate | ℹ️ Info | Truthful status implemented but unreachable; harness is manual-only |
| src/main_balloon.cpp | 886-889 | Empty processIncomingCommands() ("For now, it's a placeholder") | ℹ️ Info | Pre-existing vestigial stub; command flow uses CmdHandler().process() — not phase logic, but a named placeholder worth deleting during CR-05 cleanup |
| src/command_protocol.cpp | 163 | "CRC8 placeholder" pad byte comment | ℹ️ Info | Wire-format reserved byte; intentional, mirrors harness |

No TBD/FIXME/XXX markers in any phase-modified file.

### SUMMARY vs Reality

1. 01-02/01-03/01-04 SUMMARY claims are accurate at the code level — every structural gate re-verified independently (constants, framing, terminal states, setters, wiring, filters), and both runnable claims (harness, build) reproduced by this verifier.
2. All 9 task commits verified in git log (fd77066..6850f54 + docs).
3. The one overstatement is systemic, not per-plan: 01-04's "every ACK corresponds to an executed action ... CTRL-03/CTRL-04 delivered" and REQUIREMENTS.md's CTRL-03/CTRL-04 "Complete" do not account for CR-05 — the module-level delivery is real, but the system-level disable contract does not hold. The review (01-REVIEW.md, committed after the SUMMARYs) is correct; this verifier confirmed the legacy timer path line-by-line.

### Human Verification Required (post-gap-closure)

Recorded so the next cycle and UAT pick them up; not the cause of gaps_found (that is CR-05 alone).

1. **End-to-end command round trip over real radios** — Power both boards; trigger a capture. Expect ACK within 2 s, queue row "ACK Received", counters advance. Why human: RF delivery, AUX timing, mode switching (WR-01/WR-02 apply).
2. **Retry/TIMEOUT on degraded link** — Power balloon off; issue a command. Expect 3 paced attempts (D-05 window, D-07 backoff), then "Timeout", LED red "No link". Why human: state-machine runtime needs two radios.
3. **Camera settings on the sensor** — Send each of the 7 settings; capture and inspect. Expect visible changes; NACK_BUSY on sensor failure. Why human: physical camera required.
4. **Auto-capture interval behavior (after CR-05 fix)** — Enable at 10 s and at 60 s; disable. Expect captures at exactly the commanded cadence and none after disable. Why human: timing across the radio link. Note: until CR-05 is fixed, this test will FAIL the disable half — that is the gap, not a UAT procedure.

### Gaps Summary

The gap-closure plans did what they claimed: the protocol stack is now wire-correct and regression-locked, all handlers execute, retry terminates, and the UI tells the operator the truth per command. Four of the five previously-failed truths' root causes are closed with evidence this verifier reproduced independently.

What blocks the phase goal now is a single, small, code-only defect the review surfaced after execution: **CR-05 — the balloon's legacy 30-second capture timer was never removed and runs beside the commanded auto-capture**. It defeats the "disable stops automatic capture" clause of SC-5 (and plan 01-04's own must-have), corrupts interval fidelity above 30 s, and forks the image-ID sequence the phase deliberately unified. The fix is to delete or gate the legacy block in `processCamera()` (src/main_balloon.cpp:658-691) and keep AutoCapture as the only capture/ID authority — no hardware needed to close it. CTRL-03/CTRL-04 should return to In Progress in REQUIREMENTS.md until then; the other four requirements are code-satisfied pending hardware UAT.

Deferred items (D-13/D-15 → Phase 3) and carried warnings (WR-01/02/06/08/09/10) are documented above and do not block the phase goal.

---

_Verified: 2026-08-18T03:49:52Z_
_Verifier: Claude (gsd-verifier)_

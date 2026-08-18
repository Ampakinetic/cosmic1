---
phase: 01-command-protocol-control
verified: 2026-08-18T05:48:20Z
status: gaps_found
score: 1/5 must-haves verified
behavior_unverified: 4 # SC-2/SC-3/SC-4/SC-5 present + wired; runtime behavior needs hardware UAT (SC-5 upgraded from FAILED — CR-05 closed)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 1/5
  gaps_closed:
    - "SC-5 / CR-05: legacy 30 s capture timer fully removed — verified independently by this verifier (negative grep gates all zero; loop() subsystem block clean; AutoCap().begin:387 / CmdHandler().process:725 / AutoCap().process:728 intact; single image-ID authority confirmed; both targets build SUCCESS; harness 10/10)"
  gaps_remaining: []
  regressions: [] # SC-1 routes, protocol constants, real-seq CRC site, retry mechanics, sensor setters, /status flow all re-checked intact
gaps:
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) — success responses must conform to the documented RESPONSE wire format (0x11), a phase deliverable ('Command packet protocol specification')"
    status: partial
    reason: "NEW CR-01 (01-REVIEW.md ca682b4), independently confirmed by this verifier: createResponsePacket (src/command_protocol.cpp:371-387) never assigns packet.type — ResponsePacket packet{} zero-initializes it to 0x00 — and serializeResponse emits resp.type verbatim (src/command_protocol.cpp:159). CommandHandler::process() builds ALL successful responses (every ACK and STATUS) through this factory (src/command_handler.cpp:98-103), so the entire success path goes onto the wire with header type 0x00 while NACKs carry 0x11 (createNACK :256). Documented contract: include/command_protocol.h:92 '0x11 for responses', common_types.h:38 RESPONSE=0x11. Current endpoint pair still interoperates (deserializeResponse reads buffer[2] at :210 but never validates it), so the phase round trip is not broken today — but the protocol deliverable is non-conformant on its entire success path and any type-filtering consumer (Phase 2 image pipeline sharing the channel, sniffer, telemetry router) will misroute or drop every successful response. The regression harness masks this because its JS mirror hardcodes PACKET_TYPE_RESPONSE (scripts/verify_protocol_roundtrip.mjs:139) instead of modeling createResponsePacket (WR-05), so the wire-format evidence for the response path is known incomplete."
    artifacts:
      - path: "src/command_protocol.cpp"
        issue: "createResponsePacket (:371-387) leaves packet.type unassigned (0x00); createCommandPacket has the same latent asymmetry (commands correct only because serializeCommand hardcodes 0x10 at :60)"
      - path: "scripts/verify_protocol_roundtrip.mjs"
        issue: ":139 hardcodes 0x11 in serializeResponse mirror — certifies a type byte the firmware does not emit (WR-05)"
    missing:
      - "Set packet.type = PACKET_TYPE_RESPONSE (0x11) in createResponsePacket and PACKET_TYPE_COMMAND in createCommandPacket (symmetry)"
      - "Add harness clause that transcribes createResponsePacket faithfully and asserts serialized byte 2 == 0x11 for both ACK and NACK paths"
  - truth: "Balloon status reporting is truthful (plan 01-04 must-have artifact: 'real GET_STATUS' — status source for SC-3 evidence and Phase 2)"
    status: partial
    reason: "NEW CR-02 (01-REVIEW.md), independently confirmed: handleGetStatus casts the real esp32-camera framesize_t to the project FrameSize enum across differently-numbered values (src/command_handler.cpp:579). Verified against the pinned library (.pio/libdeps/esp32-s3-balloon/esp32-camera/driver/include/sensor.h:83-102): real QQVGA=1/QVGA=5/VGA=8/UXGA=13 vs project QQVGA=5/QVGA=6/VGA=9/UXGA=13 (include/command_protocol.h:57-68, whose comment falsely claims the values match esp_camera.h). The balloon boots at FRAMESIZE_QVGA (real 5) so GET_STATUS reports FrameSize(5) = project QQVGA 160x120 while the camera is actually at QVGA 320x240. Mitigating: GET_STATUS currently has no issuer anywhere in the base station (confirmed by grep — only queue-row label and timeout constant reference it), so no current UI behavior is corrupted; the defect is latent in a delivered artifact whose truthfulness plan 01-04 claimed and this path's prior verification certified. The SET direction is unaffected (framesizeFromInt maps by enum name, :703-735)."
    artifacts:
      - path: "src/command_handler.cpp"
        issue: ":579 static_cast<FrameSize>(camera->getFrameSize()) — raw numeric cast between mismatched enums"
      - path: "include/command_protocol.h"
        issue: ":57-68 FrameSize values do not match the real framesize_t despite the comment claiming so; value 8 named QXGA/'400x296' actually targets real QXGA 2048x1536 (WR-01: the UI 'QXGA 400x296' option always NACKs)"
    missing:
      - "Name-based reverse mapping (frameSizeFromEsp) used by handleGetStatus instead of the raw cast"
      - "Fix the false 'matching esp_camera.h framesize_t' comment; resolve WR-01 (rename project value 8 to CIF and map it to real FRAMESIZE_CIF 400x296 in framesizeFromInt and the base-station option label)"
  - truth: "Failed commands are retried with timeout and user is notified (SC-4) — retry-path response accounting must stay consistent under the designed retry flow"
    status: partial
    reason: "NEW CR-03 (01-REVIEW.md), independently confirmed: findTrackedCommand (src/command_sender.cpp:363-371) matches any non-IDLE slot including terminal ACKED/FAILED/TIMEOUT slots, and handleResponse (:319-357) has no terminal-state guard, so a second response for the same refSequence re-runs the transition and executes pendingCommandCount-- a second time. pendingCommandCount is uint8_t (include/command_sender.h:101) — underflow to 255 makes hasPendingCommands() true forever (UI shows pending=1 permanently, slot accounting and statistics corrupt). This is the NORMAL retry path, not an exotic one: retryCommand (:428-442) retransmits with the SAME sequenceNumber, so when ACK latency approaches the 2 s CAPTURE_NOW window the sender retries, the balloon re-executes and sends a second ACK with the same refSequence, and both the late first ACK and the second ACK process (1→0→255). Prior verification's line-level check of the retry mechanics missed this hole; it sits squarely in SC-4's truth domain and in CTRL-06/PRI-02."
    artifacts:
      - path: "src/command_sender.cpp"
        issue: "handleResponse (:319-357) lacks a terminal-state guard; findTrackedCommand (:363-371) returns terminal slots for response matching"
    missing:
      - "Guard in handleResponse: if cmd->state is ACKED/FAILED/TIMEOUT, return (duplicate/late response) — or restrict findTrackedCommand to PENDING/SENT for response matching"
deferred:
  - truth: "D-13: separate camera-controls page from telemetry display"
    addressed_in: "Phase 3"
    evidence: "Plan 01-03 deferral truth; Phase 3 goal 'Full base station control panel with telemetry, maps, and gallery'; accepted in commit 0b52b9e"
  - truth: "D-15: accordion panels grouping camera settings"
    addressed_in: "Phase 3"
    evidence: "Plan 01-03 deferral truth; WEB-04 top-down layout restructure; accepted in commit 0b52b9e"
behavior_unverified_items:
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2)"
    test: "Power both ESP32-S3 boards with E32 modules linked; issue a capture command from the web UI"
    expected: "Balloon executes, ACK arrives within the D-05 window (2 s for CAPTURE_NOW), UI queue row shows ACK Received; degraded link yields 3 paced retries then Timeout"
    why_human: "Requires physical radios; E32 AUX timing (WR-04 blocking transmit / AUX-low race) can only be evaluated on hardware — no host harness drives the sender/handler state machines over a link"
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "Send SET_RESOLUTION / SET_SATURATION / SET_EXPOSURE / SET_WB from the UI, then capture and inspect the image"
    expected: "Sensor accepts the values; visible change in captured images; NACK_BUSY if a sensor call fails"
    why_human: "Sensor acceptance of saturation/ae_level/wb_mode values and visual image assessment need the physical camera module"
  - truth: "Failed commands are retried with timeout and user is notified (SC-4)"
    test: "Power the balloon off; issue a capture command; watch the Command Queue panel"
    expected: "Row shows Sent, then Failed/Timeout after 3 paced attempts (D-05 window + D-07 backoff); LED turns red 'No link'; counters advance"
    why_human: "Retry state transitions at runtime need two radios; no host test exercises the CommandSender state machine (millis/E32-coupled). Note: run this AFTER gap 3 (terminal-state guard) is fixed — the duplicate-ACK race is most likely exactly here, on a slow link"
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5)"
    test: "Enable auto-capture at 10 s and at 60 s; then disable; issue a manual CAPTURE_NOW between them"
    expected: "Captures at exactly the commanded cadence (including above 30 s, no interleave); zero automatic captures after the ACKed disable; manual trigger still works; image IDs form one sequence"
    why_human: "Timing behavior across a real RF link with physical radios and camera cannot be exercised by host builds or the wire-format harness; the CR-05 fix is code-proven by absence (negative grep gates) but runtime cadence needs hardware"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification after CR-05 gap closure)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-18T05:48:20Z
**Status:** gaps_found
**Re-verification:** Yes — after CR-05 gap closure (plan 01-05 executed, commits 352b195 + b5726b3)

## Goal Achievement

The prior gap is genuinely closed. CR-05 (legacy 30-second capture timer) was verified closed by this verifier independently, not from SUMMARY claims: all negative grep gates return zero (processCamera / processIncomingCommands / nextImageId / createCameraPacket / CameraData absent from src/main_balloon.cpp; isTimeToCapture absent from all of src/ + include/), the positive wiring gates are intact (AutoCap().begin(&Camera()) :387, CmdHandler().process() :725, AutoCap().process() :728), loop()'s subsystem block contains no capture step, project-wide live capture calls exist only in AutoCapture::process (commanded interval) and the CAPTURE_NOW handler (manual), AutoCapture::allocateImageId() is the only image-ID sequence, and both firmware targets plus the 10-clause wire-format harness pass on this verifier's own runs. SC-5's deterministic blocker is gone; its remaining risk is runtime behavior over the radio link, so it moves from FAILED to PRESENT_BEHAVIOR_UNVERIFIED — same class as SC-2/SC-3/SC-4, resolved by hardware UAT item 4.

However, the fresh code review (01-REVIEW.md, commit ca682b4 — post-dating the gap-closure commits) found 3 new Critical defects, and this verifier confirmed each one line-by-line in the current code before accepting them. They are all deterministic, code-level, and hardware-free to fix, and each lands inside a phase must-have domain: (1) every successful balloon response is serialized with packet type 0x00 instead of the documented 0x11 RESPONSE — the protocol-spec deliverable is non-conformant on its entire success path, and the regression harness masks it by hardcoding 0x11; (2) GET_STATUS misreports camera resolution via a raw cast between differently-numbered enums (latent — GET_STATUS has no issuer today — but it falsifies plan 01-04's "truthful status" artifact claim this path previously certified); (3) the retry state machine has no terminal-state guard, so the normal retry-edge duplicate ACK double-decrements the uint8 pending-command counter and permanently corrupts the "user is notified" half of SC-4.

Phase status is therefore gaps_found again — for a different reason than either prior round: the previous gap was a superseded code path left in place; the new gaps are correctness holes in delivered, wired code. The 01-05 SUMMARY's "Phase 1 has zero open code gaps" was accurate when written and is superseded by the review; the ROADMAP "Plans: 5/5... zero open code gaps" note is likewise now stale.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as both prior verifications).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression intact: 12 routes registered (main_basestation.cpp:461-472) incl. all 7 settings + auto-capture pair; queue panel + status flow (:1011-1024) unchanged. New WR-01 noted: the "QXGA 400x296" resolution option targets real QXGA 2048x1536 and will always NACK on the OV2640 — one broken option, not a broken form |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED + GAP (partial) | Interop between the current endpoints still holds (commands correct 0x10 via serializeCommand :60; neither receiver validates the response type byte), and the prior protocol fixes remain intact (real seq byte before CRC at :61; length-driven framing; 240-byte limits; harness 10/10 on verifier re-run). But NEW CR-01: createResponsePacket leaves packet.type unassigned → every ACK/STATUS on the wire carries 0x00, violating the documented 0x11 contract for the entire success path, with the harness masking it (see gap 1). Physical RF round trip (incl. WR-04 AUX race) remains hardware UAT item 1 |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED (carried) | Adjust path intact and unaffected: 7 sensor setters real (camera_manager.cpp:426-474), handlers ACK only on true return, SET_RESOLUTION maps by enum name (:703-735). NEW CR-02 hits the report path, not the adjust path: GET_STATUS currentResolution is a raw cross-enum cast (:579) — boot QVGA reports as QQVGA (see gap 2; latent, GET_STATUS has no issuer). Sensor acceptance of values remains hardware UAT item 3 |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED + GAP (partial) | Prior mechanics intact (backoff :196-209 with clamped shift, ackTimeoutFor :24 used at :243, retryCommand resets sendTime on both outcomes :428-442, 5 counted pendingCommandCount-- sites, cancel guard, notification flow to /status). But NEW CR-03: no terminal-state guard in handleResponse + findTrackedCommand matching terminal slots → the designed retry-edge duplicate ACK double-decrements the uint8 pending counter → underflow → hasPendingCommands() true forever, UI pending=1 permanently (see gap 3). Runtime retry transitions remain hardware UAT item 2 |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED (upgraded from FAILED) | CR-05 CLOSED, verifier-proven: legacy timer gone (all negative gates zero), AutoCapture sole capture trigger (only live captureImage calls: auto_capture.cpp:98 + command_handler.cpp:201; camera_manager.cpp:228 sits in uncalled captureBoth()), sole ID authority (allocateImageId at command_handler.cpp:207 + auto_capture.cpp:99 only), disable handler executes AutoCap().disable() (:558), builds 2/2 SUCCESS, harness 10/10 exit 0. Remaining: runtime cadence/disable over the RF link — hardware UAT item 4 |

**Score:** 1/5 truths verified (4 present, behavior-unverified — routed to human UAT; 3 of them carry new code gaps)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | D-13: separate camera-controls page (controls stay cards on the single page) | Phase 3 | WEB-04 top-down layout restructure; plan 01-03 deferral truth; commit 0b52b9e |
| 2 | D-15: accordion settings groups (stacked single-field forms retained) | Phase 3 | WEB-04 top-down layout restructure; commit 0b52b9e |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/main_balloon.cpp` | Legacy capture path fully removed; AutoCap wiring intact | ✓ VERIFIED | 0 occurrences of processCamera/processIncomingCommands/nextImageId/createCameraPacket/CameraData; AutoCap().begin :387, CmdHandler().process :725, AutoCap().process :728; loop() subsystem block clean; commit 352b195 = 46 deletions |
| `src/camera_manager.h` / `.cpp` | Dead timing helper swept; capture/setter/getter methods untouched | ✓ VERIFIED | 0 isTimeToCapture occurrences in src/ + include/; setters :426/:445/:464 intact; commit b5726b3 |
| `src/auto_capture.cpp` | process() comment reworded; behavior unchanged | ✓ VERIFIED | :86-100 wraparound-safe idiom + T-01-09 baseline-before-attempt retained, no CameraManager reference |
| `src/command_protocol.cpp` | Protocol serialization | ✗ DEFECT (gap 1) | Real-seq CRC (:61) and size limits intact — but createResponsePacket :371-387 never sets packet.type (NEW CR-01) |
| `src/command_handler.cpp` | Handlers, framing, typed responses, truthful status | ⚠️ PARTIAL | Framing and all handlers still execute; GET_STATUS currentResolution misreports via cross-enum cast :579 (NEW CR-02) — "truthful status" claim now false for that field |
| `src/command_sender.cpp` | Retry terminal states, framing, queue API | ⚠️ PARTIAL | All prior-verified mechanics intact — but handleResponse lacks the terminal-state guard (NEW CR-03), corrupting accounting on duplicate responses |
| `src/main_basestation.cpp` | Complete UI layer | ✓ VERIFIED | 12 routes, queue panel, computed LED unchanged; WR-06 (uint16-truncated ACK edge at :503-507) and WR-10 (chip latches latest issued interval, seq-wrap freeze) are Warning-level defects noted below |
| `scripts/verify_protocol_roundtrip.mjs` | Host wire-format regression harness | ⚠️ PARTIAL | 10/10 PASS on verifier re-run — but :139 hardcodes the response type byte, so it certifies 0x11 the firmware does not emit (WR-05; masks gap 1) |
| `platformio.ini` | Build separation | ✓ VERIFIED | Both envs build SUCCESS on verifier run (balloon 40.6 s, basestation 24.2 s) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| main_basestation POST routes | CmdSender().sendCommand | 12 command routes incl. 4 settings + auto-capture pair | ✓ WIRED | Regression intact |
| main_balloon processPacketHandling | AutoCapture::process | AutoCap().process() :728 every pass — now the ONLY periodic capture call | ✓ WIRED | CR-05 closure core; verifier-confirmed |
| main_balloon initializeSubsystems | AutoCapture::begin | AutoCap().begin(&Camera()) :387 | ✓ WIRED | Intact |
| command_handler handleCaptureNow / AutoCapture::process | AutoCapture::allocateImageId | :207 / auto_capture.cpp:99 — the only two callers project-wide | ✓ WIRED | Single ID authority restored (note: plan's link wording cited handleAutoCaptureEnable, which does not and need not allocate — enable just starts the timer) |
| command_handler handleAutoCaptureDisable | AutoCap().disable | :558, ACK reflects the call | ✓ WIRED | With the legacy path gone, the disable contract now holds at system level |
| handleStatus | CmdSender state queries | getCommandQueue/getCommandState/getCommandRetryCount (:1011-1028) | ✓ WIRED | Per-command outcomes reach the 1 s poll |
| CommandHandler success responses | serializeResponse → wire | createResponsePacket → resp.type byte | ✗ DEFECTIVE (gap 1) | Emits 0x00 for all ACK/STATUS; NACK path emits 0x11 — inconsistent on-wire type field |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| /status | sent/acked/failed/pending | CmdSender counters + hasPendingCommands | Yes | ⚠️ FLOWING-WITH-DEFECT — pending can latch 255 after one duplicate response (gap 3) |
| /status | lastCmd/lastSeq/lastState/lastRetry | appState + getCommandState/RetryCount | Yes | ✓ FLOWING |
| /status | connected/linkText | Computed from lastAckTime + terminal outcomes | Yes | ⚠️ FLOWING-WITH-DEFECT — WR-06: uint16-truncated ACK-edge compare (:503-507) misses edges at 65536 boundaries |
| /status | queue[] | getCommandQueue over 5-slot table | Yes | ✓ FLOWING |
| GET_STATUS response | imageId/enabled/interval | AutoCap() module | Yes | ✓ FLOWING — lastImageId divergence source removed (CR-05); no issuer exists yet |
| GET_STATUS response | currentResolution | static_cast<FrameSize>(camera->getFrameSize()) :579 | Wrong data | ✗ CORRUPTED — cross-enum numeric cast reports QVGA as QQVGA (gap 2) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run) | 10/10 PASS, exit 0 | ✓ PASS (but see WR-05: response-type clause is not faithful to firmware — passes vacuously) |
| Both firmware targets compile | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (verifier-run) | 2 succeeded | ✓ PASS |
| CR-05 removal gates | Negative greps (5 legacy symbols + isTimeToCapture) + positive greps (3 wiring sites) — verifier-run | 0 / 0 / all present | ✓ PASS |
| Retry state machine exercised by a test | none exists — no host harness for CommandSender state machine | n/a | ? SKIP (hardware UAT item 2; gap 3 must be fixed first to make that UAT meaningful) |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists. The declared verification commands (harness + builds) were executed above. SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-01, 01-02, 01-03, 01-05 | Trigger camera capture from base station web UI | ✓ SATISFIED (code; UAT 1 outstanding) | Full path intact; single shared image-ID sequence now protected (CR-05 closed). WR-03 note: a retried CAPTURE_NOW re-executes and consumes extra IDs — Warning, duplicates suppressed nowhere |
| CTRL-02 | 01-01, 01-03, 01-04 | Adjust ALL camera settings remotely | ✓ SATISFIED (code; UAT 3 outstanding) | 7/7 forms/routes + 7/7 executing handlers intact; SET path name-mapped and correct. WR-01: "QXGA 400x296" option always NACKs (mislabeled enum target) |
| CTRL-03 | 01-01, 01-03, 01-04, 01-05 | Manual + automatic capture modes | ✓ SATISFIED at code level (was BLOCKED; UAT 4 outstanding) | CR-05 closed: disable semantics restored, AutoCapture sole trigger — verifier-proven by absence gates |
| CTRL-04 | 01-01, 01-03, 01-04, 01-05 | Automatic capture fixed interval timing | ✓ SATISFIED at code level (was BLOCKED; UAT 4 outstanding) | Exact commanded cadence across 1000..3600000 ms; no 30 s interleave; one ID sequence |
| CTRL-06 | 01-01, 01-02, 01-03 | Failed commands retried with timeout | ✓ SATISFIED with defect — gap 3 | Mechanics verified intact, but the duplicate-response accounting hole (NEW CR-03) sits inside this requirement's retry path |
| PRI-02 | 01-01, 01-02 | Retry with timeout for failed transmissions | ✓ SATISFIED with defect — gap 3 | Same evidence as CTRL-06 |

Orphaned requirements: none — REQUIREMENTS.md traceability maps exactly the six phase IDs (CTRL-05 correctly routes to Phase 2); all six appear in plan `requirements` fields.

### Plan Prohibition Verdicts (judgment-tier — autonomous, non-authoritative; human review recommended at UAT)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-05 | MUST NOT acknowledge success for actions not executed — no capture path outside AutoCapture and CAPTURE_NOW, no second timer or ID counter | PASS (code level) | Proven by absence: verifier-run negative greps all zero; only live capture calls are AutoCapture::process and the CAPTURE_NOW handler; one ID sequence. Runtime confirmation rides UAT item 4 |
| 01-04 (restored by 01-05) | No ack-only stubs, no fabricated status | PASS (code level), with residual flag | Every handler executes; AUTO_CAPTURE_DISABLE ACK now truthful. Residual: GET_STATUS fabricates currentResolution via the cross-enum cast (gap 2) — same truthfulness spirit, lower stakes (no issuer today). Human review at UAT |

### Anti-Patterns Found

Confirmed by this verifier unless marked "review-reported". Severity follows impact on the phase goal.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/command_protocol.cpp | 371-387, 159 | NEW CR-01: success responses serialized with type 0x00, not 0x11; whole success path non-conformant; harness masks it | 🛑 Blocker (gap 1) | Protocol deliverable violated; Phase 2 type-routing consumers will misroute every ACK |
| src/command_handler.cpp | 579 | NEW CR-02: GET_STATUS resolution misreported via cross-enum cast (boot QVGA reads as QQVGA) | 🛑 Blocker (gap 2) | Delivered status artifact untruthful; latent (no issuer) but falsifies a certified claim |
| src/command_sender.cpp | 319-371 | NEW CR-03: no terminal-state guard — duplicate/late ACK double-decrements uint8 pendingCommandCount → permanent UI pending=1 | 🛑 Blocker (gap 3) | Normal retry-edge path corrupts SC-4's notification state |
| scripts/verify_protocol_roundtrip.mjs | 139 | WR-05: JS mirror hardcodes 0x11 — certifies a type byte the firmware does not emit (confirmed) | ⚠️ Warning | Response-path wire-format evidence incomplete until gap 1's harness clause lands |
| include/command_protocol.h / main_basestation.cpp / command_handler.cpp | 57-68 / 612-622 / 713-716 | WR-01: project value 8 named "QXGA 400x296" targets real QXGA 2048x1536 — UI option always NACKs; enum comment false | ⚠️ Warning | One broken resolution option; folded into gap 2's fix |
| src/command_handler.cpp | 676-679 | WR-02: single pending-command slot — second command in same read burst overwrites the first silently | ⚠️ Warning | Recovered via base-station retry after full D-05 window |
| src/command_handler.cpp | 147-226 | WR-03: no duplicate-sequence suppression — retried CAPTURE_NOW re-executes, burns image IDs | ⚠️ Warning | Gaps in Phase 2 image-ID sequence; compounding with WR-09 |
| src/e32_lora.cpp | 158-202, 104-131 | WR-04 (carried, was WR-01): blocking transmit up to ~7 s; setMode delay(50) | ⚠️ Warning | Deferred to hardware bring-up (documented in plan 01-02) |
| include/command_protocol.h | 181 | WR-09: 2 s CAPTURE_NOW ACK window can be shorter than worst-case capture; slow success looks like timeout → retry → re-execute | ⚠️ Warning | Interacts with gaps 3 and WR-03 at UAT |
| src/main_basestation.cpp | 1058-1071 | WR-10 (review-reported): auto-capture chip latches latest issued interval, not the ACKed one; seq-wrap freezes the chip | ⚠️ Warning | UI truthfulness of the auto-capture state chip |
| src/main_basestation.cpp | 503-507 | WR-06 (confirmed): ACK-edge detection truncates the 32-bit counter to 16 bits — LED can flip to "No link" at 65536 boundaries | ⚠️ Warning | Link-LED truthfulness edge case |
| src/e32_lora.cpp | 264-344 | WR-07 (carried, was WR-02): config API contradicts E32 datasheet; setChannel silently no-ops | ⚠️ Warning | Uncalled; fix at radio bring-up |
| src/camera_manager.cpp | 290-334 | WR-11 (review-reported): thumbnail size estimate too small — creation usually fails | ⚠️ Warning | Dormant in Phase 1 (no thumbnail path exercised); live for Phase 2 |
| src/command_handler.cpp / command_sender.cpp | 630-640 / 268-278 | IN-06 (carried, was WR-06): 0xAA 0xAA 0x55 loses frame sync; duplicated validatePacket | ℹ️ Info | Noise-robustness gap |
| src/command_protocol.cpp | 190-256 | IN-08: dead payload/response structs misdocument the wire layout; IN-05 duplicate-valued enum aliases | ℹ️ Info | Documentation hazard for Phase 2 protocol work |
| src/main_basestation.cpp | 1110-1123 | IN-02: dead sendHTML/updateLED; physical LED blinks unconditionally | ℹ️ Info | Cleanup |
| src/main_balloon.cpp | 375 | IN-03: hardcoded E32 pins duplicate sensor_pins.h macros | ℹ️ Info | Divergence risk |
| src/command_handler.cpp | 529 | IN-04: interval bounds restated as literals instead of the auto_capture.h constants | ℹ️ Info | Divergence risk |

No TBD/FIXME/XXX markers in any phase-modified file (verifier grep clean). The prior IN-12 (camera-ready gate NACK_BUSYs AUTO_CAPTURE_DISABLE) remains structurally present but is no longer compounding a broken disable contract.

### SUMMARY vs Reality

1. 01-05 SUMMARY claims are accurate and reproduce: both commits present (352b195: 46 deletions in main_balloon.cpp; b5726b3: camera_manager sweep + comment reword), all source gates pass on verifier re-run, both builds and the harness green on verifier re-run.
2. The 01-05 SUMMARY's "Phase 1 has zero open code gaps" was true at its timestamp (before the re-review) and is superseded by 01-REVIEW.md (ca682b4), whose 3 criticals this verifier independently confirmed in the current code. The ROADMAP "zero open code gaps" note is now stale.
3. REQUIREMENTS.md marks CTRL-01..04, CTRL-06, PRI-02 Complete: CTRL-03/CTRL-04 marks are now accurate at the same code-level standard as the others (CR-05 closed, verifier-proven); CTRL-06/PRI-02 carry the gap-3 defect; all six remain pending hardware UAT.

### Human Verification Required

Recorded for UAT; items 1-3 are the carried behavior-unverified truths, item 4 is newly unblocked by the CR-05 fix. None of these is the cause of gaps_found (that is the 3 new code gaps).

1. **End-to-end command round trip over real radios** — Power both boards; trigger a capture. Expect ACK within 2 s, queue row "ACK Received", counters advance. Why human: RF delivery, AUX timing, mode switching (WR-04 applies).
2. **Retry/TIMEOUT on degraded link** — Power balloon off; issue a command. Expect 3 paced attempts (D-05 window, D-07 backoff), then "Timeout", LED red "No link". Why human: state-machine runtime needs two radios. Run after gap 3 is fixed — the duplicate-ACK race is most probable exactly here.
3. **Camera settings on the sensor** — Send each of the 7 settings; capture and inspect. Expect visible changes; NACK_BUSY on sensor failure. Why human: physical camera required.
4. **Auto-capture interval and disable behavior (newly unblocked)** — Enable at 10 s and at 60 s; disable; manual trigger between. Expect captures at exactly the commanded cadence, zero captures after the ACKed disable, one continuous image-ID sequence. Why human: timing across the radio link.

### Gaps Summary

The CR-05 gap-closure plan did exactly what it claimed, and this verifier proved it independently: the legacy 30-second timer is gone, AutoCapture is the sole capture trigger and image-ID authority, both targets build, and the wire-format harness passes. SC-5's deterministic blocker is closed and the truth joins SC-2/SC-3/SC-4 in the hardware-UAT queue.

What blocks the phase goal now is a different class of problem, surfaced by the post-closure code review and confirmed line-by-line by this verifier: three correctness defects in delivered, wired protocol code. (1) Every successful response is serialized with packet type 0x00 instead of the documented 0x11 — the command-protocol deliverable is non-conformant on its entire success path, and the regression harness hides this by hardcoding the correct value, so the fix must also add a faithful harness clause. (2) GET_STATUS misreports camera resolution through a raw cast between two differently-numbered enums — latent today (no issuer) but it falsifies the truthful-status claim plan 01-04 made and this path previously certified, and the same mislabeled enum makes the UI's "QXGA 400x296" option permanently fail. (3) The retry state machine accepts duplicate responses for terminal commands — the exact race the designed retry flow produces at ACK-latency edges — and double-decrements a uint8 counter, permanently corrupting the pending-command display. All three are small, code-only fixes requiring no hardware; all three sit inside must-have truth domains (SC-2 protocol conformance, truthful status artifact, SC-4 retry/notification integrity).

Recommended next step: `/gsd-plan-phase --gaps` for the three structured gaps above (they share a theme — protocol/response-path correctness — and could close in one focused plan with an extended harness), then re-verify, then `/gsd-secure-phase 1` and hardware UAT (items 1-4).

Deferred items (D-13/D-15 → Phase 3) and carried warnings (WR-01..WR-11 minus the folded ones, IN-*) are documented above and do not block the phase goal.

---

_Verified: 2026-08-18T05:48:20Z_
_Verifier: Claude (gsd-verifier)_

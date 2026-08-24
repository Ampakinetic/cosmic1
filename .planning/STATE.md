---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
current_plan: "11 (01-11: branch-conditional remediation) — COMPLETE"
status: executing
stopped_at: "Completed 01-11-PLAN.md (phase 01: all 11 plans executed; G-01-5/G-01-6 resolved on bench evidence; G-01-7/G-01-8 open; security gate + gap round next)"
last_updated: "2026-08-24T00:01:58.159Z"
progress:
  total_phases: 3
  completed_phases: 2
  total_plans: 22
  completed_plans: 20
current_phase: 2
current_phase_name: command-protocol-control
---

# Project State

**Last Updated:** 2026-08-18

## Current Status

**Project:** Cosmic1 Base Station Camera Control Extension
**Phase:** Phase 1 (command-protocol-control) — ALL 11 plans executed (01-01..01-06 + UAT gap closures 01-07..01-11). 01-11 remediation round COMPLETE (2026-08-24): G-01-5 RESOLVED (AUX phantom-failure accounting fixed, f265556 — 0 FAILED across 3 post-fix bench sessions vs ~41/capture baseline) and G-01-6 RESOLVED (image-ID NVS persistence, 678d4f1 — gallery grows 2->3->4->5 across reboots); ride-alongs verified (GPIO39 flood zero, thumbnails correctly sized). Bench round surfaced two NEW open gaps: G-01-7 concurrent-transfer starvation (interim mitigation: space captures) and G-01-8 SET_RESOLUTION VGA+ FB-OVF defect. Remaining before phase complete: G-01-7/G-01-8 remediation round + security gate (/gsd-secure-phase 1)
**Milestone:** v1.0

## Current Position

**Current Plan:** 11 (01-11: branch-conditional remediation) — COMPLETE
**Total Plans in Phase:** 12
**Status:** 01-11 COMPLETE (branch-c f265556 + branch-e 678d4f1 + R1 91bee03 + R2 c67e1a5 + bench re-verification 2026-08-24) — G-01-5/G-01-6 resolved on operator evidence; WINDOWS entries 3 (G-01-7 starvation) / 4 (G-01-8 VGA FB-OVF) open — /gsd-ship blocked until their round lands
**Progress:** [██████████░] 95% (Phases 2+3: all plans executed; Phase 1: 11 of 11 — G-01-5/G-01-6 resolved; G-01-7/G-01-8 + security gate remain before phase close)

## Progress

- ✅ Project initialized
- ✅ Requirements defined (24 requirements)
- ✅ Roadmap created (3 phases)
- ✅ Phase 1 plan created
- ✅ Phase 1 plan 01-01 executed (T1–T8 tracer, committed via closeout — see 01-01-SUMMARY.md)
- ✅ Phase 1 re-verification #3: human_needed (2026-08-18) — response-path criticals CR-01/02/03 verified closed by plan 01-06 (verifier's own reads/greps/harness/builds); zero open code gaps; 5 items routed to UAT (`01-UAT.md`)
- ✅ Phase 1 gap-closure plan 01-02 executed — protocol defects CR-01..CR-04 + WR-03/04/05 closed, D-05/D-07/D-16-support encoded (see 01-02-SUMMARY.md)
- ✅ Phase 1 gap-closure plan 01-03 executed — all 7 settings forms + auto-capture UI (WR-07 long-first validation), D-16 Command Queue panel, IN-03 computed link LED (see 01-03-SUMMARY.md)
- ✅ Phase 1 gap-closure plan 01-04 executed — real sensor setters for saturation/exposure/WB, AutoCapture interval timer module, truthful GET_STATUS, shared image-ID sequence, main-loop wiring (see 01-04-SUMMARY.md)
- ✅ Phase 1 gap-closure plan 01-05 executed — CR-05 closed: legacy 30 s capture timer removed, AutoCapture is the sole capture/image-ID authority; both targets green, wire harness 10/10 (see 01-05-SUMMARY.md)
- ✅ Phase 1 gap-closure plan 01-06 executed — response-path criticals closed: packet.type set in both factories + faithful harness clause (f, 15/15); frameSizeFromEsp name-based GET_STATUS mapping + CIF relabel; handleResponse terminal-state guard (see 01-06-SUMMARY.md)
- ✅ Phase 2 plan 02-01 executed (tracer) — CR-04/WR-11 thumbnail fix + full Phase 2 wire contract + WR-12 type dispatch (see 02-01-SUMMARY.md)
- ✅ Phase 2 plan 02-02 executed — balloon full pipeline: full announce + FIFO window servicing, TX arbitration + 5 s telemetry beacon (PRI-01/SC-5)
- ✅ Phase 2 plan 02-03 executed — base full pipeline: windowed ARQ (D-21/D-24), end-to-end CRC32 (D-23), SD storage + sidecars, transfer progress UI (D-20)
- ✅ Phase 2 plan 02-04 executed — CTRL-05 event triggers: altitude/distance/flight-phase capture inside AutoCapture, UI-configurable thresholds
- ✅ Phase 2 gap-closure plan 02-05 executed (2026-08-19) — all 4 02-VERIFICATION gaps closed: kind-addressable 6-byte window requests + balloon thumbnail servicing (Gap 2/D-22), bounded push/window interleaving (preemption 5000 ms) + completion-aware class-ranked eviction (Gap 1/D-19/D-24), passCount reset on progress + slot-pressure full reset (Gaps 1-RX/3/D-20), kind-suffixed sidecars IMG_{id}_T.JSON (Gap 4/D-30); both targets green, harness 47 checks green, PRI-01 order-gates green (see 02-05-SUMMARY.md)
- ✅ Phase 1 UAT gap closures executed (2026-08-22/23): 01-07 in-page AJAX submit (G-01-4 code), 01-08 real E32 register config + boot-time 9.6k air-rate enforcement (G-01-3 code half), 01-09 hardware bench session — G-01-3 RESOLVED on hardware (SD_MMC transport switch 794df00 + both boards at 9.6k; Storage tile OK; CRC-verified full-image transfer end-to-end) and G-01-4 runtime truth confirmed; NEW residual G-01-5 (thumbnail push-burst loss, B2/B4-class) open with a named discriminator path (see 01-09-SUMMARY.md)
- ✅ Phase 1 plan 01-10 executed (2026-08-23): WR-01 closed at code level (c9770b1, validity-gated PowerMgr reads) + bench discriminator round — G-01-5 mechanism NAMED (E32 AUX handshake phantom TX-failure: e32_lora.cpp:223 write-then-:227-wait; v3 37 sent/41 FAILED booked while base received 72/72 chunks; real air loss 1-2 chunks/16 self-healed, CRC FAIL 0; B5 NOT reproduced — downgraded), G-01-6 branch CONFIRMED (image-ID reboot reset overwrites IMG_00001_*, proven twice with 4/4 COMPLETE transfers); mid-round CDC log consolidation (3fc35a7); GPIO39 LED error-flood + thumbnail-sizing quirk routed to 01-11; gaps stay OPEN pending 01-11 remediation + bench re-verification (see 01-10-SUMMARY.md)
- ✅ Phase 1 plan 01-11 executed (2026-08-23/24): remediation round — branch-c AUX phantom-failure accounting (f265556) + branch-e image-ID NVS persistence (678d4f1, e-site auto_capture.cpp) + R1 STATUS_LED 39->41 edge-gated (91bee03) + R2 thumbnail QQVGA guard (c67e1a5); bench re-verification across 3 operator sessions RESOLVED G-01-5 (0 phantom FAILED vs ~41/capture baseline; spaced captures 4/4 kinds COMPLETE) and G-01-6 (IDs 1..5 sequential across reboots; gallery 2->3->4->5, no overwrite); NEW gaps G-01-7 (concurrent-transfer starvation; interim mitigation: space captures) + G-01-8 (SET_RESOLUTION VGA FB-OVF, latent since 01-04) routed open with named levers (see 01-11-SUMMARY.md)

## Current Phase

**Phase 1: Command Protocol & Control**

- Status: gap-closure rounds complete through 01-11; all 11 plans executed. Remaining before phase complete: G-01-7/G-01-8 remediation round (WINDOWS entries 3/4 open) + security gate (`/gsd-secure-phase 1`; Phase 2/3 gates also pending)
- Requirements: 6 (CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02)
- Goal: Establish bidirectional LoRa communication for camera control

### Delivered (code-level, builds green on both targets)

- Command protocol spec + CRC16 serializer (`include/command_protocol.h`, `src/command_protocol.cpp`)
- E32-900T30D UART LoRa driver (`include/e32_lora.h`, `src/e32_lora.cpp`)
- Base station sender with retry (3x, per-command ACK timeouts) (`src/command_sender.cpp`)
- Balloon command handler with ACK/NACK + CameraManager wiring (`src/command_handler.cpp`)
- Base station web UI: capture trigger + quality/brightness/contrast forms (`src/main_basestation.cpp`)
- Balloon integration + `esp32-s3-basestation` build env (`src/main_balloon.cpp`, `platformio.ini`)
- Protocol gap closure (01-02): CRC covers the real sequence byte (CR-01); shared CMD_MAX_PACKET_SIZE=240 with oversize rejection and correctly sized buffers (CR-02); length-driven framing in both receivers — embedded 0D 0A survives (CR-03); retry terminal states with D-05 ACK timeouts (2000/5000/10000ms), D-07 exponential backoff (2000/4000/8000ms), cancel guard (CR-04/WR-03/WR-04); STATUS responses typed end-to-end (WR-05); queue snapshot API for the D-16 UI view; host wire-format regression harness (`scripts/verify_protocol_roundtrip.mjs`)
- UI gap closure (01-03): all 7 camera settings forms + routes with WR-07 long-first validation (3 of 7 → 7 of 7); auto-capture card + `/auto-capture(-stop)` routes with 4-byte big-endian interval payload; D-16 Command Queue panel (pinned last command + every occupied slot, locked vocabulary, retry counts); IN-03 link LED computed from ACK activity (LINK_STALE_MS 30s); UI-SPEC spacing/typography harmonization + 480px collapse
- Balloon gap closure (01-04): CameraManager setSaturation/setExposure/setWBMode sensor setters + cached getters (7 of 7 settings execute for real; failed sensor calls NACK_BUSY); AutoCapture module (`include/auto_capture.h` + `src/auto_capture.cpp`) — wraparound-safe millis timer, enable() re-validates 1000..3600000 ms and resets the baseline (first capture one full interval after enable), baseline advances before the attempt so a failed capture cannot spin; single uint16 image-ID sequence shared by CAPTURE_NOW and interval captures; GET_STATUS reports tracked state; main_balloon.cpp loop wiring; auto_capture.cpp excluded from the basestation build — both targets green
- CR-05 gap closure (01-05): legacy 30 s capture timer deleted from `src/main_balloon.cpp` (processCamera definition/declaration/loop call + colliding static nextImageId + dead createCameraPacket site + vestigial processIncomingCommands placeholder); CameraManager::isTimeToCapture swept project-wide; AutoCapture is the balloon's ONLY automatic capture trigger and ONLY image-ID sequence — SC-5 disable semantics hold at code level (T-01-14/15/16 mitigated); both firmware targets green, wire-format harness 10/10
- Response-path gap closure (01-06): `createResponsePacket`/`createCommandPacket` assign `packet.type` first-field (0x11/0x10), shared constant replaces inline casts, harness transcribes the real factory with defective-variant teeth (15/15); `frameSizeFromEsp` by-name reverse mapping makes GET_STATUS resolution truthful, value-8 enum relabeled FRAMESIZE_CIF + UI label fixed (WR-01 closed); `handleResponse` terminal-state guard before storage/counters ends the duplicate-ACK underflow (T-01-17/18/19 mitigated)

### Key Decisions (01-02)

- D-05 encoded: ACK-timeout window selected per command type via `ackTimeoutFor()` — CAPTURE_NOW 2000ms, SET_* + auto-capture config 5000ms, GET_STATUS 10000ms; flat 2000ms constant removed
- D-07 encoded: retry pacing moved above the PENDING/SENT branches as exponential backoff (CMD_RETRY_BACKOFF_BASE_MS << shift, shift clamped at 2) — the waits pace both transmit-failure retries and ACK-timeout retries
- CR-03 fixed by length-driven framing (header body-length field announces the packet length; end marker tested only at the framed position) rather than byte escaping — symmetric fix in both receivers
- Host regression harness transcribes the wire format as an executable spec, including the defective pre-CR-01 serializer variant so the sweep provably has teeth (255/65535 accepted)

### Known Gaps (post re-verification #3, 2026-08-18)

- **All verifier-confirmed code gaps CLOSED:** CR-01/02/03 (response path) by plan 01-06 (commits 75b8514/36674ff/56704e2); CR-05 by plan 01-05. Verifier re-proved each with its own reads, greps, harness run (15/15), and dual-target builds; no regressions; all 6 phase requirements satisfied at code level
- **CR-04 (review Critical, DORMANT):** `createThumbnail` failure paths leave `currentThumbnail.buffer` dangling → double-free risk on next `freeCurrentThumbnail()`. Verifier proved dormancy (zero callers in Phase 1; `captureBoth` unreachable; null-guarded frees). **Deferred to Phase 2** — IMG-02 (thumbnail generation) wires the first caller; must-fix-before-first-caller recorded in 01-VERIFICATION.md
- **Open advisories (01-REVIEW.md fa921a7, 1C/11W/8I):** WR-12 receive-side packet-type validation (CRC-valid RESPONSE heard by balloon would execute as command), WR-13 balloon health-check logs failure on healthy boots (log-only), WR-02..11 carried. None block Phase 1 must-haves
- **Hardware UAT outstanding (5 items, `01-UAT.md`):** radio round-trip, degraded-link retry incl. duplicate-ACK edge (meaningful only now, post-guard), physical-sensor settings incl. CIF option, auto-capture cadence/disable, prohibition review. SC-2/SC-3/SC-4/SC-5 runtime halves code-proven only until these run

## Project Reference

See: `.planning/PROJECT.md`

**Core value:** Users can remotely control the balloon camera and view captured images through the base station web interface, with real-time telemetry and map tracking always available.

**Current focus:** Phase 01 — command-protocol-control

## Next Steps

1. **G-01-7/G-01-8 remediation round (Phase 1 close-out):** G-01-7 = serialize transfers (thumbnail push+heal completes before its full pull; never evict a pending heal entry for a new capture) + optional inter-window RX-settle gap for tail-chunk friction — interim mitigation documented: space captures (wait for both finalize lines between triggers); G-01-8 = bound SET_RESOLUTION to the allocated frame buffer (NACK above it, or re-init the camera on size change; camera_manager.cpp:398-412), then re-run the settings visible-effect + CIF 400x296 spot-checks (Test 3 clauses)
2. **Security gate (before phase complete):** `/gsd-secure-phase 1` (Phase 2/3 gates also still pending)
3. **Phase 3 backlog:** full-resolution gallery image viewer todo captured (`.planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md`) — operator bench wish from the 01-09 session
4. **Then:** `/gsd-progress` toward phase transitions and ship review (WINDOWS ledger: 2 open entries — G-01-7 (entry 3) + G-01-8 (entry 4); blocks /gsd-ship until their round closes them)

## Configuration

**Mode:** YOLO (auto-approve)
**Granularity:** Coarse
**Execution:** Parallel
**Model Profile:** Adaptive

**Workflows Enabled:**

- Research: Yes
- Plan Check: Yes
- Verifier: Yes

**Git Tracking:** Enabled

---
*State updated: 2026-08-19 - 02-05 executed (transfer state-machine gap closure: all 4 02-VERIFICATION gaps closed at code level); Phase 02 all 5 plans executed; re-verification + hardware UAT + security gate pending*

## Session

**Last session:** 2026-08-23T22:12:16.499Z
**Stopped at:** Completed 01-11-PLAN.md (phase 01: all 11 plans executed; G-01-5/G-01-6 resolved on bench evidence; G-01-7/G-01-8 open; security gate + gap round next)
**Resume file:** None

## Performance Metrics

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 01 P03 | 19 min | 3 tasks | 1 files |
| Phase 01 P04 | 14 min | 2 tasks | 7 files |
| Phase 01 P05 | 7 min | 2 tasks | 4 files |
| Phase 01 P06 | 11 min | 3 tasks | 7 files |
| Phase 02 P01 | 3243s | 3 tasks | 15 files |
| Phase 02 P02 | 550s (continuation session; Task 1 in prior session) | 3 tasks | 7 files |
| Phase 02 P03 | 90m | 3 tasks | 9 files |
| Phase 02 P04 | ~12 min | 2 tasks | 8 files |
| Phase 02 P05 | ~21 min | 3 tasks | 8 files |
| Phase 03 P01 | 23min | 2 tasks | 8 files |
| Phase 03 P02 | 25min | 2 tasks | 10 files |
| Phase 03 P03 | 24m | 2 tasks | 4 files |
| Phase 03 P04 | 19m | 2 tasks | 3 files |
| Phase 03 P05 | 12 min | 2 tasks | 4 files |
| Phase 01 P07 | 7min | 2 tasks | 1 files |
| Phase 01 P08 | 14 min | 2 tasks | 2 files |
| Phase 01 P09 | 118min (5 bench-checkpoint iterations) | 2 tasks | 3 files |
| Phase 01 P10 | ~5.5h across 3 executor sessions + operator bench (2 bench checkpoints) | 3 tasks | 4 files |
| Phase 01 P11 | ~1 day across executor + operator bench sessions (Tasks 1-2 2026-08-23, bench 2026-08-24) | 3 tasks | 7 files |

## Decisions

- [Phase 01]: 01-03: chip and LED state latched server-side — auto-capture chip state latched from the newest ACKed AUTO_CAPTURE_ENABLE/DISABLE queue entry by sequence number (survives command-slot reuse); link LED red state requires lastTerminalFailTime newer than lastAckTime so a late ACK for an older command cannot mask a fresh TIMEOUT/FAILED — Plan 01-03 specifies ACK-gated chip semantics and the IN-03 prohibition on green-when-failed; slot reuse and ACK ordering are the two cases a naive per-poll derivation gets wrong
- [Phase 01]: 01-04: image IDs unified — AutoCapture owns the single uint16 ID sequence (allocateImageId pre-increments), shared by CAPTURE_NOW and interval captures so GET_STATUS lastImageId is truthful across both modes; enable() resets the baseline so the first capture fires one full interval after enable, and process() advances the baseline before the attempt so a failed capture cannot drive a tight failure loop (T-01-09 mitigation); interval bounds re-validated inside the module so a bypassed handler cannot drive the timer below 1 Hz (T-01-08)
- [Phase 01]: 01-05: legacy 30 s capture timer fully removed (not debug-gated) — AutoCapture is the balloon's sole automatic capture trigger and sole image-ID sequence; its packets were never transmitted so the block's only live effects were the CR-05 harms (post-disable captures, >30 s cadence pollution, ID collision, frame-buffer churn). Single-authority invariant enforced by recursive negative-grep source gates, not just build passes
- [Phase 01]: 01-06: packet type owned by the factory, not the serializer — createResponsePacket/createCommandPacket assign packet.type as the first field (0x11/0x10) so every construction path emits the documented byte; the wire harness now transcribes the factory (zero-init + defective variant), not just the serializer (WR-05)
- [Phase 01]: 01-06: FrameSize translation is by NAME in both directions — framesizeFromInt forward, frameSizeFromEsp reverse (real QVGA=5 -> project code 6); value-8 wire code relabeled FRAMESIZE_CIF (real 400x296 mode, reachable on OV2640); real sizes without a protocol code report boot-default QVGA
- [Phase 01]: 01-06: terminal-state guard in CommandSender::handleResponse only — duplicate/late responses for ACKED/FAILED/TIMEOUT slots return before response storage and counter changes; findTrackedCommand still matches terminal slots because cancelCommand (own WR-03 decrement guard) and UI queries legitimately resolve them
- [Phase ?]: Phase 2 wire contract locked: 0x12/0x14 receivers force fixed body lengths (27/17) instead of trusting header bodyLen; 0x13 keeps header bodyLen == dataLen so chunk framing stays command-shaped
- [Phase ?]: Base reassembly follows the push stream: a new manifest supersedes any in-flight transfer; CRC-mismatch completions keep the previous verified thumbnail — never fabricated state (D-22 windowed pull arrives 02-03)
- [Phase ?]: Binary HTTP bodies on this WebServer core use setContentLength + send + sendContent (no raw-pointer send overload exists); parameterized /img/{id}_t.jpg routes via not-found dispatch with strictly-numeric id parsing
- [Phase 02]: 02-02: PRI-01/SC-5 satisfied by a live 0x14 telemetry beacon — user-confirmed at blocking checkpoint (research Q1 option a); beacon outranks image chunks in transmit arbitration, never the reverse
- [Phase ?]: Window requests ride the Phase 1 tracked-command machinery with CMD_ACK_TIMEOUT_WINDOW_MS (15 s) while a separate 8 s stall clock watches the chunk stream — the ACK bounds only the arm handshake (02-03)
- [Phase ?]: Fulls stream straight to SD via seek+offset with no whole-image RAM buffer; complete is set only after esp_rom_crc32_le read-back verification, and SD-degraded transfers report INCOMPLETE (notStored) even at 100 percent chunks (02-03)
- [Phase ?]: D-22 consequence flagged for hardware UAT: the balloon serves windows only from announced full entries, so a thumbnail-hole window re-request cannot retrieve thumbnail bytes — the base degrades honestly to INCOMPLETE after 3 passes (02-03)
- [Phase ?]: 02-04: event triggers live inside AutoCapture.process() behind the SC-5 master enable — AUTO_CAPTURE_DISABLE stops interval AND event captures; fire() is the single gated capture path (D-28 spacing wraps every automatic source, T-01-09 baseline-before-attempt, shared allocateImageId) and every fire advances lastCaptureTime so D-27 (event resets interval baseline) falls out of the arithmetic
- [Phase ?]: 02-04: eventsEnabled defaults OFF at boot — event captures require explicit opt-in via the UI toggle; delta defaults 150 m / 500 m / 20 s spacing (research sketch constants), all re-validated in-module so a spoofed SET_EVENT_THRESHOLDS cannot machine-gun the camera (T-02-11 double validation)
- [Phase ?]: 02-04: base polls GET_STATUS every 30 s (skipped while commands are in flight) and CommandSender latches the newest STATUS payload — the Event Capture card displays balloon-reported truth, never the last-submitted form
- [Phase ?]: 02-05: window requests are kind-addressable on the wire (imageId BE16, imageKind u8 at offset 2, startChunk BE16, count u8 — 6 bytes, harness-pinned to the literal bytes) — a thumbnail heal rides the SAME windowed ARQ as a full pull (D-22 restored as the single reliability mechanism); the balloon serves THUMBNAIL windows from thumbBuffer on any entry whose push finished, and a THUMBNAIL request never evicts anything
- [Phase ?]: 02-05: fairness preemption lives INSIDE the chunk branch — pushPending() services a starved armed window (> IMG_WINDOW_SERVICE_PREEMPT_MS 5000 ms, strictly under the base's 8000 ms stall) before push work, while the beacon early-return and command-response loop order stay untouched (PRI-01 proven by node order-gates); overflow eviction is class-ranked with the windowEverArmed ANNOUNCED entry (the active pull) as last resort
- [Phase ?]: 02-05: D-24 bounds only CONSECUTIVE unhealed stalls — acceptChunk is the sole passCount zero-writer (accepted-chunk progress retires charged passes); thumbnail heals fire only when no full pull is active AND the same-id FULL manifest arrived (push provably finished), with a 24 s idle fallback for oversize thumbnails that never announce a full; heal chunks route to the armed THUMBNAIL slot before the FULL-first precedence
- [Phase ?]: 02-05: sidecars are kind-suffixed — IMG_{id}_T.JSON for thumbnails vs IMG_{id}.JSON for fulls (mirrors D-31's _T.JPG, the Phase 3 gallery contract) so neither finalization truncates the other's D-30 record; slot-pressure evictions fully reset the slot (*oldest = ImageRxTransfer{}) so a new manifest starts at 0/N
- [Phase ?]: 03-01: Battery tile JS written in final battery-aware form in Task 1 (renders em-dash under pre-extension JSON), so Task 2 added only the JSON fields
- [Phase ?]: 03-01: Beacon battery truth gated on voltage in [1.8,8.0] V AND nonzero raw ADC read — floating sense line never reports a fake pack; PowerMgr().update() wired on 1s millis timer in processPowerManagement
- [Phase ?]: Pull-based trajectory recording: TrajectoryBuffer polls ImageRx().getTelemetrySnapshot() with beacon-seq dedup — producer untouched
- [Phase ?]: Offline map swap covers (not hides) Leaflet so tile recovery stays live; periodic re-probe every 3rd poll flips back automatically
- [Phase ?]: TRAJ_MAX_POINTS stays 500: measured 500-point /api/state payload 14,009 B (traj 13,395 B) — under 16 KB budget
- [Phase ?]: ALRT-06 beacon loss = (expected - received) over the sliding 60s window: equals interior seq-gap count at the locked 5s cadence but also covers edge loss and total outage
- [Phase ?]: Critical alerts latch on the condition rising edge; ack while condition persists keeps the row hidden until it clears and re-fires (03-03)
- [Phase ?]: Gallery index is a fixed static RAM array (1000 entries) built at boot, rebuilt only when finalizeImage's indexVersion advances - pagination never rescans the directory (03-04)
- [Phase ?]: Sidecar presence bits (SD_SC_PRESENT_*) drive /gallery detail serialization - only sidecar-carried fields render, never zero-filled; gpsValid derives from the null-vs-numeric telemetry triple (03-04)
- [Phase ?]: Latest Capture card retired in 03-04: the newest gallery grid item is the latest-capture surface; latestThumbId JSON field and /img routes remain
- [Phase ?]: 03-05: WiFi dual-mode discipline without the dual-mode constant — during a runtime AP-to-STA join the AP keeps serving (WiFi.begin ORs STA into the running mode); update() converges via WiFi.mode(WIFI_STA) on WL_CONNECTED or WIFI_AP on deadline, so WIFI_AP_STA never appears and the radio never rests dual-mode
- [Phase ?]: 03-05: Join deadline is wrap-safe millis subtraction (D-26 idiom); requestSwitch persists to NVS FIRST then drives the radio so a reboot mid-switch honors the operator's choice
- [Phase ?]: 03-05: /api/state wifi block carries ssid (JSON-escaped) beyond {mode,ip,joining,errorSsid} — the locked mode-line copy needs it; the password never appears in any response (T-03-12); explicit AP switch clears joinErrorSsid
- [Phase ?]: 01-07: all section#capture control forms submit in-page via ONE delegated submit listener (route from each form's action attribute, generic form.elements serialization, empty body for /capture and /auto-capture-stop) — defaultPrevented + id-skip guards keep alerts-form/wifi-form single-shot; per-form message divs recovered by js-capture-msg marker class render only the server-returned verdict (IN-03); pollOnce() after every settle/catch; addition confined to the HTML_FOOTER PROGMEM literal (1223f46 safe)
- [Phase ?]: 01-08: E32 register truth from the official manual beats hobby-doc layouts — SPED is [7:6] parity / [5:3] baud / [2:0] air rate (worked example 0x1A), config return frame is HEAD echo 0xC0/0xC2 (0xC1 accepted for older revisions) + five register bytes, factory default C0 00 00 1A 06 04, config-mode UART fixed 9600 8N1
- [Phase ?]: 01-08: config writes are echo-verified (success only when the module's return frame matches every register byte) and boot-time air-rate enforcement is fail-open inside E32LoRa::begin — both boards converge to 9.6 kbps every boot via a masked read-modify-write touching only SPED bits [2:0]; a failed write logs the loud modules-may-mismatch ERROR (01-09 stop condition) but never aborts boot (1223f46 lesson)
- [Phase ?]: 01-08: single-lever scope held — air rate is the only lever pulled; pacing/stall-window changes in image_tx/image_rx managers stay out unless the 01-09 discriminator run proves B4 saturation persists at 9.6 kbps
- [Phase ?]: 01-09: SD transport is SDMMC, not SPI — operator datasheet ground truth (built-in slot CLK=39/CMD=38/DATA=40, no CS) replaced the 02-03 SPI wiring assumption; authorized scope expansion moved src/sd_storage.cpp + include/sd_storage.h to SD_MMC 1-bit (commit 794df00), base_station_config.h constants renamed SD_CLK/CMD/DATA_PIN; GPIO39 STATUS_LED coexistence safe (pinMode@2060 precedes SDStorage().begin()@2117)
- [Phase ?]: 01-09: G-01-3 root cause A was a TRANSPORT mismatch, not wiring - the base's built-in SD slot is SDMMC (CLK=39/CMD=38/DATA=40, no CS); the 02-03 SPI breakout assumption never matched this hardware, so the fix was the SD_MMC 1-bit switch (794df00), operator-authorized scope expansion
- [Phase ?]: 01-09: the air-rate mismatch event (base-only flash severed the pair - no telemetry, window TIMEOUTs - until the balloon was reflashed to 9.6k) proved 01-08's boot-time enforcement is load-bearing; both-boards-same-rate is a hard invariant of the link
- [Phase ?]: 01-09: G-01-3 closed on hardware (Storage OK + CRC-verified full-image transfer); residual narrowed to the thumbnail PUSH burst (G-01-5, B2/B4-class) - the full's windowed ARQ recovered everything while the thumbnail's post-hoc bounded heal did not, localizing the loss to the push exposure profile, not transfer logic
- [Phase ?]: [Phase 01]: 01-10: G-01-5 mechanism is a FOURTH class — E32 AUX handshake phantom transmit-failure (e32_lora.cpp:223 write-then-:227-wait books FAILED after bytes left; v3 balloon 37 sent/41 FAILED while base received 72/72, 4/4 kinds COMPLETE); B5 close-range RX-overload NOT reproduced (downgraded); 01-08 pacing levers stay unpulled — the AUX accounting fix is the remediation lever
- [Phase ?]: [Phase 01]: 01-10: G-01-6 = image-ID reboot reset — fresh-boot balloon re-allocates ID 1 and overwrites IMG_00001_* (proven twice with perfect transfers); remediation is persisting the counter or deriving next ID from card state at boot, rides 01-11
- [Phase ?]: 01-10: GPIO39 STATUS_LED hot-loop error flood (esp32-hal-gpio __digitalWrite warning, 9563/3513/6122 lines per session, ~7-10 ms blocking each; updateLED READY/NO_LINK branches write every pass) routed to 01-11 as ride-along cleanup — NOT the loss mechanism; thumbnail-sizing quirk (v3 thumb 7138 B == full) flagged for the same round
- [Phase 01]: 01-11 Task 2 executed branch-c + branch-e + ride-alongs R1/R2 (operator-confirmed routing): AUX phantom-failure accounting fixed (f265556 - missed AUX-low after a complete write no longer books FAILED; short-write and pre-write readiness stay true failures), image-ID counter persisted to NVS (678d4f1 - fresh-boot captures append IMG_00002_* instead of overwriting; fail-open), STATUS_LED remapped 39->41 + edge-gated writes (91bee03 - kills the 3.5k-9.6k-line/session HAL error flood), thumbnail QQVGA frame-dimension guard (c67e1a5 - full-size frames bail honestly instead of pushing as thumbnails). Builds 2/2 + harness 49/49 green. e-site deviation: trace named balloon auto_capture.cpp, not the plan-anticipated sd_storage.cpp (trace-named-site clause) — 01-10 discriminator evidence: E32 AUX handshake phantom TX-failure named as the G-01-5 mechanism; image-ID reboot reset named as the G-01-6 mechanism; single-lever discipline held (no pacing constants touched)
- [Phase ?]: [Phase 01]: 01-11 Task 3 bench re-verification CLOSED both gaps on operator evidence and honestly routed two new ones: G-01-5 resolved (f265556 — phantom FAILED 0/0/0 across three post-fix sessions vs ~41/capture baseline; spaced series 4/4 kinds COMPLETE discriminates the fix from the concurrency residual), G-01-6 resolved (678d4f1 — IDs 1..5 sequential across reboots, gallery index 2->3->4->5 with appends, no overwrite); G-01-7 concurrent-transfer starvation + G-01-8 SET_RESOLUTION VGA FB-OVF recorded open with named levers (serialize transfers / NACK-above-buffer); IMG-02/IMG-03 flipped complete — the 01-10 blocking condition (bench-verified remediation) is discharged, narrower residual truths live in the WINDOWS ledger (entries 3/4)

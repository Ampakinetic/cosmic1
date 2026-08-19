---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
current_plan: 2
status: executing
stopped_at: Completed 03-01-PLAN.md
last_updated: "2026-08-19T15:55:26.399Z"
progress:
  total_phases: 3
  completed_phases: 1
  total_plans: 16
  completed_plans: 11
current_phase: 2
current_phase_name: enhanced-web-interface
---

# Project State

**Last Updated:** 2026-08-18

## Current Status

**Project:** Cosmic1 Base Station Camera Control Extension
**Phase:** Phase 2 — all 5 plans executed (02-01 tracer + 02-02..02-04 + 02-05 gap closure); all 4 verifier gaps closed at code level; next: re-verification of the gap closures, hardware UAT, then security gate
**Milestone:** v1.0

## Current Position

**Current Plan:** 2
**Total Plans in Phase:** 5
**Status:** Ready to execute
**Progress:** [███████░░░] 69% (Phase 1: all 6 plans executed; Phase 2: all 5 plans executed — both wire-harness green, both firmware targets green)

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

## Current Phase

**Phase 1: Command Protocol & Control**

- Status: verification — human_needed. All 6 plans executed; zero open code gaps (re-verification #3 independently confirmed CR-01/02/03 closures; no regressions; all 6 requirements satisfied at code level). Remaining before phase complete: hardware UAT (`/gsd-verify-work 1`, 5 items) + security gate (`/gsd-secure-phase 1`)
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

**Current focus:** Phase 03 — enhanced-web-interface

## Next Steps

1. **Phase 02 re-verification:** run the phase verifier (`/gsd-verify-work 2` or equivalent) — the 4 gap closures in 02-05 need independent confirmation (verifier's own reads/greps/harness/builds), mirroring the Phase 1 re-verification loop
2. **Hardware UAT:** fulls complete under the 20 s capture cadence, a link-loss thumbnail hole heals RETRYING→COMPLETE, telemetry age stays within ~10 s during transfers, and an SD card shows IMG_{id}.JPG + IMG_{id}_T.JPG + IMG_{id}.JSON + IMG_{id}_T.JSON after both kinds finalize (Gap 1/2/PRI-01/Gap 4 runtime halves — see 02-05-SUMMARY Known Limitations)
3. **Security gate (before phase complete):** `/gsd-secure-phase 2` (Phase 1's gate also still pending — `01-UAT.md` items + `/gsd-secure-phase 1`)
4. **Then:** `/gsd-progress` toward the phase transition and Phase 3 (Enhanced Web Interface)

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

**Last session:** 2026-08-19T15:55:26.358Z
**Stopped at:** Completed 03-01-PLAN.md
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

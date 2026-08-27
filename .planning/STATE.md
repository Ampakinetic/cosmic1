---
gsd_state_version: 1.0
milestone: v1.0
current_plan: 3
status: executing
stopped_at: "Completed 01-22-PLAN.md (G-01-7 base RX half: full-arm deadline IMG_FULL_ARM_DEADLINE_MS 20 s bounds the thumbnail-heal serialization hold; builds 2/2, harness 52/52; G-01-7 closure judged at the 01-24 bench)"
last_updated: "2026-08-27T15:53:07.143Z"
state_head: 84f8aea77da55679887d5de77fd48d4a3a5fbbfa
progress:
  total_phases: 3
  completed_phases: 0
  total_plans: 34
  completed_plans: 31
milestone_name: milestone
current_phase: 2
current_phase_name: Command Protocol & Control
---

# Project State

**Last Updated:** 2026-08-18

## Current Status

**Project:** Cosmic1 Base Station Camera Control Extension
**Phase:** Phase 1 (command-protocol-control) — 20 plans executed (01-01..01-20). 01-20 bench re-verification round COMPLETE (session #6, 2026-08-27 evening through 2026-08-28 ~00:20 local, base6.log + balloon11.log, 12 images): COMMAND SURVIVABILITY UNDER UNSPACED LOAD SOLVED AT BENCH — series A's 3 unspaced CAPTURE_NOW all ACKed with zero series-A command timeouts; the 01-18 quiet gate held both retried transmits under image-24's chunk storm and five more held-command lifecycles ran to ACKED. G-01-9 defect C (silent manifest air-loss) DEAD — the 01-17 receipt-informed re-announce ran live (image 31 recovered 82/82 base6.log:13096; images 25/26 manifests re-delivered 3x/4x; every unrecovered full named at the bound): G-01-9 RESOLVED and WINDOWS entries 5/8/9 flipped fixed (8 = CR-04 on code fix + builds 2/2 + harness exit 0 with the unstageable camera-down scenario honestly recorded; 9 = WR-08 WIRED-but-unexercised, zero chunk TX failures at bench). G-01-7 stays OPEN rescoped to ONE named axis — BURST FULL-DELIVERY (the 3-re-announce bound expires while the base serializes the burst's thumbnails; depth-3 queue eviction) with levers named; interim mitigation: space captures. Session provenance: a bootloop saga preceded the session (GPS UART1 RX claimed reserved OPI-PSRAM pin 35 — fixed 3f2c2c6 to GPIO 41; balloon6-10.log are debug captures, base6.log lines 1-~1618 are pre-session noise). SC-3 visible-effect pairs STILL unjudged (fifth round riding — zero settings commands issued), and the WR-03 cadence discriminator + CIF-cycle/QVGA-restore clauses ride the next bench moment. Remaining before phase complete: that bench moment (SC-3 pairs + WR-03 discriminator; optional G-01-7 residual round) + security gate (/gsd-secure-phase 1)
**Milestone:** v1.0

## Current Position

**Current Plan:** 3
**Total Plans in Phase:** 24
**Status:** Ready to execute
**Progress:** [█████████░] 92% (all 22 plans executed across Phases 1-3; Phase 1 close-out remains: G-01-9/G-01-7-residual round + security gates before phase complete)

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
- ✅ Phase 1 plan 01-12 executed (2026-08-24): G-01-7 levers (1064480 — pendingHealThumbnail hold at both activation sites, evictEntriesOlderThan mid-service guard, IMG_WINDOW_RX_SETTLE_MS 500 two-sided) + G-01-8 framesize re-init with recovery bounded to allocatedFrameSize (f51bad6 — cached-settings re-init, mandatory recovery-to-previous-size); builds 2/2 + harness 49/49; bench session #4 (balloon4/base4) RESOLVED G-01-8 (CIF/VGA/SVGA honest re-init, ZERO FB-OVF at settings steps, 6/6 captures SUCCESS, camera never died; series-B steps d/e/f unexercised, ride Test 3) and honestly left G-01-7 OPEN rescoped (levers engaged: hold 3x, zero mid-service evictions — but image 7 thumb INCOMPLETE 35/36 under BUSY-deferral pass burn, image 7 full CRC mismatch at 36/36 received, image 8 full silently lost with its manifest); NEW gap G-01-9 (full-sized thumbs 4/6 passing the metadata-only guard; balloon-source-side stored-bytes CRC corruption; no FULL-manifest re-announce) + CommandSender retry overrun (13x 'attempt 4/3', WINDOWS 6, deferred-items.md) routed open (see 01-12-SUMMARY.md)
- ✅ Phase 1 plans 01-13/01-14/01-15 executed (2026-08-24): CR-01 kind-tagged chunk wire + kind-exact routing + CR-03 thumbnail drain/payload bound; CR-02/WR-01 success-gated bounded manifest re-announce + WR-02 two-pass overflow scan; 7dd5dab defer-aware D-24 pass accounting + 45c8b80 BUSY-deferral retry-ordinal label (rides WR-05 window class); builds 2/2, harness 52/52
- ✅ Phase 1 plans 01-17/01-18/01-19 executed (2026-08-26/27): 01-17 receipt-informed bounded FULL-manifest re-announce keyed on fullWindowEverArmed (c517c4f, + WR-08 success-gated cursor advance with IMG_CHUNK_TX_RETRY_MAX same-index bound), 01-18 command-transmit channel-quiet gate (receive-time latch on inbound 0x13 frames holds first transmit + timeout retry while a frame arrived within 750 ms, 30 s best-effort bound, composes with 01-15 without touching D-07/terminal guards), 01-19 CR-04 scoped camera gate (5b9a8a7 commandRequiresCamera covers exactly CAPTURE_NOW + the seven SET_* classes; IMAGE_WINDOW_REQUEST/GET_STATUS/auto-capture dispatch ungated) + WR-03 manual-capture baseline (markCaptureBaseline on handleCaptureNow success only); builds 2/2 + harness green each round
- ✅ Phase 1 plan 01-20 executed (2026-08-27/28): bench re-verification session #6 (base6.log + balloon11.log, 12 images, operator-run; round-#8 behavior + GPS pin fix 3f2c2c6 after a bootloop saga — balloon6-10.log are debug captures, base6.log lines 1-~1618 pre-session) — command survivability under unspaced load SOLVED AT BENCH (3/3 series-A ACKs zero command timeouts; quiet gate held 2 retries under the chunk storm + 5 late held-to-ACKED lifecycles), G-01-9 defect C closed (re-announce live: image 31 recovered 82/82 base6.log:13096, manifests re-delivered 3x/4x, every drop named at the bound) -> G-01-9 RESOLVED, WINDOWS 5/8/9 fixed (8 on code+builds+harness with the unstageable camera-down scenario recorded; 9 WIRED-unexercised — zero chunk TX failures at bench); G-01-7 open rescoped to burst full-delivery (verdicts 2/6, levers named, interim: space captures); SC-3 pairs fifth-round riding, WR-03 discriminator + CIF/QVGA-restore clauses riding (see 01-20-SUMMARY.md)

## Current Phase

**Phase 1: Command Protocol & Control**

- Status: gap-closure rounds complete through 01-20 (all 20 plans executed; WINDOWS ledger 1 open entry). Remaining before phase complete: the next bench moment (SC-3 visible-effect pairs — fifth round riding — + WR-03 cadence discriminator + CIF-cycle/QVGA-restore clauses; optional G-01-7 burst-full-delivery residual round) + security gate (`/gsd-secure-phase 1`; Phase 2/3 gates also pending)
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

**Current focus:** Phase 01 — Command Protocol & Control

## Next Steps

1. **Next bench moment (SC-3 + WR-03 clauses riding; optional G-01-7 residual round):** SC-3 visible-effect pairs (brightness -2 vs +2 + one other class — STILL unjudged after five rounds, zero settings commands in sessions 5/6), WR-03 auto-capture cadence discriminator (zero AUTO_CAPTURE_ENABLE commands in sessions 5/6), and the CIF-cycle + QVGA-restore clauses under Test 3 (session 6 ran SVGA-class fulls clean but never cycled CIF or restored QVGA with the console attached). Optionally a G-01-7 burst-full-delivery residual round — levers named: full-window-priority interleave during thumb serialization (or arm FULL windows between thumb passes), re-arm the re-announce bound on manifest receipt, burst-admission depth. Interim mitigation unchanged: space captures
2. **Security gate (the remaining phase-close gate):** `/gsd-secure-phase 1` (Phase 2/3 gates also still pending)
3. **Phase 3 backlog:** gallery UX todos captured — full-resolution image viewer (2026-08-23), tile number overlay + Incomplete-badge explanatory copy (2026-08-24, operator bench feedback from session #4), gallery detail resolution+filesize display (2026-08-25, operator request queued post-verification, out of 01-16 scope), thumb-first image delivery + antenna-pointing overlay (2026-08-27, captured at 898fcc6)
4. **Then:** `/gsd-progress` toward phase transitions and ship review (WINDOWS ledger: 1 open entry — G-01-7 burst full-delivery, entry 3; entries 5/8/9 closed at the 01-20 bench; blocks /gsd-ship until the residual round closes it)

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
*State updated: 2026-08-28 - 01-20 executed (bench session #6 ledger flips: G-01-9 resolved, WINDOWS 5/8/9 fixed, G-01-7 open rescoped to burst full-delivery); all 20 Phase 01 plans executed; next bench moment (SC-3/WR-03 clauses) + security gate pending*

## Session

**Last session:** 2026-08-27T15:53:06.494Z
**Stopped at:** Completed 01-22-PLAN.md (G-01-7 base RX half: full-arm deadline IMG_FULL_ARM_DEADLINE_MS 20 s bounds the thumbnail-heal serialization hold; builds 2/2, harness 52/52; G-01-7 closure judged at the 01-24 bench)
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
| Phase 01 P12 | ~4h across executor + operator bench sessions (Tasks 1-2 2026-08-23/24, bench session #4 2026-08-24 ~13:16) | 3 tasks | 7 files |
| Phase 01 P13 | 17min | 3 tasks | 8 files |
| Phase 01 P14 | 8min | 2 tasks | 3 files |
| Phase 01 P15 | 11min | 2 tasks | 3 files |
| Phase 01 P16 | ~2h across executor + operator bench session #5 (2026-08-25) | 2 tasks | 4 files |
| Phase 01 P17 | 15min | 3 tasks | 4 files |
| Phase 01 P18 | 7min | 2 tasks | 2 files |
| Phase 01 P19 | 7min | 2 tasks | 3 files |
| Phase 01 P20 | ~2h across executor + operator bench session #6 (2026-08-27/28, bootloop debug saga included) | 2 tasks | 3 files |
| Phase 01 P21 | 12min | 3 tasks | 3 files |
| Phase 01 P22 | 10min | 2 tasks | 2 files |

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
- [Phase ?]: [Phase 01]: 01-12 Task 3 bench re-verification resolved G-01-8 on operator evidence (f51bad6 — CIF/VGA/SVGA each via 'Camera: framesize growth requires re-init' balloon4.log:1791/:2784/:3588 with ZERO FB-OVF at every settings step vs the 4,379 baseline, 6/6 captures SUCCESS, camera never died) and honestly kept G-01-7 OPEN despite its levers provably engaging (hold 3x, zero mid-service evictions) — the series-A truth failed on three DISTINCT mechanisms: BUSY-deferral pass burn (image 7 thumb 35/36, balloon4.log:1154/:1157), balloon-source-side stored-bytes CRC corruption (image 7 full 36/36-received CRC mismatch base4.log:854), and a silently lost FULL manifest (image 8, balloon4.log:722) — rescoped as defer-aware pass accounting + re-announce + thumb fix; NEW gap G-01-9 opened (full-sized thumbs 4/6 passing the metadata-only QQVGA guard — payload must be validated, not fb->width/height; kind-tagged balloon chunk logging named as the defect-B discriminator) and the CommandSender retry overrun (13x 'attempt 4/3') routed to WINDOWS entry 6
- [Phase ?]: 01-13: CR-01 fixed at root cause on the wire — every 0x13 chunk frame carries an imageKind byte (u8 after imageId, 6-byte body overhead) validated at deserialize (kinds outside THUMBNAIL/FULL_IMAGE rejected, T-01-13-01); the base's routing heuristic is deleted — every chunk routes by exact (imageId, imageKind), so a late thumbnail-heal straggler can never occupy full-image indices (the 36/36-received stored-bytes CRC mismatch mechanism, G-01-9 defect B)
- [Phase ?]: 01-13: CR-03 thumbnail payload honesty — createThumbnail drains ONE stale frame after the QQVGA/quality-20 downshift (fb_count 2 / GRAB_LATEST can hand back the pre-downshift capture with restamped metadata, logged with width/height/len as the payload-vs-metadata discriminator) and bounds fb->len at THUMB_MAX_BYTES 8192 through the existing honest bail path (~5x the 1341-1703 B correct-thumb ceiling, far below the 7157-28808 B impostors; G-01-9 defect A)
- [Phase ?]: 01-13: wire-change discipline — both boards MUST be reflashed together before the next bench run (mixed old/new firmware breaks chunk framing entirely, 01-09 severance lesson); no gap status flips in this plan, G-01-9 closure requires the 01-16 bench series
- [Phase ?]: 01-14: manifest one-shot states are consumed only on transmit success — ANNOUNCED (CR-02) and PUSH_THUMB_CHUNKS (WR-01) advance inside the success branch; failures retry one-per-pass bounded by shared IMG_MANIFEST_MAX_ATTEMPTS 3, and at the bound the kind drops honestly (buffer freed + zeroed, named log, park at THUMB_PUSHED keeping thumbBuffer for heals / proceed via completedThumbState)
- [Phase ?]: 01-14: WR-02 overflow victim scan is two-pass — pass 1 skips armed+incomplete windows (BUSY predicate copied verbatim), pass 2 (all-mid-service only) drops the skip with a named log so the depth-3 queue never wedges; the skip lives ONLY in that scan, evictionClassOf ranking and sweepExpiredEntries TTL byte-identical (carried 01-12 prohibition)
- [Phase ?]: 01-14: manifestAttempts is ONE shared counter for the two mutually exclusive manifest phases, reset on each success and in freeEntry; G-01-9 defect C code-level mechanism removed but NOT closed — closure requires the 01-16 bench (no gap status flips this plan)
- [Phase ?]: 01-15: D-24 passes count transfer OPPORTUNITIES, not re-requests — windowRequestSeq tracks each transfer's outstanding IMAGE_WINDOW_REQUEST and both stall sites extend the stall clock (no pass, no duplicate) while the tracked command is PENDING/SENT; a pass is charged only after the request terminalizes (ACKED/FAILED/TIMEOUT, with a recycled IDLE slot reading terminal in the conservative direction). The advance and both finalize paths cancel the serviced/outstanding request (WR-04 churn + post-terminal re-arm)
- [Phase ?]: 01-15: NACK_BUSY on IMAGE_WINDOW_REQUEST defers instead of terminalizing — the command returns to PENDING and retries inside its own D-05/D-07 budget (existing backoff block paces it), staying non-terminal so the ImageRx in-flight guard is correct; the deferral is scoped to this one class (every other command keeps FAILED-on-BUSY) and bounded by maxRetries, so the link LED stops flagging routine deferrals and budget exhaustion still books a real failure
- [Phase ?]: 01-15: retryCommand prints the retry ordinal (post-increment retryCount, 1..maxRetries) against maxRetries — the 'attempt 4/3' label (WINDOWS entry 6) is gone with zero counter/bound/timing change; one mechanism split across two files (BUSY deferral keeps the request non-terminal BECAUSE the base stall guard keys on it)
- [Phase 01]: 01-16: closure honesty on partial evidence — a gap with three defects (G-01-9) closes its log-proven thirds (A: thumbs, B: CRC) with verbatim bench quotes while the third (C) stays open NARROWED to the exact uncovered class (TX-success + air loss consumes the one-shot announce); the reboot clause was judged on base-side wire evidence (beacon seq reset 451->0 amid continuous post-reset chunk flow proves a fresh ImageTxManager; image 21 NVS-continued + QVGA-class) with the missing balloon console explicitly recorded rather than assumed
- [Phase 01]: 01-16: two log-reading traps documented for future rounds — '[HTTP] GET /img/... -> 404' is the handleNotFound DISPATCH banner (main_basestation.cpp:3641) printed before the image handler serves 200, NOT a response code; and healed-thumb finalize lines undercount 'B persisted' by design (sd_storage.cpp:234 accounting restarts at each reopen flip) while complete=true still reflects the read-back CRC — neither is a defect
- [Phase 01]: 01-17: window-arm IS the receipt signal — the FULL-manifest re-announce keys on fullWindowEverArmed (set ONLY at non-thumb arms, so a THUMBNAIL heal never stops it; image-15 class), fires only in pushPending's idle slot (no push work, no armed window) after 10 s idle, and consumes one attempt + one idle period per transmit regardless of TX verdict; bounded at 3 with park-and-free + named drop (mirrors the 01-14 announce bound)
- [Phase 01]: 01-17: SERVED means bytes offered, not cursor reached — both TX push paths gate cursor advance on transmit success with a bounded same-index retry (IMG_CHUNK_TX_RETRY_MAX 3, named skip log); a final chunk skipped after the bound completes the window (windowArmed cleared, tail re-request re-opens service) but leaves the entry ANNOUNCED, never SERVED for bytes that never left the balloon (WR-08)
- [Phase 01]: 01-18: command transmit channel-quiet gate — a receive-time latch on inbound 0x13 chunk frames (sole storm class; 0x12/0x14 unlatched) holds BOTH the PENDING first transmit and the SENT timeout retry while a frame arrived within 750 ms; a hold writes only channelHoldStartMs (no retry, no failure, no D-05 window start — sendTime is set only by an actual transmit), bounded by a 30 s best-effort transmit
- [Phase 01]: 01-18: wrap-don't-rewrite — the quiet gate composes with 01-15 (a held PENDING IMAGE_WINDOW_REQUEST stays non-terminal so windowRequestInFlight extends the D-24 stall clock, no pass charged) and leaves the D-07 backoff block, BUSY-deferral branch, terminal-state guard, ackTimeoutFor, and frame dispatch byte-identical to pre-plan HEAD (diff-hunk-proven)
- [Phase 01]: 01-19: CR-04 gate scoping, not removal - commandRequiresCamera() classifies exactly CAPTURE_NOW + the seven SET_* sensor classes as camera-touching (NACK_BUSY refusal byte-identical in message/counter/shape); AUTO_CAPTURE_ENABLE/DISABLE, GET_STATUS, IMAGE_WINDOW_REQUEST, SET_EVENT_THRESHOLDS and unknown commands dispatch ungated so a camera-down window never strands announced fulls or blinds the status poll
- [Phase 01]: 01-19: WR-03 - public AutoCapture::markCaptureBaseline() (lastCaptureTime = millis(), the same field fire() advances) called exactly once in handleCaptureNow success; a failed manual capture leaves the baseline untouched (a failed attempt must not defer the schedule); safe while disabled since the interval branch is gated on enabled and enable() resets the baseline
- [Phase 2]: 01-20: session-6 verdict honesty — command survivability (quiet gate) and defect-C silent-loss both SOLVED at bench, but series-A verdicts failed 2/6 on a NEW named axis (burst full-delivery: re-announce bound expires during burst thumb serialization; depth-3 queue eviction), so G-01-7 stays open rescoped rather than closing on the solved sub-truths
- [Phase 2]: 01-20: evidence-class flips held — CR-04 (WINDOWS 8) flipped on code fix + builds + harness with the unstageable camera-down scenario recorded by name (never fabricated); WR-08 (WINDOWS 9) flipped WIRED-but-unexercised (zero chunk TX failures at bench); the log-provenance deviation (balloon11.log carries the session's balloon console after the GPS-pin-35 bootloop saga, fixed 3f2c2c6) is recorded in the ledgers themselves
- [Phase 2]: 01-20: SC-3 visible-effect pairs left unjudged a FIFTH round (zero settings commands at bench) and the WR-03 cadence discriminator + CIF/QVGA-restore clauses unexercised — all ride the next bench moment rather than being fabricated; Test 3 stays issue
- [Phase 2]: 01-21: the re-announce drop-clock is receipt-informed on two levers — every inbound IMAGE_WINDOW_REQUEST (any kind, any verdict, including unknown/evicted rejects) stamps a channel-liveness clock that holds the idle-slot re-announce for IMG_FULL_REANNOUNCE_BUSY_MS 15 s consuming nothing, and a request MATCHING an ANNOUNCED never-FULL-armed entry re-arms its reannounceAttempts budget with a named log; the 01-17 IMG_FULL_REANNOUNCE_MAX 3 named-drop terminal path survives verbatim (receipt evidence holds the clock, it never deletes the bound)
- [Phase 2]: 01-21: eviction ranking is by receipt evidence, not airtime — evictionClassOf's ANNOUNCED case returns protected last-resort class 5 when windowEverArmed OR lastWindowRequestMs != 0; class 3 stays 'queued, no airtime invested' for never-requested entries only, and the class-5 eviction label names receipt evidence (session-6 image-24 class dead)
- [Phase 01]: 01-22: the 01-12 serialization hold is deadline-bounded, not deleted - IMG_FULL_ARM_DEADLINE_MS 20 s (10 s margin inside the balloon's unheld re-announce budget) judged on the FIFO-oldest queued full's manifestArrivedMs (scan hoisted before the hold branch, a pure read); outside the deadline the hold + one-shot log are byte-identical, past it the named deadline-release log falls through to the unchanged activation tail (no new D-21 trigger; the preempted heal defers via gate 1 and finalizes on its D-24 budget) — Session 6 proved the D-24 3-pass bound is looser than the balloon's re-announce clock under a 3-capture burst; arming a FULL window early is the single base-side action that stops the re-announce clock and protects the entry from class-3 eviction, composing with 01-21's balloon-side levers

# Roadmap: Cosmic1 Base Station Camera Control

**Project:** Base Station Camera Control Extension
**Mode:** Vertical MVP (each phase delivers end-to-end user capability)
**Created:** 2025-08-18

## Phase Overview

| # | Phase | Goal | Requirements | Status |
|---|-------|------|--------------|--------|
| 1 | Command Protocol & Control | Establish bidirectional LoRa communication for camera control | CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02 | Pending |
| 2 | Image Transmission | Transfer images from balloon to base station over LoRa with thumbnails | IMG-01, IMG-02, IMG-03, IMG-04, IMG-05, CTRL-05, PRI-01, PRI-03 | Pending |
| 3 | Enhanced Web Interface | Full base station control panel with telemetry, maps, and gallery | WEB-01, WEB-02, WEB-03, WEB-04, WEB-05, IMG-06, ALRT-01, ALRT-02, ALRT-03, ALRT-04, ALRT-05, ALRT-06 | Pending |

## Phase Details

### Phase 1: Command Protocol & Control

**Goal:** Establish bidirectional LoRa communication for camera control

**Mode:** mvp

**Requirements:**

- CTRL-01: User can trigger camera capture from base station web interface
- CTRL-02: User can adjust all camera settings remotely
- CTRL-03: System supports both manual and automatic capture modes
- CTRL-04: Automatic capture supports fixed interval timing
- CTRL-06: Camera commands that fail are retried with timeout
- PRI-02: Camera commands use retry mechanism with timeout

**Success Criteria:**

1. Base station web interface has camera control section with trigger button and settings forms
2. LoRa command packets transmitted from base station to balloon and acknowledged
3. Balloon receives camera commands and adjusts camera settings accordingly
4. Failed commands are retried with timeout and user is notified
5. Both manual trigger and interval-based auto-capture work end-to-end

**Deliverables:**

- Command packet protocol specification (request/response/ack)
- Base station command interface (UI + LoRa transmission)
- Balloon command handler (receive, parse, execute camera commands)
- Retry logic with timeout
- Basic camera control web UI

**Plans:** 25/27 plans executed through round #10 (01-24 bench session #7 FAILED on the D1 crash regression — balloon hard-crashed twice in series A, nothing flipped; G-01-7 unjudgeable, SC-3/WR-03/CIF/QVGA clauses riding, review 7d96a98 WR-01..03 unrouted). Gap-closure round #11 planned 2026-08-28 as 01-25..01-27: D1 debug round (addr2line root cause + fix + D2 stamp fix), review 7d96a98 routing + dispositioned fixes, bench re-verification session #8 (D1/D2/G-01-7 acceptance + the six-round-riding clauses). Then the security gate

Plans:
**Wave 1**

- [x] PLAN.md — executed tracer (non-standard filename; summary at 01-01-SUMMARY.md)
- [x] 01-02-PLAN.md — Protocol integrity: CRC/sequence fix, 240-byte limits, length-driven framing, retry terminal states

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 01-03-PLAN.md — Base station UI: all 7 settings forms, auto-capture controls, per-command outcome panel, real link LED (2026-08-18)
- [x] 01-04-PLAN.md — Balloon execution: camera sensor setters, AutoCapture interval module, real GET_STATUS, loop wiring

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 01-05-PLAN.md — CR-05 gap closure: remove the legacy 30 s capture timer running beside commanded auto-capture; AutoCapture becomes the sole capture/image-ID authority (CTRL-01/CTRL-03/CTRL-04)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 01-06-PLAN.md — Response-path gap closure: 0x11 type on all ACK/STATUS via createResponsePacket + faithful harness clause (CR-01/WR-05), name-based GET_STATUS resolution mapping + CIF relabel (CR-02/WR-01), terminal-state guard against duplicate-ACK count underflow (CR-03) (CTRL-01/CTRL-02/CTRL-06/PRI-02)

**Wave 5** *(UAT gap closure, 2026-08-23; wave numbers restart for this round — 01-07/01-08 parallel, 01-09 after both)*

- [x] 01-07-PLAN.md — G-01-4 gap closure: delegated in-page AJAX submit handler on section#capture covering all control forms (fetch + per-form message + pollOnce), riding the PROGMEM footer script (CTRL-01, CTRL-02, CTRL-03, CTRL-04)
- [x] 01-08-PLAN.md — G-01-3 gap closure (code half): real E32-900T30D register read/write with echo verification + boot-time 9.6 kbps air-rate enforcement, begin()-integrated fail-open on both boards (IMG-01, IMG-04, PRI-03)

**Wave 6** *(blocked on Wave 5)*

- [x] 01-09-PLAN.md — G-01-3 gap closure (hardware half): operator checkpoints — SD wiring audit vs base_station_config.h constants + second-card retry + boot-line confirm; flash both boards, bench discriminator run (B1-B4 trace matrix), end-to-end transfer + storage + in-page-submit verification (IMG-01, IMG-02, IMG-04, IMG-05, PRI-03)

**Wave 7** *(gap-closure discriminator round, 2026-08-23; blocked on Wave 6)*

- [x] 01-10-PLAN.md — G-01-5/G-01-6 gap closure (discriminator round): WR-01 dummy-PowerData fix (validity-gated PowerMgr wiring), bench operator checkpoints — G-01-6 zero-tooling /gallery total-vs-grid discriminator, G-01-5 serial discriminator on both consoles (heal-phase lines), second-capture re-test, close-vs-separated reliability tallies (B5), SC-3 ride-alongs (settings visible-effect + CIF 400x296) (IMG-02, IMG-03, IMG-06, PRI-03, CTRL-02)

**Wave 8** *(blocked on Wave 7)*

- [x] 01-11-PLAN.md — G-01-5/G-01-6 gap closure (remediation): routing decision from 01-10 evidence -> single-lever branch implementation (a: image_protocol.h pacing / b: E32_TARGET_TX_POWER enforcement / c: heal-servicing defect / d: galleryCountSeen latch reset / e: persistence-index fix / close) + bench re-verification with before/after tallies (IMG-02, IMG-03, IMG-06, PRI-03)

**Wave 9** *(gap-closure remediation round, 2026-08-24; blocked on Wave 8)*

- [x] 01-12-PLAN.md — G-01-7/G-01-8 gap closure (remediation): serialize thumbnail heal ahead of full-pull activation + never-evict-mid-service guard + inter-window RX-settle gap (G-01-7); camera framesize re-init with recovery bounded to allocatedFrameSize (G-01-8); bench re-verification series A (unspaced captures) / B (settings incl. CIF 400x296 + visible-effect spot-checks closing SC-3) (IMG-02, IMG-03, PRI-03, CTRL-02)

**Wave 10** *(gap-closure round #7, 2026-08-24; blocked on Wave 9)*

- [x] 01-13-PLAN.md — G-01-9 defect B/A gap closure (wire honesty): CR-01 imageKind byte in the 0x13 chunk frame (protocol + serializer + factory + base dispatch + harness, one atomic commit) with kind-exact onChunkFrame routing + kind-stamped TX logs; CR-03 thumbnail payload guard (stale-frame drain after QQVGA downshift + THUMB_MAX_BYTES 8192 bound) (IMG-01, IMG-02, IMG-03, IMG-04)

**Wave 11** *(blocked on Wave 10; 01-14 and 01-15 are file-disjoint and run in parallel)*

- [x] 01-14-PLAN.md — G-01-9 defect C gap closure (balloon TX hardening): CR-02/WR-01 success-gated manifest transitions with a shared bounded attempt counter (IMG_MANIFEST_MAX_ATTEMPTS 3, park-and-free at the bound); WR-02 mid-service-aware overflow victim scan with a never-wedge fallback (IMG-02, IMG-03, PRI-03)
- [x] 01-15-PLAN.md — G-01-7 residual gap closure (base defer-aware reliability): defer-aware D-24 pass accounting via windowRequestSeq in-flight tracking + request cancellation at advance/finalize (WR-04); NACK_BUSY deferral retry for IMAGE_WINDOW_REQUEST (WR-05 window class); retry-ordinal label fix (WINDOWS entry 6 / IN-01) (CTRL-06, PRI-02, IMG-03)

**Wave 12** *(blocked on Wave 11)*

- [x] 01-16-PLAN.md — Bench re-verification: flash both boards (wire change), series A (3 unspaced captures, every kind COMPLETE), settings series (CIF/SVGA both-kinds COMPLETE with sized thumbs, QVGA restore, reboot boot-resolution, SC-3 visible-effect pairs), dashboard regression glance; G-01-7/G-01-9 + WINDOWS 3/5/6 flipped on evidence (CTRL-01, CTRL-02, IMG-02, IMG-03)

**Wave 13** *(narrow residual round #8, 2026-08-25; blocked on Wave 12 — 01-17/01-18/01-19 are file-disjoint and run in parallel)*

- [x] 01-17-PLAN.md — G-01-9 defect C + WR-08 (balloon TX truth): receipt-informed FULL-manifest re-announce while ANNOUNCED-and-idle (no FULL window ever armed) with a named drop log at the bound; chunk-cursor advance + SERVED gated on transmit success with a same-index retry bound; CR-04/WR-08 routed in WINDOWS entries 8/9 (IMG-02, IMG-03, IMG-04, PRI-03)
- [x] 01-18-PLAN.md — G-01-7 residual (base): command-survivability quiet gate — command transmits (first attempts and timeout retries) hold while inbound 0x13 chunk frames arrived within 750 ms, consuming nothing (no retry, no failure, no ACK-window start), 30 s best-effort bound (CTRL-01, CTRL-06, PRI-02, IMG-03)
- [x] 01-19-PLAN.md — CR-04 + WR-03 (balloon command path): camera-ready gate scoped to the 8 camera-touching handlers (IMAGE_WINDOW_REQUEST/GET_STATUS/SET_EVENT_THRESHOLDS/AUTO_CAPTURE_* servable while ImageTx/radio live during camera-down); manual capture advances the auto-capture interval baseline (IMG-03, IMG-05, CTRL-03, CTRL-04)

**Wave 14** *(blocked on Wave 13)*

- [x] 01-20-PLAN.md — Bench re-verification session #6: flash both boards, series A (3 unspaced captures — 3/3 ACKed, 6/6 COMPLETE, zero command timeouts, zero silent full losses), WR-03 cadence discriminator, SC-3 visible-effect pairs (fourth round riding), settings regression + dashboard glance; G-01-7/G-01-9 + WINDOWS 3/5/8/9 flipped on evidence (CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02, IMG-02, IMG-03)

**Wave 15** *(gap-closure round #10, 2026-08-28; wave numbers restart for this round — 01-21/01-22/01-23 are file-disjoint and run in parallel)*

- [x] 01-21-PLAN.md — G-01-7 burst full-delivery (balloon half): re-announce drop-clock known-busy hold on inbound window traffic + per-entry receipt re-arm, overflow eviction ranked by receipt evidence (IMG-03, IMG-04, PRI-03)
- [x] 01-22-PLAN.md — G-01-7 burst full-delivery (base half): full-arm deadline bounds the thumbnail-heal serialization hold — a queued FULL activates within 20 s of manifest arrival (IMG-02, IMG-03, PRI-03)
- [x] 01-23-PLAN.md — Review round #9 routing: WR-01..WR-05 as WINDOWS entries 10-14 + operator disposition decision (fix-now vs waive-with-reason) + approved companion fixes (CTRL-06, PRI-02, IMG-05, IMG-06, WEB-05)

**Wave 16** *(blocked on Wave 15)*

- [x] 01-24-PLAN.md — Bench re-verification session #7: flash both boards, series A re-run (6/6 COMPLETE target), WR-03 cadence discriminator, SC-3 visible-effect pairs (fifth round riding), CIF cycle + QVGA restore, dashboard LOOK glance; G-01-7 + WINDOWS 3/10-14 flipped on evidence (CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02, IMG-02, IMG-03)

**Wave 17** *(gap-closure round #11, 2026-08-28; round-local wave 1 — the D1 blocker first, before any bench re-run)*

- [x] 01-25-PLAN.md — D1 debug round (BLOCKER, G-01-10/WINDOWS 15): addr2line the session-7 crash dumps against the retained ELF, disposition every round-#10 suspect, rank one root cause, implement the evidence-selected fix + the D2 receipt-ever stamp fix (G-01-11/WINDOWS 16); builds + harness, no bench claims (CTRL-01, CTRL-06, PRI-02, IMG-01, IMG-03, IMG-04, PRI-03)

**Wave 18** *(blocked on Wave 17 — shares src/image_tx_manager.cpp with 01-25)*

- [x] 01-26-PLAN.md — Review 7d96a98 routing (01-VERIFICATION gap 4, the one gap the ledgers did not carry): WR-01..WR-03 as WINDOWS entries 17-19 + operator disposition + approved fixes (WR-01 ps_malloc degradation fall-through, WR-02 honest health check, WR-03 uint32 ACK counter) (IMG-02, IMG-03, CTRL-06)

**Wave 19** *(blocked on Waves 17-18; bench re-verification trailing wave)*

- [ ] 01-27-PLAN.md — Bench re-verification session #8: flash both boards, D1 spaced smoke + series A (zero crash signatures), D2 boot-window discriminator, G-01-7 series-A acceptance, WR-03 cadence discriminator, SC-3 visible-effect pairs (sixth round riding), CIF cycle + QVGA restore, dashboard LOOK glance; G-01-10/G-01-11/G-01-7 + WINDOWS 3/15/16/17-19 flipped on evidence (CTRL-01, CTRL-02, CTRL-03, CTRL-04, CTRL-06, PRI-02, IMG-02, IMG-03)

### Phase 2: Image Transmission

**Goal:** Transfer images from balloon to base station over LoRa with thumbnails

**Mode:** mvp

**Requirements:**

- IMG-01: Captured images transmitted from balloon to base station over LoRa
- IMG-02: Thumbnail preview displays immediately on base station
- IMG-03: Full resolution images transfer in background after thumbnail
- IMG-04: Images chunked into packets for reliable LoRa transmission
- IMG-05: Base station stores received images on SD card
- CTRL-05: Automatic capture supports event-based triggers
- PRI-01: Telemetry data always has priority over camera/image data
- PRI-03: System gracefully handles LoRa bandwidth limitations

**Success Criteria:**

1. Captured images are chunked and transmitted over LoRa with sequence tracking
2. Thumbnail preview displays on base station within 10 seconds of capture
3. Full image transfers complete in background without blocking telemetry
4. Images successfully saved to SD card with metadata
5. Telemetry continues to update at 5-second rate during image transfers
6. Event-based auto-capture triggers on altitude/location changes

**Deliverables:**

- Image chunking protocol (sequence numbers, CRC, reassembly)
- Priority queue implementation (telemetry > images)
- Thumbnail generation on balloon
- Image reassembly on base station
- SD card storage implementation
- Event-based capture triggers

**Plans:** 5/5 plans executed (02-05 gap closure executed 2026-08-19 — all 4 02-VERIFICATION gaps closed at code level; re-verification + hardware UAT before phase complete)

Plans:
**Wave 1**

- [x] 02-01-PLAN.md — Tracer: CR-04/WR-11 thumbnail fix + full Phase 2 wire contract (0x12/0x13/0x14, window/threshold commands) + WR-12 type dispatch; thumbnail pushed balloon→base→web UI end-to-end (IMG-01, IMG-02, IMG-04)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 02-02-PLAN.md — Balloon full pipeline: full-image announce + FIFO window servicing (D-17/D-19), TX arbitration + 5 s telemetry beacon (PRI-01/SC-5); beacon gated by blocking decision checkpoint (research Q1 interpretation, surfaced to user) (IMG-01, IMG-03, PRI-01, PRI-03)
- [x] 02-03-PLAN.md — Base full pipeline: windowed ARQ with bounded retries (D-21/D-24), end-to-end CRC32 (D-23), SD storage + sidecars (D-29..D-32), transfer progress UI (D-20) (IMG-03, IMG-04, IMG-05, PRI-03)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 02-04-PLAN.md — CTRL-05 event triggers: altitude/distance/flight-phase capture inside AutoCapture (D-25/D-27/D-28), UI-configurable thresholds (D-26)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 02-05-PLAN.md — Gap closure (02-VERIFICATION.md): kind-addressable window requests + thumbnail heal (Gap 2/D-22), bounded push/window interleaving + completion-aware eviction (Gap 1/D-19/D-24), passCount reset on progress + slot-pressure full reset (Gaps 1/3/D-20), kind-suffixed sidecars IMG_{id}_T.JSON (Gap 4/D-30) (IMG-02, IMG-03, IMG-04, IMG-05, PRI-01, PRI-03)

### Phase 3: Enhanced Web Interface

**Goal:** Full base station control panel with telemetry, maps, and gallery

**Mode:** mvp

**Requirements:**

- WEB-01: Base station web UI displays live telemetry (temperature, altitude, GPS)
- WEB-02: Base station web UI displays balloon position on OpenStreetMap
- WEB-03: Map and telemetry update every 5 seconds
- WEB-04: UI layout uses top-down design (map/telemetry top, camera/gallery below)
- WEB-05: Base station supports both AP mode and Station mode WiFi
- IMG-06: Image gallery displays all received images with pagination
- ALRT-01: Altitude threshold warnings
- ALRT-02: Low battery alerts
- ALRT-03: GPS lost notifications
- ALRT-04: Landing detection alerts
- ALRT-05: Ascent rate warnings
- ALRT-06: Signal quality monitoring

**Success Criteria:**

1. Dashboard shows live temperature, altitude, GPS coordinates updating every 5 seconds
2. OpenStreetMap displays balloon position with trajectory history
3. Top-down UI layout implemented with map/telemetry prominently at top
4. Image gallery shows all captured images with pagination controls
5. Base station successfully runs in both AP and Station WiFi modes
6. All 6 alert types trigger with visual and/or audio notifications
7. Existing balloon WiFi camera interface remains functional

**Deliverables:**

- Top-down responsive web interface layout
- OpenStreetMap integration with Leaflet.js
- Live telemetry display components
- 5-second WebSocket/polling update cycle
- Paginated image gallery with SD card file serving
- Alert system with threshold configurations
- WiFi mode switching (AP/Station)
- Comprehensive testing of all features

**Plans:** 5/5 plans executed — tracer dashboard shell first, then map, alerts, gallery, WiFi (sequential waves; every plan owns src/main_basestation.cpp, so file ownership serializes the waves)

Plans:
**Wave 1**

- [x] 03-01-PLAN.md — Tracer: D-45 single-page dashboard shell + /api/state single endpoint + 5s poll with backoff/stale badge/diff-render + six-tile telemetry panel; 0x14 beacon battery extension 17→19 bytes harness-first (WEB-01, WEB-03, WEB-04, ALRT-02 data path)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 03-02-PLAN.md — Map & trajectory: capped GPS ring buffer into /api/state, embedded gzipped Leaflet 1.9.4, altitude-banded track, auto-follow/recenter, offline canvas fallback (WEB-02)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 03-03-PLAN.md — Alert engine: six base-side conditions (derived beacon-loss signal quality; no RSSI — E32 has none), D-44 latch/auto-clear lifecycles, NVS thresholds, banner bar + beep + Alert Thresholds card (ALRT-01..ALRT-06)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 03-04-PLAN.md — Gallery: SdStorage enumeration + RAM index, /gallery pagination + sidecar detail routes, thumbnail grid + pager + incomplete badges, Latest Capture card absorbed (IMG-06)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 03-05-PLAN.md — WiFi manager: NVS-backed AP/Station with 20s fallback state machine, POST /wifi, WiFi card with two-step confirm; WEB-06 balloon-boundary negative check (WEB-05)

## Milestone Definition

**v1.0 Milestone:** Complete Base Station Camera Control Extension

- All 3 phases complete
- 24 requirements satisfied
- System tested end-to-end
- Documentation updated

## Out of Scope

The following features are explicitly deferred to v2+:

- Video streaming (LoRa bandwidth limitation)
- Camera on base station (control interface only)
- Real-time video preview
- Multiple balloon tracking
- Satellite communication fallback
- Advanced camera features (AI, face detection)

---
*Roadmap created: 2025-08-18*
*Last updated: 2025-08-18*

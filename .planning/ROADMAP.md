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

**Plans:** 4 plans (01-01 executed; 01-02..01-04 are gap closure from 01-VERIFICATION.md)

Plans:
**Wave 1**

- [x] PLAN.md — executed tracer (non-standard filename; summary at 01-01-SUMMARY.md)
- [ ] 01-02-PLAN.md — Protocol integrity: CRC/sequence fix, 240-byte limits, length-driven framing, retry terminal states

**Wave 2** *(blocked on Wave 1 completion)*

- [ ] 01-03-PLAN.md — Base station UI: all 7 settings forms, auto-capture controls, per-command outcome panel, real link LED
- [ ] 01-04-PLAN.md — Balloon execution: camera sensor setters, AutoCapture interval module, real GET_STATUS, loop wiring

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

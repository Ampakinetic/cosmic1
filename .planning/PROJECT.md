# Cosmic1 Base Station Camera Control Extension

## What This Is

A remote camera control system extension for an existing ESP32-S3 balloon tracking platform. The base station web interface becomes a control panel for the balloon module, allowing users to trigger camera captures, adjust all camera settings, and view transmitted images over LoRa - while simultaneously displaying live telemetry and GPS position on an OpenStreetMap interface.

## Core Value

**Users can remotely control the balloon camera and view captured images through the base station web interface, with real-time telemetry and map tracking always available.**

## Requirements

### Validated

<!-- Existing capabilities from current system -->

- ✓ **LoRa communication** — Balloon and base station successfully transmitting telemetry
- ✓ **GPS tracking** — MAX-M10S providing accurate position data
- ✓ **Sensor data** — BMP280 temperature/pressure readings functional
- ✓ **Basic web interface** — WiFi-based camera control on balloon module working
- ✓ **Image capture** — Camera successfully taking and storing photos locally

### Active

<!-- Current scope — Base Station Camera Control Extension -->

#### Camera Control
- [ ] **CTRL-01**: User can trigger camera capture from base station web interface
- [ ] **CTRL-02**: User can adjust all camera settings remotely (resolution, quality, brightness, contrast, saturation, exposure, white balance)
- [ ] **CTRL-03**: System supports both manual (on-demand) and automatic (scheduled) capture modes
- [ ] **CTRL-04**: Automatic capture supports fixed interval intervals
- [ ] **CTRL-05**: Automatic capture supports event-based triggers (altitude change, location change)
- [ ] **CTRL-06**: Camera commands that fail are retried with timeout

#### Image Transmission
- [ ] **IMG-01**: Captured images are transmitted from balloon to base station over LoRa
- [ ] **IMG-02**: Thumbnail preview (320x240 or smaller) displays immediately on base station
- [ ] **IMG-03**: Full resolution images transfer in background after thumbnail
- [ ] **IMG-04**: Images are chunked into packets for reliable LoRa transmission
- [ ] **IMG-05**: Base station stores received images on SD card
- [ ] **IMG-06**: Image gallery displays all received images with pagination

#### Web Interface
- [ ] **WEB-01**: Base station web UI displays live telemetry (temperature, altitude, GPS coordinates)
- [ ] **WEB-02**: Base station web UI displays balloon position on OpenStreetMap
- [ ] **WEB-03**: Map and telemetry update every 5 seconds
- [ ] **WEB-04**: UI layout uses top-down design (map/telemetry top, camera/gallery below)
- [ ] **WEB-05**: Base station supports both AP mode and Station mode WiFi connectivity
- [ ] **WEB-06**: Existing balloon WiFi camera interface remains unchanged

#### Priority & Reliability
- [ ] **PRI-01**: Telemetry data always has priority over camera/image data on LoRa link
- [ ] **PRI-02**: Camera commands use retry mechanism with timeout for failed transmissions
- [ ] **PRI-03**: System gracefully handles LoRa bandwidth limitations

#### Alerts & Monitoring
- [ ] **ALRT-01**: System provides altitude threshold warnings
- [ ] **ALRT-02**: System provides low battery alerts
- [ ] **ALRT-03**: System provides GPS lost notifications
- [ ] **ALRT-04**: System provides landing detection alerts
- [ ] **ALRT-05**: System provides ascent rate warnings
- [ ] **ALRT-06**: System provides signal quality monitoring

### Out of Scope

<!-- Explicit boundaries -->

- **Video streaming** — LoRa bandwidth insufficient; still images only
- **Camera on base station** — Base station is control interface only, no local camera
- **Real-time video preview** — Still image capture and transmission only
- **Multiple balloon tracking** — Single balloon to single base station
- **Satellite fallback communication** — LoRa only for this implementation
- **Advanced camera features** — No face detection, recognition, or AI features

## Context

**Brownfield Development:**

This is an extension to an existing, working ESP32-S3 balloon tracking system. The core hardware and software are already operational:

- **Balloon Unit**: ESP32-S3 with BMP280 sensor, MAX-M10S GPS, LoRa E32 module, camera, working telemetry transmission
- **Base Station**: ESP32-S3 with LoRa receiver, existing basic web interface
- **Communication**: LoRa link already tested and working for telemetry
- **Power Management**: Deep sleep cycles and battery monitoring implemented
- **Camera System**: Local WiFi-based camera control and capture working

**Existing Codebase:**
- PlatformIO-based C++ firmware for ESP32-S3
- Modular architecture with manager classes (SensorManager, CameraManager, LoRaComm, PowerManager)
- Existing packet protocol for telemetry and basic data
- Web interface code already present in balloon firmware

**Development Environment:**
- PlatformIO build system
- ESP32-S3 Arduino framework
- Existing hardware with proven pin mappings and wiring
- Codebase analysis available in `.planning/codebase/`

## Constraints

### Technical Constraints
- **LoRa Bandwidth**: Maximum 240 bytes per packet, must work within this limitation
- **LoRa Range**: 10+ km line of sight expected; signal quality varies
- **Power**: Balloon unit runs on battery; must manage power consumption
- **Memory**: ESP32-S3 with 8MB PSRAM, 16MB flash; must stay within limits
- **Processing**: Single-threaded event loop; no blocking operations
- **Storage**: SD card module required for base station image storage

### Communication Constraints
- **Telemetry Priority**: Telemetry always has priority over image data
- **Packet Size**: All data must fit in 240-byte LoRa packets
- **Latency**: 5-second UI update rate target
- **Reliability**: Must handle packet loss and retry logic

### Hardware Constraints
- **No camera on base station**: Base station is control interface only
- **Existing hardware**: Must work with already-assembled balloon and base station units
- **Pin mappings**: Established pin assignments cannot be changed
- **Power limits**: Balloon battery life must remain acceptable

### User Interface Constraints
- **Browser-based**: No native applications; web interface only
- **Responsive**: Must work on laptop screens
- **Real-time**: 5-second update rate for live data
- **Offline capable**: Base station runs as AP when network unavailable

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| **OpenStreetMap for map display** | No API key required, good tile quality, free to use | — Pending |
| **5-second UI update rate** | Balance between responsiveness and LoRa traffic/load | — Pending |
| **Top-down UI layout** | Dashboard style familiar to users; map/telemetry prominent, controls below | — Pending |
| **Store all images on SD card** | Complete image history for analysis; SD card provides sufficient capacity | — Pending |
| **Telemetry priority over camera** | Flight safety critical; position data more important than images | — Pending |
| **Hybrid auto-capture** | Provides both predictable interval and smart event-based captures | — Pending |
| **Retry with timeout for failed commands** | Handles transient LoRa issues without blocking system | — Pending |
| **AP + Station WiFi modes** | Maximum flexibility - can join existing network or run standalone | — Pending |
| **Comprehensive alerts** | Full situational awareness for flight monitoring | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2025-08-18 after initialization*

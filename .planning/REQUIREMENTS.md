# Requirements: Cosmic1 Base Station Camera Control

**Defined:** 2025-08-18
**Core Value:** Users can remotely control the balloon camera and view captured images through the base station web interface, with real-time telemetry and map tracking always available.

## v1 Requirements

Requirements for Base Station Camera Control Extension. Each maps to roadmap phases.

### Camera Control

- [x] **CTRL-01**: User can trigger camera capture from base station web interface
- [x] **CTRL-02**: User can adjust all camera settings remotely (resolution, quality, brightness, contrast, saturation, exposure, white balance)
- [x] **CTRL-03**: System supports both manual (on-demand) and automatic (scheduled) capture modes
- [x] **CTRL-04**: Automatic capture supports fixed interval timing
- [ ] **CTRL-05**: Automatic capture supports event-based triggers (altitude change, location change)
- [x] **CTRL-06**: Camera commands that fail are retried with timeout

### Image Transmission

- [x] **IMG-01**: Captured images are transmitted from balloon to base station over LoRa
- [x] **IMG-02**: Thumbnail preview (320x240 or smaller) displays immediately on base station
- [x] **IMG-03**: Full resolution images transfer in background after thumbnail
- [x] **IMG-04**: Images are chunked into packets for reliable LoRa transmission
- [x] **IMG-05**: Base station stores received images on SD card
- [ ] **IMG-06**: Image gallery displays all received images with pagination

### Web Interface

- [ ] **WEB-01**: Base station web UI displays live telemetry (temperature, altitude, GPS coordinates)
- [ ] **WEB-02**: Base station web UI displays balloon position on OpenStreetMap
- [ ] **WEB-03**: Map and telemetry update every 5 seconds
- [ ] **WEB-04**: UI layout uses top-down design (map/telemetry top, camera/gallery below)
- [ ] **WEB-05**: Base station supports both AP mode and Station mode WiFi connectivity
- [ ] **WEB-06**: Existing balloon WiFi camera interface remains unchanged

### Priority & Reliability

- [x] **PRI-01**: Telemetry data always has priority over camera/image data on LoRa link
- [x] **PRI-02**: Camera commands use retry mechanism with timeout for failed transmissions
- [x] **PRI-03**: System gracefully handles LoRa bandwidth limitations

### Alerts & Monitoring

- [ ] **ALRT-01**: System provides altitude threshold warnings
- [ ] **ALRT-02**: System provides low battery alerts
- [ ] **ALRT-03**: System provides GPS lost notifications
- [ ] **ALRT-04**: System provides landing detection alerts
- [ ] **ALRT-05**: System provides ascent rate warnings
- [ ] **ALRT-06**: System provides signal quality monitoring

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Advanced Features

- **ADV-01**: Multiple balloon tracking support
- **ADV-02**: Advanced camera features (face detection, AI processing)
- **ADV-03**: Satellite fallback communication (Iridium)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Video streaming over LoRa | LoRa bandwidth insufficient; still images only |
| Camera on base station | Base station is control interface only, no local camera needed |
| Real-time video preview | Still image capture and transmission only |
| Multiple balloon tracking | Single balloon to single base station for v1 |
| Satellite communication | LoRa only for this implementation |
| Advanced camera features | Out of scope for v1 (face detection, recognition, AI) |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| CTRL-01 | Phase 1 | Complete |
| CTRL-02 | Phase 1 | Complete |
| CTRL-03 | Phase 1 | Complete |
| CTRL-04 | Phase 1 | Complete |
| CTRL-05 | Phase 2 | Pending |
| CTRL-06 | Phase 1 | Complete |
| IMG-01 | Phase 2 | Complete |
| IMG-02 | Phase 2 | Complete |
| IMG-03 | Phase 2 | Complete |
| IMG-04 | Phase 2 | Complete |
| IMG-05 | Phase 2 | Complete |
| IMG-06 | Phase 3 | Pending |
| WEB-01 | Phase 3 | Pending |
| WEB-02 | Phase 3 | Pending |
| WEB-03 | Phase 3 | Pending |
| WEB-04 | Phase 3 | Pending |
| WEB-05 | Phase 3 | Pending |
| WEB-06 | - | Complete (existing) |
| PRI-01 | Phase 2 | Complete |
| PRI-02 | Phase 1 | Complete |
| PRI-03 | Phase 2 | Complete |
| ALRT-01 | Phase 3 | Pending |
| ALRT-02 | Phase 3 | Pending |
| ALRT-03 | Phase 3 | Pending |
| ALRT-04 | Phase 3 | Pending |
| ALRT-05 | Phase 3 | Pending |
| ALRT-06 | Phase 3 | Pending |

**Coverage:**

- v1 requirements: 24 total
- Mapped to phases: 24
- Unmapped: 0 ✓

---
*Requirements defined: 2025-08-18*
*Last updated: 2025-08-18 after roadmap creation*

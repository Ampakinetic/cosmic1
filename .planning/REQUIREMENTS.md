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
- [x] **IMG-06**: Image gallery displays all received images with pagination

### Web Interface

- [x] **WEB-01**: Base station web UI displays live telemetry (temperature, altitude, GPS coordinates)
- [x] **WEB-02**: Base station web UI displays balloon position on OpenStreetMap
- [x] **WEB-03**: Map and telemetry update every 5 seconds
- [x] **WEB-04**: UI layout uses top-down design (map/telemetry top, camera/gallery below)
- [x] **WEB-05**: Base station supports both AP mode and Station mode WiFi connectivity
- [ ] **WEB-06**: Existing balloon WiFi camera interface remains unchanged

### Priority & Reliability

- [ ] **PRI-01**: Telemetry data always has priority over camera/image data on LoRa link
- [x] **PRI-02**: Camera commands use retry mechanism with timeout for failed transmissions
- [x] **PRI-03**: System gracefully handles LoRa bandwidth limitations

### Alerts & Monitoring

- [x] **ALRT-01**: System provides altitude threshold warnings
- [x] **ALRT-02**: System provides low battery alerts
- [x] **ALRT-03**: System provides GPS lost notifications
- [x] **ALRT-04**: System provides landing detection alerts
- [x] **ALRT-05**: System provides ascent rate warnings
- [x] **ALRT-06**: System provides signal quality monitoring

### Image Storage (Phase 2.5)

- [x] **STORE-01**: Captured images are persisted to the balloon's FAT32 SD card (SDMMC 1-bit) as files before any transfer manifest is sent
- [ ] **STORE-02**: Undelivered images survive reboot/power-loss — boot-time index rescan re-announces and resumes transfer of files already on the card
- [x] **STORE-03**: Image transfer serves chunk data from the SD file; the volatile PSRAM queue exists only as an honestly-labeled fallback when the SD write fails
- [ ] **STORE-04**: A full card refuses new captures with an honest error while telemetry and commands continue unaffected

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
| CTRL-05 | Phase 2 | Gaps Found |
| CTRL-06 | Phase 1 | Complete |
| IMG-01 | Phase 2 | Complete |
| IMG-02 | Phase 2 | Complete |
| IMG-03 | Phase 2 | Complete |
| IMG-04 | Phase 2 | Complete |
| IMG-05 | Phase 2 | Complete |
| IMG-06 | Phase 3 | Complete |
| WEB-01 | Phase 3 | Complete |
| WEB-02 | Phase 3 | Complete |
| WEB-03 | Phase 3 | Complete |
| WEB-04 | Phase 3 | Complete |
| WEB-05 | Phase 3 | Complete |
| WEB-06 | - | Complete (existing) |
| PRI-01 | Phase 2 | Gaps Found |
| PRI-02 | Phase 1 | Complete |
| PRI-03 | Phase 2 | Complete |
| ALRT-01 | Phase 3 | Complete |
| ALRT-02 | Phase 3 | Complete |
| ALRT-03 | Phase 3 | Complete |
| ALRT-04 | Phase 3 | Complete |
| ALRT-05 | Phase 3 | Complete |
| ALRT-06 | Phase 3 | Complete |
| STORE-01 | Phase 2.5 | Complete |
| STORE-02 | Phase 2.5 | Pending |
| STORE-03 | Phase 2.5 | Complete |
| STORE-04 | Phase 2.5 | Pending |

**Coverage:**

- v1 requirements: 24 total
- Mapped to phases: 24
- Unmapped: 0 ✓

---
*Requirements defined: 2025-08-18*
*Last updated: 2025-08-18 after roadmap creation*

# Phase 3: Enhanced Web Interface - Research

**Researched:** 2026-08-19
**Domain:** ESP32 base-station web dashboard (classic WebServer + embedded HTML/JS), Leaflet/OSM map with offline fallback, NVS-backed WiFi mode switching, base-side alert engine, SD gallery over the Phase 2 sidecar contract, one telemetry-beacon wire extension (battery)
**Confidence:** HIGH (codebase contracts read verbatim; the two hardware-capability questions this phase hinges on — E32 RSSI and beacon battery — both resolved with authoritative sources)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

### Live Update Mechanism (WEB-03)
- **D-33:** Plain JSON polling at the locked 5-second rate — the browser fetches a state endpoint every 5s. No SSE, no WebSocket, no new server machinery; extends the existing classic-WebServer route patterns in `main_basestation.cpp`. — **Reversibility:** reversible — an SSE layer could later sit behind the same payload shape.
- **D-34:** ONE combined endpoint (`/api/state`) per poll carrying telemetry + link status + command queue + transfer progress (+ trajectory per D-38). Least connection churn for the ESP32 WebServer; the no-fabricated-state rule is enforced in a single serializer.
- **D-35:** On consecutive poll failures, back off 5s→15s→30s and show a visible "stale" badge with data age; snap back to 5s on recovery. Extends the IN-03 link-LED truthfulness pattern — never fabricated freshness.
- **D-36:** All panels (including transfer progress and gallery count) ride the same 5s cycle; the browser re-renders DOM only when values actually changed (diff-checked), and the gallery list refreshes only when the finalized-image count changes.

### Map & Trajectory (WEB-02, WEB-04)
- **D-37:** Leaflet + OSM tiles when internet is available; on tile-load failure (AP mode / no uplink) the map degrades gracefully to an auto-scaled offline canvas plot — trajectory, current position, coordinates, altitude-colored track. The field/chase scenario (AP mode) stays spatially usable without tile caching. — **Reversibility:** reversible — the offline plot is a fallback render path, not a data change.
- **D-38:** Full-flight trajectory in a capped ring buffer on the base (every GPS fix since boot, capped at planner's discretion — ~500–1000 points ≈ hours at 5s cadence), sent via `/api/state`. The whole flight is always visible; memory stays bounded.
- **D-39:** Map auto-follows the balloon each update; any user drag/zoom cancels following and shows a "recenter" button to resume.

### WiFi Mode Switching (WEB-05)
- **D-40:** Runtime AP/Station toggle from a WiFi card in the web UI — mode selection + station SSID/password entry, persisted in NVS/Preferences, extending the existing `wifi_config.h` setup. Automatic fallback to AP if the station network doesn't join within ~20s, so the operator can never be locked out. No AP+STA dual mode.

### Alert System (ALRT-01..06)
- **D-41:** All six alert conditions evaluated BASE-SIDE from data the base already has — altitude/GPS/temperature from the received 0x14 telemetry beacon, RSSI/SNR from the base's own E32 receiver, GPS-lost from fix age, ascent rate from altitude deltas, landing from altitude trend. Zero extra balloon airtime. **Researcher must confirm battery voltage (ALRT-02) rides the telemetry beacon — if not, adding it to the beacon payload is the one permitted balloon-side change in this phase.**
- **D-42:** Presentation: color-coded persistent banner row at the top of the dashboard plus a Web Audio API browser beep for new critical alerts; mute toggle remembered per session. No buzzer/relay hardware on the base.
- **D-43:** Alert thresholds: sensible flight defaults built in, overridable from an Alerts settings card in the UI and persisted in NVS — exactly the Phase 2 event-threshold pattern (D-26). Alerts are base-local: thresholds never touch LoRa traffic.
- **D-44:** Two lifecycle classes — safety-critical alerts (low battery, GPS lost, landing detected) LATCH until manually acknowledged; informational alerts (altitude threshold, ascent rate, signal quality) auto-clear when the condition resolves, with a cooldown so they don't re-fire every 5s poll. Mirrors the terminal-state/anti-flap discipline from Phases 1–2.

### Layout & Gallery (WEB-04, IMG-06)
- **D-45:** One scrolling top-down dashboard on a single page: alerts bar → map + telemetry → capture/settings cards → command queue + transfer progress → gallery, with sticky section nav. **This SUPERSEDES Phase 1's D-13** (camera controls on a separate page) — that decision predates the dashboard. — **Reversibility:** costly — after the single-page structure ships, undo means re-splitting the served page, nav, and poll wiring across multiple documents.
- **D-46:** Gallery: thumbnail grid, 12 per page (3–4 columns on a laptop), newest first, numbered pagination. Thumbnails are the existing QQVGA `_T.JPG` files (D-31).
- **D-47:** Clicking a gallery image opens a dedicated detail view: the full-size `IMG_{id}.JPG` when complete (thumbnail otherwise) plus its sidecar metadata from `IMG_{id}.JSON` — capture time, altitude, GPS position, trigger source, camera settings, RSSI, completeness. The D-30 sidecar was designed for exactly this.
- **D-48:** Images that finalized incomplete (D-24 bounded-retry give-up) appear in the gallery normally with a visible "incomplete" badge; the detail view shows only what actually verified. Straight extension of the no-fabricated-state rule.

### Claude's Discretion
- Ring-buffer cap for trajectory (D-38) — size from payload budget after measuring the `/api/state` JSON with a full track.
- Exact backoff schedule values (D-35) and stale-badge presentation.
- Default alert threshold values (altitude, battery, GPS-lost age, ascent rate, RSSI floor) and beep pattern/mute persistence details.
- `/api/state` JSON field layout, gallery pagination route shape, NVS key naming.
- **Leaflet asset hosting — embed Leaflet JS/CSS in firmware (or serve from SD/flash), NOT from a CDN:** the page must work in AP mode with no internet, so the map library itself must load offline; only the OSM tiles are allowed to be internet-dependent.
- Offline canvas-plot rendering details (scaling, track styling, grid).
- How the telemetry beacon's receiver-side state is exposed to the UI (direct global read vs snapshot struct per request).

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| WEB-01 | Live telemetry display (temperature, altitude, GPS) | 0x14 beacon already delivers all three fields to `TelemetrySnapshot` (image_rx_manager.h:105-114); `/status` JSON already serializes them — move into `/api/state` telemetry panel |
| WEB-02 | Balloon position on OpenStreetMap | Embedded Leaflet 1.9.4 (offline, gzipped PROGMEM) + browser-fetched OSM tiles; D-37 offline canvas fallback; trajectory from D-38 ring buffer |
| WEB-03 | 5-second update cycle | D-33/D-34 JSON polling on classic WebServer; D-35 backoff + stale badge; existing 1s poll (`setInterval(updateStatus, 1000)`) re-locked to 5s |
| WEB-04 | Top-down layout | D-45 single-page restructuring of existing cards (order + design tokens locked in 02-UI-SPEC.md); D-13 superseded |
| WEB-05 | AP + Station WiFi | D-40: Preferences/NVS + boot-time STA attempt with ~20s timeout → AP fallback; runtime switch pattern verified |
| IMG-06 | Gallery with pagination | D-46/D-47/D-48 over the D-29..D-32 SD contract; SdStorage gains enumeration (none exists today); sidecar-driven detail view |
| ALRT-01 | Altitude threshold warnings | Base-side from `TelemetrySnapshot.altitudeM`; default threshold candidates from balloon_config.h |
| ALRT-02 | Low battery alerts | **D-41 verification: battery voltage does NOT ride the 0x14 beacon** — the one permitted balloon-side change is required (beacon body 17→19 bytes, see Finding 1); balloon ADC path exists (GPIO4, 2:1 divider) |
| ALRT-03 | GPS lost notifications | Base-side from `TelemetrySnapshot.gpsValid` + beacon age; default age candidates from balloon_config.h `GPS_MAX_AGE_MS`/`EMERGENCY_NO_GPS_TIME` |
| ALRT-04 | Landing detection alerts | Base-side altitude-trend heuristics; balloon `SystemState` has LANDING=0x06 vocabulary (informational only — base decides independently) |
| ALRT-05 | Ascent rate warnings | Base-side altitude deltas between beacons; descent-rate anchor exists (`EMERGENCY_ALTITUDE_RATE 15` m/s) |
| ALRT-06 | Signal quality monitoring | **D-41's "RSSI/SNR from the E32 receiver" is not possible — the E32 series has no RSSI/SNR readout** (Finding 2). Implement as derived link quality: beacon seq-gap loss + ACK ratios + retransmission passes — still 100% base-side, zero airtime, satisfying D-41's actual constraint |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **Stack:** C++ (ESP32 firmware, Arduino Framework, PlatformIO) + HTML/CSS/JavaScript web interface. Libraries listed: esp32-camera, Adafruit BMP280, TinyGPSPlus, LoRa (Sandeep Mistry — superseded by the E32 UART driver per STATE.md), ArduinoJson.
- **Key constraints:** LoRa bandwidth 240 bytes per packet; telemetry always has priority over image data; 5-second UI update rate; single-threaded event loop (no blocking); PSRAM required for camera operations.
- **Workflow:** YOLO, coarse granularity, parallel execution, research/plan-check/verifier enabled.
- **Important files:** `.planning/PROJECT.md`, `.planning/REQUIREMENTS.md`, `.planning/ROADMAP.md`, `.planning/STATE.md`, `platformio.ini`, `PROJECT_SUMMARY.md`.

Codebase-specific constraints discovered this session (treat as binding as CLAUDE.md for this phase):

- **The base env has NO library dependencies** — `platformio.ini:301-302` reads `; Libraries for base station` / `lib_deps =` (empty). The base JSON path is deliberately hand-built String concatenation (`sd_storage.h:114-116`: "hand-built String JSON (the base env deliberately has no ArduinoJson — research Supporting table)"). Do not add ArduinoJson to the base env without a decision; follow the established String-JSON pattern.
- **Both firmware targets must stay green** (`pio run -e esp32-s3-basestation -e esp32-s3-balloon`) and the wire harness `node scripts/verify_protocol_roundtrip.mjs` (47 checks green at 02-05) must stay green.
- **Design tokens are locked** by `01-UI-SPEC.md` + `02-UI-SPEC.md`: typography exactly {14,16,18,24}px, spacing standard set {4,8,16,24,32,48,64} with `lg`=24px, the documented color/semantic palette, emoji card icons, system font stack, no new visual language. New surfaces (map, gallery, alerts bar, WiFi card) reuse these tokens; touching off-grid surfaces obligates harmonization.

## Summary

Phase 3 is base-station-local except for exactly one wire change the context pre-authorized. Every data source the six alerts and the dashboard need was verified in source: the 0x14 telemetry beacon already delivers altitude/temperature/GPS+validity to `TelemetrySnapshot`, transfer and queue snapshots already exist for `/status`, and the D-29..D-32 SD contract (`IMG_{id}.JPG` / `IMG_{id}_T.JPG` / kind-suffixed `.JSON` sidecars) gives the gallery everything except an enumeration API, which `SdStorage` does not yet have and this phase must add.

Two D-41 assumptions required verification and both resolved **against** the decision's literal mechanism:

1. **Battery voltage (ALRT-02) does NOT ride the telemetry beacon.** `TelemetryBeaconBody` (image_protocol.h:151-158) is exactly 17 bytes with no voltage field. Per D-41's own escape hatch, adding battery to the beacon is the one permitted balloon-side change — a 17→19-byte body extension (u16 millivolts) that must atomically touch the serializer/deserializer fixed-length checks, the factory, both managers' snapshots, the base JSON, and the wire harness (which pins `IMG_TELEMETRY_BEACON_BODY_SIZE = 17` as an executable assertion). The balloon has a real ADC read path (`analogRead(BATTERY_SENSE_PIN)` = GPIO4, 2:1 divider formula) but it is currently never fed into anything real — `processPowerManagement()` uses hardcoded `{3.7f, 0.1f, 85}` dummy data and `PowerMgr().update()` is never called (its "Method doesn't exist" comment is stale — the method exists at power_manager.cpp:115). Whether a physical divider is actually wired to GPIO4 is a hardware UAT question to flag, not a code question.
2. **The E32-900T30D cannot report RSSI or SNR.** Two independent authoritative sources (the EBYTE E32 library author, and a comparative E32/E22/E220 guide) state the E32 series simply lacks the function; RSSI exists only on E22/E220. ALRT-06 must therefore be a *derived* link-quality metric from data the base already has — beacon `seq` gap accounting (the beacon carries a rolling BE16 sequence at a locked 5s cadence, making loss rate exactly computable), command ACK/timeout ratios, and transfer retransmission pass counts. This honors D-41's actual constraint (base-side evaluation, zero balloon airtime) with the mechanism corrected.

The web-side stack requires **no new firmware libraries**: `WebServer`, `WiFi`, `Preferences` all ship in the Arduino-ESP32 core (project builds against core 3.3.9). Leaflet 1.9.4 is a vendored web asset, not a PlatformIO dependency — embedded gzipped in PROGMEM (~46KB total for JS+CSS) and served with `Content-Encoding: gzip`, which fits easily in the 3.75MB app partition against a current 1.07MB base firmware. A track map built from `L.circleMarker` + `L.polyline` needs none of Leaflet's marker image assets. OSM tile policy is satisfied by construction because the browser (not the ESP32) fetches tiles with normal Referer/User-Agent headers; Leaflet's default attribution control provides the required "© OpenStreetMap contributors" credit.

**Primary recommendation:** Plan around four new base-local modules following the manager/singleton conventions — (1) a trajectory ring buffer + `/api/state` serializer extension, (2) an alert engine (defaults grounded in the project's own balloon_config.h thresholds) feeding banner state into `/api/state`, (3) a WiFi manager (Preferences-backed, 20s STA→AP fallback) replacing the hardcoded `initWiFi()`, (4) an SdStorage enumeration/gallery API with a RAM index cache — plus the single pre-authorized beacon wire extension, the single-page UI restructuring per D-45 using the locked design tokens, and Leaflet served as embedded gzipped PROGMEM assets with the D-37 offline canvas fallback.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Live telemetry display (WEB-01) | Browser (JS render) | Firmware (`/api/state` serializer) | Browser renders; firmware is the single source of verified truth (no-fabricated-state rule) |
| Map display + trajectory (WEB-02) | Browser (Leaflet + canvas fallback) | CDN/Static (OSM tiles, internet-dependent) | Tiles are the only internet-dependent piece; library + fallback plot are firmware-served and offline |
| 5s update cycle (WEB-03) | Browser (poll scheduler + backoff) | Firmware (one JSON endpoint) | D-33/D-34: client-driven polling; D-35 backoff is client-side |
| WiFi mode switching (WEB-05) | Firmware (WiFi manager + NVS) | Browser (WiFi settings card) | Radio control is firmware-owned; browser only submits credentials |
| Gallery + pagination (IMG-06) | Firmware (SD enumeration + index) | Browser (grid render, page controls) | Directory lives on firmware SD; browser presents |
| Alert evaluation (ALRT-01..06) | Firmware (base-side alert engine) | Browser (banner + beep presentation) | D-41: all conditions computed base-side; D-42 presentation is browser-side |
| Beacon battery field (ALRT-02 enabler) | Balloon firmware (one wire change) | Base firmware (snapshot + JSON) | The single pre-authorized cross-boundary change |
| Balloon WiFi camera interface (WEB-06) | Balloon firmware (`app_httpd.cpp`) | — | Untouched by this phase; verify unchanged at the end |

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| WebServer (Arduino-ESP32 core) | core 3.3.9 | HTTP routes, binary streaming | Already the proven server in `main_basestation.cpp`; D-33 explicitly extends it |
| WiFi (Arduino-ESP32 core) | core 3.3.9 | AP/STA mode control (D-40) | `WiFi.mode(WIFI_STA/WIFI_AP)`, `softAP`, `begin`; official Espressif API [CITED: docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html] |
| Preferences (Arduino-ESP32 core) | core 3.3.9 | NVS persistence for WiFi creds + alert thresholds (D-40/D-43) | Ships in core — no `lib_deps` entry needed (base env has none); namespace + getString/putUChar pattern |
| SD / FS (Arduino-ESP32 core) | core 3.3.9 | Gallery enumeration (`openNextFile`) | Already the storage substrate (`sd_storage.cpp`) |
| Leaflet (vendored web asset) | 1.9.4 | Map rendering (WEB-02) | Locked by roadmap/deliverables; stable since 2023-05-18; ~42KB gz JS + ~4KB gz CSS; NOT a PlatformIO dependency — embedded in PROGMEM per the discretion note (CDN forbidden: page must work offline) [CITED: leafletjs.com/download.html] |
| Web Audio API (browser) | n/a | Critical-alert beep (D-42) | Browser-native; no asset; requires gesture-unlock (see Pitfall 7) [CITED: developer.chrome.com/blog/autoplay] |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| gzip (offline tooling, not firmware) | any | Compress Leaflet + app JS/CSS to PROGMEM arrays | At asset-build time; convert with `xxd -i`-style script (node/python) into generated headers |
| Canvas 2D API (browser) | n/a | D-37 offline map fallback | When OSM tile loads fail (AP mode / no uplink) |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Leaflet 1.9.4 | Leaflet 2.0.0-alpha.1 | 2.0 is alpha (Aug 2025) — do not ship alpha to a field dashboard |
| Leaflet | MapLibre GL JS | ~200KB+ gz, WebGL dependency, far heavier — wrong tool for an ESP32-served page |
| Leaflet + OSM raster | Inline SVG hand-rolled map | Hand-rolled projection/tiles = large custom surface; Leaflet offline-embedded already solves interaction (zoom/drag/recenter for D-39) |
| JSON polling (D-33) | SSE / WebSocket | REJECTED by D-33 — locked, do not revisit |
| WiFiManager library | D-40 UI card + Preferences | WiFiManager adds captive-portal machinery; D-40's own card fits the settings-card pattern and keeps operator control |

**Installation:**
```bash
# No PlatformIO libraries are added this phase. One vendored web asset:
# 1. Download Leaflet 1.9.4 dist (leaflet.js, leaflet.css)
# 2. gzip -9 both
# 3. Convert to generated PROGMEM headers (build-time script, committed artifact)
```

**Version verification (this session):** Arduino-ESP32 core 3.3.9 [VERIFIED: ~/.platformio/packages/framework-arduinoespressif32/cores/esp32/esp_arduino_version.h — `#define ESP_ARDUINO_VERSION_MAJOR 3`, `MINOR 3`, `PATCH 9`]; espressif32 platform 6.7.0 among installed platforms; node v24.8.0 and PlatformIO Core 6.1.19 available for the harness.

## Package Legitimacy Audit

> This phase installs **no registry packages** (no npm, no PlatformIO `lib_deps` additions). The only third-party code is the Leaflet 1.9.4 dist vendored into firmware as generated PROGMEM headers.

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| Leaflet 1.9.4 (vendored asset, not a dependency) | npm (informational) | stable since 2023-05-18 | extremely high | github.com/Leaflet/Leaflet | OK | Approved as vendored asset |
| WebServer / WiFi / Preferences / SD | Arduino-ESP32 core 3.3.9 (bundled) | core ships them | n/a | github.com/espressif/arduino-esp32 | OK | Already in use |

**Packages removed due to [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

*Note: Leaflet version/size facts come from the official download page and unpkg dist listing [CITED: leafletjs.com/download.html, unpkg.com/leaflet@1.9.4/dist/] — download the dist from the official site or unpkg pinned to `leaflet@1.9.4`, never from an unverified mirror. Its BSD 2-Clause license is [ASSUMED] from training knowledge — confirm on the download page before vendoring (permissive either way for redistribution with notice).*

## Architecture Patterns

### System Architecture Diagram

```
                        BROWSER (operator laptop/phone)
                              │
              ┌───────────────┼────────────────────────┐
              │  single-page dashboard (D-45)          │
              │  alerts bar → map+telemetry → cards    │
              │  → queue+transfers → gallery           │
              │  poll /api/state every 5s (D-33/D-34)  │
              │  backoff 5→15→30s + stale badge (D-35) │
              │  diff-checked DOM re-render (D-36)     │
              │  Leaflet from /leaflet.js (embedded)   │
              │  OSM tiles ←—— internet (STA mode) ——→ │
              │      └─ tile fail → canvas fallback    │
              │         (D-37, AP mode)               │
              └──────┬───────────────┬─────────────────┘
        HTML/JS/CSS  │               │ img/{id}, /gallery, /api/*
             (gzip,  │               │  POST /wifi, /alerts
           PROGMEM)  ▼               ▼
        ┌─────────────────────────────────────────────┐
        │  BASE ESP32-S3  — classic WebServer (core)  │
        │  ┌────────────┐ ┌──────────┐ ┌────────────┐ │
        │  │ /api/state │ │ gallery  │ │ WiFi mgr   │ │
        │  │ serializer │ │ routes   │ │ (D-40)     │ │
        │  └─────┬──────┘ └────┬─────┘ └─────┬──────┘ │
        │        │             │             │        │
        │  ┌─────▼──────┐ ┌────▼────────┐ ┌──▼──────┐ │
        │  │ trajectory │ │ SdStorage   │ │ Prefer. │ │
        │  │ ring buf   │ │ enumerate + │ │ (NVS)   │ │
        │  │ (D-38)     │ │ sidecars    │ │ creds + │ │
        │  └─────┬──────┘ └────┬────────┘ │ thresh  │ │
        │  ┌─────▼─────────────▼──────┐   └─────────┘ │
        │  │ alert engine (D-41..44)  │               │
        │  │ 6 conditions, latch vs   │               │
        │  │ auto-clear + cooldown    │               │
        │  └─────┬────────────────────┘               │
        │        │                                     │
        │  ImageRx().getTelemetrySnapshot()  ← 0x14 beacons (5s)
        │  CmdSender() queue/ACK counters    ← 0x11 responses
        │  E32 UART (transparent, MODE_NORMAL — NO RSSI available)
        └────────┬────────────────────────────────────┘
                 │ LoRa 240B frames
        ┌────────▼────────────────────────────────────┐
        │  BALLOON ESP32-S3 (otherwise UNCHANGED)     │
        │  ONE permitted change: sendTelemetryBeacon  │
        │  adds u16 batteryMilliV (PowerMgr ADC read, │
        │  GPIO4) → beacon body 17→19 bytes           │
        └─────────────────────────────────────────────┘
```

The primary use case traces: browser polls `/api/state` → base serializes telemetry snapshot + trajectory ring buffer + alert banners + queue/transfers → browser diff-renders map (Leaflet; canvas if offline), banners (beep if new critical), panels. Gallery route enumerates SD via index cache; detail view reads `IMG_{id}.JSON` sidecars. WiFi card POSTs credentials → WiFi manager persists to NVS, reboots the radio (STA with 20s AP fallback).

### Recommended Project Structure

Follows `.planning/codebase/STRUCTURE.md` conventions (manager class + `begin()/update()` + singleton accessor, headers in `src/` beside implementation as the newer modules do):

```
src/
├── main_basestation.cpp     # restructured single-page UI, /api/state, new routes
├── trajectory_buffer.h/.cpp # NEW — D-38 capped ring buffer of GPS fixes (base)
├── alert_engine.h/.cpp      # NEW — D-41..D-44 base-side evaluation + NVS thresholds
├── wifi_manager.h/.cpp      # NEW — D-40 Preferences-backed AP/STA + fallback
├── sd_storage.h/.cpp        # EXTEND — enumeration/index for gallery (IMG-06)
├── web_assets.h             # NEW (generated) — gzipped Leaflet JS/CSS PROGMEM arrays
include/
├── image_protocol.h         # EXTEND — TelemetryBeaconBody + battery field (17→19)
src/
├── command_protocol.cpp     # EXTEND — beacon serialize/deserialize fixed lengths
├── image_tx_manager.cpp     # EXTEND — sendTelemetryBeacon reads PowerMgr voltage
├── image_rx_manager.cpp     # EXTEND — TelemetrySnapshot gains battery field
scripts/
├── verify_protocol_roundtrip.mjs  # EXTEND — beacon size 17→19 assertions
├── embed_web_assets.mjs     # NEW (optional) — dist → gzip → C header generator
```

### Pattern 1: `/api/state` — one serializer, verified truth only

**What:** D-34's single endpoint extends the existing `handleStatus()` (main_basestation.cpp:1370-1499) which already aggregates queue, link truth, event thresholds, transfers, storage, telemetry. Add: trajectory array, alert-banner state, gallery count, WiFi mode/IP.
**When to use:** every 5s poll; the only live-data endpoint.
**Example** (shape, following the existing hand-built String JSON):
```cpp
// Source: pattern from src/main_basestation.cpp:1441-1453 (telemetry block)
const TelemetrySnapshot& beacon = ImageRx().getTelemetrySnapshot();
if (beacon.valid) {
    uint32_t ageMs = millis() - beacon.receivedMs;
    json += "\"telemetry\":{";
    json += "\"ageMs\":" + String(ageMs) + ",";
    // ... altitudeM, tempC, lat, lon, gpsValid, batteryMv (new)
}
```
**Payload-budget discipline (D-38 discretion):** 500 trajectory points as verbose JSON objects (`{"lat":x,"lon":y,"alt":z}` ≈ 55-65 B/pt) is ~30KB per poll — heavy for String-building heap and the single-threaded server. Use compact array-of-arrays with rounded fixed precision (`[[lat6,lon6,altM],...]` ≈ 22-28 B/pt → ~14KB at 500 pts), or cap + decimate oldest points. **Measure before locking the cap** — the discretion note says exactly this.

### Pattern 2: Embedded gzipped static assets

**What:** Leaflet JS/CSS (and optionally the app's own JS) live in PROGMEM as gzipped byte arrays, served with `Content-Encoding: gzip`.
**When to use:** every page load; browser caches via `Cache-Control`.
**Example:**
```cpp
// Binary-serve shape already proven in this codebase (thumbnail route,
// src/main_basestation.cpp:1545-1548) — reuse it for gzipped assets:
server.sendHeader("Content-Encoding", "gzip");
server.sendHeader("Cache-Control", "public, max-age=86400");
server.setContentLength(LEAFLET_JS_GZ_LEN);
server.send(200, "application/javascript", "");
server.sendContent(reinterpret_cast<const char*>(LEAFLET_JS_GZ), LEAFLET_JS_GZ_LEN);
```
[CITED: esp32.com/viewtopic.php?t=4708, mischianti.org byte-array gzipped pages tutorial — pattern concordant with the codebase's own binary route]

### Pattern 3: D-37 map degradation ladder

**What:** Leaflet with an OSM tile layer; `tileerror` events (or a load timeout) switch a `mode` flag → hide the Leaflet container, render the same trajectory on a `<canvas>` with auto-scaled coordinates (altitude-colored polyline, current position, grid + coordinate labels). Both consume the identical `/api/state` trajectory data — the fallback is a render path, not a data path.
**When to use:** AP mode or STA-without-uplink; also at page load in AP mode before any tile attempt resolves.
**Leaflet usage without image assets:** use `L.circleMarker` (vector) for the balloon position and `L.polyline` for the track — Leaflet's default icon PNGs (`images/` folder) are only needed for `L.marker` with default icons, so this choice removes that asset dependency entirely.

### Pattern 4: Alert engine — D-44 lifecycle classes

**What:** A manager (`Alerts()`) evaluated on each received beacon + each 5s tick, exposing a snapshot into `/api/state`. Inputs: `TelemetrySnapshot` (altitude/temp/GPS/age + NEW batteryMv), beacon seq gaps, CmdSender counters, transfer pass counts.
**Lifecycle:** safety-critical (low battery, GPS lost, landing) LATCH until an acknowledge action (POST `/alerts/ack`) — mirror the Phase 1/2 terminal-state discipline; informational (altitude threshold, ascent rate, signal quality) auto-clear on resolve with a cooldown (e.g., no re-fire within N polls) — mirror the anti-flap patterns (`IMG_RETRANSMIT_MAX_PASSES`, D-28 spacing).
**Thresholds:** NVS-persisted via Preferences (D-43), defaults from the project's own config (see Code Examples), UI card cloned from the Phase 2 event-threshold card shape (form + persist + display of persisted truth).

### Pattern 5: WiFi manager — D-40

**What:** Preferences namespace (e.g., `"wifi"`) storing mode + ssid/pass; boot: `WiFi.persistent(false)`, then STA attempt with ~20s `WL_CONNECTED` wait → AP fallback; runtime switch goes through disconnect/`WIFI_MODE_NULL` before entering the new mode. Web card POSTs credentials; response tells the operator which address to reconnect on (AP IP is fixed; STA IP comes from DHCP and must be shown + serial-printed).
```cpp
// Source: concordant pattern from official docs + community
// [CITED: docs.espressif.com .../api/wifi.html, stackoverflow.com/q/65230038]
prefs.begin("wifi", true);
String ssid = prefs.getString("ssid", "");
prefs.end();
WiFi.persistent(false);          // stop autoconnect to stale core NVS creds
if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    // non-blocking wait with ~20s deadline in loop(), NOT delay(20000)
} else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, MAX_CONNECTIONS);
}
```
**Critical constraint:** the single-threaded, no-blocking rule means the 20s STA wait must be a loop()-driven state machine (the codebase's millis-idiom), never `delay()`. During the wait, `server.handleClient()` keeps running so the operator can still reach the fallback UI on the AP interface only after fallback fires — plan the state machine so the web server always stays alive.

### Pattern 6: Gallery over the sidecar contract

**What:** `SdStorage` gains an index: enumerate `/images` once (`openNextFile()`), parse `IMG_{id}` names, cache {id, hasThumb, hasFull, size, sidecar presence} in RAM; refresh when the finalized-image count changes (D-36). Page route returns 12 entries newest-first + page count; detail route reads `IMG_{id}.JSON` / `IMG_{id}_T.JSON` and returns parsed metadata (hand-built JSON out, String-line parse in). Thumbnails stream via the EXISTING `/img/{id}_t.jpg` route; fulls via `/img/{id}` — both already handle absence honestly (404).
**When to use:** D-46/D-47/D-48 verbatim: incomplete images listed normally with a badge; detail view shows only verified data (sidecar `complete` flag false → show thumbnail + recorded chunk accounting, never imply full bytes).

### Anti-Patterns to Avoid

- **CDN-loaded Leaflet** — explicitly forbidden by the discretion note: the page must work in AP mode. Only OSM tiles may be internet-dependent.
- **Optimistic WiFi state in the UI** — show the mode the radio is actually in (queried), never the just-submitted form (IN-03 no-fabricated-state extends to WiFi).
- **`delay()` anywhere in the new paths** — 20s STA wait, tile-fallback detection, alert cooldowns all use the millis-idiom state machines.
- **Re-sending verbose trajectory objects every poll** — blows heap and stalls the single-threaded server (see Pattern 1 budget math).
- **New visual language** — map/gallery/alerts/WiFi surfaces reuse the locked tokens (4 font sizes, spacing set, semantic palette); alerts add at most new semantic colors for warning/critical (see Open Questions).
- **Touching balloon behavior beyond the beacon field** — D-41's boundary; the plan's verification should include a negative check (balloon diff limited to beacon-related lines).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Map pan/zoom/projection | Custom map renderer | Leaflet 1.9.4 (embedded) | Tile math, interactions, D-39 follow/recenter logic all solved; 42KB gz |
| Browser-side audio beep | Embedded audio file + `<audio>` plumbing | Web Audio `OscillatorNode` | Zero assets, few lines, works after gesture-unlock |
| NVS persistence | Raw `nvs.h` flash writes | `Preferences` (core) | Namespace API, wear-levelled, no partition math |
| WiFi credentials storage | Globals/`wifi_config.h` recompile | Preferences + runtime card (D-40) | Operator-changeable without reflashing; 20s lockout-proof fallback |
| Directory → gallery mapping | Guessing IDs from counters | Enumerate + parse sidecars | Files are the truth (some may be incomplete/absent); sidecars carry the D-47 metadata |
| Beacon battery transport | New packet type | Extend 0x14 body by 2 bytes | Pre-authorized minimal change; new type = new machinery both sides |

**Key insight:** everything base-side this phase is presentation over already-verified state — the codebase's manager/snapshot/serializer seams (`getTelemetrySnapshot`, `getTransferSnapshot`, `getStatus`, `serveFile`) are precisely the integration points. The only new *truth* is battery voltage, and it rides the existing beacon.

## Common Pitfalls

### Pitfall 1: Beacon wire extension done half-way
**What goes wrong:** adding the battery field to the struct but not the fixed-length checks/harness desynchronizes the pair — base rejects every beacon (length != expected) or the harness goes red.
**Why it happens:** `IMG_TELEMETRY_BEACON_BODY_SIZE = 17` is asserted in at least four places: image_protocol.h:107, command_protocol.cpp:480/492/515-516 (serializer + `length != CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4` check), and scripts/verify_protocol_roundtrip.mjs:73 (+ the 7+17+4=28 assertions at :919-920).
**How to avoid:** one atomic change-set across image_protocol.h (struct + constant), command_protocol.cpp (both directions), image_tx_manager.cpp (populate), image_rx_manager.cpp (snapshot field), main_basestation.cpp (JSON), harness constant + assertions; run `node scripts/verify_protocol_roundtrip.mjs` and both target builds in the same task.
**Warning signs:** base telemetry goes `null` after the balloon updates; harness length assertion failure.

### Pitfall 2: Beacon battery read returns garbage (hardware not wired)
**What goes wrong:** `analogRead(GPIO4)` floats or reads a divider ratio different from the assumed 2:1 — alerts fire on fabricated voltage, violating no-fabricated-state.
**Why it happens:** power_manager.cpp:315-324 *assumes* a divider ("Assuming 3.3V reference and voltage divider ratio … 2.0 = voltage divider ratio"); main_balloon.cpp:588-592 logs "Battery monitoring may not be working" when the raw read is 0 — the platform itself is unsure the sense line exists.
**How to avoid:** gate the beacon field on a plausibility check (e.g., 1.8V < V < 8V, nonzero raw) with an invalid-flag path (send 0 + validity bit or a flags bit), exactly like `gpsValid`; add a hardware UAT item to verify the divider value on the physical unit and calibrate the ratio constant.
**Warning signs:** constant 6.6V (rail-to-rail max) or 0V readings in the UI.

### Pitfall 3: RSSI assumed available on the E32
**What goes wrong:** planner/executor implements an "RSSI read" driver call that doesn't exist, or copies E22/E220 command bytes (`C0 C1 C5 …`) from web examples that search results misattribute to E32.
**Why it happens:** D-41 literally says "RSSI/SNR from the base's own E32 receiver"; E22/E220 examples dominate searches.
**How to avoid:** ALRT-06 = derived link quality: beacon seq-gap loss % over a sliding window (seq is BE16 rolling at exactly `TELEMETRY_BEACON_INTERVAL_MS` = 5000, image_protocol.h:101), ACK ratio (already in appState), retransmission pass counts. No driver change, no UART command traffic.
**Warning signs:** any plan step touching e32_lora.cpp for RSSI.

### Pitfall 4: `/api/state` payload bloat stalls the single-threaded server
**What goes wrong:** 30KB String-built JSON per poll fragments the heap and blocks `handleClient()` while building/sending; image streaming and polls serialize behind each other.
**Why it happens:** trajectory (D-38) + existing panels compound; String concatenation reallocs repeatedly.
**How to avoid:** compact trajectory encoding (array-of-arrays, fixed decimals); cap/decimate; `reserve()` the String; consider serving trajectory deltas only when the client's last-seen seq matches (still one endpoint, still poll-shaped — D-34 preserved); keep the diff-check client rule (D-36) so renders — not transfers — absorb the rate.
**Warning signs:** poll latency creeping past 1s; heap fragmentation warnings; UI stale-badging during gallery page loads.

### Pitfall 5: SD enumeration too slow for the 5s cycle
**What goes wrong:** full `openNextFile()` rescan per gallery request takes seconds at hundreds of files (community-reported ~4s/100 files on some cards).
**Why it happens:** sequential-only API; flat dir grows all flight.
**How to avoid:** RAM index built at boot + refreshed only when the finalized count changes (D-36 already mandates count-driven refresh); pagination slices the index, never the directory.
**Warning signs:** gallery route latency; watchdog-ish UI stalls when opening the gallery section.

### Pitfall 6: WiFi runtime switch orphans the operator
**What goes wrong:** switching to STA leaves the browser pointed at the old AP IP; if STA fails *after* AP is torn down, the operator is locked out — the exact scenario D-40's fallback exists to prevent.
**Why it happens:** tearing down AP before STA confirms connected; blocking wait that starves the web server.
**How to avoid:** keep AP up until `WL_CONNECTED` (then drop AP — no dual-mode steady state, satisfying "No AP+STA"); non-blocking 20s deadline then revert; always show the active IP (AP fixed / STA DHCP) in the card and serial log before/during the switch.
**Warning signs:** `WiFi.mode(WIFI_AP_STA)` persisting as a steady state; `delay()` in the switch path.

### Pitfall 7: Alert beep never plays
**What goes wrong:** Web Audio `AudioContext` is created "suspended" before any user gesture; programmatic beeps are silently dropped.
**Why it happens:** Chrome autoplay policy — `resume()` requires a user activation [CITED: developer.chrome.com/blog/autoplay].
**How to avoid:** arm audio on first interaction (any click on the page calls `ctx.resume()`); until armed, show critical alerts visually-only with an "audio muted until you interact" hint consistent with the mute toggle (D-42); test in a fresh tab.
**Warning signs:** beeps work only after the developer clicked around.

### Pitfall 8: Map memory/asset assumptions
**What goes wrong:** default `L.marker` icons 404 (images/ folder not vendored), or the offline canvas fallback diverges from the Leaflet view's data.
**How to avoid:** `L.circleMarker` + `L.polyline` only (no image assets); single trajectory-data path feeding both renderers (Pattern 3).
**Warning signs:** broken-image markers; fallback plot showing different points than the tiled map.

### Pitfall 9: Stale telemetry shown as fresh during transfers
**What goes wrong:** beacon cadence dips under heavy transfer load; UI keeps rendering old values without an age signal.
**Why it happens:** PRI-01 arbitration delays beacons behind bursts; the existing UI already displays age, but new alert logic (GPS-lost, signal-quality) consumes the same stale snapshot.
**How to avoid:** every alert condition that reads telemetry includes the age term (`receivedMs`); D-35's stale badge covers the panel level; 02-UAT already tracks "telemetry age stays within ~10 s during transfers" — reuse that acceptance shape.
**Warning signs:** GPS-lost alert firing while a transfer is simply delaying beacons; consider gating GPS-lost on sustained age (e.g., > 3× cadence).

### Pitfall 10: Restructuring breaks the locked UI contracts
**What goes wrong:** the D-45 single-page rebuild silently introduces new font sizes/spacing/colors or renames the locked state vocabularies.
**How to avoid:** treat 01/02-UI-SPEC as law: 4 font sizes, spacing set with lg=24, semantic palette, locked command/transfer vocabularies; harmonize-on-touch obligations for any off-grid surface touched (e.g., transfer panel 13/12/11px → 14px if the panel is rebuilt anyway).
**Warning signs:** diff of served HTML introducing `font-size:` values outside {14,16,18,24}.

## Code Examples

Verified patterns from project sources (verbatim) and cited external ones.

### 0x14 beacon body — the field inventory D-41 asked about (no battery)

```cpp
// Source: [VERIFIED: include/image_protocol.h:148-158] — quote verbatim:
// 0x14 body — fixed 17 bytes: seq u16, altitudeCm i32, tempCentiC i16,
// latE6 i32, lonE6 i32, flags u8 (bit0 gpsValid). Minimal telemetry-over-E32
// surface; richer telemetry stays Phase 3.
struct TelemetryBeaconBody {
    uint16_t seq;          // BE16 — rolling beacon sequence
    int32_t  altitudeCm;   // BE32 — altitude in centimeters
    int16_t  tempCentiC;   // BE16 — temperature in centi-degrees C
    int32_t  latE6;        // BE32 — latitude degrees * 1e6
    int32_t  lonE6;        // BE32 — longitude degrees * 1e6
    uint8_t  flags;        // bit0 gpsValid
};
```
The comment itself says "richer telemetry stays Phase 3" — the extension was anticipated. Fixed size constant: `static constexpr size_t IMG_TELEMETRY_BEACON_BODY_SIZE = 17;` [VERIFIED: include/image_protocol.h:107]. Recommended extension: `uint16_t batteryMilliV;  // BE16` (0 = not measurable — validity by value, or add flags bit1 `batteryValid`).

### Beacon transmit side — where battery gets populated

```cpp
// Source: [VERIFIED: src/image_tx_manager.cpp:122-145] (excerpt, verbatim fields):
    GPSData gps = Sensors().getGPSData();
    BMP280Data bmp = Sensors().getBMP280Data();
    bool gpsValid = (gps.satellites > 0);
    TelemetryBeaconBody body{};
    body.seq = beaconSeq++;
    ...
    body.tempCentiC = static_cast<int16_t>(lroundf(bmp.temperature * 100.0f));
    body.flags = gpsValid ? 0x01 : 0x00;
```
Extension point: add the PowerMgr read here (`PowerMgr().getBatteryVoltage()` after ensuring `PowerMgr().update()` runs — the method EXISTS at power_manager.cpp:115; main_balloon.cpp:697's `// Method doesn't exist` comment is stale).

### Balloon battery ADC path (the ALRT-02 source)

```cpp
// Source: [VERIFIED: src/power_manager.cpp:315-324] — quote verbatim:
float PowerManager::readBatteryVoltage() {
    // Read from ADC pin (assuming voltage divider)
    int rawValue = analogRead(BATTERY_SENSE_PIN);
    // Convert ADC reading to voltage
    // Assuming 3.3V reference and voltage divider ratio
    float voltage = (rawValue / 4095.0f) * 3.3f * 2.0f;  // 2.0 = voltage divider ratio
    return voltage;
}
```
`BATTERY_SENSE_PIN`: `#define BATTERY_SENSE_PIN 4   // Battery voltage monitoring (ADC)` [VERIFIED: include/sensor_pins.h:46]. 2-byte u16 millivolts covers the computed 0-6.6V range.

### Default alert thresholds — grounded in the project's own config

```cpp
// Source: [VERIFIED: include/balloon_config.h] — quote verbatim:
#define BATTERY_LOW_THRESHOLD      3.3   // Volts - below this, enable power saving   (line 30)
#define BATTERY_CRITICAL_THRESHOLD 3.0   // Volts - below this, emergency mode        (line 31)
#define GPS_MAX_AGE_MS              30000 // Maximum GPS data age in ms               (line 88)
#define EMERGENCY_ALTITUDE_RATE      15          // m/s - too fast descent             (line 126)
#define EMERGENCY_NO_GPS_TIME        300         // 5 minutes without GPS              (line 128)
```
Suggested defaults (planner's discretion per CONTEXT): altitude warn threshold(s) around the existing altitude feature gates (`ALTITUDE_THRESHOLD_HIGH 1000` / `CRITICAL 5000`, balloon_config.h:77-78); ascent-rate warn symmetric around `EMERGENCY_ALTITUDE_RATE` (e.g., ±10-15 m/s); GPS-lost alert ≈ 30-60s sustained no-fix (between `GPS_MAX_AGE_MS` and the 300s emergency bound); signal-quality bands on beacon-loss % (e.g., >20% degraded / >50% poor) — calibrate during UAT.

### Landing-detection vocabulary (base-side heuristic, informational anchor)

```cpp
// Source: [VERIFIED: src/system_state.h:29-37 excerpt] — enum class FlightPhase : uint8_t
// { ..., LANDING = 0x06 }; and [VERIFIED: src/system_state.h:21] LANDING_DETECTED = 0x06 (event)
```
Base cannot see this enum (system_state.cpp is balloon-only, excluded from the base build — platformio.ini:275); ALRT-04 must be an independent altitude-trend heuristic (e.g., sustained |rate| below X m/s below Y m absolute or near launch-altitude baseline for Z seconds). Treat the balloon vocabulary as naming inspiration only.

### Existing binary-serve shape to reuse for gzipped assets

```cpp
// Source: [VERIFIED: src/main_basestation.cpp:1544-1548] — quote verbatim:
            // This WebServer core has no raw-pointer send overload — set the
            // length, emit headers, then stream the binary body
            server.setContentLength(ImageRx().getLatestThumbLength());
            server.send(200, "image/jpeg", "");
            server.sendContent(reinterpret_cast<const char*>(ImageRx().getLatestThumbData()),
                               ImageRx().getLatestThumbLength());
```

### WiFi today (the code D-40 replaces)

```cpp
// Source: [VERIFIED: src/main_basestation.cpp:40-43, 653-664 excerpts] — quote verbatim:
const char* WIFI_SSID = "Cosmic1-BaseStation";
const char* WIFI_PASSWORD = "balloontrack";
const int WIFI_CHANNEL = 6;
const int MAX_CONNECTIONS = 4;
...
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, 0, MAX_CONNECTIONS);
```
Note: `include/wifi_config.h` (`const char *WIFI_SSID = "***"`) is NOT included by main_basestation.cpp (its includes are e32_lora/command_sender/command_protocol/image_rx_manager/sd_storage) — it belongs to the balloon-side legacy web app; do not conflate the two when extending "the existing `wifi_config.h` setup" per D-40. Keep the AP credentials as compile-time constants and add NVS station credentials alongside.

### Flash/memory budget (verified numbers)

```
// [VERIFIED: partitions.csv] app0 = 0x3c0000 (3.75 MB) at 0x10000; nvs = 0x5000 (20 KB)
// [VERIFIED: build artifacts] esp32-s3-basestation/firmware.bin = 1,092,320 B
//                             esp32-s3-balloon/firmware.bin   =   589,776 B
// Headroom for embedded Leaflet (~46 KB gz) ≈ 2.8 MB — no partition change needed.
// Base env does NOT define BOARD_HAS_PSRAM (platformio.ini:294-298) — budget the
// trajectory ring buffer for internal RAM: 1000 pts × ~16 B ≈ 16 KB, comfortably fine.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Phase 1 D-13 separate control page | D-45 single scrolling dashboard | this phase (explicitly supersedes) | One document, one poll wiring, sticky nav |
| 1s `/status` poll | 5s `/api/state` poll with backoff (D-33/D-35) | this phase | Matches the locked 5s rate; halves server churn vs today's 1s |
| Hardcoded AP-only WiFi | NVS-backed AP/STA with fallback (D-40) | this phase | Operator field flexibility; lockout-proof |
| `telemetry-chip` text readout | Full telemetry panel + map + alerts (WEB-01/02) | this phase | Existing chip graduates into the panel |
| E32 "RSSI/SNR" assumption (D-41) | Derived link-quality from seq gaps/ACK ratio | this phase (research-corrected) | ALRT-06 implementable without hardware change |

**Deprecated/outdated:**
- `lora_comm.cpp` RSSI/SNR fields (`lastRssi`/`lastSnr`) — legacy Sandeep-Mistry SPI radio code, excluded from both builds; NOT a signal source for this phase.
- `wifi_config.h` credentials — balloon-legacy; base has its own constants (see Code Examples).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Leaflet license is BSD 2-Clause (permissive, vendoring OK with notice) | Standard Stack | LOW — any permissive license permits embedding; confirm at download time |
| A2 | A battery voltage divider is physically wired to GPIO4 on the balloon with ~2:1 ratio | Code Examples (Pitfall 2) | ALRT-02 shows fabricated/absent voltage — mitigated by validity gating + UAT calibration item |
| A3 | OSM standard tile layer tolerates a single-operator dashboard's tile volume in STA mode | Patterns (map) | LOW — light use within published policy; worst case tiles blocked → D-37 fallback already covers it |
| A4 | Base ESP32-S3 board has no PSRAM enabled in the base env (flag absent) so trajectory lives in internal RAM | Code Examples (budget) | LOW — 16KB fits either way; enabling the flag would only add headroom |
| A5 | u16 millivolts is the right beacon battery encoding (vs flags-bit validity) | Patterns (Pitfall 1/2) | LOW — planner may prefer flags bit1 `batteryValid`; both fit the 19-byte body |
| A6 | Web Audio beep pattern (oscillator, ~800-1200Hz, short envelope) is acceptable as "beep" for D-42 | Patterns (4) | LOW — presentation detail already in Claude's discretion |

## Open Questions

1. **Trajectory payload shape (D-38 discretion)**
   - What we know: ring buffer 500-1000 pts; compact encoding ≈ 22-28 B/pt; verbose objects ≈ 55-65 B/pt; single-threaded server constraint.
   - What's unclear: whether full-history-per-poll stays under the acceptable latency at 1000 pts, or a delta-since-seq scheme is needed.
   - Recommendation: implement compact full-track first, measure `/api/state` build+send time at 500/1000 pts, switch to deltas only if the measurement misses the 5s cadence budget.
2. **Alert banner semantics vs the locked color palette**
   - What we know: palette reserves destructive-red for failures; amber = degraded; green = OK. D-42 needs "color-coded persistent banner row" for warning vs critical.
   - What's unclear: exact new semantic colors for informational-warning vs safety-critical (both amber? amber vs red?).
   - Recommendation: reuse the existing semantic set — critical alerts use the destructive red family, informational warnings the amber family, resolved the green — keeping "no new visual language" literally true; surface in plan-check if a new hue is proposed.
3. **Landing-detection threshold defaults**
   - What we know: no project-blessed numbers for sustained-flat-altitude detection; `EMERGENCY_ALTITUDE_RATE 15` m/s descent is the only anchor.
   - What's unclear: rate floor + persistence window + ground-reference (launch baseline vs absolute).
   - Recommendation: ship conservative defaults (e.g., |rate| < 1 m/s for 60s after having been above ~50 m gain) marked as UAT-calibratable via the Alerts card.
4. **Does the physical balloon have the GPIO4 divider at all** (feeds A2/Pitfall 2) — hardware verification item for `03-UAT.md`, not resolvable from code.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| PlatformIO Core | builds | ✓ | 6.1.19 | — |
| node | wire harness (`scripts/verify_protocol_roundtrip.mjs`) | ✓ | v24.8.0 | — |
| Arduino-ESP32 core | firmware (WebServer/WiFi/Preferences/SD) | ✓ | 3.3.9 | — |
| Internet (build-time only) | one-time Leaflet 1.9.4 dist download | ✓ | — | vendor from unpkg pinned `leaflet@1.9.4` |
| Internet (runtime, optional) | OSM tiles in STA mode | field-dependent | — | D-37 offline canvas fallback (designed-in) |
| SD card on base | gallery/persistence | hardware | — | gallery degrades to empty state + storage chip already reports UNAVAILABLE honestly |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** runtime internet for tiles (fallback designed-in per D-37); SD card absence (existing honest-degradation path).

## Security Domain

> `security_enforcement` not set in `.planning/config.json` → treated as enabled. (Note: phase security gate `/gsd-secure-phase` is part of this project's workflow — Phase 1/2 gates remain pending per STATE.md.)

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no (added this phase) | None added — LAN-local control panel by design (pre-existing posture; WiFi AP password is the existing boundary) |
| V3 Session Management | no | Stateless polling; no sessions introduced |
| V4 Access Control | yes (weakly) | WiFi card POST changes radio config — any LAN client can set it today; same exposure class as the existing settings POSTs (accepted project posture; flag in security gate) |
| V5 Input Validation | yes | WR-07 discipline extended: every new POST arg (`ssid`, `password`, threshold numbers, page numbers) range/length-checked as full `long`/bounded strings BEFORE narrowing; strictly-numeric parsing pattern reused from `handleImage` (main_basestation.cpp:1526-1531) for any parameterized routes |
| V6 Cryptography | no | No new crypto; beacon CRC16/CRC32 machinery untouched (extended body still CRC-covered by existing serializer) |

### Known Threat Patterns for ESP32 WebServer + LoRa

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed beacon fields (airtamper) | Tampering | Existing fixed-length + CRC16 validation on 0x14; EXTENDED body keeps both — do not weaken the `length !=` check |
| Oversized/absurd manifest-derived gallery metadata | Tampering | Sidecar parsing treats every value as untrusted (bound string copies, numeric clamps) — sidecars were written by this firmware but the SD is removable |
| XSS via sidecar/telemetry strings into the dashboard | Tampering/Escalation | All dynamic content rendered via `textContent` (existing poll script pattern) — never `innerHTML` with server strings; keep this rule in new render code |
| WiFi credential theft from NVS | Information Disclosure | NVS is flash-local; credentials not echoed back in `/api/state` (display mode/SSID only, never the password) |
| Rogue STA network redirect | Spoofing | Operator-entered SSID; 20s fallback bounds the damage; no auto-join of open networks |
| Command injection through new POST routes | Tampering | No command strings — numeric enums only, mirroring `handleSetEventThresholds` (WR-07 full-long checks) |

## Sources

### Primary (HIGH confidence — project source, read verbatim this session)
- `include/image_protocol.h` (0x14 beacon body/size, constants), `include/command_protocol.h` (factories, timeouts), `include/common_types.h`
- `src/main_basestation.cpp` (full UI, routes, JSON serializer, WiFi init, binary-serve shape)
- `include/image_rx_manager.h` + `src/image_tx_manager.cpp` (snapshot + beacon construction)
- `include/sd_storage.h` (D-29..D-32 contract, serveFile, NO enumeration), `src/power_manager.cpp` + `src/power_manager.h` (battery read path), `include/sensor_pins.h`, `include/balloon_config.h` (threshold anchors)
- `platformio.ini`, `partitions.csv`, build artifacts, `scripts/verify_protocol_roundtrip.mjs` (beacon pinning)
- `.planning/phases/03-enhanced-web-interface/03-CONTEXT.md`, `01/02-CONTEXT.md`, `01/02-UI-SPEC.md`, `.planning/codebase/CONVENTIONS.md`, PROJECT/ROADMAP/REQUIREMENTS/STATE

### Secondary (MEDIUM confidence — official docs / authoritative web)
- [mischianti.org E32 library part 2](https://mischianti.org/lora-e32-device-for-arduino-esp32-or-esp8266-library-part-2/) — library author: E32 cannot provide RSSI (direct quote in research)
- [wolles-elektronikkiste.de E220/E22/E32 guide](https://wolles-elektronikkiste.de/en/using-lora-with-the-ebyte-e220-e22-and-e32-series) — "RSSI function is not available on the E32 modules" (cross-check)
- [OSF Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/) — attribution/Referer/UA/light-use requirements
- [Leaflet download page](https://leafletjs.com/download.html) + [unpkg dist](https://unpkg.com/leaflet@1.9.4/dist/) — version/sizes
- [arduino-esp32 WiFi API docs](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html), [SO: runtime credentials](https://stackoverflow.com/questions/65230038/how-to-change-wifi-credentials-at-runtime-in-esp32)
- [Chrome autoplay policy](https://developer.chrome.com/blog/autoplay) — AudioContext gesture requirement
- [esp32.com: PROGMEM gzipped assets](https://esp32.com/viewtopic.php?t=4708), [mischianti: gzipped byte-array pages](https://mischianti.org/web-server-with-esp8266-and-esp32-byte-array-gzipped-pages-and-spiffs-2/)

### Tertiary (LOW confidence — community, single-source)
- SD enumeration performance anecdotes (~4s/100 files) — [reddit r/arduino](https://www.reddit.com/r/arduino/comments/1d8qj9n/reading_filenames_on_sd_card/), [pschatzmann.ch SD walk guide](https://www.pschatzmann.ch/home/2025/11/15/walking-an-entire-arduino-sd-card-directory-tree-a-performance-guide/) — treated as a caution, mitigated by the RAM index cache regardless

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — nothing new to install; all firmware APIs verified present in core 3.3.9; Leaflet facts from official sources
- Architecture: HIGH — every integration point read in source this session; patterns extend proven codebase seams
- Pitfalls: HIGH — beacon-extension blast radius verified line-by-line; E32-RSSI and autoplay pitfalls authoritatively sourced
- Alert defaults: MEDIUM — project config anchors exist, but landing/quality thresholds need UAT calibration (flagged)

**Research date:** 2026-08-19
**Valid until:** 2026-09-18 (stable domain; Leaflet 1.9.4 and core 3.3.9 are pinned versions)

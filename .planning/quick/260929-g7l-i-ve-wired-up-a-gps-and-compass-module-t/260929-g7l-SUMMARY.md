---
phase: quick-260929-g7l
plan: 01
subsystem: base-firmware + base-dashboard
tags: [gps, qmc5883l, compass, antenna-pointing, uart1, i2c, web-ui, api]
requires:
  - balloon-proven GPS UART idiom (src/sensor_manager.cpp initGPS — Serial1.begin with explicit pins)
  - base-owned I2C bus (src/status_display.cpp Wire.begin on pins 1/2 before initBaseSensors runs)
  - existing antenna great-circle math + map render paths (both preserved verbatim)
provides:
  - GET /api/base — tiny sensor-only JSON (~150 B) for the 1 s antenna mini-poll
  - `base` block in /api/state (gpsValid, lat, lon, sats, gpsAgeMs, compassPresent, headingValid, headingDeg, headingAgeMs)
  - [GPSBASE] 10 s bridge-console discriminator line (nmea byte count, fix, sats, heading, compass presence)
  - EDITABLE CONSTANTS: BASE_GPS_RX_PIN/TX_PIN, COMPASS_DECLINATION_DEG, COMPASS_MOUNT_FLIP, BASE_GPS_STALE_MS, BASE_HEADING_STALE_MS
affects:
  - antenna pointing card (chips, needle, rotate/elevation/distance/tilt commands now firmware-driven)
  - map observer marker (tiled Leaflet + D-37 offline canvas + first-fit bounds)
tech-stack:
  added: [TinyGPSPlus@^1.0.3 (esp32-s3-basestation env only — same spec the balloon env pins)]
  patterns: [register-level I2C driver (E32 precedent), millis-gated non-blocking sensor reads, single shared JSON serializer, settle-then-schedule mini-poll]
key-files:
  created: []
  modified:
    - include/base_station_config.h
    - src/main_basestation.cpp
    - platformio.ini
decisions:
  - "One shared serializer: appendBaseSensorsJson() feeds BOTH /api/state and /api/base, so the two payloads cannot drift (plan key-link)."
  - "Freshness is firmware-side: BASE_GPS_STALE_MS / BASE_HEADING_STALE_MS fold into gpsValid/headingValid inside the serializer; the client trusts the flags."
  - "GPS latch keyed on TinyGPSPlus location.isUpdated() && isValid() so lastFixMs is genuine fix freshness, not a per-pass re-stamp of a stale parser value."
  - "QMC5883L driven register-level (0x0B=0x01, 0x09=0x15, STATUS 0x08 DRDY, six LE bytes at 0x00) — no compass library, mirroring the E32 register-level precedent."
  - "Compass-absent degrades, never halts (SD convention): loud [GPSBASE] ERROR line, warn chip, math waits honestly."
  - "Base marker drawn from ONE latched snapshot consumed by antRender (both feeds converge); lastBase carries it to the offline canvas so the render paths agree."
  - "pin-45 risk made visible, not silently accepted: nmea= byte counter + [GPSBASE] line + 'no fix' chip; rewire is one editable constant (BASE_GPS_RX_PIN, 47 suggested free pin)."
metrics:
  duration: ~23 min
  completed: 2026-09-28
  tasks: 2
  files: 3
status: complete
actuals:
  tokens: 11000    # chars/4 over the realized diff (44,307 diff chars / 4)
  tasks: 2
  commits: 1
---

# Quick Task 260929-g7l Summary

**Base GPS + QMC5883L compass now drive the antenna pointing card and the map observer marker from the base station's own hardware, replacing every browser-sensor path in the page.**

The operator wired a MAX-M10S-class GPS (TX -> base RX pin 45) and a QMC5883L compass (I2C 0x0D on the existing pins-1/2 bus) to the base board. The firmware reads both non-blocking, exposes position + true-north heading through a new tiny /api/base endpoint and a `base` block in /api/state (one shared serializer), and the dashboard's antenna card + map consume that via a 1 s mini-poll — the monitoring tablet has no compass, so the rig's own sensors are now the only sensor source. Manual position and a new manual rig-tilt input remain honest fallbacks.

## Tasks Executed

| # | Task | Commit | Result |
|---|------|--------|--------|
| 1 | Base sensor firmware layer (UART GPS + QMC5883L I2C + /api/base + /api/state base block) | 9f9b2ee (shared single commit per plan Task 2 step 3) | Base env SUCCESS, all verify greps green |
| 2 | Web UI source swap (antenna card + map observer marker both paths) + dual builds + harness + commit | 9f9b2ee | Both envs SUCCESS, harness exit 0, negative gates clean |

Single commit `9f9b2ee` — exactly `include/base_station_config.h`, `src/main_basestation.cpp`, `platformio.ini` (476 insertions, 145 deletions); no other env's lib_deps touched; balloon build byte-identical inputs.

## What Landed

**Firmware (Task 1)**
- `initBaseSensors()` — compass probe at 0x0D over the already-running Wire bus, then 0x0B=0x01 / 0x09=0x15 (OSR 512, 8 G, 50 Hz, continuous); loud `[GPSBASE] ERROR` when absent, station continues without heading. GPS: `setRxBufferSize(512)` BEFORE `begin(GPS_BAUD_RATE, SERIAL_8N1, BASE_GPS_RX_PIN, BASE_GPS_TX_PIN)` — the balloon's proven UART1 idiom.
- `processBaseSensors()` every loop pass before `processLoRa()`: buffer-bounded UART drain feeding `TinyGPSPlus.encode()` with byte counting; fix latched only on `location.isUpdated() && isValid()`; compass read behind a 200 ms millis gate with DRDY check and count-checked Wire transfers (short read keeps the last good heading latched); 10 s `[GPSBASE] nmea=… fix=… sats=… heading=… compass=…` discriminator via Serial0.
- Routes + serializer: `GET /api/base` (handleApiBase) next to /api/state; `appendBaseSensorsJson(String&)` is the ONE base-block serializer both endpoints call; inserted into handleApiState's wifi-block region.
- setup() calls `initBaseSensors()` after `initStorage()` under a `showBootStage("BASE SENSORS")` stage line (Wire up since `StatusOLED().begin`).

**Web UI (Task 2)**
- Deleted the entire browser-sensor half: orientation handler, geolocation callbacks, the enable routine with its insecure-origin hint, the enable button, `headingAbs`, `ANT.on`. Negative gates verified by grep (0 matches for antEnableSensors / antOnOrientation / antOnGeo / DeviceOrientation / watchPosition / isSecureContext, plus webkitCompassHeading / geolocation / tablet-GPS vocabulary).
- `ANT.base` is the single latched snapshot; `renderAntenna(data)` seeds it from the 5 s poll payload and the new 1 s `pollBase()` fetch of /api/base converges on the same `antRender()` consumer. Heading/observer derive per render from the snapshot.
- Chips (locked chip pattern kept): compass "no data · check I2C wiring 0x0D" warn / "stale" warn / "compass: N°" ok; "base GPS: no fix" warn / "fix · N sats · Ns old" ok-warn-by-age / "manual" warn when overridden; balloon chip untouched.
- Great-circle math, deadbands, needle, elevation/distance/boresight stepper: verbatim; tilt command now compares elevation against the MANUAL rig tilt (`#ant-tiltin` number input, persisted through the existing localStorage pattern alongside offsetDeg).
- Map: `updateBaseMarker()` — orange `L.circleMarker` (radius 7, "Base station" tooltip) + ~200 m heading tick polyline, included in the first-fit bounds; `lastBase` snapshot drives the SAME dot + tick on the D-37 offline canvas through its existing projection; nothing renders when `gpsValid` is false and a stale fix removes the markers.

## Verification

- `pio run -e esp32-s3-basestation` — SUCCESS (TinyGPSPlus 1.1.0 resolved from local cache; no CDN fetch needed)
- `pio run -e esp32-s3-balloon` — SUCCESS (balloon env untouched, as the in-flight D1 round #15 requires)
- `node scripts/verify_protocol_roundtrip.mjs` — exit 0
- Task 1 verify greps: initBaseSensors 3, processBaseSensors 3, appendBaseSensorsJson 5, api/base 4 (+1 later call-site comment/update), TinyGPSPlus in platformio.ini, BASE_GPS_RX_PIN in config — all green
- Task 2 verify greps: all six negative gates 0 matches; `api/base` 9; `ant-tiltin|updateBaseMarker` 9
- `git status --porcelain` on the three files clean after commit; `git show --stat` confirms exactly the three plan files in `9f9b2ee`; no tracked-file deletions

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] Added a "Base GPS" button to exit the manual position override**
- **Found during:** Task 2
- **Issue:** The plan kept the manual lat/lon override "exactly as-is", but the old design implicitly self-healed back to the hardware source because `watchPosition` kept re-asserting the GPS position. With the browser source gone, a clicked "Use" would pin the observer position to manual values with no way back except a page reload — a one-way trap the "base GPS: manual" warn chip would then report forever.
- **Fix:** Added `#ant-manual-clear` ("Base GPS") next to "Use"; it clears `ANT.manLat/manLon` and `obsSrc` so the firmware snapshot drives the card again.
- **Files modified:** src/main_basestation.cpp (one HTML string, one ANT_EL id, one click binding)
- **Commit:** 9f9b2ee

**2. [Rule 3 - Blocking-issue avoidance, proactive] platformio.ini line-ending warning**
- `git add` warned "LF will be replaced by CRLF in platformio.ini" — the repo's autocrlf behavior on an edited file, not a defect; committed content is normalized by git. No action needed; recorded for transparency.

## Known Stubs

None — every value the antenna card and map render comes from the firmware serializer or an explicit manual input; no placeholder data paths.

## Bench Honesty (no hardware claims)

Per project evidence discipline: this summary makes NO bench/hardware claims — code-level + build-green only. Whether pin 45 actually delivers NMEA on the base board is an operator bench observation. The firmware makes the answer visible in one look: the `[GPSBASE]` line's `nmea=` counter (zero bytes = wiring problem), `fix=`/`sats=`, and the dashboard chip. If pin 45 is dead, the rewire is one editable constant (`BASE_GPS_RX_PIN` -> 47, documented in include/base_station_config.h with the balloon's pin-45 caveat carried over).

## Self-Check: PASSED

- Commit `9f9b2ee` exists on main, touching exactly include/base_station_config.h, src/main_basestation.cpp, platformio.ini
- SUMMARY written at .planning/quick/260929-g7l-i-ve-wired-up-a-gps-and-compass-module-t/260929-g7l-SUMMARY.md
- Both build envs green + harness exit 0 (this session, post-edit)

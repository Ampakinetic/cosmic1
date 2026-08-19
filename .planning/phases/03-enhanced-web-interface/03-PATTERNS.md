# Phase 3: Enhanced Web Interface - Pattern Map

**Mapped:** 2026-08-20
**Files analyzed:** 12 (new + modified)
**Analogs found:** 12 / 12

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/main_basestation.cpp` (extend: routes, `/api/state`, single-page UI) | controller + component | request-response | itself (`handleStatus`, `handleImage`, `handleSetEventThresholds`) | exact |
| `src/trajectory_buffer.h/.cpp` (NEW) | service (manager) | ring-buffer / batch | `sd_storage.h/.cpp` (manager + singleton + begin()) | role-match |
| `src/alert_engine.h/.cpp` (NEW) | service (manager) | event-driven (per-beacon + per-tick) | `image_rx_manager.h/.cpp` (`onTelemetryBeaconFrame` + `process()` lifecycle) | role-match |
| `src/wifi_manager.h/.cpp` (NEW) | service (manager) | request-response + state machine | `sd_storage.h` manager skeleton + `initWiFi()` (main_basestation.cpp:653-664) | role-match |
| `src/sd_storage.h/.cpp` (extend: enumeration/index) | model/service | file-I/O | itself (`serveFile`, `finalizeImage`, sidecar paths) | exact |
| `src/web_assets.h` (NEW, generated) | config/asset | file-I/O → PROGMEM serve | binary-serve shape in `handleImage` (main_basestation.cpp:1544-1548) | partial |
| `include/image_protocol.h` (extend: beacon body 17→19) | model | transform (wire format) | itself (`TelemetryBeaconBody`, `IMG_TELEMETRY_BEACON_BODY_SIZE`) | exact |
| `src/command_protocol.cpp` (extend: beacon ser/deser) | service | transform (wire) | itself (`serializeTelemetryBeacon`/`deserializeTelemetryBeacon` lines 475-545) | exact |
| `src/image_tx_manager.cpp` (extend: populate battery) | service | event-driven (timer) | itself (`sendTelemetryBeacon` lines 115-155) | exact |
| `src/image_rx_manager.cpp` (extend: snapshot battery field) | service | event-driven | itself (`TelemetrySnapshot` in image_rx_manager.h:105-114) | exact |
| `scripts/verify_protocol_roundtrip.mjs` (extend: 17→19 assertions) | test | batch | itself (beacon size assertions ~lines 73, 919-920) | exact |
| `scripts/embed_web_assets.mjs` (NEW, optional) | utility (build tooling) | file-I/O transform | no analog — new tool; simple gzip→C-header generator | none |

Browser-side work (dashboard JS, Leaflet map, alerts banner, gallery UI, WiFi card) lives inside `main_basestation.cpp`'s PROGMEM HTML template — the poll-script pattern at main_basestation.cpp:495-568 is the analog.

## Pattern Assignments

### `src/main_basestation.cpp` — `/api/state` + new routes (controller, request-response)

**Analog:** existing `handleStatus()` (lines 1370-1499)

**Route registration pattern** (lines 715-735): every new route is one `server.on(path, HTTP_GET/POST, handler)` line inside `initWebServer()`. Parameterized paths CANNOT use `server.on()` — dispatch from `handleNotFound()` like `/img/{id}` (lines 1577-1587). Gallery pagination can use query args (`server.arg("page")`) on a registered route; per-image detail (`/gallery/{id}`) must ride `handleNotFound`.

**Hand-built String JSON pattern** (from `handleStatus`, lines 1439-1453 — telemetry block to extend with batteryMv + trajectory + alerts):
```cpp
const TelemetrySnapshot& beacon = ImageRx().getTelemetrySnapshot();
if (beacon.valid) {
    uint32_t ageMs = millis() - beacon.receivedMs;
    json += "\"telemetry\":{";
    json += "\"ageMs\":" + String(ageMs) + ",";
    json += "\"altitudeM\":" + String(beacon.altitudeM, 1) + ",";
    // ... lat/lon with String(x, 6), gpsValid, then },"  — absent data becomes null, never fabricated
} else {
    json += "\"telemetry\":null,";
}
```
NOTE: the base env deliberately has NO ArduinoJson (platformio.ini base `lib_deps` empty) — keep hand-built String concatenation; `reserve()` the String for the larger `/api/state` payload (Pitfall 4).

**POST validation pattern (WR-07)** — `handleSetEventThresholds` (lines 1271-1327): check `server.hasArg(...)` for every field, then range-check each as full `long` BEFORE narrowing. Clone this for `/wifi` (bounded-length ssid/password strings) and `/alerts` threshold POSTs:
```cpp
long altM = server.arg("altitude-m").toInt();
if (altM < 10 || altM > 5000) {
    sendResponse(400, "Error", "Invalid altitude delta (10-5000 m)");
    return;
}
```

**Strictly-numeric path parsing** — `handleImage` (lines 1508-1537): prefix/suffix slicing + per-char `isDigit` + `strtol` + range bound (`id <= 0 || id > 0xFFFF`). Reuse verbatim for `/gallery/{id}` detail routes.

**Binary/gzip serve pattern** (lines 1544-1548) — reuse for `web_assets.h` Leaflet files:
```cpp
server.setContentLength(ImageRx().getLatestThumbLength());
server.send(200, "image/jpeg", "");
server.sendContent(reinterpret_cast<const char*>(ImageRx().getLatestThumbData()),
                   ImageRx().getLatestThumbLength());
```
Add `server.sendHeader("Content-Encoding", "gzip");` and `Cache-Control` before `send` for Leaflet.

**Browser poll pattern** (lines 495-568): `fetch('/status').then(r => r.json())` + DOM update via `textContent` ONLY (never `innerHTML` with server strings — XSS rule from security domain). `setInterval(updateStatus, 1000)` at line 567 is re-locked to 5000ms with D-35 backoff. The dataset-gate diff-check at lines 532-539 (`thumbImg.dataset.id !== String(thumbId)`) is the exact D-36 diff-render idiom to extend.

**Non-blocking loop pattern** (lines 606-638): `loop()` = `server.handleClient()` + per-manager `process()`/`update()` + millis-idiom timers. New `Alerts().process()` / WiFiMgr state machine slot in here; the D-26 status-poll timer (lines 620-628) is the millis-timer template. Never `delay()` (except the existing trailing `delay(10)`).

**WiFi code being replaced** (lines 40-43, 653-664): hardcoded `WiFi.mode(WIFI_AP); WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, 0, MAX_CONNECTIONS);` → moves into `wifi_manager.cpp` with Preferences + STA-first/20s-fallback state machine.

---

### `src/trajectory_buffer.h/.cpp` (NEW — service, ring buffer)

**Analog:** `sd_storage.h` manager/singleton skeleton

Copy the established manager shape (sd_storage.h:73-84, 147-152):
```cpp
class SdStorage {
public:
    SdStorage();
    bool begin();
    // ... accessors returning const snapshots, never fabricated
};
// Global Instance Access
extern SdStorage& SDStorage();
```
Conventions (`.planning/codebase/CONVENTIONS.md`): header beside implementation in `src/`, singleton free-function accessor, `begin()/update()` lifecycle, snapshot structs by value/const-ref (model on `TelemetrySnapshot`, image_rx_manager.h:105-114 — `valid` flag, absent data reported as absent). Ring buffer in internal RAM (base env has no PSRAM); no heap per point. No existing ring buffer in codebase — the shape is new, the manager wrapper is not.

---

### `src/alert_engine.h/.cpp` (NEW — service, event-driven)

**Analog:** `ImageRxManager` lifecycle + `computeLinkTruth()` truth discipline

Copy the frame-entry + process split (image_rx_manager.h:125-137):
```cpp
void onTelemetryBeaconFrame(const uint8_t* frame, size_t length);  // event-driven input
void process();                                                     // loop-driven timers
```
Alerts().process() evaluates per beacon + per 5s tick from `ImageRx().getTelemetrySnapshot()` + `CmdSender()` counters — never touches LoRa itself.

**Latch/anti-flap discipline analog** — `computeLinkTruth()` (main_basestation.cpp:93-113): single computation, terminal-state vocabulary, UNKNOWN-before-data. D-44's latch-until-ack vs auto-clear-with-cooldown mirrors this and the D-24 terminal flags (`terminal`/`complete`/`crcMismatch` in image_rx_manager.h:94-100 — one bool per distinct terminal state, never a magic enum).

**Threshold defaults source:** `include/balloon_config.h` (`BATTERY_LOW_THRESHOLD 3.3`, `BATTERY_CRITICAL_THRESHOLD 3.0`, `GPS_MAX_AGE_MS 30000`, `EMERGENCY_ALTITUDE_RATE 15`) — see 03-RESEARCH.md Code Examples for verbatim lines.

**NVS persistence** — Preferences (core 3.3.9, no lib_deps needed):
```cpp
prefs.begin("alerts", false);
prefs.putFloat("battLowV", v);
prefs.end();
```
UI card clones the event-thresholds card shape (form + WR-07 validation + display persisted truth).

---

### `src/wifi_manager.h/.cpp` (NEW — service, request-response + state machine)

**Analog:** `SdStorage` manager skeleton + existing `initWiFi()` (main_basestation.cpp:653-664)

Pattern (research Pattern 5, concordant with core docs):
```cpp
prefs.begin("wifi", true);
String ssid = prefs.getString("ssid", "");
prefs.end();
WiFi.persistent(false);          // stop stale core NVS autoconnect
if (ssid.length()) { WiFi.mode(WIFI_STA); WiFi.begin(...); }  // 20s non-blocking deadline
else { WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, MAX_CONNECTIONS); }
```
Critical: the ~20s STA wait is a millis-idiom state machine driven from `loop()` (model: D-26 timer, main_basestation.cpp:623-628) — `server.handleClient()` keeps running; keep AP up until `WL_CONNECTED` then drop AP (never steady `WIFI_AP_STA`). AP credentials stay compile-time constants; only station creds go in NVS. Show the actually-active mode/IP (no-fabricated-state, IN-03) — the WiFi card displays queried truth, not the submitted form.

---

### `src/sd_storage.h/.cpp` (extend — enumeration/index for gallery)

**Analog:** itself — `serveFile()` (h:119-124), path builders `imagePath`/`sidecarPath` (h:141-142), `SdImageMetadata` (h:30-61)

New API shape to add: RAM index built at boot via `openNextFile()` over `/images`, parse `IMG_{id}` names (5-digit zero-padded, D-32), cache {id, hasThumb, hasFull, sidecar presence}; refresh only when finalized count changes (D-36). Follow the existing honest-absence rule: `serveFile` "absence is reported honestly, never fabricated" — enumeration does the same (some ids may be incomplete/absent; files are the truth). Sidecar metadata for the detail view parses the hand-built String JSON written by `writeSidecar` (h:144) — treat every parsed value as untrusted (bound copies, numeric clamps per security domain). Thumbnails/fulls already stream via existing `/img/` routes — do not duplicate.

---

### `include/image_protocol.h` + `src/command_protocol.cpp` + `src/image_tx_manager.cpp` + `src/image_rx_manager.cpp` (beacon battery, wire transform)

**Analog:** the existing 0x14 path end-to-end. Atomic change-set required (Pitfall 1 — size pinned in 4+ places):

- Struct (image_protocol.h:151-158): add `uint16_t batteryMilliV;  // BE16` and bump `IMG_TELEMETRY_BEACON_BODY_SIZE` 17→19 (image_protocol.h:107).
- Serializer (command_protocol.cpp:475-513): add `writeUint16(buffer + offset, pkt.body.batteryMilliV); offset += 2;` and update the `packetLength` arithmetic comment (line 480). Deserializer mirrors with `readUint16` (lines 515-545, including the strict `length != CMD_HEADER_SIZE + IMG_TELEMETRY_BEACON_BODY_SIZE + 4` check at 516 — keep, do not weaken).
- Populate (image_tx_manager.cpp:115-145): add the PowerMgr read into `sendTelemetryBeacon()`. Battery validity by value (0 = not measurable, mirroring the gpsValid invalid-flagged-else-zero pattern at lines 130-143) or flags bit1. ADC source: `PowerManager::readBatteryVoltage()` (power_manager.cpp:315-324, GPIO4, 2:1 divider assumed — plausibility-gate 1.8V < V < 8V per Pitfall 2). Ensure `PowerMgr().update()` actually runs (currently never called; the "Method doesn't exist" comment in main_balloon.cpp is stale — `update()` exists at power_manager.cpp:115).
- Snapshot (image_rx_manager.h:105-114): add `uint16_t batteryMv;` + validity to `TelemetrySnapshot`; then surface in `/api/state` telemetry block.
- Harness: update `IMG_TELEMETRY_BEACON_BODY_SIZE = 17` constant + `7+17+4=28` assertions in `scripts/verify_protocol_roundtrip.mjs`; run harness + both builds in the same task.

---

### `src/web_assets.h` (generated) + `scripts/embed_web_assets.mjs` (optional, new)

**No firmware analog** for the generator (simple dist→gzip→C-header script; node available at v24.8.0). Serving analog is the binary route shape above. Keep arrays in PROGMEM with `_length` constants; gzip at build time, commit the generated header.

## Shared Patterns

### No-fabricated-state / single truth computation
**Source:** `computeLinkTruth()` (main_basestation.cpp:88-113) — ONE computation shared by JSON + LED; snapshot structs carry `valid` flags; absent → `null`.
**Apply to:** `/api/state` serializer, alert engine, WiFi card, gallery (incomplete badge), stale badge (D-35).

### Manager + singleton + begin()/update() lifecycle
**Source:** `sd_storage.h:73-84,147-152`; also `ImageRx()`, `CmdSender()`, `E32LoRaModule()`.
**Apply to:** `TrajectoryBuffer()`, `Alerts()`, `WiFiMgr()` (or chosen names) — new manager calls slotted into `setup()` init sequence (main_basestation.cpp:590-594) and `loop()` (606-638).

### WR-07 input validation
**Source:** `handleSetEventThresholds` (main_basestation.cpp:1271-1303) — hasArg checks + full-long range checks before narrowing; `handleImage` (1508-1537) for numeric path params.
**Apply to:** `/wifi`, `/alerts`, `/alerts/ack`, gallery page params.

### Non-blocking millis-idiom timers
**Source:** D-26 status poll (main_basestation.cpp:620-628); LED blink self-throttle (1622-1628).
**Apply to:** WiFi 20s fallback, alert cooldowns, trajectory ring-buffer insertion cadence, gallery index refresh.

### Locked UI design tokens
**Source:** `01-UI-SPEC.md` + `02-UI-SPEC.md` — fonts {14,16,18,24}px, spacing {4,8,16,24,32,48,64}, semantic palette, emoji card icons, `textContent`-only rendering.
**Apply to:** every new surface (map, gallery, alerts bar, WiFi card). Diff check: no `font-size:` outside the locked set.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `scripts/embed_web_assets.mjs` | utility (build) | file-I/O transform | No existing asset-generation scripts; trivial gzip→header generator, use RESEARCH.md Pattern 2 |
| Trajectory ring buffer internals | service | batch | No existing ring buffer; only the manager wrapper has an analog — encode compact array-of-arrays per RESEARCH Pattern 1 |

## Metadata

**Analog search scope:** `src/`, `include/`, `scripts/` (full file set listed in 03-RESEARCH.md Sources)
**Files scanned:** main_basestation.cpp (1629 lines, sections 1-280/495-740/1271-1629), sd_storage.h (full), image_protocol.h (95-165), image_rx_manager.h (85-145), command_protocol.cpp (470-545), image_tx_manager.cpp (115-155), power_manager.cpp (105-135; 315-324 quoted in research)
**Pattern extraction date:** 2026-08-20

---
phase: 03-enhanced-web-interface
reviewed: 2026-08-19T17:50:25Z
depth: standard
files_reviewed: 20
files_reviewed_list:
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/sd_storage.h
  - platformio.ini
  - scripts/embed_web_assets.mjs
  - scripts/verify_protocol_roundtrip.mjs
  - src/alert_engine.cpp
  - src/alert_engine.h
  - src/command_protocol.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - src/sd_storage.cpp
  - src/trajectory_buffer.cpp
  - src/trajectory_buffer.h
  - src/web_assets.h
  - src/wifi_manager.cpp
  - src/wifi_manager.h
  - vendor/provenance.json
findings:
  critical: 1
  warning: 8
  info: 8
  total: 17
status: issues_found
---

# Phase 03: Code Review Report

**Reviewed:** 2026-08-19T17:50:25Z
**Depth:** standard
**Files Reviewed:** 20
**Status:** issues_found

## Summary

Adversarial review of the Phase 3 implementation (dashboard/tracer, map, alerts, gallery, WiFi mode manager) plus the shared protocol/storage modules it touches. All 20 changed source files were read in full; cross-file call chains were traced (RX/TX window machinery, SD finalize paths, alert consumers of the telemetry snapshot, WiFi switch flow, web asset embedding).

Overall the implementation is disciplined — hand-rolled parsers are bounded and clamped, web rendering uses `textContent` only (no XSS sinks), path builders are strictly numeric, protocol serializers round-trip, and the vendor asset hashes in `vendor/provenance.json` match the generated `src/web_assets.h`. However, the review found one Critical correctness defect in SD finalization metadata, a reachable dual-armed-window race in the image transfer machinery, an alert-thresholds persist/ordering inconsistency, missing seq-ordering on the telemetry snapshot, and an unauthenticated/CSRF-exposed set of state-changing HTTP routes. Raw vendor blobs (`vendor/leaflet.js`, `vendor/leaflet.css`, `vendor/LICENSE`) were verified via the provenance hash cross-check rather than line review.

Verified non-findings (checked, correct): `ResponseStatusData` native memcpy is a documented little-endian island with an explicit padding note (`include/command_protocol.h:169-189`, struct is 32 bytes ≤ 50); `MAX_IMAGE_SIZE == IMG_MAX_IMAGE_SIZE` (50000) enforced by `static_assert`; all gallery/sidecar parsing is length-bounded and range-clamped; image/gallery route ids are strictly numeric 1..0xFFFF (no path traversal); `jsonEscape` covers quotes/backslashes/control chars; all client DOM writes use `createElement`/`textContent`.

## Critical Issues

### CR-01: `finalizeImage` fabricates `storedToSd: true` from a stale per-kind byte counter

**File:** `src/sd_storage.cpp:286-303`
**Issue:** `SdStorage` keeps ONE file handle and ONE `persistedBytes` counter per image *kind* (via `fileFor`). `finalizeImage` computes `m.storedToSd = *persistedBytes > 0` (line 303) — but the counter is only reset inside `openTransfer`/handle-flip, never on finalize. Any full image that is finalized **without its handle having been opened for its id in this round** inherits the *previous* full's byte count and records `storedToSd: true` in its sidecar despite having zero (or stale) bytes on disk. This is directly reachable:

- `src/image_rx_manager.cpp:382` — slot-pressure eviction finalizes a QUEUED full (`finalizeIncomplete(*oldest, "slot pressure")`). QUEUED fulls deliberately never call `openTransfer` (lazy open, the CR-01-from-plan-02 fix), so the counter still holds the *previous* full transfer's bytes.
- `src/image_rx_manager.cpp:782-786` — `activateNextPull` sets `pullActive = true`, finds `fm >= totalChunks` (all chunks arrived while QUEUED — unsolicited chunks route into non-terminal FULL slots per `onChunkFrame:262`), and calls `finalizeTransfer` **before** the `openTransfer` at line 793.

Result: `IMG_x_f.JSON` (and `/gallery/{id}` detail, and the UI "stored" badge) claims persisted-on-SD for an image with nothing on the card — violating the module's own documented contract ("storedToSd is computed truth ... never fabricated", `src/sd_storage.cpp:299-301`, `include/sd_storage.h`).

**Fix:** Capture whether THIS id owned the kind handle before closing it, and gate on that:

```cpp
bool heldThisId = (*handle != nullptr && *handleId == meta.imageId);
if (heldThisId) {
    handle->close();
    *handle = File();
}
...
SdImageMetadata m = meta;
m.storedToSd = heldThisId && *persistedBytes > 0;   // bytes landed only if this id held the handle
```

## Warnings

### WR-01: Thumbnail-heal window and full-pull window can be armed simultaneously for the same image id

**File:** `src/image_rx_manager.cpp:190-196, 255-266, 531-547` (with `src/image_tx_manager.cpp:690, 709`)
**Issue:** The heal gate only prevents *issuing* a heal while a pull is active (gate 1, line 190); nothing prevents a *pull from activating* while a heal is armed. Sequence: a thumbnail with holes stalls; gate 2 passes via the 24 s oversize idle fallback (lines 193-195) because no FULL slot exists; the heal window arms. Later, the same id's FULL manifest arrives while no pull is active — the manifest-while-idle branch (line 534, `findActivePull() == nullptr`) activates the full pull *immediately* and arms its window. Two windows are now in flight for one id, and the chunk-routing precedence (lines 255-266) sends **all** of that id's chunks — including the FULL pull's — into the THUMBNAIL slot. If `expectedLen` matches (same chunking), full-image bytes are reassembled into the thumbnail buffer/SD file (CRC catches it at finalize → INCOMPLETE); otherwise chunks are rejected. Both transfers for the id degrade INCOMPLETE and the airtime is wasted. The balloon side compounds it: `handleWindowRequest` rejects only *other* entries' armed windows (`src/image_tx_manager.cpp:690`), so a FULL window silently re-arms over an armed THUMBNAIL window on the SAME entry (line 709), abandoning the heal mid-flight.

**Fix:** In both activation paths (`activateNextPull` and the manifest-while-idle branch at line 534), defer activation while a same-id THUMBNAIL slot is non-terminal with `windowActive` set:

```cpp
// skip/defer this id while its heal window owns the link
ImageRxTransfer* heal = findTransfer(t->imageId, static_cast<uint8_t>(ImageKind::THUMBNAIL));
if (heal != nullptr && !heal->terminal && heal->windowActive) {
    continue;   // or return, leaving the full QUEUED for the next process() pass
}
```

Optionally also treat same-entry armed-window displacement as BUSY on the balloon side (`image_tx_manager.cpp:703-714`) instead of silently re-arming.

### WR-02: `setThresholds` applies new thresholds to live evaluation even when the NVS persist fails

**File:** `src/alert_engine.cpp:126-144`
**Issue:** `thresholds = t;` (line 132) executes *before* the NVS writes. If any `putInt`/`putFloat` fails, the function returns false — the route reports failure to the operator — but the in-memory thresholds are already the new values, so live alert evaluation immediately uses them until reboot while the operator believes the change was rejected. Divergent state between what the UI says and what the engine does.

**Fix:** Assign only after a successful persist:

```cpp
Preferences prefs;
prefs.begin("alerts", false);
bool ok = prefs.putInt("altWarnM", t.altWarnM) != 0;
/* ... remaining keys ... */
prefs.end();
if (ok) {
    thresholds = t;
}
return ok;
```

### WR-03: Telemetry snapshot has no seq-ordering — delayed out-of-order beacons overwrite newer state

**File:** `src/image_rx_manager.cpp:285-313` (consumers: `src/alert_engine.cpp:182-219`, `src/trajectory_buffer.cpp:46-72`)
**Issue:** `onTelemetryBeaconFrame` unconditionally overwrites the snapshot with whatever beacon arrived last; there is no "is this seq newer than the last applied?" check. A delayed/re-delivered beacon with an OLDER seq (LoRa retry, queue backlog) regresses the snapshot to stale values. Downstream effects: the altitude-RATE condition computes a wrong signed rate over the regression (false ALRT-05 fire or suppressed fire, `alert_engine.cpp:187-191`); `TrajectoryBuffer::append` accepts the stale point as "new" (its seq gate only checks `!=`), producing a chronology glitch in the full-flight track; and the signal-loss window double-counts the arrival as a fresh beacon.

**Fix:** Keep the last applied seq and drop older beacons with a wrap-aware comparison before touching the snapshot/trajectory:

```cpp
// in onTelemetryBeaconFrame, before applying:
if (snapshotValid && seqIsOlder(pkt.body.sequence, lastAppliedSeq)) {
    return;   // stale reorder — the newer state is already applied
}
// uint16 wrap-aware: older = (uint16_t)(last - cur) < 0x8000 && cur != last
```

### WR-04: No authentication or CSRF/Origin protection on state-changing HTTP routes

**File:** `src/main_basestation.cpp:2006-2032` (routes), `2876-2916` (`handleWifiSwitch`)
**Issue:** Every state-changing endpoint (`POST /wifi`, `/capture`, `/set-*`, `/auto-capture*`, `/alerts`, `/alerts/ack`) accepts unauthenticated form-encoded bodies. In STA mode the base is reachable on the LAN, and form POSTs are "simple requests" — no CORS preflight — so any web page in the operator's browser can silently forge them (CSRF). Worst case: `POST /wifi` with attacker-chosen credentials persists them to NVS (`src/wifi_manager.cpp:213-225`), permanently moving the base station onto the attacker's network on next boot (a persistence mechanism, not just a one-shot act). Combined with WR-05, the AP surface is likewise open to anyone who has read the source.

**Fix:** Minimum viable hardening — verify the Origin header on every POST (plus a fallback same-host check), e.g.:

```cpp
// in setup: const char* hdrs[] = {"Origin"}; server.collectHeaders(hdrs, 1);
bool requestAuthorized() {
    if (!server.hasHeader("Origin")) return true;          // non-browser clients (curl, tests)
    String o = server.header("Origin");
    String ap = "http://" + WiFi.softAPIP().toString();
    String st = "http://" + WiFi.localIP().toString();
    return o.startsWith(ap) || o.startsWith(st);
}
// guard each POST handler: if (!requestAuthorized()) { server.send(403, ...); return; }
```

A per-boot session token issued to the page and echoed in POSTs would be the stronger fix.

### WR-05: Hardcoded AP credentials visible in source

**File:** `src/wifi_manager.cpp:16-17`
**Issue:** `WIFI_AP_SSID = "Cosmic1-BaseStation"` / `WIFI_AP_PASSWORD = "balloontrack"` are compile-time constants (documented D-40 design: the lockout-proof fallback must never depend on NVS or the UI). That trade-off is legitimate, but the consequence should be stated as an accepted risk: anyone who has seen the repo (or flashed firmware) can join the fallback AP, and via WR-04 that grants full device control — camera commands, WiFi reconfiguration, alert state. The AP is the *admin* fallback, so the known password is the entire security boundary.

**Fix:** Not a code change so much as risk closure: at minimum append a per-device suffix (e.g. last 2 MAC bytes rendered into the SSID, password left documented), or keep the constants and explicitly record the accepted risk plus the WR-04 Origin check as the compensating control in the phase docs.

### WR-06: Sidecar short-write does not trigger storage degradation — `/api/state` keeps reporting OK

**File:** `src/sd_storage.cpp:374-380`
**Issue:** `writeSidecar` detects a short write (`written != json.length()`) and returns false, but unlike the image-chunk write path it never marks storage degraded. A card filling up mid-flight can therefore fail every *sidecar* write (the last few hundred bytes of each transfer) while `SdStorage::available` stays true and the storage chip in `/api/state` keeps reporting healthy — contradicting the phase's "stop storing and surface it" policy (image bytes may even still be landing; only metadata is silently lost, which is exactly what CR-01 shows is expensive to reason about later).

**Fix:** Route the failure through the same degradation path used for image write failures so `/api/state` reports it:

```cpp
size_t written = f.print(json);
if (written != json.length()) {
    Serial.printf("SdStorage: sidecar write short (%u/%u) — degrading\n", ...);
    degrade("sidecar write short");   // same policy as chunk-write failures
    return false;
}
```

### WR-07: Balloon-side battery logic runs on fabricated dummy data — low-power failsafe is inert

**File:** `src/main_balloon.cpp:629, 707, 818-820`
**Issue:** `updateSystemState` and `processPowerManagement` both build `PowerData powerData = {3.7f, 0.1f, 85, millis(), true}` — hardcoded "85%, 3.7 V, valid". Every balloon-side consumer of battery truth sees a healthy battery forever: the `BATTERY_CRITICAL_THRESHOLD` emergency path, the low-battery camera-disable logic, and the legacy telemetry packet's `batteryVoltage = 3.7f` (line 818) which publishes fabricated voltage to the ground. Only the base-side ALRT-02 alert sees the real D-41 beacon voltage. The balloon's own failsafe (the thing that must act when the ground link is dead) can never fire.

**Fix:** Read real values from the power manager at all three sites, e.g. `PowerData powerData = PowerMgr().read();` (or `getBatteryPercentage()` / `getBatteryVoltage()` per the existing API), and mark validity from the sensor read — never synthesize a valid-looking constant.

### WR-08: `WiFiManager::requestSwitch` performs no module-side credential bounds validation despite the header contract

**File:** `src/wifi_manager.h:35-37`, `src/wifi_manager.cpp:129-159`
**Issue:** The header documents `WIFI_STA_SSID_MAX_LEN` / `WIFI_STA_PASS_MIN_LEN` / `WIFI_STA_PASS_MAX_LEN` as "shared by POST /wifi and the module", but the module never references them — the only validation lives in `handleWifiSwitch` (`src/main_basestation.cpp:2904-2906`). Any future caller of `requestSwitch` (test harness, serial console, a second route) bypasses the bounds entirely, and an over-length SSID gets persisted to NVS and fed to `WiFi.begin`. The documented defense-in-depth contract is not implemented where it claims to live.

**Fix:** Enforce the bounds inside the module:

```cpp
if (mode == WifiMode::STATION
        && (ssid.length() < 1 || ssid.length() > WIFI_STA_SSID_MAX_LEN
            || pass.length() < WIFI_STA_PASS_MIN_LEN
            || pass.length() > WIFI_STA_PASS_MAX_LEN)) {
    return false;
}
```

## Info

### IN-01: Stale protocol comment — telemetry beacon body length

**File:** `include/command_protocol.h:257`
**Issue:** Comment says "Telemetry beacon (0x14): header bodyLen = 17" — the body has been 19 bytes since the D-41 battery extension (`include/image_protocol.h:107`). A future maintainer sizing a buffer from the comment gets it wrong.
**Fix:** Update the comment to 19 (or drop the literal and reference the struct).

### IN-02: Stale queue comment — eviction policy description

**File:** `include/image_protocol.h:59-61`
**Issue:** "overflow drops the OLDEST entry" — `ImageTxManager` actually performs class-ranked eviction (QUEUED fulls before armable thumbnails, etc.). The comment describes superseded behavior.
**Fix:** Reword to "overflow evicts by class rank (see ImageTxManager::evictEntriesOlderThan / enqueueCapture)".

### IN-03: Inaccurate boot-join comment — no interface serves during a boot-time STA join

**File:** `src/wifi_manager.cpp:66-68`
**Issue:** The comment claims "Either way a serving interface exists ... the AP comes up immediately". During a *boot-time* STA join (unlike the runtime switch, which keeps the AP up) the radio is STA-only for up to 20 s with no address — the `getStatus()` else-branch (lines 199-205) correctly admits "nothing is serving yet". Doc/behavior mismatch on an operator-visible window.
**Fix:** Correct the comment: boot-time joins run STA-only with no serving interface for up to `WIFI_JOIN_TIMEOUT_MS`, then fall back to the AP.

### IN-04: Counter truncation/sequence-wrap staleness in long sessions

**File:** `src/main_basestation.cpp:2063-2066, 3027-3028`
**Issue:** `static_cast<uint16_t>(acked) > appState.ackedAtLastPoll` truncates a uint32 cumulative counter to 16 bits — after 65 535 acked commands the LED/latch comparison misbehaves on wrap; `autoCaptureAckSeq` has the same uint16 sequence-wrap edge (a stale latch until the sequence re-passes the old value). Unlikely within a single flight at 5 s polls, but these are long-session correctness edges.
**Fix:** Keep both fields uint32 (drop the cast), or compare with wrap-aware subtraction.

### IN-05: GET handler mutates application state

**File:** `src/main_basestation.cpp:3021-3034`
**Issue:** `handleApiState` (a GET) updates `appState.autoCaptureAckSeq` / clears the pending-transition latch as a side effect of *rendering*. Benign under the single-threaded loop, but it makes the poll a state machine participant — a skipped poll (network drop) loses the transition edge.
**Fix:** Move latch updates into the main loop's periodic state refresh; let handlers only read.

### IN-06: Vendor fetch uses trust-on-first-use pinning

**File:** `scripts/embed_web_assets.mjs:49-77`
**Issue:** `fetchPinned` records the sha256 of whatever it downloads into `vendor/provenance.json`; there is no independently pinned expected hash, so a compromised first delivery (CDN or DNS) is silently locked in as the "trusted" value. Subsequent regenerations do verify against the recorded hash (lines 104-108), which is the real protection.
**Fix:** Document the TOFU model in the header comment, or hand-pin the sha256 of leaflet 1.9.4 (published checksums exist) and have `fetchPinned` verify against it before writing provenance.

### IN-07: `deserializeCommand` silently zeroes the payload on length mismatch instead of rejecting the frame

**File:** `src/command_protocol.cpp:127-139`
**Issue:** If a frame's declared `payloadLength` exceeds the bytes actually present (corruption, truncation), the code sets `cmd.payloadLength = 0` and still returns the command as valid — the handler then sees a payload-less variant of a command that declared a payload (usually NACKed downstream as a param error rather than a framing error, muddying diagnostics).
**Fix:** Return false when `payloadOffset + cmd.payloadLength > length - 4` — a truncated frame is a framing error.

### IN-08: `ResponseStatusData` native-struct memcpy is a documented little-endian/padding island

**File:** `src/command_protocol.cpp:277-290`, `include/command_protocol.h:169-189`
**Issue:** `sendStatusResponse` memcpy's the native struct (`resp.data, &status, resp.dataLength`). The header documents the field order/padding reasoning and `static_assert(sizeof(StatusResponseData) == 32)`-style bounds hold (32 ≤ 50), so this is intentional — but it is the one place in the protocol where wire format depends on target layout. Kept as awareness only.
**Fix:** None required; if the protocol ever gains a third participant (e.g. a Python ground tool), replace with explicit field-wise serialization as done everywhere else.

---

_Reviewed: 2026-08-19T17:50:25Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

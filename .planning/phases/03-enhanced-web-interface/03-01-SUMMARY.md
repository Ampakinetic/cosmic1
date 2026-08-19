---
phase: "03"
plan: "01"
subsystem: "base-station-web-ui"
tags: [dashboard, web-ui, telemetry, poll, protocol]
requires:
  - "Phase 1 command UI (cards, Command Queue D-16, link LED IN-03)"
  - "Phase 2 transfer pipeline (0x12/0x13/0x14 frames, transfer rows D-20)"
provides:
  - "D-45 single-page dashboard layout (section-nav / map / capture / queue)"
  - "GET /api/state combined JSON serializer (/status aliased to same handler)"
  - "5s poll ladder with 5->15->30s backoff + stale badge (D-35)"
  - "Six-tile telemetry panel fed by the 0x14 beacon snapshot (WEB-01)"
  - "0x14 beacon body 17->19 bytes: batteryMilliV BE16 + flags bit1 batteryValid"
affects:
  - "src/main_basestation.cpp (page restructure, serializer, poll script)"
  - "include/image_protocol.h, src/command_protocol.cpp (beacon wire format — both sides)"
  - "src/image_tx_manager.cpp, src/main_balloon.cpp (balloon beacon populate)"
  - "include/image_rx_manager.h, src/image_rx_manager.cpp (base snapshot)"
  - "scripts/verify_protocol_roundtrip.mjs (beacon assertions)"
tech-stack:
  added: []
  patterns:
    - "settle-chained poll scheduler (next fetch scheduled only after previous settles)"
    - "diff-checked DOM writes (dataset gates, list signatures, textContent-only)"
    - "harness-first atomic protocol change (RED -> GREEN across transcription + firmware)"
key-files:
  created: []
  modified:
    - "src/main_basestation.cpp"
    - "include/image_protocol.h"
    - "src/command_protocol.cpp"
    - "src/image_tx_manager.cpp"
    - "src/main_balloon.cpp"
    - "include/image_rx_manager.h"
    - "src/image_rx_manager.cpp"
    - "scripts/verify_protocol_roundtrip.mjs"
key-decisions:
  - "Battery tile JS written in final battery-aware form during Task 1 — renders em-dash identically under Task 1 JSON (no battery key), so Task 2 needed only the JSON fields"
  - "Beacon battery truth gated on voltage in [1.8, 8.0] V AND nonzero raw analogRead(BATTERY_SENSE_PIN) — floating ADC never reports a fake pack"
  - "PowerMgr().update() wired on a 1s millis timer in processPowerManagement (D-26 idiom); the stale 'method doesn't exist' comment replaced — update() exists at power_manager.cpp"
metrics:
  duration: 23min
  completed: 2026-08-20
  status: complete
requirements-completed:
  - WEB-01
  - WEB-03
  - WEB-04
  - ALRT-02
actuals:
  tokens: 14291   # chars/4 over the realized src/include/scripts diff vs phase base a5e6290
  tasks: 2
  commits: 2
coverage:
  - id: D1
    description: "D-45 single-page dashboard: sticky section-nav (Map/Capture/Queue), locked card order, map placeholder frame, file-wide design-token harmonization"
    requirement: WEB-04
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation — SUCCESS (PROGMEM page compiles)"
        status: pass
      - kind: other
        ref: "greps on src/main_basestation.cpp: section-nav/map-frame present; no forbidden font-size (11|12|13|15|20)px anywhere in file"
        status: pass
    human_judgment: true
    rationale: "Visual layout, sticky-nav behavior, and spacing harmonization need browser eyes on the flashed base station; structure is build- and grep-verified only"
  - id: D2
    description: "GET /api/state combined serializer (one payload: telemetry + link + queue + transfers + storage; /status aliased) with 5s poll, 5->15->30s backoff, amber stale badge, silent recovery"
    requirement: WEB-03
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation — SUCCESS"
        status: pass
      - kind: other
        ref: "greps: '/api/state' route registered to handleApiState, POLL_INTERVAL_MS/POLL_BACKOFF_STEPS constants, no 'updateStatus, 1000'"
        status: pass
    human_judgment: true
    rationale: "Runtime poll/backoff/badge behavior executes in the browser against live WiFi; only static structure is automated"
  - id: D3
    description: "Six-tile telemetry panel (Link/Altitude/Temperature/GPS/Battery/Data age) with honest-null empty states and counting age from client-side poll timing"
    requirement: WEB-01
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation — SUCCESS"
        status: pass
    human_judgment: true
    rationale: "Tile rendering against live 0x14 beacons requires flashed hardware; empty-state copy is server-rendered and build-verified"
  - id: D4
    description: "0x14 beacon battery extension: body 17->19 bytes (batteryMilliV BE16 + flags bit1), balloon PowerMgr-gated populate, base snapshot + JSON fields, legacy 17-byte frame rejection"
    requirement: ALRT-02
    verification:
      - kind: unit
        ref: "node scripts/verify_protocol_roundtrip.mjs — 49/49 PASS incl. (img-e) 30-byte length, batteryMilliV round-trip, hand-built legacy 17-byte rejection (RED captured first)"
        status: pass
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation — both SUCCESS"
        status: pass
    human_judgment: false
---

# Phase 03 Plan 01: Dashboard Tracer Summary

**Single-page D-45 dashboard with /api/state 5s-poll (backoff + stale badge), six-tile telemetry panel, and the 0x14 beacon extended 17->19 bytes to carry battery voltage for ALRT-02.**

## Accomplishments

### Task 1 — Dashboard tracer (commit 6064854)
- Restructured the PROGMEM page into the D-45 single-page layout: sticky `.section-nav` (Map/Capture/Queue only), then `#map` (telemetry panel + map placeholder), `#capture` (Manual Capture, Camera Settings, Auto-Capture, Event Capture), `#queue` (Command Queue, Image Transfers, Latest Capture last).
- `handleStatus` renamed to `handleApiState`; `/api/state` registered with `/status` aliased to the same serializer (one payload, two paths); `json.reserve(4096)`.
- Poll rewritten: `POLL_INTERVAL_MS = 5000`, `POLL_BACKOFF_STEPS = [5000, 15000, 30000]`, settle-chained scheduling (next fetch only after previous settles — no overlap), amber stale badge pinned in the Link tile ("Stale — data N s old · retrying every 15|30 s"), silent recovery snapping to 5s.
- Six-tile telemetry panel on the auto-fit grid; absent telemetry renders the exact copy "No telemetry received yet"; age tile counts client-side between polls (1s ticker — the only periodic work).
- Diff-checked rendering throughout: setText/setClass/setColor guards, queue/transfer list rebuilds gated on content signatures, thumbnail swap gated on dataset.id, textContent-only DOM writes.
- File-wide token harmonization: paddings 24px, message padding 16px, fonts restricted to {14,16,18,24}px, transfer-row/kind/chip spacing aligned; the superseded telemetry-chip was dropped.

### Task 2 — 0x14 beacon battery extension (commit 8e2426b, harness-first)
- RED: harness constant 17->19, length assertion 28->30, new battery round-trip clause (batteryMilliV 4200, flags 0x03) and legacy 17-byte rejection clause — run captured 3 expected failures before any firmware edit.
- GREEN, atomic across harness + firmware: `TelemetryBeaconBody` gains `batteryMilliV` (BE16, bytes 17-18; flags stays offset 16, bit1 = batteryValid); serializer/deserializer mirror the field in `command_protocol.cpp` and the node transcription.
- Balloon (`image_tx_manager.cpp`): populates mV via public `PowerMgr().getBatteryVoltage()` gated on voltage in [1.8, 8.0] V AND nonzero `analogRead(BATTERY_SENSE_PIN)`; else 0 + bit1 clear.
- `main_balloon.cpp` `processPowerManagement()` now calls `PowerMgr().update()` on a 1s millis timer (the beacon's cached voltage is only as fresh as the last update) — replacing the stale "Method doesn't exist" comment (update() exists at power_manager.cpp).
- Base: `TelemetrySnapshot` gains `batteryMv`/`batteryValid`, populated in `onTelemetryBeaconFrame` (flags & 0x02); `/api/state` emits both; the Battery tile renders volts or an honest em-dash.
- Balloon-side diff vs phase base confined to `main_balloon.cpp`, `image_tx_manager.cpp`, and shared protocol files — the one permitted balloon-side change per the D-41 amendment.

## Deviations from Plan

None — plan executed exactly as written. (Note: the Battery tile's battery-aware JS expression was written during Task 1 in its final form; it renders the identical em-dash under Task-1 JSON, so Task 2's diff shrank by that one line. No behavior difference at either commit.)

## Intentional Placeholders (not defects)

- `#map` contains only the waiting frame ("Waiting for GPS fix — the track appears once the balloon reports a valid position.") — the Leaflet/OSM map itself is plan 03-02's deliverable (WEB-02), by design.
- The Battery tile shows "—" until a beacon with flags bit1 arrives — the honest absent state, not a stub.

## Verification

- `pio run -e esp32-s3-basestation` — SUCCESS (both commits).
- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS (Task 2).
- `node scripts/verify_protocol_roundtrip.mjs` — 49/49 PASS (Task 2: RED 3 expected failures captured first, then green).
- Positive/negative greps on `src/main_basestation.cpp` — all pass (`/api/state`, `POLL_INTERVAL_MS`, `section-nav`, `map-frame`, `Stale`; no `updateStatus, 1000`; no forbidden font-size tokens file-wide).
- Per-file greps: `batteryMilliV` (protocol + tx), `batteryValid`/`batteryMv` (rx + base JSON), `PowerMgr().update()` (main_balloon.cpp:703).
- `git diff --name-only a5e6290 -- src/ include/` — 7 files, balloon-side changes limited to the permitted set.

## Auth Gates

None.

## Commits

- 6064854 — feat(03-01): D-45 single-page dashboard + /api/state + 5s poll ladder
- 8e2426b — feat(03-01): 0x14 beacon battery extension — 17->19 bytes, harness-first atomic change-set

## Self-Check: PASSED

All 8 modified files plus the SUMMARY exist on disk; both commit hashes (6064854, 8e2426b) present in git history.

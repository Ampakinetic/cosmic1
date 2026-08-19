# Phase 3: Enhanced Web Interface - Context

**Gathered:** 2026-08-19
**Status:** Ready for planning

<domain>
## Phase Boundary

This phase delivers **the full base station mission-control panel**: a top-down web dashboard with live telemetry, an OpenStreetMap view with full-flight trajectory, a paginated image gallery reading the Phase 2 SD-card contract, a base-side alert system covering all six alert types (ALRT-01..06), and runtime AP/Station WiFi mode switching. It builds entirely on the Phase 1 control UI and Phase 2 transfer pipeline already in `src/main_basestation.cpp` — this phase is base-station-local (plus one possible telemetry-beacon payload addition, see D-41 note), with **zero changes to balloon behavior, the command protocol's existing semantics, or the Phase 2 transfer machinery**. It does NOT add new LoRa capabilities, new capture modes, or any v2 feature (video, multi-balloon, satellite).

</domain>

<decisions>
## Implementation Decisions

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

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Planning
- `.planning/PROJECT.md` — Core value, constraints (240-byte packets, telemetry priority, single-threaded loop, PSRAM), locked key decisions (5s update rate, top-down layout, OSM)
- `.planning/REQUIREMENTS.md` — Phase 3 owns WEB-01..05, IMG-06, ALRT-01..06
- `.planning/ROADMAP.md` — Phase 3 scope, success criteria, deliverables

### Prior Phases (contracts this phase builds on)
- `.planning/phases/01-command-protocol-control/01-CONTEXT.md` — D-01..D-16 (note: D-13 superseded by D-45 here)
- `.planning/phases/02-image-transmission/02-CONTEXT.md` — **D-29..D-32 SD-card gallery contract** (`IMG_{id}.JPG`, `IMG_{id}_T.JPG`, kind-suffixed sidecars), D-24 incomplete-finalization semantics, D-26 threshold-UI pattern
- `.planning/phases/01-command-protocol-control/01-UI-SPEC.md` — Phase 1 UI design contract (spacing/typography harmonization, 480px collapse behavior)
- `.planning/phases/02-image-transmission/02-UI-SPEC.md` — Phase 2 UI additions (transfer progress rows, event-capture card)

### Source Files (integration points)
- `src/main_basestation.cpp` — The existing web UI this phase restructures: settings forms, Command Queue panel (D-16), transfer progress (D-20), link LED (IN-03), `/img/{id}_t.jpg` serving
- `include/wifi_config.h` — Current WiFi setup that D-40's runtime toggle extends
- `include/common_types.h` + `include/command_protocol.h` — 0x14 telemetry beacon packet definition (field inventory for D-41/ALRT-02)
- `src/e32_lora.cpp` — E32 receiver where RSSI/SNR for ALRT-06 are observed
- `src/system_state.h` / `src/system_state.cpp` — Flight-phase machine (context for landing-detection semantics; balloon-side, informational)

### Codebase Maps
- `.planning/codebase/CONVENTIONS.md` — Naming, singleton accessors, error-handling patterns
- `.planning/codebase/STACK.md` — PlatformIO dual-target build, library set
- `.planning/codebase/STRUCTURE.md` — Where new code goes (web pages: `include/camera_index.h` + `src/app_httpd.cpp` conventions)

NOTE: codebase maps predate Phases 1–2 — where they disagree with source or STATE.md, the source and STATE.md are authoritative (same caveat as 02-CONTEXT).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Base web UI skeleton** (`src/main_basestation.cpp`) — cards, forms, queue panel, transfer progress, link LED: the dashboard reorganizes these rather than replacing them
- **SD-sidecar gallery data** — D-29..D-32 contract means the gallery needs no new state: enumerate images, read `IMG_{id}.JSON` / `IMG_{id}_T.JSON` for metadata/completeness
- **Threshold-settings pattern** (Phase 2 D-26) — Alerts card clones the event-thresholds card shape (form + persist + display balloon/base-reported truth)
- **0x14 telemetry beacon receiver state** — WEB-01's data source already arrives every 5s
- **E32 driver** — RSSI/SNR available at the receiver for ALRT-06/signal-quality display
- **ESP32 NVS/Preferences** — persistence substrate for WiFi credentials (D-40) and alert thresholds (D-43)

### Established Patterns
- No-fabricated-state: UI shows only verified truth (extends to stale badge, alerts, incomplete badges)
- Settings form + ACK + truthful status display
- Singleton accessors (`Sensors()`, `LoRaComm()`, …); manager classes with `begin()/update()` lifecycle
- Single-threaded event loop — HTTP handlers must not block; time-slice all periodic work
- Terminal-state vocabulary and anti-flap cooldowns (informs D-44 alert classes)

### Integration Points
- `/api/state` route (new) — one serializer aggregating telemetry, link status, queue, transfers, trajectory, WiFi mode
- Gallery routes (new) — paginated SD enumeration + per-image detail (metadata from sidecars)
- WiFi manager (new or extended) — NVS-backed mode/credentials with AP fallback
- Alert engine (new, base-local) — evaluates on each received telemetry beacon, feeds banner state into `/api/state`

</code_context>

<specifics>
## Specific Ideas

No specific requirements — all decisions were selections from presented options, no freeform additions.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 3-Enhanced Web Interface*
*Context gathered: 2026-08-19*

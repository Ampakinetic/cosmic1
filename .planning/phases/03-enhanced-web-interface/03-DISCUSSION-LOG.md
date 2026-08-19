# Phase 3: Enhanced Web Interface - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-19
**Phase:** 3-Enhanced Web Interface
**Areas discussed:** Update mechanism, Map & offline behavior, Alert system design, Layout & gallery details

---

## Update mechanism

| Option | Description | Selected |
|--------|-------------|----------|
| JSON polling (recommended) | Page fetches a state endpoint every 5s. Zero new server machinery — extends existing route patterns in main_basestation.cpp. | ✓ |
| SSE (server-sent events) | One long-lived chunked connection; hand-rolled on classic WebServer, moderate risk. | |
| WebSocket | Bidirectional push; requires ESPAsyncWebServer/library swap — biggest change to a proven stack. | |

**User's choice:** JSON polling (recommended) → **D-33**

| Option | Description | Selected |
|--------|-------------|----------|
| One /api/state (recommended) | Single JSON with telemetry + link + queue + transfers; one request per cycle. | ✓ |
| Per-panel endpoints | Parallel fetches, per-panel cadence, 3-4x requests per cycle. | |
| You decide | Planner picks split from payload measurements. | |

**User's choice:** One /api/state (recommended) → **D-34**

| Option | Description | Selected |
|--------|-------------|----------|
| Back off + stale badge (recommended) | 5s→15s→30s on failures, visible stale badge + data age, snap back on recovery. | ✓ |
| Steady 5s always | Keep polling at 5s regardless; simplest. | |
| Back off + pause when hidden | Same backoff plus stop polling on hidden tab (visibilityState). | |

**User's choice:** Back off + stale badge (recommended) → **D-35**

| Option | Description | Selected |
|--------|-------------|----------|
| Same cycle, diff-checked (recommended) | Everything rides /api/state; DOM re-renders only on changed values; gallery refreshes on count change. | ✓ |
| Event-driven refresh | State-version bump triggers re-fetch; adds versioning convention. | |
| You decide | Planner picks from payload/render measurements. | |

**User's choice:** Same cycle, diff-checked (recommended) → **D-36**

---

## Map & offline behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Degrade to offline plot (recommended) | Leaflet+OSM online; auto-scaled canvas trajectory plot offline (AP mode). Field-usable without tile caching. | ✓ |
| Tiles or placeholder | Offline shows numeric lat/lon/altitude only; simplest, loses spatial awareness in the chase scenario. | |
| Pre-cached tiles on SD | Cache region tiles before flight; best fidelity, region must be pre-selected, adds cache tooling. | |

**User's choice:** Degrade to offline plot (recommended) → **D-37**

| Option | Description | Selected |
|--------|-------------|----------|
| Full flight, capped ring (recommended) | Ring buffer of every GPS fix since boot (~500-1000 pts) sent in /api/state. | ✓ |
| Sliding window | Last N minutes only; loses launch point on long flights. | |
| You decide | Planner sizes buffer from payload/heap measurements. | |

**User's choice:** Full flight, capped ring (recommended) → **D-38**

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-follow + override (recommended) | Re-centers each update; drag cancels follow, recenter button resumes. | ✓ |
| Always auto-center | Locks to balloon; no free panning. | |
| Free pan + recenter button | Never auto-moves. | |

**User's choice:** Auto-follow + override (recommended) → **D-39**

| Option | Description | Selected |
|--------|-------------|----------|
| Runtime toggle + NVS (recommended) | WiFi card in UI; station credentials in NVS; auto-fallback to AP after ~20s so lockout is impossible. | ✓ |
| Boot-time only | Config file/compile flag; re-flash to change networks. | |
| Dual mode always | AP+STA simultaneously; most convenient, least stable ESP32 WiFi configuration. | |

**User's choice:** Runtime toggle + NVS (recommended) → **D-40**

---

## Alert system design

| Option | Description | Selected |
|--------|-------------|----------|
| Base-side from telemetry (recommended) | All six evaluated from received 0x14 beacon + local RSSI/SNR + fix age + altitude deltas. Zero balloon airtime. Researcher confirms battery voltage in beacon. | ✓ |
| Balloon-side packets | Balloon detects and sends alert packets; adds protocol types + airtime. | |
| Hybrid | Base evaluates; balloon flight-phase machine rides beacon for landing detection. | |

**User's choice:** Base-side from telemetry (recommended) → **D-41**

| Option | Description | Selected |
|--------|-------------|----------|
| Banner + browser beep (recommended) | Color-coded persistent banners + Web Audio beep for new criticals; mute toggle per session. Browser is the speaker — no hardware. | ✓ |
| Visual only | Banners/badges only; easy to miss during a chase. | |
| Visual + relay/buzzer pin | Physical buzzer GPIO on the base; loudest, adds hardware not in the parts list. | |

**User's choice:** Banner + browser beep (recommended) → **D-42**

| Option | Description | Selected |
|--------|-------------|----------|
| Defaults + UI override (recommended) | Flight defaults + Alerts settings card, NVS-persisted (Phase 2 D-26 pattern); base-local, no LoRa traffic. | ✓ |
| Fixed defaults | Compile-time constants; re-flash to re-tune. | |
| You decide | Planner proposes defaults; UI configurability by remaining effort. | |

**User's choice:** Defaults + UI override (recommended) → **D-43**

| Option | Description | Selected |
|--------|-------------|----------|
| Two classes (recommended) | Safety-critical latch until acknowledged; informational auto-clear with cooldown. | ✓ |
| All auto-clear | Simplest; transient conditions can erase the only trace. | |
| All latch + ack | Nothing lost; constant dismissing on bumpy descents. | |

**User's choice:** Two classes (recommended) → **D-44**

---

## Layout & gallery details

| Option | Description | Selected |
|--------|-------------|----------|
| One scrolling dashboard (recommended) | Single page: alerts → map/telemetry → capture/settings → queue/transfers → gallery; sticky nav. Supersedes Phase 1 D-13. | ✓ |
| Dashboard + control subpage | Preserves D-13 literally; capture one click further away. | |
| Tabbed sections | Compact; hides queue/progress behind a tab. | |

**User's choice:** One scrolling dashboard (recommended) → **D-45** (supersedes 01 D-13)

| Option | Description | Selected |
|--------|-------------|----------|
| 12 per page, grid (recommended) | Thumbnail grid, 12/page, newest first, numbered pagination; light payload/DOM. | ✓ |
| 24 per page, grid | More visible at once; bigger page payload. | |
| You decide | Planner sizes from measured thumbnails/render perf. | |

**User's choice:** 12 per page, grid (recommended) → **D-46**

| Option | Description | Selected |
|--------|-------------|----------|
| Detail view + metadata (recommended) | Dedicated view: full image (or thumbnail) + sidecar metadata (time, altitude, GPS, trigger, settings, RSSI, completeness). D-30 built for this. | ✓ |
| Lightbox overlay | In-place pop-over; metadata needs separate expand. | |
| You decide | Planner picks by implementation weight. | |

**User's choice:** Detail view + metadata (recommended) → **D-47**

| Option | Description | Selected |
|--------|-------------|----------|
| Show with badge (recommended) | Incomplete images visible with badge; detail shows only verified data. Extends no-fabricated-state. | ✓ |
| Filter toggle | Show-with-badge default + hide-incompletes toggle. | |
| Hide them | Only complete images; silently drops history holes. | |

**User's choice:** Show with badge (recommended) → **D-48**

---

## Claude's Discretion

- Trajectory ring-buffer cap; backoff schedule values; default alert thresholds; beep pattern/mute persistence
- `/api/state` field layout; gallery route shape; NVS key naming
- Leaflet assets embedded in firmware (not CDN) so the page works offline in AP mode — only OSM tiles may be internet-dependent
- Offline canvas-plot rendering details; beacon-receiver state exposure to the UI

## Deferred Ideas

None — discussion stayed within phase scope.

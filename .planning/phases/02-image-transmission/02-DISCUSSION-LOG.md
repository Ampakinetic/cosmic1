# Phase 2: Image Transmission - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-19
**Phase:** 2-Image Transmission
**Areas discussed:** Transfer model (push vs pull), Chunk reliability strategy, Event-based capture triggers, SD storage & metadata format

---

## Transfer Model (push vs pull)

| Option | Description | Selected |
|--------|-------------|----------|
| Hybrid: push thumb, pull full | Balloon pushes thumbnail immediately (meets IMG-02's 10s window, no round-trip), then sends a manifest; base station requests full-image chunks when the link is quiet. Reuses Phase 1 command/response; base controls bandwidth. | ✓ |
| Full push (balloon streams) | After capture, balloon streams thumbnail then full image automatically. Simplest balloon logic — but base can't pause/skip/prioritize; resume logic lives on balloon. | |
| Full pull (base requests all) | Balloon only announces captures; base requests thumbnail AND full image. Maximum control, but every thumbnail pays a round-trip against the 10s window. | |

**User's choice:** Hybrid: push thumb, pull full
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Automatic, radio priority only | Existing priority queue guarantees telemetry preempts image chunks (PRI-01). Pull starts automatically on manifest; no operator controls. | ✓ |
| Automatic + operator pause/resume | Plus explicit transfer controls in the web UI (pause/resume/skip). More agency, more UI + command types. | |
| Adaptive rate by link quality | Base paces chunk requests by RSSI/packet loss — slows on degraded links. Most graceful under PRI-03, most complex. | |

**User's choice:** Automatic, radio priority only
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| FIFO queue, finish in order | New thumbnail still pushes immediately; full-image pulls complete in capture order. No wasted airtime, gallery ordered. | ✓ |
| Newest wins, supersede | New capture abandons in-progress pull — freshest wins; wastes airtime already spent. | |
| Bounded FIFO, drop oldest | FIFO with pending cap; oldest un-started images dropped and marked skipped on burst. | |

**User's choice:** FIFO queue, finish in order
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Percent + chunk counts | Per-image row: percent + chunks received/total, from actual chunk accounting. Extends D-16 Command Queue pattern; honest by construction. | ✓ |
| State badge only | queued / transferring / complete / incomplete per image. Least UI work, hides real progress. | |
| Detailed chunk map | Percent + counts + per-chunk received/missing strip. Diagnostic-grade; most UI effort. | |

**User's choice:** Percent + chunk counts
**Notes:** No-fabricated-state prohibition explicitly applied.

---

## Chunk Reliability Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Windowed request/response | Base requests a window of chunks (~16); re-requests corrupt/missing before advancing. Clean ARQ, bounded balloon memory, exact progress. | ✓ |
| Blast once, then NACK holes | Balloon blasts entire image on one request; base NACKs missing. Fewer round-trips, needs whole image in balloon PSRAM. | |
| Best-effort, no retransmit | Each chunk once; holes stay holes; marked incomplete. Minimum protocol; guaranteed losses on real links. | |

**User's choice:** Windowed request/response
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Push, re-request if holed | Thumbnail pushes once; if holed, base falls back to the same windowed-pull mechanism. One reliability mechanism system-wide. | ✓ |
| Proactive double-send | Thumbnail chunks sent twice. Robust, zero round-trips; 2x airtime on every capture. | |
| Best-effort, holes tolerated | One-shot; partial until full image supersedes. Cheapest; IMG-02 can silently fail. | |

**User's choice:** Push, re-request if holed
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| End-to-end CRC32 in manifest | Manifest carries CRC32 over the entire image; verified after reassembly. Catches reassembly errors per-chunk CRC cannot. | ✓ |
| Chunk accounting only | All chunks + valid chunk CRCs = complete. Misordered/duplicated chunk passes silently. | |
| CRC32 + MD5 digest | Plus MD5 for post-hoc provenance vs balloon-stored images. Strongest, costs CPU + bigger manifest. | |

**User's choice:** End-to-end CRC32 in manifest
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Bounded rounds, keep partial | Bounded retransmit passes (~3); then finalized incomplete — kept on SD, marked, slot freed. | ✓ |
| Retry until superseded | No budget; keeps re-requesting until complete or superseded. One bad image can stall the FIFO. | |
| Discard incomplete | Partials discarded, not stored. Cleanest gallery; throws away spent airtime and link evidence. | |

**User's choice:** Bounded rounds, keep partial
**Notes:** —

---

## Event-Based Capture Triggers

| Option | Description | Selected |
|--------|-------------|----------|
| Altitude + location only | Exactly the success-criteria minimum: altitude delta + horizontal distance delta. | |
| Deltas + flight phases | Deltas PLUS flight-phase transitions — SystemState already computes them; one comparison each. | ✓ |
| Deltas + phases + time net | Plus time-based safety net (capture if nothing fired in N minutes). | |

**User's choice:** Deltas + flight phases
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed defaults, no UI | Compile-time thresholds only. No new commands; reflashing to tune. | |
| UI-configurable | Thresholds adjustable from base web UI via new SET_EVENT_TRIGGER commands + settings card — consistent with Phase 1's 7 settings forms. | ✓ |
| Enable/disable toggles only | Fixed defaults + remote per-event enable/disable. Middle ground. | |

**User's choice:** UI-configurable
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Event resets interval | Both modes coexist (PROJECT.md hybrid); event capture resets interval baseline — no double-capture burst near deadline. | ✓ |
| Fully independent | Independent baselines; occasional redundant capture pair. | |
| Mutually exclusive modes | Interval OR event per flight. Contradicts PROJECT.md's logged hybrid decision. | |

**User's choice:** Event resets interval
**Notes:** PROJECT.md hybrid auto-capture decision confirmed as pre-locked. |

| Option | Description | Selected |
|--------|-------------|----------|
| Global min spacing | Minimum spacing between any two automatic captures (15–30 s) regardless of source. | ✓ |
| No guard | Every threshold crossing fires immediately; noisy GPS can machine-gun captures. | |
| Spacing + per-event cooldowns | Plus per-event-type rate limits. Tightest control, most state. | |

**User's choice:** Global min spacing
**Notes:** —

---

## SD Storage & Metadata Format

| Option | Description | Selected |
|--------|-------------|----------|
| Image ID | `IMG_0042.JPG` — balloon-assigned ID, same as manifest/queue/GET_STATUS. Survives clock issues. | ✓ |
| Receive timestamp | Base-clock-based names; decouples from protocol ID. | |
| ID + timestamp hybrid | Both in filename; longest names. | |

**User's choice:** Image ID
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Sidecar JSON per image | `IMG_0042.JSON`: capture time, altitude, GPS, trigger source, camera settings, RSSI, chunk stats, completeness. Never touches image bytes. | ✓ |
| Embedded header | Fixed-size header before JPEG in same file. Atomic; every consumer must know the offset. | |
| Central index file | One manifest for all images. Fewer files; single corruption point. | |

**User's choice:** Sidecar JSON per image
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Separate thumb + full files | `IMG_0042_T.JPG` at thumbnail completion; `IMG_0042.JPG` when pull finishes. Matches two-stage transfer. | ✓ |
| Replace thumb with full | Single eventual file; replace risks torn reads mid-serving. | |
| No stored thumbnail | Store full only; UI regenerates previews. ESP32 downscaling cost per view. | |

**User's choice:** Separate thumb + full files
**Notes:** —

| Option | Description | Selected |
|--------|-------------|----------|
| Flat directory | One images directory (+ `_T` suffix). Simplest Phase 3 serving, no clock logic. | ✓ |
| Per-flight-date folders | Tidier across flights; clock-dependent, directory walking. | |
| Split images/meta dirs | Binaries and JSON in separate trees; two places to sync. | |

**User's choice:** Flat directory
**Notes:** —

## Claude's Discretion

Window size, chunk payload size, thumbnail dimensions/quality (within IMG-02's ≤320x240 and the 10s window — airtime-verified), default threshold values, which flight phases fire captures, manifest field encoding, SD-full policy.

## Deferred Ideas

None — discussion stayed within phase scope.

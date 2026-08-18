---
phase: 02-image-transmission
plan: "02"
subsystem: image-transfer
tags: [lora, image-transfer, windowed-arq, telemetry-beacon, tx-arbitration, esp32, psram-ownership, fifo-eviction]

# Dependency graph
requires:
  - phase: 02-image-transmission (02-01)
    provides: locked 0x12/0x13/0x14 wire contract, balloon thumbnail push path (ImageTxManager enqueue + one-chunk-per-pass), base receive/reassembly, IMG_* constants, PayloadImageWindowRequest layout
provides:
  - D-17 pull half (balloon side): full images announced exactly once after their thumbnail push, then served ONLY through window contexts armed by IMAGE_WINDOW_REQUEST — the balloon never volunteers full chunks (Pitfall 5)
  - IMAGE_WINDOW_REQUEST dispatch (0x30) in the balloon CommandHandler with ACK/NACK_INVALID/NACK_BUSY mapped through the existing createResponsePacket path — tracked-command contract, terminal-state guard, and duplicate-ACK guard apply unchanged
  - PRI-01/SC-5 satisfied by a working mechanism: the 0x14 telemetry beacon transmit side (every 5 s from live Sensors() state) plus fixed-priority TX arbitration — beacon outranks image chunks at every transmit opportunity; command responses outrank both by loop order
  - Bounded-memory transfer queue: depth-3 drop-oldest, newer-ID implicit eviction (FIFO pull order), 15-minute TTL sweep — every eviction frees both PSRAM buffers
  - IMG_MAX_IMAGE_SIZE (50000) enqueue gate — oversized fulls skip full transfer with a logged warning naming ID and size while the thumbnail still pushes (PRI-03, research Q4 resolution); paired with the base's MAX_IMAGE_SIZE
  - createTelemetryBeaconPacket factory — the 0x14 type byte is factory-owned like every other packet type (CR-01 pattern now complete across all five types)
affects: [02-image-transmission (02-03 base pull side, 02-04 event thresholds, 02-VERIFICATION UAT)]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 10213    # 40851 diff chars / 4 (92bf3ce: 32694 + a70a070: 8157)
  tasks: 2         # code tasks executed (Task 1 prior session, Task 3 continuation); Task 2 was the blocking decision checkpoint
  commits: 2

# Tech tracking
tech-stack:
  added: []         # no new libraries this plan
  patterns:
    - Fixed-priority TX arbiter as a small explicit branch at the top of process() (Pattern 6) — NOT a revival of the excluded SPI-era LoRaManager queue; beacon-due returns before any chunk work, so beacon XOR chunk per pass
    - Baseline-before-attempt for periodic transmits (AutoCapture millis idiom): lastBeaconMs advances before lora->transmit() so a failed beacon cannot drive a tight retry loop; wraparound-safe unsigned subtraction for due-ness
    - Idempotent window arming by cursor reset (Pitfall 10): arming re-points windowNextIndex at startChunk — duplicate/re-requested windows re-send the same indices with no ID allocation and no queue mutation
    - FIFO enforced via monotonically increasing enqueueSeq comparisons (never lastActivityMs) across findActiveEntry / findWindowServiceEntry / drop-oldest / newer-ID eviction
    - Invalid-GPS still beacons: zeros + gpsValid clear beats a stale gap; the base reports honestly from the flag (no fabricated state, D-20 kinship)

key-files:
  created: []      # no new files — this plan expands 02-01 modules
  modified:
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp
    - src/command_handler.cpp
    - include/command_handler.h
    - include/image_protocol.h
    - src/command_protocol.cpp
    - include/command_protocol.h

key-decisions:
  - "USER DECISION (Task 2 blocking checkpoint, research Q1 option a): beacon-on — the minimal 0x14 telemetry beacon transmit side ships in Phase 2. D-18's wording presupposed telemetry already flows on the E32 link; research proved the referenced priority queue is dead stubbed code, so PRI-01/SC-5 are satisfied only by building the beacon plus arbitration. Beacon outranks image chunks in transmit arbitration, never the reverse."
  - "Window-request ACKs ride the existing createResponsePacket path (0x11 first-field) — the base's tracked-command table, terminal-state guard, and duplicate-ACK guard apply to chunk ACKs unchanged (Phase 1 CR-03 lesson applied)"
  - "A window request for a NEWER image ID implicitly completes older ANNOUNCED entries (evicted, buffers freed): FIFO pull order means the base has moved on; keeping stale entries would waste the bounded PSRAM budget"
  - "IMG_MAX_IMAGE_SIZE pairs with the base's MAX_IMAGE_SIZE (both 50000) and must stay equal or the balloon would announce manifests the base rejects; the balloon emits no manifest for oversize fulls so the cap's only documentation surface is the balloon-side Serial warning plus the hardware-UAT UXGA test"
  - "createTelemetryBeaconPacket added so the 0x14 type byte is factory-assigned like 0x10/0x11/0x12/0x13 (CR-01 pattern extended to the last packet type); callers never assign packet.type by hand"
  - "GPS validity for the beacon flag is satellites > 0 — the same signal main_balloon uses for appState.gpsActive; no new validity source invented"
  - "lastBeaconMs advances BEFORE the transmit attempt (AutoCapture millis idiom): a failed beacon retries at the next due cycle instead of spinning the loop; failures log as transition events, never silently skipped"

patterns-established:
  - "Priority-by-loop-order + priority-by-branch: command responses beat the beacon structurally (CmdHandler().process() runs first in processPacketHandling), the beacon beats chunks by an early-return branch in ImageTxManager::process() — two mechanisms, one fixed order, at most one chunk transmit (~250-400 ms) of delay for a beacon or response"
  - "Clamp-the-tail validation for range requests: reject count<1 / count>16 / startChunk>=totalChunks outright, then trim startChunk+count overrun to what exists — a crafted request cannot index past the owned buffer (T-02-04)"

requirements-completed: [IMG-01, IMG-03, PRI-01, PRI-03]

metrics:
  duration: 2 sessions — Task 1 in the prior executor's session (commit 92bf3ce); Task 3 in the continuation session (~8 min, incl. verification)
  completed: 2026-08-19
  tasks_completed: 3   # 2 code tasks + 1 resolved blocking decision checkpoint
  commits: 2

status: complete
---

# Phase 02 Plan 02: Full-Image Pull Path + TX Arbitration & Telemetry Beacon Summary

Balloon-side hybrid transfer completed: fulls announce once after their thumbnail and serve chunks FIFO through idempotent base-armed windows with bounded PSRAM, while a fixed-priority arbiter keeps command responses and a new 5-second 0x14 telemetry beacon strictly ahead of image traffic (PRI-01/SC-5 now satisfied by a working, user-confirmed beacon mechanism).

## Task 2 Decision Record (blocking checkpoint, resolved)

**Decision:** beacon-on — ship the minimal 0x14 telemetry beacon transmit side in Phase 2 (research Open Question Q1, option a).

- **Why it was gated:** D-18 stated PRI-01 is enforced by the balloon's "existing radio priority queue," but 02-01 research proved that queue (LoRaManager) is dead stubbed code excluded from both builds — no telemetry flows on the E32 link at all, making SC-5 ("telemetry continues to update at 5-second rate during image transfers") unobservable without new work. The planner's adoption of option (a) was an interpretation, not a user decision, so the transmit side was built only after explicit user confirmation.
- **User response:** beacon-on. PRI-01/SC-5 are satisfied this phase by a live beacon; beacon outranks image chunks in transmit arbitration, never the reverse.
- **What was already safe under either reading (landed in 02-01, unaffected):** the 0x14 wire type, serializer/deserializer, and the base's honest null-telemetry handling.

## What Was Built

### Task 1 — Full-image announcement + window servicing (commit 92bf3ce, prior session)

- **src/image_tx_manager.cpp + include/image_tx_manager.h**: entry state machine extended (ANNOUNCE_FULL → ANNOUNCED; THUMB_PUSHED parks non-armable entries). After the thumbnail push completes, an armable entry emits the FULL_IMAGE manifest exactly ONCE (totalSize, chunkSize 200, ceil chunk count, end-to-end CRC32, captureTimeMs, settings trailer) and from then on serves chunks ONLY through a window context — never free-runs (Pitfall 5). Enqueue gate: fulls over IMG_MAX_IMAGE_SIZE (50000) never arm; Serial warning names image ID and size, thumbnail still pushes, base never sees a manifest (no airtime wasted on a doomed transfer — PRI-03/Q4). `handleWindowRequest(const uint8_t*, size_t)` decodes PayloadImageWindowRequest big-endian (readUint16), validates imageId against queued ANNOUNCED entries, bounds startChunk/count (reject count<1 or >16 or startChunk>=totalChunks; clamp tail overrun), arms {start, count, nextIndex} idempotently by cursor reset, implicitly evicts older entries on a newer-ID request (FIFO pull order, D-19), and returns BUSY while another entry's window is mid-service. process() services one window chunk per pass (slice at chunkIndex*200, dataLen = min(200, remaining)); only one entry services at a time. Eviction policy: depth-3 drop-oldest with warning, newer-ID eviction, 15-minute TTL sweep — every eviction frees both PSRAM buffers.
- **src/command_handler.cpp + include/command_handler.h**: `case CameraCommand::IMAGE_WINDOW_REQUEST` reads the pending payload, calls `ImageTx().handleWindowRequest(...)`, maps WindowRequestResult to the existing response machinery (ARMED→ACK, UNKNOWN_IMAGE/INVALID_RANGE→NACK_INVALID, BUSY→NACK_BUSY — the base retries with its existing timeout/retry machinery). Every answer flows through createResponsePacket; no hand-rolled frames. New handler `handleImageWindowRequest(const CommandPacket&)`.
- **include/image_protocol.h**: IMG_MAX_IMAGE_SIZE = 50000 with the pairing note against base MAX_IMAGE_SIZE.
- **src/command_protocol.cpp**: command-name case for IMAGE_WINDOW_REQUEST logging.

### Task 3 — TX arbitration + 5-second telemetry beacon (commit a70a070, this session)

- **src/image_tx_manager.cpp**: fixed-priority arbiter at the top of process() (Pattern 6 — a small explicit branch, not a revival of the excluded SPI-era LoRaManager): after the AutoCap ID poll, `if ((millis() - lastBeaconMs) >= TELEMETRY_BEACON_INTERVAL_MS) { sendTelemetryBeacon(); return; }` — the beacon branch consumes the pass's single transmit and returns BEFORE any chunk work; command responses already outrank the beacon by loop order (CmdHandler().process() first in processPacketHandling). `sendTelemetryBeacon()` assembles the 0x14 body from live Sensors() state (the same sources main_balloon feeds SysState): monotonically increasing beaconSeq u16, altitudeCm = round(GPS altitude m × 100) i32, tempCentiC = round(BMP280 temperature C × 100) i16, latE6/lonE6 = round(GPS deg × 1e6) i32, flags bit0 = gpsValid from satellites > 0. Invalid GPS still beacons (zeros, flag clear) — a stale gap is worse than an invalid-flagged sample. lastBeaconMs advances BEFORE the transmit attempt (AutoCapture millis idiom, wraparound-safe compare) so a failed beacon cannot drive a tight retry loop. Transition-only logging: first beacon after boot and transmit failures — never per beacon.
- **include/command_protocol.h + src/command_protocol.cpp**: `createTelemetryBeaconPacket(const TelemetryBeaconBody&)` factory assigning PACKET_TYPE_TELEMETRY_BEACON as the FIRST field (CR-01 lesson extended to the last packet type without a factory).

## Verification Results

- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS (re-run after Task 3)
- Task 1 gates (prior session): handleWindowRequest / IMAGE_WINDOW_REQUEST / IMG_TX_QUEUE_DEPTH greps PASS; wire harness all clauses PASS
- Task 3 gates: TELEMETRY_BEACON_INTERVAL_MS and serializeTelemetryBeacon present in src/image_tx_manager.cpp; `grep LoRaManager` finds nothing (excluded radio class stays dead); node order check (beacon constant present, transmit sites present) PASS
- `node scripts/verify_protocol_roundtrip.mjs` — all clauses PASS (no wire-format regressions)
- Mutual exclusion verified by construction: the beacon branch returns before pushPending(); each branch performs exactly one lora->transmit() per process() pass

## Hardware UAT (deferred to 02-VERIFICATION by plan design)

- SC-5 runtime half: /status telemetry age stays <= ~10 s while a full image transfers
- Window re-requests after induced loss complete without duplication (idempotent arming)
- UXGA oversize-cap confirmation (research Q4): set the camera to UXGA, capture, confirm the thumbnail still arrives while the full transfer is skipped with the logged warning naming the image ID — this is the explicit user-visible confirmation point for keeping the cap at 50000; revisit the constant and airtime budget then if UXGA fulls are required, never raise it silently

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] No createTelemetryBeaconPacket factory existed**

- **Found during:** Task 3 (beacon packet construction)
- **Issue:** 02-01 shipped factories for 0x12/0x13 (and Phase 1 for 0x10/0x11) but no factory for TelemetryBeaconPacket — constructing it in ImageTxManager with a hand-assigned packet.type would violate the codebase's CR-01 lesson ("the factory owns the wire type byte," institutionalized in image_protocol.h's packet-struct comments and 02-01's key decisions).
- **Fix:** Added createTelemetryBeaconPacket(const TelemetryBeaconBody&) to src/command_protocol.cpp + include/command_protocol.h, mirroring createManifestPacket; the beacon caller uses the factory like every other transmit site.
- **Files modified:** include/command_protocol.h, src/command_protocol.cpp (beyond the plan's Task 3 file list)
- **Commit:** a70a070

Other than that, Tasks 1 and 3 executed exactly as written; Task 2's decision outcome (beacon-on) is recorded above and in STATE.md.

## TDD Gate Compliance

Not a TDD plan (`type: execute`); the host wire harness remains the executable specification — no wire-format changes were made this plan (factory only constructs what serializeTelemetryBeacon already encoded), so the harness passed unchanged (re-run confirmed).

## Self-Check: PASSED

- All 7 modified files exist on disk — FOUND
- Commits 92bf3ce (Task 1) and a70a070 (Task 3) — FOUND in git log
- No tracked files deleted by either task commit
- No stubs introduced (stub scan over all touched files: no placeholder data paths, no unwired components — the beacon feeds the 02-01 base telemetry surface, which now receives live data)

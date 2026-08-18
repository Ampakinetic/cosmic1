---
phase: 02-image-transmission
plan: "01"
subsystem: image-transfer
tags: [lora, image-transfer, wire-protocol, crc32, reassembly, esp32, psram-ownership, regression-harness]

# Dependency graph
requires:
  - phase: 01-command-protocol-control (01-01..01-06)
    provides: 240-byte framed command/response transport with CRC16, CommandSender/CommandHandler singletons, AutoCapture as sole capture authority (CR-05), wire harness with factory-owns-type teeth
provides:
  - Phase 2 wire contract locked in include/image_protocol.h: 0x12 manifest (27-byte body), 0x13 chunk (5-byte overhead + dataLen), 0x14 telemetry beacon (17-byte body), ImageKind/CaptureSource enums, chunk/window/queue/retransmit constants
  - CR-04/WR-11 closed: createThumbnail is capture-then-allocate (esp_camera_fb_get before malloc(fb->len)); every early return nulls thumbnail.buffer and clears valid
  - WR-12 closed on both sides: CommandHandler (balloon) accepts 0x10 only; CommandSender (base) dispatches 0x11/0x12/0x13/0x14 on buffer[2] before body arithmetic; unknown types reset the accumulator
  - Balloon push path (D-17 half, IMG-01): ImageTxManager polls AutoCap().getLastImageId(), takes PSRAM ownership of full+thumbnail buffers, pushes thumbnail as manifest + one chunk per process() pass
  - Base receive path (IMG-02 code path): ImageRxManager validates manifests before allocation (Pitfall 11), reassembles chunks with duplicate/foreign rejection, verifies end-to-end CRC32 (D-23), retains newest verified thumbnail, serves GET /img/{id}_t.jpg
  - Base UI shows the latest verified thumbnail (latestThumbId from /status) and the 0x14 telemetry snapshot (absent stays absent)
  - Harness extended to 47 clauses: manifest/chunk/beacon round-trips, oversize rejection, embedded 0x0D 0x0A framing, type-dispatch receivers for both sides, ResponseStatusData layout arithmetic
affects: [02-image-transmission (02-02, 02-03, 02-04)]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 28717    # 114869 diff chars / 4 over the 15 files changed
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added:
    - esp_rom_crc.h (esp_rom_crc32_le) — end-to-end image CRC on both firmwares
  patterns:
    - Fixed-body packet types force their bodyLen at the receiver (manifest 27, beacon 17) instead of trusting the header field; only variable-body types (response, chunk) read bodyLen from header offsets 4-5
    - Receiver type dispatch at buffer[2] BEFORE body arithmetic, with per-type expectedTotal arithmetic — the shared WR-12 shape, transcribed as makeTypeDispatchReceiver in the harness
    - Capture-then-allocate for camera buffers: size comes from the real fb->len after esp_camera_fb_get, never from a pre-capture estimate
    - PSRAM ownership copies (ps_malloc + memcpy) taken at enqueue before the next capture's freeCurrentImage can reclaim the source
    - Exactly one E32 transmit per process() pass (Pattern 4) — the synchronous transmit costs ~250-400 ms of blocked loop
    - Binary HTTP bodies on this WebServer core: setContentLength + send(headers) + sendContent(bytes) — no raw-pointer send overload exists
    - Parameterized HTTP routes via not-found dispatch with strictly-numeric id parsing (exact-match routing cannot express /img/{id}_t.jpg)

key-files:
  created:
    - include/image_protocol.h
    - include/image_tx_manager.h
    - src/image_tx_manager.cpp
    - include/image_rx_manager.h
    - src/image_rx_manager.cpp
  modified:
    - include/command_protocol.h
    - src/command_protocol.cpp
    - src/camera_manager.cpp
    - src/camera_manager.h
    - src/command_handler.cpp
    - src/main_balloon.cpp
    - src/command_sender.cpp
    - src/main_basestation.cpp
    - scripts/verify_protocol_roundtrip.mjs
    - platformio.ini

key-decisions:
  - "Fixed-body vs variable-body framing: 0x12/0x14 receivers force the body length constant (27/17) and never read the header bodyLen; 0x13 keeps header bodyLen == dataLen so chunk framing stays command-shaped (7+5+bodyLen+4). Encodes the contract where it is enforced."
  - "Header seq-byte echo for new types: manifest echoes imageId low byte, chunk echoes chunkIndex low byte, beacon echoes seq — deterministic, debuggable on a logic analyzer, unused by framing arithmetic."
  - "Drop-oldest queue semantics via monotonically increasing enqueueSeq (not lastActivityMs): push activity would corrupt age ordering; enqueue order is stable."
  - "A new manifest supersedes any in-flight base transfer: the base follows the balloon's push stream, so the newest image wins the reassembly slot; same-id re-push restarts reassembly cleanly."
  - "Chunk length strictness: a chunk must carry exactly min(chunkSize, totalSize - offset) bytes — full chunks or the exact final remainder; anything else is rejected as malformed."
  - "CRC-mismatch completions keep the PREVIOUS verified thumbnail (no fabricated state); the windowed-pull re-request that heals holes arrives in 02-03 (D-22)."
  - "ResponseStatusData reserved shrank 17->10 to admit the four event-threshold fields; field-sum stays 28 bytes, sizeof stays 32 (padding), still <= CMD_MAX_RESPONSE_DATA=50."
  - "image_protocol.h includes only common_types.h (avoids the circular include with command_protocol.h); command_protocol.h includes image_protocol.h after common_types.h."

patterns-established:
  - "Forced-body-length dispatch: receivers of fixed-size body types compute expectedTotal from the protocol constant, making a lying header bodyLen field harmless"
  - "Ownership-moving completion: finalizeTransfer transfers buffer ownership into the latest-thumbnail slot by nulling the source pointer before the cleanup call — free paths stay single-owner"

requirements-completed: [IMG-01, IMG-02, IMG-04]

# Coverage metadata (#1602) — one entry per shipped deliverable.
coverage:
  - id: D1
    description: "A thumbnail round-trips the wire: createThumbnail JPEG -> 0x12 manifest + 0x13 chunks with length-driven framing that survives embedded 0x0D 0x0A pairs, proven by the host harness end-to-end"
    requirement: IMG-04
    verification: "node scripts/verify_protocol_roundtrip.mjs — clauses img-a..img-h all PASS (47 clauses total)"
  - id: D2
    description: "After any capture the balloon pushes the thumbnail with no operator action: ImageTxManager detects the new AutoCap image ID, takes PSRAM ownership, and pushes manifest + one chunk per loop pass"
    requirement: IMG-01
    verification: "pio run -e esp32-s3-balloon SUCCESS; PRI-01 loop order CmdHandler -> AutoCap -> ImageTx wired in processPacketHandling"
  - id: D3
    description: "The base reassembles pushed chunks, verifies end-to-end CRC32 against the manifest, and serves the verified thumbnail at GET /img/{id}_t.jpg; the UI displays it from latestThumbId"
    requirement: IMG-02
    verification: "pio run -e esp32-s3-basestation SUCCESS; handleImage route + status JSON + Latest Capture card present; the 10-second window is a hardware-UAT item for 02-VERIFICATION"
  - id: D4
    description: "Both receivers branch on the type byte at buffer[2] before body arithmetic and discard unknown types (WR-12); createThumbnail leaves no dangling buffer on any failure path (CR-04)"
    requirement: IMG-04
    verification: "Harness img-g: CRC-valid frames with unknown/foreign type bytes discarded by both side rules; camera_manager.cpp every early return nulls thumbnail.buffer"

metrics:
  duration: 3243s (~54 min)
  completed: 2026-08-18
  tasks_completed: 3
  commits: 3

status: complete
---

# Phase 02 Plan 01: Image Push Path — Thumbnail Over LoRa Summary

Closed the CR-04/WR-11 double-free, locked the Phase 2 wire contract (0x12/0x13/0x14), and shipped the tracer slice end-to-end: balloon pushes a CRC32-verified thumbnail over the E32 link, base reassembles it and serves it at /img/{id}_t.jpg with a live UI card.

## What Was Built

### Task 1 — Wire contract + CR-04/WR-11 closure (commit b60bde3)

- **include/image_protocol.h** (new): the phase's single wire contract — packet-type constants (0x12 IMAGE_MANIFEST / 0x13 IMAGE_CHUNK / 0x14 TELEMETRY_BEACON), ImageKind + CaptureSource enums, transfer constants (IMG_CHUNK_PAYLOAD_SIZE=200, IMG_WINDOW_MAX_CHUNKS=16, IMG_TX_QUEUE_DEPTH=3, IMG_RETRANSMIT_MAX_PASSES=3, IMG_WINDOW_STALL_MS=8000, IMG_ENTRY_TTL_MS=900000, TELEMETRY_BEACON_INTERVAL_MS=5000, fixed body sizes 27/17), wire body structs (ImageManifestBody, ImageChunkBody, TelemetryBeaconBody), command payload structs (PayloadImageWindowRequest, PayloadSetEventThresholds), and packet structs whose `type` field is factory-owned.
- **src/command_protocol.cpp / include/command_protocol.h**: serializeManifest/deserializeManifest (38-byte frames, bodyLen forced to 27 in the header, imageId low byte echoed at header offset 3), serializeChunk/deserializeChunk (rejects dataLen > 200; frame arithmetic 7+5+dataLen+4), serializeTelemetryBeacon/deserializeTelemetryBeacon (28-byte frames), factories createManifestPacket/createChunkPacket assigning packet.type as the FIRST field (CR-01 lesson). CameraCommand gained IMAGE_WINDOW_REQUEST=0x30 and SET_EVENT_THRESHOLDS=0x31; ResponseStatusData gained the four event fields with reserved shrinking 17->10 (field-sum 28, sizeof 32, fits CMD_MAX_RESPONSE_DATA=50).
- **src/camera_manager.cpp**: createThumbnail fully reworked to capture-then-allocate — esp_camera_fb_get() at QQVGA/quality-20 FIRST, then malloc(fb->len); every early return nulls thumbnail.buffer, clears valid, and restores the original frame size/quality. estimateImageSize deleted; constructor stamps lastCaptureSource=1 (interval default). (CR-04/WR-11 closed before its first Phase 2 caller.)
- **scripts/verify_protocol_roundtrip.mjs**: extended to 47 clauses — manifest/chunk/beacon round-trip mirrors, createManifestPacketMirror with a defective variant (type never assigned -> 0x00 -> discarded by dispatch), makeTypeDispatchReceiver transcribing the WR-12 shape for both sides (balloon [0x10], base [0x11,0x12,0x13,0x14]) with per-type expectedTotal arithmetic, payload codecs, ResponseStatusData layout table.

### Task 2 — Balloon push path (commit 729f4d3)

- **src/image_tx_manager.cpp + include/image_tx_manager.h** (new): ImageTx() singleton. process() polls AutoCap().getLastImageId() (the CR-05 single authority); on change calls Camera().captureThumbnail() (first caller of the fixed path), takes PSRAM ownership of full+thumbnail buffers via ps_malloc+memcpy, computes esp_rom_crc32_le for both, snapshots camera settings (by-name framesize mapping mirroring CommandHandler::frameSizeFromEsp). Bounded queue (depth 3) with drop-oldest on overflow via enqueueSeq + Serial warning. pushPending transmits the manifest then exactly ONE chunk per pass (Pattern 4). Full-image announce deferred to 02-02 by design.
- **src/command_handler.cpp**: WR-12 balloon half — processIncomingByte switches on buffer[2] and accepts 0x10 only; anything else resets the accumulator. handleCaptureNow stamps lastCaptureSource=MANUAL before captureImage().
- **src/main_balloon.cpp**: ImageTx().begin after AutoCap().begin; ImageTx().process() after AutoCap().process() in processPacketHandling (PRI-01 arbitration half: command responses always get the transmit opportunity first).
- **platformio.ini**: base env excludes image_tx_manager.cpp.

### Task 3 — Base receive path + serving + UI (commit 0657f3a)

- **src/image_rx_manager.cpp + include/image_rx_manager.h** (new): ImageRx() singleton. onManifestFrame validates EVERYTHING before allocating (Pitfall 11: totalSize in 1..MAX_IMAGE_SIZE=50000, chunkSize in 1..200, totalChunks == ceil(totalSize/chunkSize) >= 1, known imageKind); a new manifest supersedes any in-flight transfer. onChunkFrame rejects foreign imageIds, out-of-range indexes, and wrong-length chunks; duplicates drop idempotently via the presence array; completion verifies esp_rom_crc32_le against the manifest CRC (D-23) and retains the thumbnail (kind=THUMBNAIL only in this plan). process() ages out stalled transfers after IMG_WINDOW_STALL_MS. onTelemetryBeaconFrame tracks the 0x14 snapshot with a millis stamp.
- **src/command_sender.cpp**: WR-12 base half — after the header is buffered, a switch on buffer[2] picks the body arithmetic per type (0x11 response 7+4+bodyLen+4; 0x12 manifest forced 27; 0x13 chunk 7+5+bodyLen+4; 0x14 beacon forced 17); unknown types reset. CRC-valid 0x12/0x13/0x14 frames forward to ImageRx() and never touch the tracked-command table.
- **src/main_basestation.cpp**: LoRaSerial.setRxBufferSize(1024) BEFORE E32LoRaModule().begin (Pitfall 2 — chunk bursts overrun the default 256-byte buffer); ImageRx().begin/process wired; GET /img/{id}_t.jpg dispatched from handleNotFound with strictly-numeric id parsing, serving the retained verified thumbnail as image/jpeg (404 honestly for anything else); handleStatus gains latestThumbId + a telemetry object (null when no beacon ever received); new "Latest Capture" UI card with a dataset-gated img swap (a stale image never renders under a new id) and a telemetry chip.
- **platformio.ini**: balloon env excludes image_rx_manager.cpp.

## Verification Results

- `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — both SUCCESS
- `grep` confirmations: setRxBufferSize(1024), onManifestFrame, esp_rom_crc32_le, latestThumbId — all present
- `node scripts/verify_protocol_roundtrip.mjs` — all 47 clauses PASS (img-a..img-h among them)
- Tracer feedback gate (autonomous run): full automated verify re-run end-to-end after Task 3 — green; slice proven before expansion is complete

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] WebServer core lacks a raw-pointer send overload**

- **Found during:** Task 3 (base build failed to compile)
- **Issue:** `server.send(200, "image/jpeg", const uint8_t*, size_t)` does not exist in this ESP32 core's WebServer (candidates all take String/const char*/Stream).
- **Fix:** Canonical binary-body pattern — `setContentLength(len)` + `send(200, "image/jpeg", "")` (headers only; the pre-set length is honored) + `sendContent((const char*)data, len)` (binary-safe write). Verified against the core's WebServer.cpp source.
- **Files modified:** src/main_basestation.cpp
- **Commit:** 0657f3a

Or: no other deviations — Tasks 1 and 2 executed exactly as written.

## TDD Gate Compliance

Not a TDD plan (`type: execute`); the host harness serves as the executable specification and was extended in the same commit as the wire contract, with defective-variant teeth per the plan's verification design.

## Self-Check: PASSED

- Created files exist: include/image_protocol.h, include/image_tx_manager.h/.cpp, include/image_rx_manager.h/.cpp — all FOUND
- Commits exist: b60bde3, 729f4d3, 0657f3a — all FOUND in git log
- No tracked files deleted by any task commit

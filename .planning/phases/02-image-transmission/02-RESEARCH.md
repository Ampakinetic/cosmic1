# Phase 2: Image Transmission - Research

**Researched:** 2026-08-19
**Domain:** Embedded firmware — chunked image transfer over UART-attached LoRa (E32-900T30D), ARQ protocol extension of existing command protocol, SD storage on ESP32-S3, event-based capture triggers
**Confidence:** HIGH (codebase) / MEDIUM (external hardware data)

## Summary

Phase 2 is a **protocol-extension phase on a verified Phase 1 foundation**, not a greenfield build. The command protocol (`command_protocol.h/.cpp`) already provides everything the chunk protocol needs: 240-byte framed packets with CRC16, length-driven framing that survives embedded `0x0D 0x0A` bytes (JPEG data contains them), byte-order helpers, ACK timeout classes, and a 5-slot tracked-command table on the base. The chunk protocol is best built as **new packet types (manifest + chunk) alongside new CameraCommand IDs for window requests**, exactly as CONTEXT.md D-17/D-21/D-22/D-23 prescribe.

Three load-bearing discoveries reshape the plan:

1. **The "existing radio priority queue" referenced by D-18 is dead code.** `lora_comm.cpp` is excluded from both build envs (`-<lora_comm.cpp>`) and `LoRaComm()` is stubbed via `stubs.cpp` — `begin()` returns true and does nothing [VERIFIED: src/stubs.cpp:38-42]. **No telemetry currently transmits over the E32 link at all**: `PacketHandler::sendPacket()` is a placeholder (`bool success = true; // Placeholder`) [VERIFIED: src/packet_handler.cpp:156-157] and `processCommunications()` in the balloon loop is fully commented out [VERIFIED: src/main_balloon.cpp:654-684]. PRI-01 ("telemetry always has priority over image data") must be **newly built** as TX arbitration at the point where the balloon decides what to send next on the E32. See Open Question Q1 — SC-5 ("telemetry continues to update at 5-second rate during image transfers") is not demonstrable without a minimal telemetry-over-E32 path.
2. **CR-04/WR-11 mechanics confirmed by direct read.** `createThumbnail()` frees `thumbnail.buffer` on both failure paths (fb-miss at line 313, oversize at line 329) without nulling the member — and the pre-capture allocation of `estimateImageSize(FRAMESIZE_QQVGA, 15)` = `15*200+1000` = **4000 bytes** [VERIFIED: src/camera_manager.cpp:291, 714-734] is routinely undersized (QQVGA q15 JPEGs run 4-12 KB [CITED: esp32-camera issue #150, community size reports]). The root fix is ordering: capture the frame buffer first, then allocate exactly `fb->len` — eliminating the estimate entirely for this path.
3. **The base station has no SD wiring, no SD code, and no SD pins anywhere** [VERIFIED: include/base_station_config.h — no SD pin defines; `grep SD\.` in main_basestation.cpp: 0 hits]. The SD library ships in the arduino-esp32 core (no new PlatformIO dependency), but physical pin choice is unestablished — a user/hardware confirmation item (Open Question Q2).

Airtime math (arithmetic from verified rate): at the presumed 9.6 kbps air rate, a 240-byte packet costs ~200 ms payload time (~210-280 ms with preamble). Default captures (QVGA 320x240, quality 10 [VERIFIED: include/balloon_config.h:40-41] → ~8-15 KB) need ~40-70 chunks → **15-30 s background transfer**; a QQVGA thumbnail at quality ~20 (~2.5-4 KB) needs ~12-18 chunks → **3-6 s push**, inside IMG-02's 10-second window only if quality is tuned down from the current q15 and chunks are paced one per loop pass. PRI-03 is real: UXGA transfers take minutes.

**Primary recommendation:** Land CR-04/WR-11 as the entry task; extend the protocol with `0x12 IMAGE_MANIFEST` / `0x13 IMAGE_CHUNK` packet types plus `IMAGE_WINDOW_REQUEST` as a new CameraCommand; build a small fixed-priority TX arbiter on the balloon (telemetry > responses > chunks); stream chunks one per loop pass to respect the blocking E32 driver; write received chunks directly to SD by seek+offset; extend AutoCapture with event triggers reading Sensors()/SysState() baselines.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Transfer Model**
- **D-17:** Hybrid push/pull — balloon pushes the thumbnail immediately after capture (no round-trip, serves IMG-02's 10-second display window), then announces the full image with a manifest packet (image ID, total size, chunk count, end-to-end CRC32); the BASE station pulls the full image chunk-by-chunk via the Phase 1 command/response protocol. — **Reversibility:** costly.
- **D-18:** Automatic pacing only — no operator transfer controls. PRI-01 is enforced solely by the balloon's existing radio priority queue (Emergency > GPS > Telemetry > Camera > Status); the pull proceeds whenever no higher-priority traffic is waiting.
- **D-19:** FIFO transfer queue — full-image pulls complete in capture order; a new capture never abandons an in-progress pull (its thumbnail still pushes immediately).
- **D-20:** Transfer progress UI shows percent + chunks received/total per image, derived from actual chunk accounting. Extends the Phase 1 D-16 Command Queue panel pattern; the no-fabricated-state prohibition applies.

**Chunk Reliability**
- **D-21:** Windowed request/response ARQ — base requests a window of chunks (~16 suggested), re-requests corrupt/missing chunks before advancing. Bounds balloon-side memory; progress is exact by construction.
- **D-22:** One reliability mechanism system-wide — a pushed thumbnail with holes falls back to the same windowed-pull re-request path. No parallel best-effort path.
- **D-23:** End-to-end integrity — manifest carries CRC32 over the entire original image; base verifies after reassembly. Per-chunk CRC16 rides the transport but cannot catch misordered/duplicated chunks.
- **D-24:** Bounded retransmit rounds — after a bounded number of passes over missing chunks (3 suggested), the image is finalized incomplete: kept on SD, flagged in metadata, transfer slot freed (PRI-03).

**Event-Based Capture Triggers**
- **D-25:** Trigger events = altitude delta + horizontal distance delta + flight-phase transitions. The existing `SystemState` flight-phase state machine already computes the transitions.
- **D-26:** Thresholds UI-configurable from the base station web UI via new command types, following the Phase 1 camera-settings pattern (settings forms + ACK + GET_STATUS reporting).
- **D-27:** Event captures reset the interval baseline — both modes coexist; an event firing near the interval deadline never produces a double-capture burst. All captures share the single `uint16` image-ID sequence owned by AutoCapture.
- **D-28:** Global minimum spacing between any two automatic captures (15-30 s suggested).

**SD Storage (Base Station)**
- **D-29:** Files named by balloon-assigned image ID: `IMG_{id}.JPG` (zero-padded). Same ID rides manifest, transfer queue, GET_STATUS `lastImageId`.
- **D-30:** Sidecar JSON per image (`IMG_{id}.JSON`): capture time, altitude, GPS position, trigger source, camera settings at capture, RSSI at receipt, chunk statistics, completeness flag.
- **D-31:** Separate thumbnail files (`IMG_{id}_T.JPG`) written the moment the thumbnail completes; full image lands as `IMG_{id}.JPG` when its pull finishes.
- **D-32:** Flat directory layout (single images directory; `_T` suffix convention). No clock-dependent folder logic.

### Claude's Discretion
- Window size and chunk payload size (must fit the established 240-byte transport budget) — researcher validates via LoRa airtime math.
- Thumbnail dimensions/JPEG quality target — constrained by IMG-02 ("320x240 or smaller") and the 10-second display window; verify airtime feasibility, propose defaults.
- Default threshold values for altitude/distance deltas and the minimum-spacing constant — propose sensible flight defaults.
- Which flight-phase transitions fire captures (all vs. launch/apex/landing only).
- Manifest packet field encoding details (binary layout within the 240-byte budget).
- SD-card-full behavior (oldest-image rollover vs. stop-storing-and-warn) — pick the safe default; flag the choice in the plan.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| IMG-01 | Captured images transmitted balloon→base over LoRa | New 0x12/0x13 packet types on the E32 transport; push/pull model per D-17; wire format and framing rules documented below |
| IMG-02 | Thumbnail preview (≤320x240) displays within 10 s | Thumbnail is QQVGA 160x120 (qualifies); airtime math shows 3-6 s at quality ~20 with 200 B chunks; CR-04/WR-11 fix is the entry task |
| IMG-03 | Full images transfer in background after thumbnail | FIFO pull queue (D-19) serviced one chunk per loop pass; 15-30 s for default QVGA captures; window ARQ per D-21 |
| IMG-04 | Images chunked into packets with reliable transmission | Chunk framing proposal (5-byte chunk header, ≤200 B payload, 216 B total); windowed ARQ; per-chunk CRC16 already in transport; end-to-end CRC32 via `esp_rom_crc32_le` |
| IMG-05 | Base station stores received images on SD card | SD library in arduino-esp32 core; custom-SPI pin pattern; seek+offset streaming write; D-29..D-32 naming/sidecar; SD pins are an open question |
| CTRL-05 | Event-based auto-capture triggers | FlightPhase machine active by default [VERIFIED]; altitude from GPS feed; distance from consecutive fixes; extend AutoCapture per D-25..D-28 |
| PRI-01 | Telemetry always has priority over image data | **Critical finding: no telemetry currently rides the E32** — priority must be built as new TX arbitration; open question Q1 on scope |
| PRI-03 | Graceful handling of LoRa bandwidth limitations | D-24 bounded retransmit rounds; one-chunk-per-loop pacing; realistic throughput ceiling ~600-800 B/s documented |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **LoRa bandwidth: 240 bytes per packet** — enforced by `CMD_MAX_PACKET_SIZE = 240` [VERIFIED: include/command_protocol.h:181]
- **Telemetry always has priority over image data** (PRI-01)
- **5-second UI update rate**
- **Single-threaded event loop (no blocking)** — directly tensioned by the blocking E32 driver (see Pitfall 3)
- **PSRAM required for camera operations** (balloon)
- Stack: C++ / Arduino framework / PlatformIO; libraries esp32-camera, Adafruit BMP280, TinyGPSPlus, ArduinoJson (balloon env only — base env has **no** lib_deps [VERIFIED: platformio.ini:298-299])
- Pin mappings established and unchangeable — **except SD pins, which were never established** (Open Question Q2)

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Thumbnail generation + JPEG capture | Balloon firmware (CameraManager) | — | OV2640 + PSRAM live on the balloon; `createThumbnail` already recaptures at QQVGA |
| Manifest creation + chunk servicing | Balloon firmware (new image-TX module) | — | Image bytes live in balloon RAM; chunk reads are offset slices of the held buffer |
| Transfer arbitration / PRI-01 | Balloon firmware (TX arbiter) | — | Only the sender can yield airtime to telemetry; half-duplex link |
| Chunk reassembly + CRC32 verify | Base station firmware (new image-RX module) | — | Receiver owns the bitmap, re-request list, and finalization |
| SD persistence + sidecar JSON | Base station firmware (new SD module) | — | SD hardware is a base-station constraint |
| Transfer progress UI | Base station web UI (main_basestation.cpp) | — | Extends D-16 queue panel; no-fabricated-state prohibition |
| Event-trigger evaluation | Balloon firmware (AutoCapture extension) | — | Sensors + flight-phase machine are balloon-local; image-ID sequence is owned by AutoCapture |
| Threshold configuration | Base UI → command protocol → balloon handler | — | D-26 follows the Phase 1 settings-command pattern |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| arduino-esp32 SD (built into core) | espressif32 platform (PIO 6.1.19 resolved) | SD card mount, file create/write/seek on base station | Ships with the core — zero new PlatformIO dependencies; FAT32 via FS API [CITED: docs.espressif.com arduino-esp32 + Random Nerd Tutorials SD guide] |
| arduino-esp32 FS/WebServer (in use) | current | File serving for thumbnails/full images | Already the base station's server (`WebServer server(80)` [VERIFIED: src/main_basestation.cpp:47]); `server.streamFile` serves SD files |
| esp_rom_crc (ROM, `<esp_rom_crc.h>`) | ESP32-S3 ROM | End-to-end CRC32 for manifests (D-23) | Hardware ROM tables, no flash cost, same function both firmwares [CITED: techoverflow.net/2022/08/05, sourcevu esp-idf esp_rom_crc32_le] |
| TinyGPSPlus | ^1.1.0 (balloon env) | GPS fix source for altitude/distance triggers | Already wired (`Sensors().getGPSData()`); provides lat/lon/altitude/speed |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| ArduinoJson | ^6.21.3 (balloon env only) | Sidecar JSON (D-30) | **Not recommended on base** — base env has no lib_deps and its UI hand-builds JSON with String concatenation [VERIFIED: src/main_basestation.cpp:1013-1084]; hand-format the sidecar to match, avoiding an env dependency change |
| esp32-camera | ^2.0.4 (balloon env) | Thumbnail recapture path | Only for the CR-04/WR-11 rework of `createThumbnail` |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| SPI SD library | SD_MMC (1-bit) | SD_MMC needs dedicated SDMMC pins (usually 2/4/14/15 on S3) and a wired SDMMC socket; SPI microSD breakout is the common DevKitC-1 companion and pin-flexible — use SPI unless hardware dictates otherwise |
| Hand-built sidecar JSON | ArduinoJson on base | ArduinoJson is cleaner for nested data but adds a dependency to an env that deliberately has none; the sidecar is flat key-value, String formatting is sufficient |
| New 0x12/0x13 packet types | Chunks inside RESPONSE (0x11) packets | REJECTED: `CMD_MAX_RESPONSE_DATA = 50` and `uint8_t data[50]` cap responses at 50 bytes [VERIFIED: include/command_protocol.h:99, 182] — 22% payload efficiency, 5x transfer time |

**Installation:** none — no new packages. All additions are arduino-esp32 core components (SD, FS, esp_rom_crc) or existing balloon-env libraries.

## Package Legitimacy Audit

> This phase installs **no external packages**. SD, FS, WebServer, and `esp_rom_crc.h` are components of the arduino-esp32 core already delivered by the `espressif32` platform. No registry (npm/PyPI/crates) lookups apply; there is nothing to legitimacy-check and no `checkpoint:human-verify` gate required for installs.

| Package | Registry | Age | Downloads | Source Repo | Verdict | Disposition |
|---------|----------|-----|-----------|-------------|---------|-------------|
| (none) | — | — | — | — | — | No external packages introduced |

**Packages removed due to SLOP verdict:** none
**Packages flagged as suspicious SUS:** none

## Architecture Patterns

### System Architecture Diagram

```
BALLOON (esp32-s3-balloon env)                      BASE STATION (esp32-s3-basestation env)
================================================    =================================================

 [Capture sources]                                  [Web UI (main_basestation.cpp)]
 CAPTURE_NOW cmd ─┐                                      ▲  /status poll (progress rows)
 AutoCapture ─────┤                                      ▼  settings forms (thresholds)
 event triggers ──┘→ CameraManager                     HTTP routes
                       │ captureImage()                     ▲
                       ▼                                     │ JSON (hand-built, D-20 truth)
                  [CR-04/WR-11 fixed]                  ImageRxManager (new)
                  createThumbnail()                        ▲ chunk bitmap, re-request list
                       │                                     │ CRC32 verify (esp_rom_crc32_le)
                       ▼                                     ▼
                  ImageTxManager (new) ── E32 UART ──→  LoRaSerial RX (enlarged buffer)
                  FIFO queue {id, buf,     (9600 bd)          │
                  crc32, chunkSize}                           ▼
                       ▲                            0x12 manifest ──→ open file + metadata
                       │ window request (0x10 cmd)  0x13 chunks ────→ seek+write by offset
                       │                            window-complete RESPONSE ──→ advance/retry
                       ▼                                       │
                  TX Arbiter (new, PRI-01):                     ▼
                  telemetry-beacon > responses            SD module (new)
                            > chunk service            IMG_{id}_T.JPG / IMG_{id}.JPG
                       │                                  IMG_{id}.JSON sidecar
                       ▼
                  CommandHandler (existing, extended)
                  + new SET_EVENT_THRESHOLD cmds

 Air link: half-duplex, ~210-280 ms per 240 B packet @ 9.6 kbps air rate (presumed factory default)
```

Primary use case trace: capture → thumbnail bytes queued → manifest(0x12, kind=thumb) + chunk(0x13) push → base reassembles → writes `IMG_{id}_T.JPG` → UI shows thumbnail (IMG-02) → manifest(0x12, kind=full) → base issues window request → balloon streams chunks one per loop pass → window-complete → re-request holes → CRC32 verify → `IMG_{id}.JPG` + sidecar (IMG-05) → progress rows reach complete (D-20).

### Recommended Project Structure
```
include/
├── image_protocol.h        # NEW: manifest/chunk wire structs, packet type constants, window-request command IDs
├── image_tx_manager.h      # NEW (balloon): FIFO transfer queue, chunk servicing, TX arbitration entry
├── image_rx_manager.h      # NEW (base): reassembly bitmap, window state machine, progress snapshot API
├── sd_storage.h            # NEW (base): file naming (D-29/31/32), sidecar writer, disk-full policy
└── auto_capture.h          # EXTENDED: event-trigger thresholds, min-spacing, baseline reset (D-25..D-28)
src/
├── command_protocol.cpp    # EXTENDED: serialize/deserialize manifest + chunk packet types (byte-order helpers reused)
├── command_handler.cpp     # EXTENDED: window-request servicing hook, threshold commands, type-byte dispatch (WR-12)
├── command_sender.cpp      # EXTENDED: type-aware receive dispatch (0x11/0x12/0x13), window-request tracking
├── image_tx_manager.cpp    # NEW (balloon env only)
├── image_rx_manager.cpp    # NEW (base env only)
├── sd_storage.cpp          # NEW (base env only)
├── auto_capture.cpp        # EXTENDED: event evaluation from Sensors()/SysState()
├── camera_manager.cpp      # FIXED: CR-04/WR-11 (entry task)
├── main_balloon.cpp        # EXTENDED: loop wiring for tx manager + telemetry beacon arbitration
└── main_basestation.cpp    # EXTENDED: loop wiring, SD init, transfer panel + threshold forms, image routes
scripts/
└── verify_protocol_roundtrip.mjs  # EXTENDED: manifest/chunk wire-format clauses
```

Follow the established build-filter discipline: new balloon-only/base-only sources must be added to the **other** env's exclusion list in `platformio.ini` (the pattern used for `auto_capture.cpp` [VERIFIED: platformio.ini:277]).

### Pattern 1: Length-driven framing for new packet types (extends CR-03)
**What:** Every wire packet shares the 7-byte header + 4-byte trailer; the header's body-length field (offsets 4-5, big-endian) announces the body size; end markers are tested only at the framed position.
**When to use:** All new packet types — manifest, chunk, telemetry beacon.
**Why it is mandatory here:** JPEG payload bytes routinely contain `0x0D 0x0A`; scanning for end markers corrupts reassembly (the Phase 1 CR-03 lesson). The E32 in transparent mode is also a **byte pipe with auto sub-packing** — RF packet boundaries do not correspond to UART write boundaries [CITED: EBYTE E32 user manual via cdebyte.com]; only stream framing works.

```
[0xAA][0x55][type][seqLow][bodyLen BE16][0x00] [body...] [CRC16 BE][0x0D][0x0A]
 ←——————— 7-byte header ———————→                ←body→    ←——— 4-byte trailer ———→
```

Existing receivers compute `expectedTotal = CMD_HEADER_SIZE + <type overhead> + bodyLen + 4` with type overhead 5 for commands [VERIFIED: src/command_handler.cpp:658] and 4 for responses [VERIFIED: src/command_sender.cpp:294]. **New types must add their own branch switching on `receiveBuffer[2]` before this arithmetic** — which is also exactly the WR-12 fix.

### Pattern 2: Chunk packet with command-shaped body
**What:** Give `0x13 IMAGE_CHUNK` a body of `[imageId BE16][chunkIndex BE16][dataLen u8][data...]` — 5 bytes of overhead before the payload, mirroring the command block shape.
**When to use:** All balloon→base image data.
**Example (proposed layout, planner finalizes):**
```cpp
// Proposed for include/image_protocol.h — values are the researcher's proposal, not yet locked
static constexpr PacketType PACKET_TYPE_IMAGE_MANIFEST = static_cast<PacketType>(0x12);
static constexpr PacketType PACKET_TYPE_IMAGE_CHUNK    = static_cast<PacketType>(0x13);

struct ImageChunkBody {
    uint16_t imageId;     // big-endian on wire (writeUint16)
    uint16_t chunkIndex;  // 0-based
    uint8_t  dataLen;     // <= 200
    // dataLen bytes follow
};

struct ImageManifestBody {           // 15-byte fixed part; optional capture metadata appended
    uint16_t imageId;     // BE
    uint8_t  imageKind;   // 0 = thumbnail, 1 = full image (D-22 unifies the paths)
    uint32_t totalSize;   // BE
    uint16_t chunkSize;   // BE, e.g. 200
    uint16_t totalChunks; // BE
    uint32_t crc32;       // BE — end-to-end CRC over the ORIGINAL image bytes (D-23)
};
```
Budget check (arithmetic from verified constants): header 7 + body-overhead 5 + payload 200 + trailer 4 = **216 ≤ 240** [CMD_MAX_PACKET_SIZE: include/command_protocol.h:181; CMD_HEADER_SIZE=7: :193]. Chunk payload of 200 B also matches the familiar `CMD_MAX_PAYLOAD_SIZE = 200` [VERIFIED: include/command_protocol.h:180].

### Pattern 3: Window request rides the existing command/response machinery
**What:** The base's window pull is a new `CameraCommand` (e.g. `IMAGE_WINDOW_REQUEST = 0x30`) with payload `[imageId BE16][startChunk BE16][count u8]`, sent through `CmdSender().sendCommand()` like any Phase 1 command. The balloon answers with the chunk stream and a **window-complete RESPONSE** (`refSequence` = request sequence) — so the existing tracked-command table, terminal-state guard, and duplicate-response guard apply unchanged (the CR-03 lesson applied to chunk ACKs, per CONTEXT).
**When to use:** Full-image pulls (D-21) and thumbnail hole re-requests (D-22 — same command, same image ID).
**Timeout:** must be a NEW, longer class — 16 chunks × ~250-400 ms ≈ 4-6.5 s of stream exceeds the 5 s SETTINGS window; suggest ~15000 ms alongside `CMD_ACK_TIMEOUT_COMPLEX_MS = 10000` [VERIFIED: include/command_protocol.h:185-187].

### Pattern 4: One chunk per loop pass (blocking-driver coexistence)
**What:** The balloon services at most one chunk transmit per `loop()` iteration. `E32LoRa::transmit()` is synchronous — AUX busy-waits bounded by `waitForAuxHigh(1000)`, `waitForAuxLow(1000)`, `waitForAuxHigh(5000)` [VERIFIED: src/e32_lora.cpp:158-202] — so each chunk costs ~250-400 ms of blocked loop; one per pass keeps telemetry, sensor updates, and command reception lurching along between chunks and respects the 100 ms balloon loop cadence [VERIFIED: `MAIN_LOOP_INTERVAL_MS 100`, src/main_balloon.cpp:59].
**When to use:** Both the thumbnail push and window servicing.

### Pattern 5: Stream-to-SD reassembly (no big RAM buffer)
**What:** On manifest receipt, open the file, then `file.seek(chunkIndex * chunkSize)` and write each chunk's payload as it validates; close on completion or D-24 incomplete finalization.
**Why:** Avoids holding a `totalSize` buffer (manifest size is attacker/corruption-influenced `uint32`); produces correct partial files for the D-24 incomplete-but-kept policy; sidesteps the base station's unverified PSRAM flags (its env lacks `-DBOARD_HAS_PSRAM`, present only in the balloon env [VERIFIED: platformio.ini:97 vs :291-295]).

### Pattern 6: TX arbitration for PRI-01
**What:** A single fixed-priority decision at each transmit opportunity on the balloon: (1) pending command RESPONSE (a blocked Phase 1 ACK stalls the whole command UI), (2) telemetry beacon when due, (3) one chunk from the active transfer. This is a ~30-line function, not the dead `LoRaManager` queue — that class must NOT be resurrected (it is excluded from both builds for SPI-dependency reasons [VERIFIED: platformio.ini:75-76, 267-268]).

### Anti-Patterns to Avoid
- **Resurrecting `lora_comm.cpp`/`LoRaManager`:** excluded from both builds; SPI/Sandeep-Mistry heritage contradicts the UART E32 transport (CONTEXT.md canonical-refs note).
- **Chunks inside RESPONSE packets:** 50-byte data cap makes transfers 5x slower and pollutes the command-tracking semantics.
- **Scan-for-`0x0D 0x0A` parsing of image data:** guaranteed corruption (CR-03 lesson).
- **Estimating JPEG sizes before capture:** the root cause of WR-11. Allocate from `fb->len` after `esp_camera_fb_get()` returns.
- **Pre-empting the in-flight image's buffer:** the next capture frees `currentImage` [VERIFIED: `freeCurrentImage()` at src/camera_manager.cpp:186] — the transfer queue must own a copy before the next capture runs (D-19).
- **Assuming RF packet boundaries match writes:** E32 auto sub-packing merges bursts up to the 512-byte module buffer [CITED: EBYTE user manual].

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| CRC32 (end-to-end, D-23) | Table-driven CRC32 in C++ | `esp_rom_crc32_le(0, data, len)` from `<esp_rom_crc.h>` | ROM-resident, hardware tables, identical on both firmwares so the convention cannot diverge [CITED: techoverflow.net, sourcevu esp-idf] |
| SD card access | Raw SPI sector reads, custom FAT | arduino-esp32 `SD` + `FS` API | Filesystem edge cases (wear, FAT variants, long filenames) are solved problems [CITED: docs.espressif.com] |
| Chunk retransmit bookkeeping from scratch, untested | Ad-hoc flags | Window bitmap (received/missing per chunk index) + bounded passes (D-24) | Bitmap makes progress accounting exact by construction — the D-20 no-fabricated-state rule derives from it |
| Serial framing for image bytes | Delimiter scanning | The existing length-driven framing (Pattern 1) | Proven by the Phase 1 wire harness (`scripts/verify_protocol_roundtrip.mjs`, 15/15) |
| Haversine distance | (acceptable to hand-roll) | `TinyGPSPlus::distanceBetween(lat1, lon1, lat2, lon2)` static helper | [ASSUMED — training knowledge; verify signature during implementation; fallback is a 10-line local haversine] |

**Key insight:** every reliability primitive this phase needs (CRC16 framing, byte-order helpers, tracked retries, terminal-state guards, duplicate guards) already exists in Phase 1 code. The phase's net-new logic is limited to: chunk/window state, TX arbitration, SD persistence, event triggers.

## Common Pitfalls

### Pitfall 1: CR-04/WR-11 thumbnail double-free (MUST-FIX entry task)
**What goes wrong:** `createThumbnail` failure paths free `thumbnail.buffer` without nulling; the next `freeCurrentThumbnail()` double-frees → heap corruption/crash. The pre-capture 4000-byte estimate makes failure the common case once IMG-02 wires the first caller.
**Why:** Allocation happens before the frame exists; both failure paths (fb miss, `fb->len > estimatedSize`) leave the member dangling [VERIFIED: src/camera_manager.cpp:311-317 and 328-334 — `free(thumbnail.buffer);` with no `= nullptr`].
**How to avoid:** Restructure to capture-then-allocate: get `fb`, allocate exactly `fb->len`, copy, restore settings; null the member on every early return. 01-VERIFICATION.md records this as "must be fixed before or as the first task of the Phase 2 thumbnail work."
**Warning signs:** heap corruption after a failed thumbnail; boot loops after several captures.

### Pitfall 2: Serial2 RX buffer overflow during chunk bursts
**What goes wrong:** Dropped bytes mid-frame corrupt chunks (CRC16 catches it, but throughput collapses via re-requests).
**Why:** Default HardwareSerial RX ring buffer is **256 bytes**; `setRxBufferSize()` only works **before** `begin()` [CITED: docs.espressif.com arduino-esp32 Serial API]. E32 auto sub-packing can deliver up to 512 merged bytes in one RF reception; the base's `while (lora->available() > 0)` drain loop is fast, but a web-server `handleClient()` call between drains can stall it. The base loop is `delay(10)` [VERIFIED: src/main_basestation.cpp:410].
**How to avoid:** `LoRaSerial.setRxBufferSize(1024)` before `E32LoRaModule().begin(...)` in base `initLoRa()` [VERIFIED call site: src/main_basestation.cpp:442-444]; keep one-chunk-per-loop pacing on the balloon side so arrivals stay bounded.
**Warning signs:** high per-chunk CRC failure rate that improves when the browser is idle.

### Pitfall 3: Blocking transmit stalls the single-threaded loop
**What goes wrong:** Sending a whole window (16 chunks) inside one handler call blocks ~4-6.5 s; sensor reads, command reception, and the watchdog starve.
**Why:** `E32LoRa::transmit()` synchronously waits the full AUX cycle; worst-case timeouts sum to ~7 s [VERIFIED: src/e32_lora.cpp:158-202]. This is the carried WR-04 advisory, now load-bearing.
**How to avoid:** Pattern 4 (one chunk per loop pass). Do NOT "fix" the driver to async in this phase — scope discipline; pacing achieves the same liveness.
**Warning signs:** loop-time warnings (`maxLoopTime > MAIN_LOOP_INTERVAL_MS * 2`, the existing `checkSystemHealth` heuristic [VERIFIED: src/main_balloon.cpp:901-903]).

### Pitfall 4: Window-request ACK timeout shorter than the answer
**What goes wrong:** The CommandSender marks a window request TIMEOUT at 5 s while the balloon is still streaming its 16 chunks (~6 s) — then re-requests the same window, duplicating traffic on a half-duplex link.
**Why:** `ackTimeoutFor()` classes are TRIGGER 2 s / SETTINGS 5 s / COMPLEX 10 s [VERIFIED: src/command_sender.cpp:24-46]; none fits a window stream.
**How to avoid:** New timeout class for the window command (~15 s), and/or design the window-complete RESPONSE so the tracked command resolves the moment the stream ends.

### Pitfall 5: Half-duplex collision (base transmits during balloon stream)
**What goes wrong:** Both sides transmit simultaneously; bytes are lost both ways.
**Why:** E32 transparent mode is half-duplex; nothing in the current code prevents it (Phase 1's request/response cadence implicitly serialized the link).
**How to avoid:** The window protocol is self-serializing: base speaks only when (a) issuing the next window request after a window-complete RESPONSE or timeout, or (b) a manifest arrives while idle. Never free-run request timers during an active stream.

### Pitfall 6: Receive-side type confusion (WR-12 becomes mandatory)
**What goes wrong:** The balloon's receiver frames ANY `0xAA 0x55` packet and hands it to `deserializeCommand`; a CRC-valid manifest/chunk/response heard by the balloon would execute as a command (and symmetrically on the base, chunks would be parsed as responses with the wrong body-overhead arithmetic).
**Why:** No receive path validates the type byte at `buffer[2]` [VERIFIED: src/command_handler.cpp:630-683, src/command_sender.cpp:267-317 — 01-VERIFICATION.md WR-12 flags exactly this as "live risk when Phase 2 image traffic shares the channel"].
**How to avoid:** The multi-type dispatch required by Patterns 1-3 IS the fix — branch on the type byte before body arithmetic; reject unknown types. Add harness clauses.

### Pitfall 7: In-flight image buffer freed by the next capture
**What goes wrong:** New capture calls `freeCurrentImage()` while a pull is mid-stream → freed-memory reads, garbage chunks.
**Why:** `CameraManager` holds exactly one `currentImage`; D-19 requires the pull to continue.
**How to avoid:** The transfer queue takes ownership (malloc + memcpy into PSRAM) at capture time, before the next capture can free it. Budget: queue depth × image size (8 MB PSRAM; depth 2-3 is ample for a flight; cap and drop-oldest with a logged warning if exceeded).

### Pitfall 8: SD write latency vs. web responsiveness
**What goes wrong:** FAT allocation + sector writes block tens-to-hundreds of ms; combined with `server.handleClient()` the UI poll stutters.
**How to avoid:** Write chunks as they arrive (small, buffered by the FS layer); avoid rewriting the sidecar per chunk (write it at finalization); format the card FAT32 (not exFAT) before flight.

### Pitfall 9: Endianness drift in new payloads (IN-08 carried)
**What goes wrong:** `memcpy` of native structs emits little-endian on Xtensa; readers using `readUint16/32` (big-endian) misparse — Phase 1 already hit this (`handleAutoCaptureEnable` big-endian note [VERIFIED: src/main_basestation.cpp:933-936]).
**How to avoid:** All new wire fields via `CommandProtocol::writeUint16/writeUint32/readUint16/readUint32` [VERIFIED: src/command_protocol.cpp:325-347]. The GET_STATUS struct remains a known little-endian island — document, don't extend it; new threshold/status reporting uses explicit byte-order encoding.

### Pitfall 10: imageId wraparound and filename collisions
**What goes wrong:** `uint16` IDs wrap 65535→0; `IMG_0000.JPG` overwrites.
**How to avoid:** Zero-pad to 5 (`%05u`); treat wrap as accepted for v1 (a flight cannot reach 65 k captures at ≥1 s spacing) but include the ID in the sidecar for post-flight disambiguation. Duplicate-sequence re-execution (WR-03, carried) can also burn IDs — re-requested window commands must be idempotent (chunk send by index, no new ID), which Pattern 3 gives for free.

### Pitfall 11: Manifest totalSize trust
**What goes wrong:** A corrupt/malicious manifest announces a huge `totalSize`; seek-past-end or disk exhaustion.
**How to avoid:** Validate at manifest time: `totalSize <= MAX_IMAGE_SIZE` (50000 exists today [VERIFIED: include/base_station_config.h:50] — planner should decide whether UXGA support raises it), `totalChunks == ceil(totalSize/chunkSize)`, `chunkSize <= 200`; NACK/ignore otherwise.

### Pitfall 12: Trigger storm from GPS jitter (D-28 is the guard)
**What goes wrong:** Noisy altitude/position deltas machine-gun captures, flooding the transfer queue and the radio.
**How to avoid:** D-28 minimum spacing (suggest 20 s) enforced inside AutoCapture across ALL automatic sources; hysteresis on deltas (only count movement beyond threshold from the last-capture baseline); require GPS `valid` before distance evaluation.

## Code Examples

### Window-request payload (proposed — planner finalizes constants)
```cpp
// Source: proposed, derived from the verified Phase 1 payload pattern
// (PayloadAutoCaptureEnable big-endian encoding, include/command_protocol.h:153-155)
struct PayloadImageWindowRequest {
    uint16_t imageId;     // BE via writeUint16
    uint16_t startChunk;  // BE
    uint8_t  count;       // chunks in this window (<= window max, e.g. 16)
};

uint16_t seq = CmdSender().sendCommand(CameraCommand::IMAGE_WINDOW_REQUEST, payload, 5);
// tracked like any Phase 1 command; resolved by the window-complete RESPONSE
```

### CRC32 at both ends (D-23)
```cpp
// Source: [CITED: techoverflow.net/2022/08/05/how-to-compute-crc32-with-ethernet-polynomial-0x04c11db7-on-esp32-c-h/]
#include <esp_rom_crc.h>
uint32_t crc = esp_rom_crc32_le(0, imageBuffer, imageLen);
// identical call on the base station over reassembled bytes -> compare against manifest crc32
```

### SD init with custom SPI pins on ESP32-S3 (base station)
```cpp
// Source: [CITED: randomnerdtutorials.com/esp32-microsd-card-arduino/ +
//          github.com/espressif/arduino-esp32/issues/8457 (custom pins ignored unless instance passed)]
#include <SPI.h>
#include <SD.h>
SPIClass sdSPI(HSPI);                 // FSPI/HSPI instance, NOT the default bus
sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
if (!SD.begin(SD_CS_PIN, sdSPI)) { /* degrade: no storage, warn in UI — no fabricated state */ }
```

### Enlarged RX buffer before begin (Pitfall 2)
```cpp
// Source: [CITED: docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html]
LoRaSerial.setRxBufferSize(1024);     // BEFORE begin(); ignored after
E32LoRaModule().begin(&LoRaSerial, LORA_RX_PIN, LORA_TX_PIN, LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN, LORA_BAUD_RATE);
```

### Event-trigger evaluation sketch (extends AutoCapture, D-25..D-28)
```cpp
// Source: proposed; constants are defaults for planner confirmation
// Altitude: Sensors().getGPSData().altitude (already fed to SysState each loop,
//           [VERIFIED: src/main_balloon.cpp:639 setCurrentAltitude(gpsData.altitude)])
// Phase:    SysState().getFlightPhase() — 8-value enum [VERIFIED: src/system_state.h:29-38],
//           active by default ([VERIFIED: flightModeEnabled = true, src/system_state.cpp:33])
static constexpr float  EVENT_ALT_DELTA_M      = 150.0f;
static constexpr float  EVENT_DIST_DELTA_M     = 500.0f;
static constexpr uint32_t EVENT_MIN_SPACING_MS = 20000;   // D-28 band midpoint
```

### Receive-side type dispatch (WR-12 fix shape)
```cpp
// Source: proposed extension of the verified framing path (src/command_sender.cpp:286-317)
// once receiveIndex >= CMD_HEADER_SIZE, switch on the type byte BEFORE body arithmetic:
switch (receiveBuffer[2]) {
    case static_cast<uint8_t>(PACKET_TYPE_RESPONSE):      /* existing 7+4+bodyLen+4 path */ break;
    case static_cast<uint8_t>(PACKET_TYPE_IMAGE_MANIFEST):/* 7+manifestLen+4 path */ break;
    case static_cast<uint8_t>(PACKET_TYPE_IMAGE_CHUNK):   /* 7+5+bodyLen+4 path (same formula as commands) */ break;
    default: resetReceiveState(); /* unknown type — discard frame */ break;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| SPI LoRa (Sandeep Mistry, `lora_comm.cpp`) | UART E32-900T30D via `E32LoRa` | Pre-Phase 1 | SPI code excluded from builds; priority queue in it is dead — PRI-01 needs a new arbiter |
| Legacy 30 s capture timer | AutoCapture sole authority (Phase 1 CR-05 closure) | Phase 1 | Event triggers MUST extend AutoCapture, not add a parallel timer |
| Flat 2 s ACK timeout | Per-command classes (D-05) | Phase 1 plan 01-02 | Window requests need their own class |
| Delimiter-scanned framing | Length-driven framing (CR-03) | Phase 1 plan 01-02 | New image types inherit stream-safe framing for free |

**Deprecated/outdated:**
- `.planning/codebase/ARCHITECTURE.md` predates Phase 1 — per CONTEXT.md, source + STATE.md are authoritative where they disagree (verified during this research: the SPI description in ARCHITECTURE.md is stale).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | E32 modules run the EBYTE factory default air rate of 9.6 kbps (config never written: `setParameters`/`writeConfig` uncalled, WR-07 confirms config API issues) | Airtime math, all timing figures | All transfer-time estimates scale inversely; at 19.2 kbps everything is ~2x faster; at 0.3 kbps the 10 s thumbnail window is impossible — confirm in hardware UAT before tuning constants |
| A2 | `TinyGPSPlus::distanceBetween(lat1, lon1, lat2, lon2)` exists as a static helper | Don't Hand-Roll | Trivial — fall back to a local haversine |
| A3 | QQVGA q15 JPEG ≈ 4-12 KB; q20 ≈ 2.5-4 KB (community-reported ranges, scene-dependent) | Thumbnail sizing | If scenes produce larger JPEGs, thumbnail quality must drop further or IMG-02's 10 s window is missed; measure on hardware with real scenes |
| A4 | Physical SD card module is (or will be) wired to the base station — hardware constraint per PROJECT.md, but no pin assignment exists in code | IMG-05, SD section | Blocks the SD tasks; needs user confirmation of pins before or at implementation (Open Question Q2) |
| A5 | E32 transmit of a 240 B packet occupies ~210-280 ms (200 ms payload at 9.6 kbps + preamble/header margin) | All throughput figures | Same sensitivity as A1 |
| A6 | Default full-image captures are QVGA/q10 ≈ 8-15 KB (`BALLOON_CAMERA_FRAMESIZE FRAMESIZE_QVGA`, `BALLOON_CAMERA_QUALITY 10` are verified defaults [VERIFIED: include/balloon_config.h:40-41]; the byte range is the assumption) | Transfer-time figures | UXGA settings lengthen transfers to minutes — acceptable under PRI-03 but worth UI messaging |
| A7 | Base-station PSRAM is physically present (board is ESP32-S3 DevKitC-1 w/ OPI PSRAM config) despite the env's missing `-DBOARD_HAS_PSRAM` flag | Pattern 5 rationale | Pattern 5 (stream-to-SD) does not depend on it — no functional risk |

## Open Questions

1. **PRI-01 scope: there is no telemetry on the E32 link today.** D-18 references a priority queue that is stubbed dead code; `PacketHandler::sendPacket()` is a placeholder and `processCommunications()` is commented out [VERIFIED: src/packet_handler.cpp:156-157, src/main_balloon.cpp:654-684]. SC-5 ("Telemetry continues to update at 5-second rate during image transfers") cannot be observed without telemetry flowing. Options: (a) add a minimal telemetry beacon packet over E32 (small new packet type; balloon already computes `TelemetryData` every 5 s [VERIFIED: `TELEMETRY_INTERVAL_MS 5000`, src/main_balloon.cpp:60]); (b) interpret PRI-01 structurally (arbitration exists; telemetry display deferred to Phase 3 WEB-01). **Recommendation: (a)** — it is the only reading that satisfies SC-5 and PRI-01's "on LoRa link" wording, and it is ~1 small module. Planner should surface this for confirmation.
   - What we know: dead code paths, verified; requirement wording verified.
   - What's unclear: whether the user intends telemetry-over-E32 as Phase 2 deliverable surface.
   - Recommendation: include a minimal beacon; keep the packet type reserved for Phase 3's richer telemetry.
2. **SD card wiring/pins on the base station.** No SD pins in any config [VERIFIED: include/base_station_config.h, include/sensor_pins.h — no SD defines; base uses only LoRa 14/48/19/20/21 and LED 39 [VERIFIED: src/main_basestation.cpp:24-32]]. Free S3 GPIOs must be chosen to avoid future conflicts. Needs user/hardware input (or a `checkpoint:human-verify` in the plan).
3. **Flight-phase transitions that fire captures** (Claude's discretion): FlightPhase has **8** values — `GROUND, LAUNCH, POWERED_ASCENT, BALLOON_ASCENT, APEX, PARACHUTE_DESCENT, LANDING, RECOVERY` [VERIFIED: src/system_state.h:29-38] — note CONTEXT D-25 describes a 6-state chain; the wire enum is authoritative. Recommendation: fire on first entry to `LAUNCH`, `APEX`, `PARACHUTE_DESCENT`, `LANDING` (4 of 8) — the four operationally meaningful moments; skip GROUND/RECOVERY (pre/post flight) and the two ascent sub-phases (altitude-delta triggers already cover steady ascent).
4. **`MAX_IMAGE_SIZE` (50000) vs. UXGA support.** A UXGA q10 JPEG can exceed 50 KB; either raise the manifest-validation cap, cap capture resolution during transfers, or accept NACK of oversized images. Recommendation: keep 50000 for v1 (QVGA default is well under), document the limit in the sidecar/UI.
5. **SD-full policy (Claude's discretion):** recommend **stop-storing-and-warn** — no silent deletion of flight history; disk-full surfaces in the UI and sidecar flag. A 16-32 GB card holds >100k QVGA images, making the event unlikely. Flag in plan per CONTEXT instruction.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| PlatformIO CLI | builds | ✓ | 6.1.19 | — |
| Node.js | wire-format harness (`scripts/verify_protocol_roundtrip.mjs`) | ✓ | v24.8.0 | — |
| arduino-esp32 core (SD/FS/esp_rom_crc) | base station SD, both CRC32 | ✓ | via espressif32 platform | — |
| 2× ESP32-S3 + E32 radios (hardware) | end-to-end UAT | Presumed (project hardware) | — | Host harness + dual-target builds for code-level work (Phase 1 precedent) |
| SD card module on base station (wiring) | IMG-05 | ✗ **unestablished** | — | None — must be wired/confirmed (Open Question Q2) |
| Camera + GPS + BMP280 on balloon (hardware) | event triggers, thumbnails | Presumed | — | Code-level via existing module APIs |

**Missing dependencies with no fallback:**
- SD card module wiring/pins on the base station — blocks the SD storage tasks until pins are chosen (software can be written against constants; hardware verification will wait for UAT).

**Missing dependencies with fallback:**
- None beyond the above.

## Security Domain

> `security_enforcement` is absent from `.planning/config.json` → treated as enabled.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No (this phase) | Open AP + unauthenticated endpoints are the carried WR-08, routed to `/gsd-secure-phase`; no new auth surface added |
| V3 Session Management | No | Stateless polling UI |
| V4 Access Control | Partially | New write-ish surfaces: threshold settings commands and (future) image routes — same exposure class as existing Phase 1 routes; keep on the WR-08 ledger |
| V5 Input Validation | **Yes** | All receive paths: length-bounded framing (existing), NEW type-byte validation (WR-12 fix), manifest field validation (totalSize/totalChunks/chunkSize bounds), chunk index bounds vs. totalChunks, dataLen <= 200 |
| V6 Cryptography | No | CRC16/CRC32 are integrity, not authenticity — see threat table |

### Known Threat Patterns for ESP32 LoRa firmware

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Spoofed commands/manifests on the shared RF channel (CRC-valid frame from a third party) | Spoofing/Elevation | Out of scope for v1 (no link crypto); bounded blast radius via manifest validation (Pitfall 11) and type dispatch (Pitfall 6); record as accepted risk for `/gsd-secure-phase` |
| Corrupt-frame parsing crashes (oversized lengths, huge totalSize) | Tampering/DoS | Existing bounds checks (`expectedTotal > sizeof(receiveBuffer)`, `bodyLen > CMD_MAX_PAYLOAD_SIZE`) [VERIFIED: src/command_handler.cpp:660-663] extended to new types; manifest caps |
| SD exhaustion via pathological manifests | DoS | totalSize cap + D-24 finalize-incomplete on bounded rounds |
| Heap exhaustion via transfer-queue depth | DoS | Fixed queue depth (2-3) with drop-oldest + logged warning (Pitfall 7) |

## Sources

### Primary (HIGH confidence — direct source reads this session)
- `include/command_protocol.h` + `src/command_protocol.cpp` — wire format, constants, byte-order helpers, factories
- `src/command_sender.cpp` + `src/command_handler.cpp` — framing paths, ACK machinery, timeout classes, mappings
- `src/e32_lora.cpp` — blocking transmit semantics, AUX waits
- `src/main_balloon.cpp` + `src/main_basestation.cpp` — loop wiring, dead telemetry path, UI patterns
- `src/camera_manager.cpp` — CR-04/WR-11 defect site, estimate function, thumbnail mechanics
- `src/auto_capture.cpp` + `include/auto_capture.h` — interval authority, image-ID sequence
- `src/system_state.h/.cpp` — FlightPhase enum (8 values), detection thresholds, `flightModeEnabled = true`
- `src/stubs.cpp` + `platformio.ini` — LoRaComm stub proof, build-filter discipline
- `.planning/phases/01-command-protocol-control/01-VERIFICATION.md` — CR-04 deferral, WR-02/03/04/12 carries
- `include/balloon_config.h`, `include/base_station_config.h`, `include/sensor_pins.h` — defaults, limits, pins

### Secondary (MEDIUM confidence — official documentation via web)
- EBYTE E32 user manual (cdebyte.com) — 512-byte buffer, auto sub-packing, transparent mode semantics
- docs.espressif.com arduino-esp32 Serial API — `setRxBufferSize` before `begin()`, 256-byte default
- docs.espressif.com / Random Nerd Tutorials + arduino-esp32 issue #8457 — SD with custom SPI pins, `SD.begin(CS, spi)` instance pattern
- techoverflow.net + sourcevu (esp-idf) — `esp_rom_crc32_le` usage and semantics
- esp32-camera GitHub issue #150 — JPEG quality 10-20 reliability range

### Tertiary (LOW confidence — community only, marked for hardware validation)
- OV2640 QQVGA JPEG byte-size ranges (A3)
- Community LoRa image-transfer practice cross-check (stuartsprojects, MDPI Sensors 2024) — consistent with locked D-21/D-23 design, no new information load-bearing

## Metadata

**Confidence breakdown:**
- Codebase/protocol facts: HIGH — every load-bearing value read directly from source with line citations this session
- Airtime/throughput math: MEDIUM — arithmetic is exact, but the air-rate input (9.6 kbps factory default) is unverified on the physical modules (A1)
- SD/thumbnail sizing: MEDIUM — official-doc patterns verified; byte sizes scene-dependent (A3)
- PRI-01 scope: LOW-MEDIUM — the gap is proven; the intended remedy is a genuine open question (Q1)

**Research date:** 2026-08-19
**Valid until:** 2026-09-18 (embedded codebase facts stable; re-verify hardware assumptions after UAT)

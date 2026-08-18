---
phase: 02-image-transmission
plan: "03"
subsystem: image-transfer
tags: [lora, image-transfer, windowed-arq, sd-storage, crc32-verification, fifo, esp32, degrade-never-halt, transfer-ui]

# Dependency graph
requires:
  - phase: 02-image-transmission (02-01)
    provides: base receive half (manifest/chunk/beacon deserialization + thumbnail RAM reassembly + retained-latest serving), IMG_* constants, PayloadImageWindowRequest layout
  - phase: 02-image-transmission (02-02)
    provides: balloon pull half (ANNOUNCED entries, idempotent window arming, one-chunk-per-pass servicing from PSRAM, ACK/NACK_INVALID/NACK_BUSY responses), 0x14 telemetry beacon transmit
provides:
  - SdStorage module (SDStorage()): dedicated SPIClass(HSPI) init on editable pins (GPIO 12/13/11/10), flat /images layout IMG_{id:05d}.JPG / IMG_{id:05d}_T.JPG / IMG_{id:05d}.JSON sidecar (D-29/D-31/D-32), seek+offset stream-to-SD chunk writes (Pattern 5), sidecar written once at finalization (D-30 field set + "kind"), stop-storing-and-warn disk-full policy with NO deletion ever (Q5), degrade-never-halt begin()
  - Base window ARQ pull half (D-21): 16-chunk IMAGE_WINDOW_REQUEST windows through the Phase 1 tracked-command machinery with the new CMD_ACK_TIMEOUT_WINDOW_MS (15000 ms) class; base speaks only on manifest-while-idle / window-complete / stall (Pitfall 5 — no free-running timers)
  - Bounded retransmission (D-24/PRI-03): per-image pass counter, IMG_WINDOW_STALL_MS (8 s) stall detection, finalize-incomplete after IMG_RETRANSMIT_MAX_PASSES (3) keeping partial bytes on SD, flagged in the sidecar, slot freed, FIFO advances
  - End-to-end integrity (D-23): complete only after esp_rom_crc32_le verification — thumbnails over the RAM reassembly, fulls over the STORED bytes read back in chained 256-byte pieces; SD-degraded fulls report INCOMPLETE with notStored, never a fabricated complete
  - D-20 transfer UI surface: locked QUEUED/RECEIVING/RETRYING/COMPLETE/INCOMPLETE vocabulary (single transferStateToString mapping), getTransferSnapshot(TransferRow*, max) in arrival order, /status transfers[] + storage{} additions, Image Transfers panel with percent bar / N-M chunks / state chips, GET /img/{id} full-image route, storage status chip (OK/UNAVAILABLE/FULL)
affects: [02-image-transmission (02-04 event thresholds UI, 02-VERIFICATION hardware UAT), Phase 3 gallery (sidecar JSON schema is its contract)]

# Actuals (#2632) — pairs with the plan's estimate to calibrate future estimates.
# Same estimateTokens scale (chars/4 over the realized diff), never a harness token count.
actuals:
  tokens: 21654    # 86614 diff chars / 4 (f1f920d + 9974cf8 + 0413e61) — plan estimated 52000; the diff is dense boilerplate-light C++, so the estimate was ~2.4x high
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []         # no new libraries — SD/FS ship with the arduino-esp32 core
  patterns:
    - Dedicated SPIClass(HSPI) instance + SD.begin(CS, sdSPI) instance-overload for custom SD pins (arduino-esp32 issue #8457: the pin-less SD.begin(CS) overload ignores custom pins entirely)
    - Slot-table transfer architecture: 8 ImageRxTransfer slots with monotonic arrivalSeq driving FIFO pull order (D-19), slot-pressure ladder (free -> oldest terminal recycle -> oldest non-pull eviction via finalizeIncomplete -> reject with log)
    - Chained esp_rom_crc32_le for read-back verification: pass the previous result (0 first) over 256-byte stack pieces — the ROM header documents chaining as supported, avoiding any whole-file RAM buffer
    - Snapshot-derived display state (D-20): TransferDisplayState computed at getTransferSnapshot time from bitmap/pass/terminal flags — states are never stored, so the UI cannot show a stale or fabricated state
    - Prefer-FULL chunk routing keyed on the 02-02 wire-order fact (thumbnail chunks always precede the full manifest of the same id; window answers follow it)

key-files:
  created:
    - include/sd_storage.h
    - src/sd_storage.cpp
  modified:
    - include/image_rx_manager.h
    - src/image_rx_manager.cpp
    - src/command_sender.cpp
    - src/main_basestation.cpp
    - include/base_station_config.h
    - platformio.ini

key-decisions:
  - "SD pins live as editable constants in base_station_config.h (SD_SCK_PIN 12 / SD_MISO_PIN 13 / SD_MOSI_PIN 11 / SD_CS_PIN 10 — FSPI-adjacent free GPIOs, no conflict with LoRa 14/48/19/20/21 or LED 39); wiring confirmation is the flagged hardware-UAT item, and differing wiring needs only a constant edit"
  - "Chunk routing resolves kind by slot lookup, preferring a non-terminal FULL slot over the THUMBNAIL slot for the same id — grounded in the 02-02 wire-order fact that thumbnail pushes always complete before the full announces, and window answers only follow the announcement"
  - "A queued-but-never-sent window request (command table full) charges NO pass: windowActive stays false and the stall path re-issues the span after 8 s — a request that never reached the air is not a retransmission"
  - "Stall (8 s) shorter than the window ACK timeout (15 s) can produce a duplicate window request; the balloon's idempotent re-arm (cursor reset, 02-02) absorbs duplicates and the bitmap drops re-delivered chunks — accepted and documented rather than adding a timer"
  - "SD-degraded full transfers finalize INCOMPLETE with notStored set even at 100% chunks received: without stored bytes there is nothing to CRC-verify, and complete is never fabricated"
  - "Sidecar "kind" field added beyond the D-30 list: both kinds share one id namespace and one sidecar name per id, so an explicit kind field prevents thumbnail/full ambiguity for Phase 3 readers"
  - "storedToSd computed inside SdStorage.finalizeImage from its own persisted-bytes tracking — the caller cannot fabricate it (prohibition discipline)"

requirements-completed: [IMG-03, IMG-04, IMG-05, PRI-03]

metrics:
  duration: 2 sessions (context compacted mid-Task 2; Task 1 + Task 2 start in the first session, Task 2 finish + Task 3 in the continuation) — active work ~90 min
  completed: 2026-08-19
  tasks_completed: 3
  commits: 3

status: complete
---

# Phase 02 Plan 03: Base SD Persistence + Full-Image Window Pull + Transfer UI Summary

Base-side pull half completed: SdStorage lands verified images on a custom-SPI microSD with once-at-finalization sidecars and a never-delete disk-full policy, ImageRxManager pulls fulls through 16-chunk windows on the Phase 1 tracked-command machinery with bitmap-exact progress and 3-pass bounded retransmission, and the web UI shows every transfer with locked-vocabulary state chips plus a GET /img/{id} route streaming stored fulls.

## Tasks Completed

| Task | Name | Commit | Key files |
|------|------|--------|-----------|
| 1 | SdStorage module (custom-SPI init, naming, stream writes, sidecar, stop-storing-and-warn) | f1f920d | include/sd_storage.h, src/sd_storage.cpp, include/base_station_config.h, platformio.ini |
| 2 | Window ARQ in ImageRxManager + window-request issuing (D-21..D-24) | 9974cf8 | include/image_rx_manager.h, src/image_rx_manager.cpp, src/command_sender.cpp |
| 3 | Base UI: transfer panel (D-20), /img/{id} route, storage status | 0413e61 | src/main_basestation.cpp |

## What Was Built

**Task 1 — SdStorage (IMG-05, Q5, D-29/D-30/D-31/D-32).** New singleton module: `static SPIClass sdSPI(HSPI)` with the instance-overload `SD.begin(SD_CS_PIN, sdSPI)` for custom pins; flat `/images` directory; `%05u` zero-padded names built only from numeric ids (T-02-08 — no network-supplied string ever enters a path); `writeChunk` seeks to `chunkIndex * chunkSize` and writes immediately (Pattern 5 — no whole-image RAM buffer, untrusted totalSize never pre-allocates); sidecar written exactly once at finalization as hand-built String JSON with the full D-30 field set plus `"kind"` and `"rssi":null` (documented E32-transparent-mode reason); `storedToSd` computed inside from persisted-bytes tracking. Disk-full policy: open/write failure degrades the module (available=false, writeFailed=true surfaced via getStatus()), existing files are never deleted or overwritten — grep-verified no `SD.remove`/`SD.rmdir` anywhere. begin() failure returns true: the station runs degraded, never halts.

**Task 2 — Window ARQ (D-21/D-22/D-23/D-24, IMG-03/IMG-04/PRI-03).** ImageRxManager reworked to an 8-slot table: each transfer carries an exact chunk bitmap, a window context (base/count of the last queued request), a monotonic pass counter, and terminal flags. Fulls pull through `issueWindowRequest` → `CmdSender().sendCommand(IMAGE_WINDOW_REQUEST, 5-byte payload)` with the new `CMD_ACK_TIMEOUT_WINDOW_MS` class in `ackTimeoutFor`; the base transmits only on manifest-while-idle, window-slice-complete, or stall. Stalls (8 s) re-request only the missing span (first-missing..last-missing, capped at 16); after 3 passes the image finalizes incomplete — partial bytes kept on SD, sidecar flags it, slot freed, FIFO advances to the next queued full. Completion verifies the manifest CRC32: thumbnails via the RAM reassembly, fulls by reading the stored file back in chained `esp_rom_crc32_le` 256-byte pieces. Manifests are fully validated before any allocation (Pitfall 11: totalSize 1..50000, chunkSize 1..200, totalChunks == ceil, known kind); duplicates drop idempotently; out-of-range indices and length mismatches are ignored. `getTransferSnapshot` derives QUEUED/RECEIVING/RETRYING/COMPLETE/INCOMPLETE per row from bitmap/pass/terminal truth — the single `transferStateToString` mapping.

**Task 3 — Base UI (D-20, IMG-04).** `initStorage()` beside `initLoRa` in setup; Image Transfers panel below Command Queue with one row per slot (id, THUMB/FULL badge, percent bar, N/M chunks, state chip — completed rows link to `/img/{id}` for fulls, `/img/{id}_t.jpg` for thumbnails); `/status` gains `transfers[]` (snapshot rows) and `storage{}` (OK/UNAVAILABLE/FULL, computed from SdStorageStatus — never hardcoded); storage chip in the status bar; `GET /img/{id}` streams stored fulls via `server.streamFile` with honest 404s; `commandDisplayName` covers IMAGE_WINDOW_REQUEST ("Image Window") for queue rows.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] Sidecar "kind" field added beyond the D-30 field list**
- **Found during:** Task 1
- **Issue:** D-30's field list does not name the image kind, but thumbnails and fulls share one id namespace and one sidecar path per id (`IMG_{id}.JSON`) — a Phase 3 reader could not tell which kind the sidecar describes.
- **Fix:** Added `"kind":"thumbnail"|"full"` to the sidecar JSON (the "file" path also disambiguates via the `_T` suffix, but the explicit field is the robust contract).
- **Files modified:** src/sd_storage.cpp
- **Commit:** f1f920d

**2. [Rule 2 - Missing functionality] /img/{id}_t.jpg SD fallback for older verified thumbnails**
- **Found during:** Task 3
- **Issue:** The 02-01 route served ONLY the retained newest thumbnail from RAM. This plan's transfer panel links completed THUMB rows for ANY id — without a fallback, every row except the newest would 404 the moment a later thumbnail verified.
- **Fix:** After the retained-RAM miss, the route falls back to `SDStorage().serveFile(id, THUMBNAIL)`; absence is still an honest 404.
- **Files modified:** src/main_basestation.cpp
- **Commit:** 0413e61

None of the remaining work deviated — plan executed as written otherwise.

## Known Limitations / UAT Flags

- **D-22 thumbnail window re-request gets FULL bytes (hardware UAT):** the 02-02 balloon services windows only from ANNOUNCED full entries — a base-side window request naming a thumbnail id cannot make the balloon re-send thumbnail bytes (they only flow during the initial push). The D-22 path is still implemented as specified (one mechanism), and the outcome is honest: the re-served bytes fail the thumbnail's length/CRC checks, and the thumbnail finalizes INCOMPLETE after the D-24 pass bound. Real-world thumbnail-hole frequency is the UAT measurement; if it proves common, a balloon-side thumbnail-window capability is a future plan.
- **SD wiring assumption (research Q2/A4):** microSD on GPIO 12/13/11/10 is unconfirmed hardware; constants are editable in base_station_config.h. FAT32 (not exFAT) required.
- **Hardware UAT (deferred, per plan):** full image completes and opens via /img/{id}; card pull shows IMG_{id}.JPG/_T.JPG/.JSON; link loss mid-transfer finalizes incomplete after 3 passes with the slot freed; SD absence leaves the station serving RAM thumbnails with an honest storage chip.

## Auth Gates

None — no authentication was required during execution.

## Known Stubs

None — every UI value (transfer rows, storage chip, queue rows) is wired to live computed state; no placeholder text, no mock data paths.

## Verification Results

- `pio run -e esp32-s3-basestation` SUCCESS (RAM 15.1%, Flash 30.9%) and `pio run -e esp32-s3-balloon` SUCCESS (RAM 41.0%, Flash 14.7%)
- All Task acceptance greps pass: SPIClass in sd_storage.cpp; %05u naming; IMG_WINDOW_MAX_CHUNKS / IMG_RETRANSMIT_MAX_PASSES / IMAGE_WINDOW_REQUEST / esp_rom_crc32_le in image_rx_manager.cpp; CMD_ACK_TIMEOUT_WINDOW_MS in command_sender.cpp; getTransferSnapshot / storage / chunks / img/ in main_basestation.cpp
- `node scripts/verify_protocol_roundtrip.mjs` — all wire-format regression checks pass (exit 0)
- Prohibition greps: no `SD.remove`/`SD.rmdir` anywhere in src/ or include/ (flight history never recycled); no fabricated-complete path exists (complete is set only after CRC verification)

## Self-Check: PASSED

All 9 created/modified files exist on disk; all 3 task commits (f1f920d, 9974cf8, 0413e61) present in git log.

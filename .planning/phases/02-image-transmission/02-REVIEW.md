---
phase: 02-image-transmission
reviewed: 2026-08-18T21:45:08Z
depth: standard
files_reviewed: 21
files_reviewed_list:
  - include/auto_capture.h
  - include/base_station_config.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/image_tx_manager.h
  - include/sd_storage.h
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
  - src/auto_capture.cpp
  - src/camera_manager.cpp
  - src/camera_manager.h
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
  - src/sd_storage.cpp
findings:
  critical: 3
  warning: 8
  info: 7
  total: 18
status: issues_found
---

# Phase 2: Code Review Report

**Reviewed:** 2026-08-18T21:45:08Z
**Depth:** standard
**Files Reviewed:** 21
**Status:** issues_found

## Summary

Reviewed the Phase 2 image-transmission layer end-to-end: wire protocol (manifest/chunk/beacon framing on the Phase 1 transport), the balloon TX queue and window-pull service, the base RX reassembly/window-ARQ driver, SD persistence, event-triggered capture, and both firmware entry points. Cross-referenced against `common_types.h`, `e32_lora.h/.cpp`, `system_state.h`, `sensor_manager.h`, and `stubs.cpp` for cross-module facts.

The framing/serialization layer is solid: length-driven receivers bound every buffer before writing, type dispatch happens before body arithmetic, CRC and end-marker checks are placed correctly, and the big-endian encode/decode pairs are symmetric. The `.mjs` regression harness faithfully mirrors the C++ and its "teeth" clauses genuinely discriminate the fixed from the defective variants.

However, the **transfer state machines have three critical cross-module defects**. (1) The D-22 "thumbnail holes heal via windowed pull" mechanism cannot work: window requests carry no `kind`, and the balloon only ever serves FULL-image chunks, so a stalled thumbnail triggers a spurious full-image window pull — and can evict the entry underlying an in-progress pull. (2) `ImageRxManager::allocateSlot`'s slot-pressure eviction path returns a slot that `startTransfer` never re-initializes, leaving `terminal=true` and stale counters — a zombie slot that can never progress and eventually exhausts all 8 slots. (3) The balloon's push-priority arbitration starves window service for 9–12 s per capture while the base abandons a pull after ~24–32 s without progress (8 s stall × 3 passes, `passCount` never resets) — under the documented default capture cadence (20 s spacing) every full-image pull systematically finalizes INCOMPLETE.

## Critical Issues

### CR-01: Thumbnail hole-healing (D-22) requests the wrong kind — window requests cannot address thumbnails, and the misdirected pull can kill an active transfer

**File:** `src/image_rx_manager.cpp:157-189`, `src/image_tx_manager.cpp:517-591`, `include/image_protocol.h:146-150`
**Issue:** `PayloadImageWindowRequest` carries only `(imageId, startChunk, count)` — no image kind. The base's thumbnail stall fallback (`ImageRxManager::process()` lines 157–189) issues a window request for a stalled THUMBNAIL slot's `imageId`. But the balloon's `handleWindowRequest` only matches entries in state `ANNOUNCED` (post-full-manifest), and `serviceWindowChunk` slices exclusively from `entry.fullBuffer`. Three consequences:

1. The re-requested "thumbnail" chunks are FULL-image bytes. On the base, `onChunkFrame` (lines 224–227) routes chunks to the non-terminal FULL slot first, so the thumbnail's holes are never healed — every thumbnail that loses one chunk finalizes INCOMPLETE even though the base's balloon-side entry still holds the thumbnail bytes in PSRAM. The D-22 requirement ("a thumbnail with holes falls back to the same windowed pull") is unimplementable as wired.
2. The misdirected request consumes airtime pulling an arbitrary region of the full image (thumbnail chunk numbering applied to the full's chunk space).
3. If the stalled thumbnail's id is NEWER than the entry being pulled, `handleWindowRequest` calls `evictEntriesOlderThan(*target)` (line 561) BEFORE arming — which frees the older entry that is the base's current active pull, mid-stream. The active pull's subsequent requests get `UNKNOWN_IMAGE`, stall passes accumulate, and the pull dies.

The thumbnail stall loop also issues requests without checking whether a full pull is active, violating the module's own "the base speaks ONLY on window completion / stall of the active pull / manifest-while-idle" discipline (Pitfall 5).

This is not a rare path: `IMG_TX_QUEUE_DEPTH=3` with FIFO push ordering means a later thumbnail's chunks routinely wait 10–25 s behind older entries' pushes (each ~300–400 ms per chunk transmit), so the base's 8 s `IMG_WINDOW_STALL_MS` fires during healthy-but-slow pushes and triggers this path in normal operation.

**Fix:** Extend the wire contract so a window request can name the kind, and serve both buffers on the balloon:
```cpp
// include/image_protocol.h — add kind byte (6-byte payload)
struct PayloadImageWindowRequest {
    uint16_t imageId;     // BE16
    uint8_t  imageKind;   // ImageKind (THUMBNAIL / FULL_IMAGE)
    uint16_t startChunk;  // BE16
    uint8_t  count;
};
```
On the balloon, arm the window against the entry's `thumbBuffer`/`thumbTotalChunks` when `imageKind == THUMBNAIL` (validate `startChunk` against that kind's chunk count in `INVALID_RANGE` checks). On the base, gate the thumbnail fallback so it never fires while a full pull is active (`findActivePull() == nullptr`), and only after the balloon's thumbnail push for that entry is provably finished (e.g., the entry's full manifest has arrived, since the push always precedes the announcement).

### CR-02: Slot-pressure eviction returns a slot that `startTransfer` never re-initializes — zombie transfer slots with corrupted accounting

**File:** `src/image_rx_manager.cpp:328-347` (allocateSlot step 3), `src/image_rx_manager.cpp:402-437` (startTransfer)
**Issue:** The re-manifest path in `startTransfer` (the `else` branch, lines 408–418) correctly full-resets the slot via `*t = ImageRxTransfer{}`. But the `allocateSlot` path does not. Step 3 evicts a non-terminal, non-pull slot by calling `finalizeIncomplete(*oldest, "slot pressure")` — which sets `terminal = true`, `complete = false`, frees the bitmap/buffer, and leaves `receivedCount`, `bytesReceived`, `passCount`, `windowActive`, `crcMismatch`, `notStored` at their stale values — then returns the slot. `startTransfer` proceeds to overwrite only the manifest-derived fields; it never clears `terminal`, `receivedCount`, `bytesReceived`, `passCount`, or `windowActive`.

Consequences for the "new" transfer living in that slot:
- `terminal == true` from its first byte: `findActivePull()` skips it (`!t.terminal`), the `process()` thumbnail loop skips it, `onChunkFrame` discards all its chunks ("chunk for finalized image ... ignored"). The transfer can never progress, never stall, and never finalize — a zombie that still shows in `getTransferSnapshot()` as INCOMPLETE.
- Each zombie permanently consumes one of `RX_TRANSFER_SLOTS = 8`. After 8 slot-pressure evictions, `allocateSlot` step 4 rejects every manifest ("no transfer slot available") — the base stops receiving images entirely until reboot.
- If `terminal` were not set, the stale `receivedCount` would corrupt completion detection (`receivedCount == totalChunks` fires early with holes in the fresh bitmap → spurious CRC-mismatch finalize) and `bytesReceived` would report wrong sidecar statistics.

Reachability: 8 simultaneous non-terminal slots requires 4 captures in flight with none finalized — plausible during event+interval bursts, especially given CR-01 (thumbnails that can never complete hold slots for ~32 s each) and CR-03 (pulls queued behind one starved active pull).

**Fix:** In `allocateSlot` step 3, reset the slot the same way step 2 does, before returning it:
```cpp
if (oldest != nullptr) {
    Serial.printf("ImageRx: slot pressure — evicting pending transfer image %u kind %u "
                  "(finalized incomplete)\n", oldest->imageId, oldest->imageKind);
    finalizeIncomplete(*oldest, "slot pressure");
    releaseSlotWork(*oldest);
    *oldest = ImageRxTransfer{};   // full re-init — no stale terminal/counters
    return oldest;
}
```
(`finalizeIncomplete` already calls `releaseSlotWork`; the explicit reset is what removes the stale flags.)

### CR-03: TX push-priority starvation vs RX stall/pass-bound — full-image pulls systematically finalize INCOMPLETE under the documented capture cadence

**File:** `src/image_tx_manager.cpp:354-385` (`pushPending` priority), `include/image_protocol.h:76` (`IMG_WINDOW_STALL_MS = 8000`), `src/image_rx_manager.cpp:114-146` (stall/pass logic), `src/image_tx_manager.cpp:281-295` (queue overflow drop-oldest)
**Issue:** Two sides of the link hold contradictory assumptions about how long a window may go unserved:

- Balloon: `pushPending()` always transmits push work (thumbnail manifest/chunks, full announcement) before window service, with no interleave budget. A single capture injects `thumbTotalChunks + 2` push passes at ~300–500 ms each (plus a beacon pass every 5 s). A typical QQVGA thumbnail (~6 KB = 30 chunks) starves window service for ~10–13 s per capture. The header comment asserts "the in-progress pull is never abandoned: its window context simply waits for the push work to drain" — an unbounded wait.
- Base: any gap > `IMG_WINDOW_STALL_MS` (8 s) without an accepted chunk charges a pass (`passCount++`), and `passCount >= IMG_RETRANSMIT_MAX_PASSES` (3) finalizes the pull INCOMPLETE. `passCount` is never reset on progress (only full slot re-init clears it) — verified: lines 125 and 175 only increment.

With the Phase 2 default cadence (D-28 min spacing 20 s → one capture per 20 s), each capture's ~10–13 s push gap exceeds the 8 s stall, so a pass is charged per capture. A 250-chunk full image (50 KB cap) needs ~16 window rounds ≈ 114 s of continuous service; it cannot finish within 3 charged passes (~24–32 s). Every full-image pull therefore fails INCOMPLETE under sustained capture — the core Phase 2 feature (windowed full-image ARQ) does not survive its own capture-rate design point.

Compounding it: `IMG_TX_QUEUE_DEPTH = 3` with 15-minute TTL means captures every 20 s overflow the queue within a minute; `enqueueCapture`'s drop-oldest then frees the very entry the base is actively pulling (`handleWindowRequest` subsequently returns `UNKNOWN_IMAGE`, burning the base's retries and stall passes).

**Fix:** (a) Bound push-induced starvation on the balloon: interleave window service — e.g., in `pushPending()`, service the armed window every N-th pass, or preempt push work when the armed window's `lastActivityMs` exceeds ~5 s. (b) On the base, only charge a pass when the balloon was actually idle (no push traffic observed — e.g., no frame of any type from that balloon recently), or reset `passCount` to 0 whenever a window slice completes with progress. (c) On queue overflow, prefer evicting entries that are not `ANNOUNCED`-with-armed-window before dropping the pull target.

## Warnings

### WR-01: Balloon CommandHandler silently drops a second complete command parsed in the same drain

**File:** `src/command_handler.cpp:72-125`, `src/command_handler.cpp:819-830`
**Issue:** `process()` first drains ALL available UART bytes (`while (lora->available() > 0)`), then executes ONE command. `processIncomingByte` assigns `pendingCommand.packet = cmd` on every complete frame with no queue — if two complete command frames arrive in the same drain (e.g., a queued GET_STATUS poll and an IMAGE_WINDOW_REQUEST transmitted back-to-back by `CmdSender::process`, which can transmit multiple commands per pass), the second overwrites the first before execution. The first command is never executed and never acknowledged; recovery depends entirely on the base's retry cycle (a full D-05 timeout + retransmit, 5–15 s per command class).
**Fix:** Track overflow: add a `hasCommand` guard in `processIncomingByte` — if a command is already pending and un-executed, either buffer a second slot or leave the frame unacknowledged deliberately with a log line naming the dropped sequence, so the failure is observable rather than silent.

### WR-02: Balloon RX UART buffer not enlarged — asymmetric with the base's own "Pitfall 2" fix

**File:** `src/main_balloon.cpp:376-401` (no `setRxBufferSize`), contrast `src/main_basestation.cpp:644-649`
**Issue:** The base enlarges its E32 UART RX buffer to 1024 bytes specifically because ~216-byte frames "in quick succession ... overrun[s]" the default 256-byte buffer. The balloon keeps the default 256 while `executeCommand` can block for hundreds of ms (CAPTURE_NOW sensor capture, plus the ~0.3–7 s synchronous `sendResponse` AUX waits). Two command frames arriving during that block (each up to 216 bytes) exceed 256 bytes and corrupt/lose bytes. Recovery is via base retry, at the cost of full timeout cycles, and the framing-resync flaw (WR-04) makes post-overrun recovery worse.
**Fix:** In `initializeSubsystems()` (before `E32LoRaModule().begin(...)`), mirror the base: `Serial2.setRxBufferSize(1024);` — the balloon passes `&Serial2` as `loraSerial`.

### WR-03: Manifest transmits are one-shot with no retry — a single failed frame permanently loses the whole transfer

**File:** `src/image_tx_manager.cpp:387-423` (`pushThumbManifest`), `src/image_tx_manager.cpp:474-511` (`announceFullManifest`)
**Issue:** Both functions advance state (`PUSH_THUMB_MANIFEST → PUSH_THUMB_CHUNKS`, `ANNOUNCE_FULL → ANNOUNCED`) even when `serialize*/lora->transmit` returns false. Chunk loss is designed to heal via D-21/D-22 pulls, but a lost MANIFEST has no heal path: thumbnail chunks then arrive at a base with no slot ("chunk for image N with no matching manifest ignored") and the image never appears; a lost FULL manifest means the base never queues the pull and the entry sits `ANNOUNCED` until TTL eviction, never transferring. Given the E32 `transmit` failure modes (AUX timeouts, lines 165–193 of `e32_lora.cpp`), this is reachable on any transient AUX glitch.
**Fix:** On `!ok`, retry the manifest once or twice before advancing state (it is a single ~38-byte frame): e.g., keep a `manifestAttempts` counter on the entry and only advance after either success or a small bound (3), logging the loss. Advancing unconditionally on the first failure guarantees silent loss.

### WR-04: Frame-resync flaw — a stray 0xAA immediately before a real frame start causes the frame to be lost

**File:** `src/command_handler.cpp:766-777`, `src/command_sender.cpp:279-290`
**Issue:** The start-byte hunt uses two states (`receiveIndex == 0` waits 0xAA; `receiveIndex == 1` waits 0x55; anything else resets to 0 and DISCARDS the current byte). For the byte stream `0xAA 0xAA 0x55 ...`: the first 0xAA is buffered, the second 0xAA fails the 0x55 test and is dropped (instead of being reconsidered as a new start byte), and the following 0x55 finds `receiveIndex == 0` and is also dropped. The legitimate frame whose start was preceded by a noise byte 0xAA is lost entirely. For command frames this is masked by retries; for pushed manifests/chunks (no retry, see WR-03) it directly converts into lost images.
**Fix:** In the reset branch, re-evaluate the incoming byte as a potential start: `receiveIndex = (byte == CMD_START_BYTE1) ? 1 : 0;` — i.e., store it rather than dropping it. The same fix should be mirrored in `scripts/verify_protocol_roundtrip.mjs`'s receiver mirrors so the harness locks the behavior.

### WR-05: Thumbnail and full sidecars share one file name — the second finalization truncates the first's metadata

**File:** `src/sd_storage.cpp:93-95` (`sidecarPath` ignores kind), `src/sd_storage.cpp:246-315`
**Issue:** Both kinds of image id N write `/images/IMG_%05u.JSON`. Thumbnail N finalizes first (sidecar v1: kind=thumbnail, its chunk accounting, its telemetry context). Later, full N finalizes and `SD.open(sidePath, FILE_WRITE)` (mode "w", truncate) overwrites v1. The thumbnail's D-30 record is lost, contradicting the module's own contract ("IMG_{id}.JSON sidecar written ONCE at finalization (D-30, Pitfall 8)"). The image files themselves (`IMG_N.JPG` vs `IMG_N_T.JPG`) are distinct — only the JSON collides.
**Fix:** Make the sidecar name kind-suffixed like the image: `IMG_%05u_T.JSON` for thumbnails (`sidecarPath` gains the kind parameter), or merge both records into one JSON document keyed by kind at second finalization (read-modify-write). The suffix approach matches the existing naming scheme.

### WR-06: `openTransfer` re-open for the same (id, kind) leaks the previous open file handle

**File:** `src/sd_storage.cpp:136-157`
**Issue:** The close-guard only fires when `*handleId != imageId`. On a same-id re-open (the documented re-manifest restart path, `image_rx_manager.cpp:408-418`), the old `File` object holding an open FATFS handle is overwritten by assignment (`*handle = f;`) without `close()`. Each same-id re-push leaks one file-object slot; FATFS has a bounded handle table, and repeated re-pushes (or an attacker-crafted duplicate manifest stream) exhaust it, after which all SD writes fail and the module degrades.
**Fix:** Close unconditionally when a handle is already open for this kind:
```cpp
if (*handle) {
    if (*handleId != imageId) {
        Serial.printf("SdStorage: closing partial %s before opening image %u\n",
                      handle->name(), imageId);
    }
    handle->close();
    *handle = File();
}
```

### WR-07: ImageTx ID polling can miss an intermediate capture — the older image is silently never transferred

**File:** `src/image_tx_manager.cpp:87-92`; interplay with `src/main_balloon.cpp:725-749` loop order
**Issue:** `ImageTxManager::process()` detects captures by polling `AutoCap().getLastImageId()` and enqueues only when the value differs from the last-enqueued id — a level poll, not an edge queue. `processPacketHandling()` runs `CmdHandler().process()` (manual CAPTURE_NOW → `allocateImageId()`) then `AutoCap().process()` (interval/event `fire()` → `allocateImageId()`) before `ImageTx().process()`. If a manual command and an interval/event capture complete in the same loop pass, `lastImageId` advances twice; ImageTx sees only the newest id and enqueues one entry — and even that entry's bytes come from `Camera().getCurrentImage()`, which the second `captureImage()` already replaced (the first buffer was freed by `freeCurrentImage()`). The missed image produces no manifest, no log — silent data loss. It also mis-attributes the capture source: `getLastCaptureSource()` reflects the second capture for the one enqueued transfer.
**Fix:** Either serialize captures (skip `AutoCap::process()` interval/event branches while a capture executed this pass — return a "capture happened" flag from `CmdHandler::process()`), or keep a small pending-id queue: `AutoCapture::allocateImageId()` could push onto a depth-2 ring that ImageTx drains, so no id is skipped and a log line names any id whose buffers were already superseded.

### WR-08: `performSystemChecks` reports a false camera health-check failure on every boot with a working camera

**File:** `src/main_balloon.cpp:475-479`
**Issue:** The condition is `if (appState.cameraActive) { // && !Camera().performHealthCheck()) { ... allPassed = false; }` — the commented-out health-check call leaves an unconditional failure branch: whenever the camera initialized successfully, the log prints "Camera system health check failed" and `allPassed` goes false, producing the misleading "Some system checks failed - continuing with reduced functionality" summary on every healthy boot. On a flight system where operators triage on boot diagnostics, a standing false alarm erodes trust in real warnings.
**Fix:** Either implement a real check (`Camera().hasValidImage()` after a test capture, or sensor-PID query) or delete the branch entirely: `if (appState.cameraActive) { SYS_INFO("Camera system health check skipped - no health API"); }` without touching `allPassed`.

## Info

### IN-01: Malformed macro `BACKUP retention_DAYS`

**File:** `include/base_station_config.h:170`
**Issue:** `#define BACKUP retention_DAYS      7` — the space makes this an object-like macro `BACKUP` expanding to `retention_DAYS 7`, not the intended `BACKUP_RETENTION_DAYS`. Currently unused so it compiles, but it is a landmine for the first consumer.
**Fix:** `#define BACKUP_RETENTION_DAYS 7`

### IN-02: Hardcoded, duplicated WiFi credentials

**File:** `include/base_station_config.h:42-43`, `src/main_basestation.cpp:40-43`
**Issue:** Two divergent hardcoded AP credentials exist (`"BalloonBaseStation"/"balloon123"` in config, `"Cosmic1-BaseStation"/"balloontrack"` in main_basestation — only the latter is live). Low risk for an offline field AP, but a single source of truth is warranted; anyone flashing a modified firmware inherits a known password.
**Fix:** Keep one constant pair in `base_station_config.h` and reference it from `main_basestation.cpp`.

### IN-03: Dead code — `CommandSender::findOldestCommand`, `sendHTML`, `updateLED`

**File:** `src/command_sender.cpp:458-471`, `src/main_basestation.cpp:1607-1609`, `src/main_basestation.cpp:1611-1620`
**Issue:** `findOldestCommand` is a private method with no callers (slot selection goes through `findFreeSlot`). `sendHTML` and `updateLED` in main_basestation are never called (`updateStatus` implements its own blink).
**Fix:** Delete all three (or wire `updateLED` into the loop if the IN-03 LED truth work intended to use it).

### IN-04: Non-numeric web inputs silently validate as 0

**File:** `src/main_basestation.cpp:1010`, `1039`, `1214`, `1269-1287`
**Issue:** `server.arg(...).toInt()` returns 0 for non-numeric strings, so `?quality=abc` passes the `0-63` range check and sends SET_QUALITY 0 (a real, valid quality setting). Same for the interval/event-threshold handlers where 0 is then rejected downstream for some fields but not others (e.g., quality).
**Fix:** Validate the raw string first (all digits / optional sign) before `toInt()`, or use `strtol` with an end-pointer check.

### IN-05: ID/counter wraparound edges collide with sentinel semantics

**File:** `src/auto_capture.cpp:267-271`, `src/main_basestation.cpp:736-740`, `1410-1419`
**Issue:** (a) `allocateImageId()` returns `++lastImageId` — after id 65535 it returns 0, colliding with "id 0 means none" on the base (`getLatestThumbId`, `handleImage`'s `id <= 0` rejection, `IMG_00000` files). (b) `ackedAtLastPoll` truncates a uint32 counter to uint16 (misses one ACK edge per 65536). (c) The auto-capture chip latch `entries[i].sequenceNumber > appState.autoCaptureAckSeq` breaks across sequence wrap. All are far-future edges (65536 captures/ACKs), noted for the record.
**Fix:** Skip 0 in `allocateImageId` (wrap to 1) and use a monotonically increasing uint32 for the base-side latches.

### IN-06: Thumbnail frames skip JPEG validation that full captures get

**File:** `src/camera_manager.cpp:285-347`
**Issue:** `captureImageToBuffer` runs `validateImageBuffer` (FFD8/FFD9 check) on full frames; `createThumbnail` does not validate the QQVGA frame. A corrupt thumbnail is CRC32'd, pushed, end-to-end verified (the CRC covers the same corrupt bytes) and displayed as a broken image with state COMPLETE.
**Fix:** Add the same `validateImageBuffer(fb->buf, fb->len)` check in `createThumbnail` and fail the thumbnail path on mismatch.

### IN-07: Duplicated paired constants `MAX_IMAGE_SIZE` / `IMG_MAX_IMAGE_SIZE`

**File:** `include/base_station_config.h:60`, `include/image_protocol.h:74`
**Issue:** Both are 50000 and a comment explicitly requires them to stay equal ("the two must stay equal or the balloon would announce manifests the base rejects"), yet nothing enforces it. A `static_assert` would lock the invariant at compile time.
**Fix:** In `image_protocol.h` (after including `base_station_config.h` where both are visible — e.g., in `image_rx_manager.cpp`): `static_assert(IMG_MAX_IMAGE_SIZE == MAX_IMAGE_SIZE, "TX cap and RX validation bound must match");`

---

_Reviewed: 2026-08-18T21:45:08Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

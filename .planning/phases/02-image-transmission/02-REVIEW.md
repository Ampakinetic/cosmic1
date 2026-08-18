---
phase: 02-image-transmission
reviewed: 2026-08-18T23:44:31Z
depth: standard
files_reviewed: 22
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
  critical: 2
  warning: 10
  info: 8
  total: 20
status: issues_found
---

# Phase 2: Code Review Report

**Reviewed:** 2026-08-18T23:44:31Z
**Depth:** standard
**Files Reviewed:** 22
**Status:** issues_found

## Summary

Adversarial review of the Phase 2 image-transmission implementation (command protocol extension, image TX/RX managers, SD persistence, telemetry beacon, auto-capture event triggers, base-station web UI, wire-format regression harness).

The protocol layer (framing, CRC16, big-endian codecs, type dispatch, untrusted-input validation) is unusually disciplined — length-driven framing, factory-owned type bytes, and pre-allocation manifest validation are all correct. However, the **SD persistence pipeline has two independent correctness defects that break full-image verification in the system's NORMAL operating mode** (periodic captures overlapping an active pull): the single per-kind file handle is stolen by queued manifests (CR-01), and stored-CRC verification reads through a second file handle before the write handle is flushed/closed (CR-02). Either defect alone causes fully-received full images to finalize INCOMPLETE. Additional warnings cover unrecoverable single-shot manifests, phantom in-flight windows after NACKed requests, and several robustness gaps.

## Narrative Findings (AI reviewer)

### Critical Issues

### CR-01: Queued full manifests steal the active pull's SD write handle — active pull silently stops persisting

**File:** `src/image_rx_manager.cpp:496` (with `src/sd_storage.cpp:146-151, 186-194`)
**Issue:** `startTransfer()` calls `SDStorage().openTransfer(m.imageId, m.imageKind, m.totalSize)` **unconditionally for every manifest**, before the QUEUED/active-pull decision at line 506. `openTransfer()` for a different id **closes the previous partial file** of that kind (`if (*handle && *handleId != imageId) { handle->close(); ... }`), and `writeChunk()` **never reopens** — when `*handleId != imageId` it logs "chunk not persisted" and returns false, which `acceptChunk()` ignores.

Sequence (the common case, not an edge): base is mid-pull on full image N (a 50 KB full = 250 chunks ≈ 75–100 s of airtime at ~300 ms/chunk); the balloon captures image N+1 (default UI auto-capture interval is 10 s), pushes its thumbnail, then emits the FULL manifest N+1. Base `startTransfer(N+1, FULL)` allocates a QUEUED slot — but its `openTransfer(N+1)` first **closes full N's open file and re-points `fullFileId` to N+1**. Every subsequent chunk of pull N fails `writeChunk`'s id check and is not persisted. Pull N completes its bitmap in RAM, finalizes, and `verifyStoredCrc32(N)` reads a short file → `total != totalSize` → `notStored` → **INCOMPLETE despite 100% chunk reception**. The cascade repeats for each subsequent manifest while any pull is active. Thumbnails are similarly affected when a heal window competes with a newer push (single `thumbFile` handle).

**Fix:** Open the SD file lazily — only when a FULL transfer actually becomes the active pull (move `openTransfer` into `activateNextPull()` / the manifest-while-idle branch), and/or make `writeChunk()` reopen the `(id, kind)` file when `*handleId != imageId`:

```cpp
// sd_storage.cpp — writeChunk: reopen instead of silently dropping
if (!*handle || *handleId != imageId) {
    if (!openTransfer(imageId, kind, 0)) {   // re-acquire the handle for this id
        return false;
    }
}
```

### CR-02: Stored-CRC verification reads the file before the write handle is flushed/closed

**File:** `src/image_rx_manager.cpp:614-648` (with `src/sd_storage.cpp:203-213, 219-231`)
**Issue:** In `finalizeTransfer()`, the CRC verification (`verifyStoredCrc32` at lines 614–632) runs **before** `writeSidecarFor()` → `SdStorage::finalizeImage()` (line 648), which is the only place the write handle is closed. Arduino-ESP32 `File` I/O rides stdio (`fwrite`/`fseek`) with buffering; each `writeChunk()`'s `seek()` flushes the *previous* write, but the **final chunk's bytes can still sit unflushed** in the write handle's stdio buffer when `verifyStoredCrc32()` opens a *second* `FILE*` (`serveFile`) and reads the file. The second handle sees a file short by up to the stdio buffer size → `total != t.totalSize` → "cannot verify" → the image finalizes INCOMPLETE even though every byte arrived and was written. This undermines the D-23 end-to-end guarantee for every fully-received full image.

**Fix:** Flush (or close) the kind's write handle before the read-back. E.g. add to `SdStorage`:

```cpp
void SdStorage::flushTransfer(uint16_t imageId, uint8_t kind) {
    File* handle; uint16_t* handleId; uint32_t* persisted;
    fileFor(kind, &handle, &handleId, &persisted);
    if (*handle && *handleId == imageId) { handle->flush(); }
}
```

and call `SDStorage().flushTransfer(t.imageId, t.imageKind);` at the top of `finalizeTransfer()` (or reorder so `finalizeImage`'s close happens before `verifyStoredCrc32`).

## Warnings

### WR-01: Manifest transmits are single-shot with no retry — a failed/lost manifest strands the image with no recovery path

**File:** `src/image_tx_manager.cpp:479-492, 566-580`
**Issue:** `pushThumbManifest()` and `announceFullManifest()` advance the entry state (`PUSH_THUMB_CHUNKS` / `ANNOUNCED`) **even when `lora->transmit()` fails**, and the "emitted exactly ONCE" design means the manifest is never retried. Chunks heal through the D-22 windowed pull, but the base can never request an image it never saw a manifest for — a lost manifest silently kills both the thumbnail and the full-image pull, and the code then pushes chunks the base will drop (no matching manifest). The failure is logged but not recovered.
**Fix:** On `transmit()` failure, stay in `PUSH_THUMB_MANIFEST` / `ANNOUNCE_FULL` for a bounded number of retries (e.g. 3 attempts spaced by the beacon cadence) before parking. Also document that air-loss of a manifest (undetectable at the UART layer) remains a protocol limitation.

### WR-02: Window-request ACK/NACK outcomes are never consulted — phantom in-flight windows burn stall passes

**File:** `src/image_rx_manager.cpp:759-800`
**Issue:** `issueWindowRequest()` sets `t.windowActive = true` as soon as `sendCommand()` *queues* (seq != 0). The comment claims "the ACK resolves the slot," but nothing in ImageRx observes the tracked command's outcome. If the balloon answers NACK_BUSY (another entry mid-service — expected during interleaved transfers) or NACK_INVALID (entry evicted — `evictEntriesOlderThan` can free a mid-service entry when the base asks for a newer full), the base still believes a window is in flight: no chunks arrive, the 8 s stall fires, a D-24 pass is charged, and after 3 passes (~24 s) a possibly-healthy transfer finalizes INCOMPLETE instead of retrying promptly on BUSY.
**Fix:** After queueing, poll the tracked command's terminal state (e.g. in `process()`, via `CmdSender().getCommandState(seq)`); on NACK/FAILED clear `windowActive` immediately (BUSY → requeue without charging a pass; INVALID → finalize incomplete at once).

### WR-03: Single-buffer capture loses images when two captures land between ImageTx polls

**File:** `src/image_tx_manager.cpp:87-92, 196-203` with `src/camera_manager.cpp:181-206`, `src/command_handler.cpp:212`, `src/auto_capture.cpp:247`
**Issue:** ImageTx discovers captures by polling `AutoCap().getLastImageId()`. The camera holds exactly one `currentImage`; `captureImage()` **frees the previous image first** (camera_manager.cpp:190). If CAPTURE_NOW and an interval/event capture execute in the same loop pass (CmdHandler and AutoCap both run before ImageTx), or a later capture overwrites/frees the buffer before the next ImageTx poll, the earlier image is silently destroyed with no transfer — a gap in image IDs the base never explains. A *failed* capture also frees the pending previous image before failing.
**Fix:** Enqueue synchronously at capture time (call into ImageTx from `fire()`/`handleCaptureNow`), or keep a one-deep pending buffer so one intermediate capture survives until the next poll.

### WR-04: serializeCommand accepts payloadLength 201–224 but silently emits a malformed packet

**File:** `src/command_protocol.cpp:53-77`
**Issue:** The bound check is `cmd.payloadLength > CMD_MAX_PACKET_SIZE - 16` (224), but the payload is only written when `cmd.payloadLength <= CMD_MAX_PAYLOAD_SIZE` (200). For a caller-supplied length of 201–224 the serializer emits a packet whose header/body advertise N payload bytes that are absent (CRC computed over the short body). Currently unreachable because `createCommandPacket` clamps to 200, but the public API itself produces corrupt frames — a defense-in-depth gap. The regression harness mirrors the same gap (`scripts/verify_protocol_roundtrip.mjs:121-141`, clause (b) asserts the 224 boundary is *accepted*).
**Fix:** `if (packetLength > CMD_MAX_PACKET_SIZE || cmd.payloadLength > CMD_MAX_PAYLOAD_SIZE) return false;` and update harness clause (b) to assert rejection at 201.

### WR-05: serveFile refuses to serve existing files after a mid-flight write failure

**File:** `src/sd_storage.cpp:331-341`
**Issue:** After `degrade()` (open/write failure → `available = false`), `serveFile()` returns `File()` for **all** reads, including files already safely stored on a healthy card. The UI's COMPLETE rows link to `/img/{id}` → 404, and CRC read-back fails, contradicting the documented disk-full policy "existing files kept" — they are kept on disk but become unservable until reboot.
**Fix:** Read availability is independent of write availability: serve when the card mounted successfully (`!status.initFailed`), gating only writes on `available`.

### WR-06: MAX_IMAGE_SIZE / IMG_MAX_IMAGE_SIZE "must stay equal" pair with no compile-time tie

**File:** `include/image_protocol.h:74`, `include/base_station_config.h:60`, consumed at `src/image_rx_manager.cpp:405`
**Issue:** The balloon's arm gate (`IMG_MAX_IMAGE_SIZE`) and the base's manifest validation bound (`MAX_IMAGE_SIZE`) are documented as a PAIRED constant ("the two must stay equal or the balloon would announce manifests the base rejects") but live in unrelated headers with only comments binding them. A one-sided edit silently reintroduces the rejected-manifest failure mode.
**Fix:** `static_assert(MAX_IMAGE_SIZE == static_cast<int32_t>(IMG_MAX_IMAGE_SIZE), "base manifest bound must match balloon arm cap");` in `image_rx_manager.cpp` (both definitions are visible there via includes).

### WR-07: Full-image capture buffer allocated from internal DRAM, not PSRAM

**File:** `src/camera_manager.cpp:262`
**Issue:** `captureImageToBuffer()` copies the full-resolution JPEG with plain `malloc` (internal heap). An SXGA/UXGA JPEG (tens of KB) can exhaust internal DRAM and fail the capture even though 8 MB PSRAM is free — and ImageTx immediately re-copies the same bytes into PSRAM (`ps_malloc`) at enqueue. The capture path never uses the PSRAM the platform requires.
**Fix:** Allocate with `heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (fallback to `malloc`), or hand the frame to ImageTx without the double copy.

### WR-08: createThumbnail ignores setFrameSize/setQuality failures — "thumbnail" can silently be full-size

**File:** `src/camera_manager.cpp:302-303`
**Issue:** The QQVGA/quality-20 downgrade calls' return values are unchecked. If the sensor rejects the switch, the "thumbnail" is captured at full resolution and quality — potentially exceeding the IMG-02 10 s airtime budget or even the 50 KB cap, degrading to the oversize path with no signal at capture time.
**Fix:** Check both returns; on failure log and bail (restoring settings), so the enqueue path's "thumbnail capture failed" branch handles it honestly.

### WR-09: try/catch in the balloon main loop provides false error containment

**File:** `src/main_balloon.cpp:243, 295-298`
**Issue:** ESP32 Arduino builds normally compile with exceptions disabled (`-fno-exceptions`); if disabled this block is a compile error, and if enabled, faults on this platform abort/reboot rather than unwinding C++ stacks. `catch (...) { handleSystemError(...) }` therefore cannot catch the failures it wraps — it only catches nothing while suggesting robustness that does not exist.
**Fix:** Remove the try/catch; rely on the watchdog + `handleSystemError` call sites at real error paths (or gate the block behind a documented exceptions-enabled build flag).

### WR-10: LED "truth" is computed but never shown on the LED; dead helpers

**File:** `src/main_basestation.cpp:725-754, 1611-1620`
**Issue:** `processLoRa()` carefully computes `lastOutcomeBad` / `lastAckTime` and `handleStatus()` derives `connected`/`linkText` (IN-03 discipline), but the physical status LED is only blindly blinked by `updateStatus()` every 5 s; `updateLED()` (which also never checks link state) is never called, and `sendHTML()` (line 1607) is dead. The IN-03 "LED truth" claim is implemented in JSON only, not on the LED.
**Fix:** Drive the LED from the computed link state (call a fixed `updateLED` that uses `lastAckTime`/`lastOutcomeBad`/`LINK_STALE_MS`) and delete the dead helpers.

## Info

### IN-01: Dead function findOldestCommand

**File:** `src/command_sender.cpp:458-471`
**Issue:** Declared and defined but never called (sendCommand uses findFreeSlot).
**Fix:** Delete, or use it in findFreeSlot's terminal-slot eviction (which currently picks the first-found, not the oldest).

### IN-02: allocateSlot's kind parameter unused

**File:** `src/image_rx_manager.cpp:327`
**Issue:** `allocateSlot(uint8_t kind)` never reads `kind` — slot selection is kind-blind.
**Fix:** Drop the parameter or use it (e.g. prefer recycling a terminal slot of the same kind).

### IN-03: Broken macro name in base_station_config.h + duplicated config sources

**File:** `include/base_station_config.h:170` (also 39-51)
**Issue:** `#define BACKUP retention_DAYS      7` — the macro name contains a space, so this defines token `BACKUP` → `retention_DAYS 7` (any future use of `BACKUP` expands destructively). More broadly, this header's WiFi/packet/LED sections are dead: `main_basestation.cpp` redefines its own SSID/password/pins, creating two divergent sources of truth for base-station configuration.
**Fix:** Rename to `BACKUP_RETENTION_DAYS 7`; delete or reconcile the duplicated WiFi/pin blocks.

### IN-04: Hardcoded WiFi AP credentials in source

**File:** `src/main_basestation.cpp:40-41`, `include/base_station_config.h:42-43`
**Issue:** AP credentials ("balloontrack" / "balloon123") are compile-time constants in a field-deployed device; anyone with repo access (or the firmware binary) knows the AP key. Acceptable for a hobby flight, but should be a deliberate, documented choice.
**Fix:** Note as a deployment constant to rotate per build, or load from NVS/Preferences at first boot.

### IN-05: Redundant self-assignment in AutoCapture::fire

**File:** `src/auto_capture.cpp:248`
**Issue:** `lastImageId = allocateImageId();` — `allocateImageId()` already pre-increments and returns the member (`return ++lastImageId;`); the assignment is a same-value write that misreads as a second increment.
**Fix:** `allocateImageId();` alone, or have allocateImageId return without touching the member from fire().

### IN-06: Image-ID wrap produces the reserved id 0

**File:** `src/auto_capture.cpp:267-271` with `src/image_rx_manager.cpp:142-144`, `src/main_basestation.cpp:1542`
**Issue:** The shared sequence wraps 65535 → 0; id 0 is the "no image" sentinel (`getLatestThumbId()==0`, `handleImage` rejects id<=0), and after a wrap a reused id can collide with a still-held terminal slot (`findTransfer` restarts it). ~65 k captures per wrap — cheap to guard.
**Fix:** `if (++lastImageId == 0) lastImageId = 1;`

### IN-07: ackTimeoutFor omits SET_EVENT_THRESHOLDS explicitly

**File:** `src/command_sender.cpp:25-55`
**Issue:** The D-05 class table lists every command except `SET_EVENT_THRESHOLDS`, which falls to `default:` — currently 5000 ms (SETTINGS), which happens to be correct by accident of the default.
**Fix:** Add the explicit `case CameraCommand::SET_EVENT_THRESHOLDS: return CMD_ACK_TIMEOUT_SETTINGS_MS;` so a future default change can't silently re-classify it.

### IN-08: Side effects inside the /status GET handler

**File:** `src/main_basestation.cpp:1410-1419`
**Issue:** `handleStatus()` mutates the auto-capture latch (`appState.autoCaptureAckSeq` etc.) while building a response. Safe under the single-threaded loop, but the latch can also MISS an ACK if its slot is recycled by 5 newer commands between 1 s polls (the "survives tracked-slot reuse" claim only holds for the latched appState copy, not the ACK observation itself).
**Fix:** Move the latch scan into `processLoRa()` (runs every loop pass), leaving handleStatus read-only.

---

_Reviewed: 2026-08-18T23:44:31Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

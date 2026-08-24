---
phase: 01-command-protocol-control
reviewed: 2026-08-24T12:00:00Z
depth: standard
files_reviewed: 22
files_reviewed_list:
  - include/auto_capture.h
  - include/command_handler.h
  - include/command_protocol.h
  - include/command_sender.h
  - include/common_types.h
  - include/e32_lora.h
  - include/image_protocol.h
  - include/image_rx_manager.h
  - include/image_tx_manager.h
  - platformio.ini
  - scripts/verify_protocol_roundtrip.mjs
  - src/auto_capture.cpp
  - src/camera_manager.h
  - src/camera_manager.cpp
  - src/command_handler.cpp
  - src/command_protocol.cpp
  - src/command_sender.cpp
  - src/e32_lora.cpp
  - src/image_rx_manager.cpp
  - src/image_tx_manager.cpp
  - src/main_balloon.cpp
  - src/main_basestation.cpp
findings:
  critical: 3
  warning: 5
  info: 7
  total: 15
status: issues_found
---

# Phase 01: Code Review Report

**Reviewed:** 2026-08-24T12:00:00Z
**Depth:** standard
**Files Reviewed:** 22
**Status:** issues_found

## Summary

Adversarial review of all 22 in-scope source files (balloon TX chain, base RX chain, command protocol, camera manager, both main firmwares, wire-format harness). Cross-module tracing was driven by the four bench-known defects and the repo's bench logs (base*.log, balloon*.log). The headline result: the "stored-bytes CRC mismatch on a fully-received full" (base4.log:854, bench priority 1) is not an SD-layer flake — it is a provable wire-protocol routing defect (CR-01): chunk frames carry no image-kind byte and the base's routing heuristic misroutes late thumbnail-heal stragglers into the active FULL transfer, so thumbnail bytes occupy full-image indices and the real full chunks are then discarded as "duplicates". The bench log itself carries the signature of this misroute (duplicate drops of chunks 21/22/25/26/27 while the 19..29 heal window was still outstanding). The other two bench-priority defects (full-sized thumbnails passing the metadata-only guard; one-shot FULL manifest consumed by a failed transmit) are confirmed as CR-02/CR-03 with log evidence. Five warnings cover failed-transmit state advances on the thumbnail push path, overflow eviction of in-flight heals, a silent capture-drop race in the balloon loop order, duplicate window-request churn, and NACK_BUSY poisoning the link-truth LED. Base-side SD storage was re-verified line-by-line and is NOT the source of the corruption (flush-before-verify present, re-manifest truncation paired with bitmap reset, kind-split seek math consistent).

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: Chunk frames carry no imageKind; routing heuristic writes thumbnail bytes into the active FULL transfer (root cause of the 36/36 stored-bytes CRC mismatch)

**File:** `include/image_protocol.h:154-159`, `src/image_rx_manager.cpp:253-284`
**Issue:** `ImageChunkBody` is `{imageId, chunkIndex, dataLen, data}` — no imageKind byte (the window REQUEST carries one, the chunk ANSWER does not). The base therefore routes every 0x13 frame by imageId alone with a heuristic: (1) a non-terminal THUMBNAIL slot with `windowActive` wins; (2) otherwise any non-terminal FULL slot wins; (3) otherwise the thumbnail slot. Rule 1's protection ends the moment the thumbnail slot finalizes (`terminal = true`), but the balloon can still be sending thumbnail chunks after that point: `CommandSender` retries a thumb-heal window request whose ACK was lost (up to 3 retries inside a 15 s window — the same churn that logs "attempt 4/3"), and each retried request makes the balloon re-arm and re-send the thumbnail window (`handleWindowRequest` arming is deliberately idempotent-restart, `src/image_tx_manager.cpp:714-728`). Those late thumbnail chunks fall through to rule 2 and land in the active FULL slot. Any index the full still needs gets thumbnail bytes; when the real full chunks for those indices arrive later they are discarded by the bitmap as duplicates. Result: 36/36 chunks "received", correct file length, wrong content, end-to-end CRC fails, image finalized INCOMPLETE. base4.log shows the exact signature: duplicate drops of chunks 2,3,21,22,25,26,27 of image 7 (lines 821-847) while the FULL's own 19..29 heal window was still outstanding (line 824), a still-retrying window request seq 28 at line 839, then `stored-bytes CRC mismatch (got FCAFC250, manifest 9DE9EFA0)` at line 854. CR-02 below (full-sized thumbnails) makes the two kinds' index spaces overlap almost exactly (36 vs 36 chunks for image 7), which is what makes the misroute so destructive.
**Fix:** Add the kind to the chunk frame — one byte in `ImageChunkBody` (e.g. after `imageId`), set from `windowKind`/push kind in `createChunkPacket` callers (`src/image_tx_manager.cpp:529,778`), validated against the slot kind in `onChunkFrame` before any bitmap/SD write (mismatched kind = frame dropped and counted, never written). Update `scripts/verify_protocol_roundtrip.mjs` and `deserializeChunk`/`serializeChunk` in `src/command_protocol.cpp` together. Interim hardening if the wire change must wait: in `onChunkFrame`, route to the FULL slot only when `t->pullActive && t->windowActive` (a queued or between-windows full accepts nothing), and drop chunks that arrive for a slot whose window/push context cannot account for them — the current fallback-to-any-slot behavior is what converts a late straggler into silent data corruption.

### CR-02: One-shot FULL manifest announcement is consumed even when the transmit fails — the full image is then silently never transferable

**File:** `src/image_tx_manager.cpp:585-603`
**Issue:** `announceFullManifest` sets `entry.state = ANNOUNCED` unconditionally, including when `serializeManifest(...) && lora->transmit(...)` returned false (line 589). The state machine treats ANNOUNCED as "manifest emitted exactly ONCE per entry" (comment at 599-600): there is no re-announce path, and chunks flow only through windows armed by a base request — a request the base can never issue because it never saw the manifest. A single failed E32 transmit (AUX handshake failure, link turnaround collision) therefore strands every full image with no log at the base and no recovery: the entry eventually ages out and the image is gone. This is the "silently lost FULL manifest" bench defect.
**Fix:** Only consume the announcement on success:
```cpp
bool ok = CommandProtocol::serializeManifest(pkt, buffer, length) && lora->transmit(buffer, length);
if (ok) {
    entry.state = ImageTxEntryState::ANNOUNCED;
} else {
    entry.announceAttempts++;                       // new field
    if (entry.announceAttempts >= ANNOUNCE_MAX_ATTEMPTS) {
        // log and park: entry.state = THUMB_PUSHED-equivalent, free fullBuffer
    }
    // otherwise stay in ANNOUNCE_FULL; the next process() pass retries
}
entry.lastActivityMs = millis();
```
Bound the retries (e.g. 3) so a dead link cannot spin the announce forever, and free `fullBuffer` when the bound is hit.

### CR-03: Thumbnail size guard checks only frame metadata, not byte length — full-size frames with QQVGA metadata still pass

**File:** `src/camera_manager.cpp:361-376`
**Issue:** The 01-11 "thumbnail-sizing quirk" guard added after balloon3.log (7138-byte thumb byte-equal to the QVGA full) validates only `fb->width == 160 && fb->height == 120`. The current bench session still shows full-sized thumbnails passing in 4/6 captures (SVGA thumbnail = 145 chunks vs the full's 144) — i.e. the esp32-camera driver (fb_count=2, `CAMERA_GRAB_LATEST`, camera_manager.cpp:168-173) can deliver a frame whose dimension METADATA reflects the new QQVGA config while the buffer BYTES are a stale full-size capture. `fb->len` is never bounded: line 376 `malloc(fb->len)` happily takes ~28 KB and the enqueue path pushes it as a "thumbnail". Consequences: double airtime per capture, and — decisively for CR-01 — a thumbnail whose chunk-index space overlaps the full's, making kind-misrouted bytes land at valid full offsets instead of failing a bounds check.
**Fix:** Bound the byte length as part of the same guard, and discard the first frame after a framesize change:
```cpp
if (fb->width != 160 || fb->height != 120 || fb->len > THUMB_MAX_BYTES) {   // e.g. 8192
    ... existing bail path ...
}
```
Additionally, after `setFrameSize(FRAMESIZE_QQVGA)` succeeds, fetch-and-discard one frame (`esp_camera_fb_get()` + `esp_camera_fb_return()`) before the real capture — with fb_count=2/GRAB_LATEST the first post-config frame can be the stale pre-config capture. A belt-and-suspenders option is parsing the JPEG SOF marker dimensions from `fb->buf` and rejecting on disagreement with the metadata.

## Warnings

### WR-01: Failed thumbnail-manifest transmit still advances the push state machine — thumbnail becomes permanently undeliverable

**File:** `src/image_tx_manager.cpp:500-515`
**Issue:** `pushThumbManifest` sets `entry.state = PUSH_THUMB_CHUNKS` (line 513) regardless of whether the manifest transmit succeeded (line 502). If the manifest frame is lost, the base never allocates a THUMBNAIL slot, so every subsequent thumb chunk is dropped at `onChunkFrame` (`findTransfer` fails), and no heal is possible — heals require an existing holed slot. The balloon meanwhile believes the push completed and proceeds to announce the full. Same defect family as CR-02 but on the push path; loses the thumbnail silently (no slot ever exists at the base to even report INCOMPLETE).
**Fix:** Mirror the CR-02 fix: advance to `PUSH_THUMB_CHUNKS` only when `ok`; on failure stay in `PUSH_THUMB_MANIFEST` with a bounded attempt counter (the manifest is 34 bytes — cheap to retry on the next process() pass).

### WR-02: Queue-overflow eviction ranks an in-flight thumbnail heal as the FIRST victim — no mid-service-window deferral on this path

**File:** `src/image_tx_manager.cpp:203-210, 327-361`
**Issue:** `evictionClassOf` maps THUMB_PUSHED to class 1 (preferred eviction victim), and the overflow branch in `enqueueCapture` (327-361) picks purely by class then age — it never inspects `windowArmed`/`windowEverArmed`. The supersede path got the G-01-7 lever-2 fix (`evictEntriesOlderThan` defers entries whose window is mid-service, 817-825), but the overflow path did not. A THUMB_PUSHED entry whose heal window is armed and mid-flight (base actively re-requesting it) is evicted on the next capture's enqueue under queue pressure: its buffers are freed mid-stream, later heal requests hit "unknown/evicted image", the base burns D-24 passes and finalizes INCOMPLETE.
**Fix:** In the overflow victim scan, skip entries with `windowArmed && windowNextIndex < windowStart + windowCount` (identical predicate to the BUSY check at 703-712) unless every entry is mid-service; at minimum demote armed THUMB_PUSHED entries to the SERVED class so they are not the first choice.

### WR-03: Manual capture and auto/event capture in the same loop pass silently drop the first image

**File:** `src/main_balloon.cpp:884-895`, `src/command_handler.cpp` (handleCaptureNow), `src/auto_capture.cpp`
**Issue:** Loop order is `CmdHandler().process()` → `AutoCap().process()` → `ImageTx().process()`. `handleCaptureNow` executes a capture but never advances AutoCap's `lastCaptureTime`, so in a pass where the interval/event timer has also elapsed, AutoCap's spacing gate passes and fires a second capture before `ImageTx().process()` ever polls: the second capture's `freeCurrentImage()`/buffer reuse replaces the first capture's bytes, and ImageTx (which tracks only the latest imageId) enqueues only the second. The manual capture was already ACKed to the base with its imageId — the operator waits for an image that is never transmitted, with no log line at either end. Window is one 100 ms loop pass per collision; probability scales with capture cadence but is nontrivial at 1 s auto-intervals.
**Fix:** In `handleCaptureNow`, after a successful capture, notify AutoCap to advance its spacing reference (e.g. `AutoCap().onExternalCapture(millis())` setting `lastCaptureTime`), or have AutoCap skip its fire when `Camera().getLastCaptureTime()` advanced within the same pass; alternatively make ImageTx drain pending captures by id rather than only `getLastImageId()`.

### WR-04: Stall re-requests issue new IMAGE_WINDOW_REQUESTs while the prior one is still inside its 15 s tracked-command window

**File:** `src/image_rx_manager.cpp:186-233` (thumb stall), window stall branch in `process()`, `src/command_sender.cpp` (tracked-command table)
**Issue:** The stall clocks (8 s) are shorter than the window-request command's ACK timeout class (15 s, with up to 3 retries). When a request's ACK is lost, the base both keeps retrying the old request AND (after the stall fires) queues a new overlapping window request — base4.log shows seq 28 still retrying ("attempt 4/3", line 839) while seq 32 (19..29) and later seq 33 are queued for concurrent servicing. Costs: duplicate tracked commands consume `MAX_PENDING_COMMANDS` slots (risking rejection of operator commands), each duplicate downlink transmit collides with in-flight chunks on the half-duplex link (the END MARKER MISS at base4.log:842), and — via the balloon's idempotent re-arm — the duplicate-chunk flood that feeds CR-01.
**Fix:** Before issuing a window request, check for an already-tracked non-terminal IMAGE_WINDOW_REQUEST for the same transfer (CmdSender exposes the queue snapshot) and skip/defer the new one; or clear/supersede the prior tracked request when a stall supersedes it. Sequencing the stall clock to start only after the previous request reaches a terminal state also works.

### WR-05: NACK_BUSY is treated as terminal FAILED and poisons the link-truth LED/dashboard with "No link"

**File:** `src/command_sender.cpp:439-449` (handleResponse), `src/main_basestation.cpp:104-122` (computeLinkTruth)
**Issue:** A `NACK_BUSY` response transitions the tracked command to FAILED (terminal, no retry). BUSY is the protocol's routine deferral (balloon returns it when another entry's window is mid-service, `src/image_tx_manager.cpp:703-712`) — yet `processLoRa` latches `lastOutcomeBad` and `computeLinkTruth` then reports NO_LINK and drives the physical LED red (solid OFF) until some later command ACKs. During any serialized pull/heal sequence the operator sees "No link" while the link is demonstrably carrying chunks.
**Fix:** Either retry on NACK_BUSY (it is transient by construction — re-queue within the existing retry budget instead of terminalizing), or classify it separately from failure for link-truth purposes (e.g. a `BUSY` state that `computeLinkTruth`/`processLoRa` treat like SENT, not like TIMEOUT/FAILED).

## Info

### IN-01: Retry log prints "attempt 4/3" on the final legal retry

**File:** `src/command_sender.cpp:524-537`
**Issue:** `retryCommand` increments `retryCount` then logs `attempt %d/%d` with `retryCount + 1` against `maxRetries`. With maxRetries=3, the third (legal) retry logs "attempt 4/3" — reproduced across bench logs (base2.log:458, base3.log:429, base4.log:115/290/372). Termination logic itself is sound; the label misleads bench operators into thinking the bound is exceeded.
**Fix:** Log `cmd->retryCount` against `maxRetries` ("retry %d/%d"), or keep "attempt" but compare against `maxRetries + 1`.

### IN-02: System checks log a false "Camera system health check failed" on every healthy boot

**File:** `src/main_balloon.cpp:561-577`
**Issue:** `performSystemChecks` warns "Power system health check skipped" and "Communication system health check skipped" unconditionally, and the camera branch (`if (appState.cameraActive)`) warns "Camera system health check failed" and clears `allPassed` whenever the camera is ACTIVE — the actual check is commented out but the failure branch was left live. Every good boot prints three false warnings plus "Some system checks failed".
**Fix:** Either restore real checks or delete the dead branches; a skipped check must not log as a failure.

### IN-03: enterConfigMode saves previousMode that exitConfigMode never uses

**File:** `src/e32_lora.cpp:610-623`
**Issue:** `previousMode` is captured and stored but `exitConfigMode` always sets MODE_NORMAL — dead state that suggests a restore semantic that does not exist.
**Fix:** Remove `previousMode`, or restore it in `exitConfigMode` if callers ever enter config from sleep/wake modes.

### IN-04: setMode records currentMode before AUX verification succeeds

**File:** `src/e32_lora.cpp:153-180`
**Issue:** `currentMode` is assigned before the AUX-pin wait; if the verify fails/returns false, the tracked mode no longer matches the radio's actual M0/M1 state, and later mode-dependent decisions (config vs normal) act on stale truth.
**Fix:** Assign `currentMode` only after the AUX check passes (or roll back on failure).

### IN-05: transmitToAddress uses an unused stack VLA and skips the initialized check

**File:** `src/e32_lora.cpp:272-290`
**Issue:** Function builds a fixed stack buffer/VLA it does not need and does not guard on `initialized` before touching the serial port.
**Fix:** Drop the unused buffer; early-return false when `!initialized` (mirroring `transmit`).

### IN-06: findOldestCommand is dead code

**File:** `src/command_sender.cpp:466-479`
**Issue:** No caller anywhere in the tree (verified by cross-file search).
**Fix:** Delete it, or use it for WR-04's outstanding-request check.

### IN-07: Stale "fixed 17-byte body" comments for what is a 19-byte body

**File:** `src/command_sender.cpp:329`, `scripts/verify_protocol_roundtrip.mjs:763`
**Issue:** Comments describe a 17-byte fixed body; the actual serialized body is 19 bytes (the size constants and the roundtrip checks are correct — only the prose is wrong).
**Fix:** Update the two comments to 19 (or reference the constant instead of a literal).

---

**Verification notes for the record:** base-side SD storage was audited as a corruption candidate and cleared: `flushTransfer` precedes `verifyStoredCrc32` (`src/image_rx_manager.cpp:672-702`), re-manifest restart truncation is paired with a full bitmap/slot reset (`:500-510`), the CR-01 reopen path is non-truncating (`src/sd_storage.cpp:204-235`), and `verifyStoredCrc32` rejects length disagreement before comparing (`:971-976`) — the image-7 file had the right length and wrong content, which is what pinpointed the chunk-routing defect (CR-01) rather than the storage layer.

_Reviewed: 2026-08-24T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_

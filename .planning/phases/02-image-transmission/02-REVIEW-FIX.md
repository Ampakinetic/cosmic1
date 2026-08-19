---
phase: 02-image-transmission
fixed_at: 2026-08-19T00:51:40Z
review_path: .planning/phases/02-image-transmission/02-REVIEW.md
iteration: 1
findings_in_scope: 12
fixed: 9
skipped: 3
status: partial
---

# Phase 2: Code Review Fix Report

**Fixed at:** 2026-08-19T00:51:40Z
**Source review:** .planning/phases/02-image-transmission/02-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 12 (2 Critical, 10 Warning; fix_scope = critical_warning)
- Fixed: 9 (2 Critical + 7 Warning)
- Skipped: 3 (design-level Warnings deferred to planning, per fix policy)

**Verification (per fix, before each commit):** `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (both envs SUCCESS) and `node scripts/verify_protocol_roundtrip.mjs` (exit 0). Baseline run before any edit confirmed the pre-fix tree passed both gates. **Verification ran in the isolated fixer worktree** (`.claude/worktrees/rf-02-132552-*`, branch `gsd-reviewfix/02-132552`, since fast-forwarded into `main` and removed) — build numbers are reproducible from the main checkout at the fast-forwarded commits, but the worktree's `.pio` cache no longer exists.

## Fixed Issues

### CR-01: Queued full manifests steal the active pull's SD write handle

**Files modified:** `src/image_rx_manager.cpp`, `src/sd_storage.cpp`, `include/sd_storage.h`
**Commit:** 81cba6e
**Applied fix:** Two-part fix, adapted from the review suggestion. (1) FULL transfers now open their SD file **lazily** — only at pull activation (`activateNextPull()` and the manifest-while-idle branch in `startTransfer()`); a QUEUED manifest no longer touches the kind handle the active pull owns. A re-manifested *active* pull still reopens (truncate) at manifest time so the restarted reassembly stays consistent with its cleared bitmap; thumbnails still open at manifest time (their push chunks follow immediately). (2) `writeChunk()` no longer silently drops chunks when the kind's handle holds a different id — it **reopens non-truncating** (`"r+"` when the file exists, create only when absent). Note: the review's sketch (`openTransfer` in `writeChunk`) was deliberately NOT used as-is — `openTransfer` opens `FILE_WRITE` ("w", truncate), so alternating ids in a thumbnail heal-vs-push window would destroy each other's persisted bytes on every handle flip; the non-truncating reopen preserves both sides' bytes. **Status: fixed — requires human verification** (logic/state-machine change; builds and harness pass, but the SD-pipeline behavior needs on-hardware confirmation against gap IMG-03).

### CR-02: Stored-CRC verification reads the file before the write handle is flushed

**Files modified:** `src/sd_storage.cpp`, `include/sd_storage.h`, `src/image_rx_manager.cpp`
**Commit:** bc34eab
**Applied fix:** Added `SdStorage::flushTransfer(imageId, kind)` — flushes the kind's write handle when it currently holds `(imageId, kind)` — and call it at the top of `ImageRxManager::finalizeTransfer()`, before `verifyStoredCrc32()` opens its second read handle. Flush (not close) so the transfer could keep writing afterwards. **Status: fixed — requires human verification** (runtime buffering behavior needs on-hardware confirmation against gap IMG-05).

### WR-04: serializeCommand accepts payloadLength 201–224 but silently emits a malformed packet

**Files modified:** `src/command_protocol.cpp`, `scripts/verify_protocol_roundtrip.mjs`
**Commit:** e9e9bdc
**Applied fix:** Bound check is now `cmd.payloadLength > CMD_MAX_PAYLOAD_SIZE` (200) alongside the packet-size check; the payload-write condition dropped its now-redundant inner bound. The JS harness mirror was updated identically, and clause (b) now asserts **201 rejected / 200 accepted** (216-byte packet) — both PASS, all other clauses unchanged.

### WR-05: serveFile refuses to serve existing files after a mid-flight write failure

**Files modified:** `src/sd_storage.cpp`, `include/sd_storage.h`
**Commit:** 90e39a1
**Applied fix:** `serveFile()` now gates on `status.initFailed` (card never mounted) instead of `available` — read availability is independent of write availability, so files stored before a degrade stay servable (no more 404s on COMPLETE rows, CRC read-back works). Writes remain gated on `available` everywhere.

### WR-06: MAX_IMAGE_SIZE / IMG_MAX_IMAGE_SIZE "must stay equal" pair with no compile-time tie

**Files modified:** `src/image_rx_manager.cpp`
**Commit:** d0d786b
**Applied fix:** `static_assert(MAX_IMAGE_SIZE == static_cast<int32_t>(IMG_MAX_IMAGE_SIZE), ...)` added where both definitions are visible (includes of `base_station_config.h` and `image_protocol.h`). A one-sided edit now fails the build.

### WR-07: Full-image capture buffer allocated from internal DRAM, not PSRAM

**Files modified:** `src/camera_manager.cpp`
**Commit:** 820361b
**Applied fix:** `captureImageToBuffer()` allocates with `heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` first, falling back to plain `malloc` when PSRAM is unavailable (`esp_heap_caps.h` included). `free()` releases either allocation on ESP32 (shared allocator).

### WR-08: createThumbnail ignores setFrameSize/setQuality failures

**Files modified:** `src/camera_manager.cpp`
**Commit:** d19b082
**Applied fix:** The QQVGA/quality-20 downgrade is now verified: on failure it logs, nulls `thumbnail.buffer`, clears `valid`, restores both settings (best-effort), and returns false — the enqueue path's "thumbnail capture failed" branch now handles it honestly instead of a silent full-size "thumbnail".

### WR-09: try/catch in the balloon main loop provides false error containment

**Files modified:** `src/main_balloon.cpp`
**Commit:** 41e3480
**Applied fix:** The `try { ... } catch (...) { ... }` wrapper removed; the loop body runs unwrapped with a comment documenting why (exceptions disabled on this platform — a catch block can never catch anything). `SYS_ERROR`/`handleSystemError` retain their 16 other call sites; watchdog containment unchanged.

### WR-10: LED "truth" is computed but never shown on the LED; dead helpers

**Files modified:** `src/main_basestation.cpp`
**Commit:** 39bdfca
**Applied fix:** Extracted the IN-03 link-truth computation from `handleStatus()` into a shared `computeLinkTruth()` (`LinkTruth::{READY,NO_LINK,UNKNOWN}`); `/status` JSON consumes it unchanged. `updateLED()` is now called every loop pass and drives the physical LED from the same truth: solid ON = Ready, 1 Hz blink = Unknown/stale, OFF = No link. Deleted dead helpers: the old blind-blink `updateStatus()` (loop now calls `updateLED()`) and the never-called `sendHTML()`. **Note:** the LED pattern mapping (solid/blink/off) is a judgment call mirroring the JSON's green/yellow/red — flag for human confirmation if other semantics were intended. **Status: fixed — requires human verification** of the intended LED semantics.

## Skipped Issues

### WR-01: Manifest transmits are single-shot with no retry

**File:** `src/image_tx_manager.cpp:479-492, 566-580`
**Reason:** Design-level change, deferred to planning. The fix requires a bounded-retry policy (attempt counters, spacing by beacon cadence, parking semantics) woven into the ImageTx entry state machine, plus a documented protocol-limitation note for undetectable air loss — a protocol behavior change with airtime/latency tradeoffs, not a mechanical patch.
**Original issue:** `pushThumbManifest()` and `announceFullManifest()` advance entry state even when `lora->transmit()` fails; the "emitted exactly ONCE" design means a lost manifest strands the image with no recovery path (base can never request an image it never saw).

### WR-02: Window-request ACK/NACK outcomes are never consulted — phantom in-flight windows burn stall passes

**File:** `src/image_rx_manager.cpp:759-800`
**Reason:** Design-level change, deferred to planning. Requires observing tracked-command terminal states in `process()` and defining new semantics (BUSY → requeue without charging a D-24 pass; INVALID → finalize incomplete immediately) that interact with the pass accounting and stall machinery — state-machine redesign, not a safe mechanical fix.
**Original issue:** `issueWindowRequest()` sets `windowActive = true` on queue; a NACK_BUSY/NACK_INVALID answer leaves a phantom window in flight — 8 s stall, a charged pass, and after 3 passes a possibly-healthy transfer finalizes INCOMPLETE instead of retrying promptly.

### WR-03: Single-buffer capture loses images when two captures land between ImageTx polls

**File:** `src/image_tx_manager.cpp:87-92, 196-203` with `src/camera_manager.cpp:181-206`, `src/command_handler.cpp:212`, `src/auto_capture.cpp:247`
**Reason:** Design-level change, deferred to planning. Both fix options restructure cross-module data ownership (synchronous enqueue at capture time creates a new AutoCap/CmdHandler → ImageTx coupling; a one-deep pending buffer changes CameraManager's buffer lifecycle) — needs planning, not a blind patch.
**Original issue:** The camera holds one `currentImage` and `captureImage()` frees the previous one first; two captures in one loop pass (or before the next ImageTx poll) silently destroy the earlier image with no transfer.

---

_Fixed: 2026-08-19T00:51:40Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_

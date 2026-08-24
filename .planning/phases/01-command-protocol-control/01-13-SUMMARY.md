---
phase: 01-command-protocol-control
plan: 13
subsystem: protocol
tags: [lora, wire-format, image-transfer, esp32, camera, crc]

requires:
  - phase: 01-12
    provides: G-01-7 levers + G-01-8 framesize re-init; bench session #4 evidence naming G-01-9 defects A/B
provides:
  - Kind-tagged 0x13 chunk wire format (imageKind byte after imageId, 6-byte body overhead, validated at deserialize)
  - Kind-exact base chunk routing (single findTransfer on (imageId, imageKind); heuristic deleted)
  - Kind discriminator in TX chunk log lines (push names THUMBNAIL, window names windowKind)
  - CR-03 thumbnail payload guard (stale-frame drain + THUMB_MAX_BYTES 8192 bound in createThumbnail)
  - Harness mirrors carrying the kind end-to-end + (img-i) kind-rejection negative check
affects: [01-16 bench re-verification, phase-01 close-out, G-01-9 closure]

actuals:
  tokens: 7300      # chars/4 over the realized 3-commit diff (29,143 chars); estimate was 17,000 at confidence low (0 calibration samples)
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Wire-discriminator byte: when a receiver must route by an identity the frame only implies, make the identity explicit on the wire in one atomic commit (serializer + deserializer + factory + dispatch + harness) rather than inferring it from timing/state context"
    - "Payload-vs-metadata guard: validate the byte length a producer claims, not just the dimension metadata it restamps — a stale buffer can carry honest-looking metadata with impostor bytes"

key-files:
  created: []
  modified:
    - include/image_protocol.h
    - include/command_protocol.h
    - src/command_protocol.cpp
    - src/command_sender.cpp
    - src/image_tx_manager.cpp
    - src/image_rx_manager.cpp
    - src/camera_manager.cpp
    - scripts/verify_protocol_roundtrip.mjs

key-decisions:
  - "CR-01 fix shape: one imageKind byte in the chunk body (after imageId) beats receiver-side inference — the base's routing heuristic (armed-heal-first, then non-terminal-FULL fallback) is deleted, not patched; every chunk routes by exact (imageId, imageKind) and a mismatched-kind frame is dropped and logged"
  - "Kind validation at deserialize (reject outside THUMBNAIL/FULL_IMAGE) is untrusted-RF defense in depth mirroring T-02-11 on the request path (T-01-13-01)"
  - "CR-03 fix shape: drain one stale frame after the QQVGA downshift (fb_count 2 / GRAB_LATEST can hand back the pre-downshift capture with restamped metadata) AND bound fb->len at 8192 (~5x the observed 1341-1703 B correct-thumb ceiling, far below the 7157-28808 B impostors)"
  - "Both boards must be reflashed together before the next bench run — this is a wire change; mixed old/new firmware breaks chunk framing entirely (01-09 severance lesson)"
  - "No gap status flips in this plan; G-01-9 closure requires the 01-16 bench series (correctly-sized thumbs, both kinds COMPLETE, zero 100-percent-received CRC mismatches)"

patterns-established:
  - "Atomic wire-contract commits: serializer, deserializer, factory, both TX call sites, base dispatch, and harness mirrors land in ONE commit so the wire never disagrees with itself"

requirements-completed: [IMG-01, IMG-02, IMG-03, IMG-04]

duration: 17min
completed: 2026-08-24
status: complete
---

# Phase 01 Plan 13: Wire-Honesty Remediation (CR-01 + CR-03) Summary

**Kind-tagged 0x13 chunk wire format with kind-exact base routing, plus a thumbnail payload guard (stale-frame drain + 8192-byte bound) — the two G-01-9 root-cause mechanisms removed from code; bench proof rides 01-16.**

## Performance

- **Duration:** ~17 min (single executor session, 2026-08-24 03:44-04:01 UTC)
- **Builds:** `pio run -e esp32-s3-balloon -e esp32-s3-basestation` — 2 succeeded at every task gate
- **Harness:** `node scripts/verify_protocol_roundtrip.mjs` — exit 0, 53 checks green (50 prior + 3 new (img-i) checks) at every task gate
- **Actuals:** ~7,300 tokens (chars/4 over the realized diff) vs 17,000 estimated at confidence low — the estimate carried a full-stack sweep premium; the realized diff was surgical

## Tasks Completed

### Task 1: CR-01 wire change — imageKind byte in the 0x13 chunk frame (70543c5)

- `include/image_protocol.h`: `ImageChunkBody.imageKind` (u8, after imageId) with the CR-01 comment; 0x13 section comment and chunk-budget comment updated to the 6-byte overhead (framed total 7 + 6 + dataLen + 4 <= 217); `ImageChunkPacket` struct comment updated. NOTHING else in the header changed — all pacing constants and the 0x12/0x14 body layouts are byte-identical (verified: all 9 anchors grep exactly once).
- `src/command_protocol.cpp`: `serializeChunk` writes the kind right after imageId (6-overhead arithmetic); `deserializeChunk` reads it, rejects kinds outside THUMBNAIL/FULL_IMAGE (T-01-13-01), and enforces the 6-overhead frame-arithmetic agreement; `createChunkPacket` signature gains `uint8_t imageKind` as the second parameter and stamps it.
- `include/command_protocol.h`: declaration updated (matches the plan-pinned signature exactly once).
- `src/command_sender.cpp`: base chunk-dispatch `bodyOverhead` 5 -> 6 (manifest/beacon fixed-body cases untouched).
- `src/image_tx_manager.cpp`: both `createChunkPacket` call sites stamp the kind (push = THUMBNAIL, window = `entry.windowKind`) — see Deviations.
- `scripts/verify_protocol_roundtrip.mjs` (same commit): `createChunkPacketMirror` accepts/stamps the kind; `serializeChunk`/`deserializeChunk` mirrors use 6-overhead arithmetic with kind read + rejection; receiver-mirror chunk framing updated; (img-c) roundtrip asserts the kind survives next to imageId/chunkIndex/dataLen; (img-d) boundary arithmetic updated to the 217-byte packet; new **(img-i) chunk kind validation** negative check — a CRC-valid chunk carrying kind 2 fails deserialization.

### Task 2: CR-01 consumers — TX discriminator logs + RX kind-exact routing (394a231)

- `src/image_tx_manager.cpp`: push-chunk log names kind (always THUMBNAIL — symmetric with the window line); window-chunk log names `entry.windowKind`. Bench logs can now discriminate which kind's bytes left the balloon at every index (the G-01-9 defect-B discriminator).
- `src/image_rx_manager.cpp` `onChunkFrame`: the ENTIRE routing heuristic (heal-window-precedence branch + wire-order non-terminal-FULL fallback, with their comments) is replaced by a single `findTransfer(c.imageId, c.imageKind)` lookup. No-match drop logs the frame's id AND kind; the terminal-slot drop line is unchanged (already printed the slot's kind). `acceptChunk`, `startTransfer`, the window driver, and the heal loop are untouched — routing only.

### Task 3: CR-03 thumbnail payload guard (90300b8)

- `src/camera_manager.cpp`: file-scope `static constexpr size_t THUMB_MAX_BYTES = 8192;` citing the bench distribution (correct QQVGA q20 thumbs 1341-1703 B; impostors 7157-28808 B).
- Stale-frame drain: after the QQVGA/quality-20 downshift succeeds and before the real capture, fetch and discard ONE frame — logged as `Camera: drained stale frame after QQVGA downshift (%ux%u, %u B)` (the payload-vs-metadata discriminator), then `esp_camera_fb_return`ed; null proceeds without draining.
- Guard bound: the dimension guard's condition gains `fb->len > THUMB_MAX_BYTES` through the existing honest bail path (frame returned, member nulled, valid cleared, settings restored); the bail log now carries the byte length and the bound.
- `setFrameSize`/`setQuality` two-path logic untouched — `allocatedFrameSize` greps confirm G-01-8 intact (count >= 3).

## Verification Results

| Check | Result |
|-------|--------|
| `pio run -e esp32-s3-balloon -e esp32-s3-basestation` | 2 succeeded (at every task gate) |
| `node scripts/verify_protocol_roundtrip.mjs` | exit 0, 53 checks green (at every task gate) |
| Pacing anchors: IMG_RETRANSMIT_MAX_PASSES=3, IMG_WINDOW_STALL_MS=8000, IMG_WINDOW_SERVICE_PREEMPT_MS=5000, IMG_WINDOW_RX_SETTLE_MS=500, IMG_THUMB_HEAL_IDLE_MS=24000, IMG_ENTRY_TTL_MS=900000 | each exactly once (untouched) |
| 0x12 / 0x14 body layouts (IMG_MANIFEST_BODY_SIZE=27, IMG_TELEMETRY_BEACON_BODY_SIZE=19) | exactly once, byte-identical |
| PRI-01: beacon early-return + command-response loop order | untouched (image_tx_manager diff touches only the two call sites + two log lines) |
| Diff hygiene | 3 commits; file sets match task declarations (Task 1 + documented deviation below) |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Task 1 commit includes `src/image_tx_manager.cpp` (kind-argument stamping at both call sites)**

- **Found during:** Task 1 implementation
- **Issue:** Task 1 changes the `createChunkPacket` signature (adds `uint8_t imageKind`), but both call sites live in `src/image_tx_manager.cpp` — a Task 2 file. Without updating them the balloon build fails, so Task 1's own verify (`pio run` 2 succeeded) cannot pass. The plan's must_haves resolve the conflict in favor of same-commit stamping: "stamped by createChunkPacket at BOTH balloon call sites ... all in one commit so the wire contract never disagrees with itself."
- **Fix:** Task 1's commit carries the minimal kind arguments (push = `static_cast<uint8_t>(ImageKind::THUMBNAIL)`, window = `entry.windowKind`) with NO log changes; Task 2 adds the discriminator log lines and the RX routing. Task 2's diff is still exactly its two declared files.
- **Files modified:** src/image_tx_manager.cpp (2 call sites, 4 lines)
- **Commit:** 70543c5

No other deviations — plan executed as written.

## Auth Gates

None.

## Known Stubs

None — no stubs, no skipped tests, no unrun verifies.

## Gaps Status (unchanged by this plan, per prohibition)

- **G-01-9:** both root-cause mechanisms removed from CODE (CR-01 kind byte + kind-exact routing; CR-03 payload guard), but the gap stays OPEN — closure requires the 01-16 bench series (correctly-sized thumbs bytes < 8192 / QVGA-class chunk counts 7-10, both kinds COMPLETE, zero stored-bytes CRC mismatches at 100 percent reception).
- **G-01-7 (rescoped):** unchanged this round; the 01-12 residual levers (defer-aware D-24 pass accounting, FULL-manifest re-announce) are NOT in this plan's scope.
- **Operational invariant for 01-16:** this is a wire change — both boards MUST be flashed together; running the pair mixed breaks chunk framing entirely.

## Self-Check: PASSED

- All 8 modified files exist and carry the committed changes (3 commits verified in git log: 70543c5, 394a231, 90300b8)
- Builds 2/2 and harness 53/53 green on the final tree

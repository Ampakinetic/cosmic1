---
phase: 02-image-transmission
verified: 2026-08-18T22:01:04Z
status: gaps_found
score: 18/27 must-haves verified
behavior_unverified: 4 # SC-2/SC-4/SC-5/SC-6 present + wired; runtime behavior needs hardware UAT
overrides_applied: 0
gaps:
  - truth: "Full image transfers complete in background (SC-3 / IMG-03; 02-02 truth 'a new capture never abandons an in-progress pull')"
    status: failed
    reason: "Three interacting code defects make pull completion structurally unsound at the phase's own documented operating point (D-28 20 s capture spacing, research A1 ~210-280 ms/packet, A3/A6 image sizes). (1) ImageRxManager passCount is NEVER reset on progress (src/image_rx_manager.cpp:125,:175 only increment; no reset anywhere — verified by grep) so ANY 3 stalls >8 s over a pull's whole lifetime finalize it INCOMPLETE even if every stall healed. (2) ImageTxManager::pushPending gives thumbnail-push/full-announce work strict, UNBOUNDED priority over window service (src/image_tx_manager.cpp:354-385) — each queued capture starves the active pull for its entire push duration (15-22 transmit passes per capture; 2-3 queued entries stack). (3) Queue overflow drop-oldest (src/image_tx_manager.cpp:281-295) frees the lowest-enqueueSeq entry regardless of an armed window — the FIFO pull target is always the oldest entry, and balloon entries are never freed on transfer completion (only newer-ID eviction, 15-min TTL, overflow), so under sustained capture the queue saturates and the 4th capture deletes the entry the base is actively pulling (subsequent requests return UNKNOWN_IMAGE). Offered airtime per 20 s cycle (push ~4.5-6.6 s + pull 12-22.5 s for a 40-75-chunk full + beacons ~1.2 s) meets or exceeds the 20 s budget at the plans' own A3/A6 numbers, so >8 s pull gaps recur and passes accumulate. The 'without blocking telemetry' half of SC-3 IS satisfied (beacon priority + loop order verified); the 'complete' half is defeated."
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "passCount never resets on progress (lines 125, 175 increment only); 3 cumulative stalls — even fully healed — finalize a pull INCOMPLETE"
      - path: "src/image_tx_manager.cpp"
        issue: "pushPending (354-385) starves window service without bound; overflow drop-oldest (281-295) can free the active pull target; entries never freed on completion"
    missing:
      - "Reset passCount to 0 on window-slice completion with progress (or charge a pass only when the balloon was actually idle), per review CR-03 fix (b)"
      - "Bound push-induced starvation: interleave window service every N passes or preempt push when the armed window's lastActivityMs exceeds ~5 s (CR-03 fix a)"
      - "On queue overflow, evict entries that are not ANNOUNCED-with-armed-window before the pull target (CR-03 fix c); free entries once their transfer completes or is superseded"
  - truth: "A thumbnail that arrives with holes falls back to the same window-pull re-request path (D-22; 02-03 truth 6)"
    status: failed
    reason: "The fallback exists (src/image_rx_manager.cpp:157-189) but cannot work: PayloadImageWindowRequest carries no image kind (include/image_protocol.h:146-150), and the balloon's handleWindowRequest matches ONLY ANNOUNCED entries and slices ONLY entry.fullBuffer (src/image_tx_manager.cpp:533,:546,:604-609) — it has no path to re-serve thumbnail bytes. A re-request for a stalled thumbnail id (a) is rejected UNKNOWN_IMAGE while the entry is still pushing, or (b) after the full is announced, is armed against the FULL buffer with thumbnail chunk numbering: the returned FULL bytes fail the thumbnail slot's expected-length check and are dropped, so every thumbnail that loses one chunk finalizes INCOMPLETE. Worse, in case (b) evictEntriesOlderThan (image_tx_manager.cpp:561) fires BEFORE arming and frees OLDER entries — if the stalled thumbnail's id is newer than the active pull's target, the misdirected re-request kills the active pull mid-stream. The thumbnail stall loop also issues requests without checking whether a full pull is active, violating the module's own speak-only-on-three-triggers discipline. Pushed manifests/chunks are one-shot (state advances even when transmit fails: pushThumbManifest :419-421, announceFullManifest :508-510 — WR-03) and the 0xAA-resync flaw drops frames preceded by noise (WR-04), so thumbnail chunk loss is expected on a real link with NO working recovery."
    artifacts:
      - path: "include/image_protocol.h"
        issue: "PayloadImageWindowRequest has no imageKind field — the wire contract cannot address thumbnails"
      - path: "src/image_tx_manager.cpp"
        issue: "handleWindowRequest serves only ANNOUNCED entries from fullBuffer; evictEntriesOlderThan can free the active pull's target on a misdirected thumbnail re-request"
      - path: "src/image_rx_manager.cpp"
        issue: "Thumbnail stall fallback (157-189) issues misdirected requests and does not gate on an active pull"
    missing:
      - "Extend PayloadImageWindowRequest with a kind byte and serve thumbnail buffers on the balloon (review CR-01 fix)"
      - "Gate the thumbnail fallback on no-active-pull and on the entry's full manifest having arrived (push provably finished)"
  - truth: "The transfer panel shows percent and chunks received/total derived from real chunk accounting only (D-20; 02-03 truth 7)"
    status: partial
    reason: "Normal path verified (getTransferSnapshot derives state from bitmap/pass/terminal truth), but allocateSlot step 3 (src/image_rx_manager.cpp:328-341) evicts a non-terminal slot via finalizeIncomplete and RETURNS it WITHOUT the full reset step 2 performs (:320): startTransfer then overwrites only manifest-derived fields, leaving terminal=true plus stale receivedCount/bytesReceived/passCount/windowActive. The new transfer is a zombie — chunks are discarded ('chunk for finalized image ignored'), it can never progress, and its UI row shows the PREVIOUS occupant's receivedCount against the new totalChunks (fabricated accounting, wrong percent). Each zombie consumes a slot until recycled by a later step-2 terminal recycle (so 'exhausts all 8 slots until reboot' is overstated, but the image in the zombie slot is silently lost)."
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "allocateSlot step 3 returns a slot after finalizeIncomplete without '*oldest = ImageRxTransfer{}' — stale terminal flag and counters"
    missing:
      - "Reset the slot the same way step 2 does before returning it (review CR-02 fix: finalizeIncomplete + releaseSlotWork + '*oldest = ImageRxTransfer{}')"
  - truth: "The sidecar records capture time, receipt time, altitude and GPS position, trigger source, camera settings, chunk statistics, completeness, storage status (D-30; SC-4 metadata half; 02-03 truth 5)"
    status: partial
    reason: "The sidecar JSON contains every D-30 field plus kind and rssi:null (verified in writeSidecar), but sidecarPath ignores kind (src/sd_storage.cpp:93-95): thumbnail N and full N share /images/IMG_{id:05d}.JSON. Both kinds finalize for every capture in the normal path — the full's later finalization opens the sidecar FILE_WRITE ('w', truncate) and DESTROYS the thumbnail's D-30 record minutes after it is written. Full-image metadata survives; thumbnail metadata is systematically lost (contradicts 'written ONCE per image at finalization' for the thumbnail kind and degrades the Phase 3 gallery contract)."
    artifacts:
      - path: "src/sd_storage.cpp"
        issue: "sidecarPath has no kind parameter — thumbnail and full sidecars collide on one name"
    missing:
      - "Kind-suffix the sidecar (IMG_{id}_T.JSON) or merge both records into one document at second finalization (review WR-05 fix)"
deferred: []
behavior_unverified_items:
  - truth: "Thumbnail preview displays on base station within 10 seconds of capture (SC-2 / IMG-02)"
    test: "Capture on the balloon (manual + interval) over real E32 radios; watch the Latest Capture card and /status latestThumbId"
    expected: "Verified thumbnail renders within 10 s; QQVGA/quality-20 thumbnail size lands in the assumed 2.5-4 KB band (A3); per-packet airtime matches the 9.6 kbps assumption (A1)"
    why_human: "RF delivery timing and real JPEG sizes need physical radios and camera; host harness proves the wire format only"
  - truth: "Images successfully saved to SD card with metadata (SC-4 / IMG-05)"
    test: "Wire the microSD (GPIO 12/13/11/10 per base_station_config.h, FAT32), complete a transfer, pull the card"
    expected: "/images contains IMG_{id}.JPG, IMG_{id}_T.JPG, IMG_{id}.JSON with correct D-30 fields; storage chip reads OK; card-absent run stays degraded with honest UNAVAILABLE"
    why_human: "SD wiring (research Q2/A4) is unconfirmed hardware; SPI pin choice and card behavior cannot be exercised by host builds"
  - truth: "Telemetry continues to update at 5-second rate during image transfers (SC-5 / PRI-01)"
    test: "Start a full-image transfer; watch /status telemetry ageMs across the transfer"
    expected: "telemetry age stays <= ~10 s throughout the transfer (beacon outranks chunks at every transmit opportunity)"
    why_human: "Runtime arbitration over the half-duplex link needs two radios; code-level priority (loop order + beacon early-return) is verified but interleaving behavior is physical"
  - truth: "Event-based auto-capture triggers on altitude/location changes (SC-6 / CTRL-05)"
    test: "Enable events via the UI toggle (eventsEnabled defaults OFF); drive altitude/distance deltas with live GPS; fire AUTO_CAPTURE_DISABLE mid-flight"
    expected: "Event-stamped captures at the deltas (defaults 150 m / 500 m / 20 s); zero automatic captures while disabled; one capture near an interval deadline (no double-capture)"
    why_human: "Needs live GPS and the flight-phase machine on real hardware; engine logic is code-verified end to end"
human_verification:
  - test: "Prohibition review (02-02 plan, judgment-tier, flagged): MUST NOT let image chunk traffic delay or displace the telemetry beacon or pending command responses (PRI-01)"
    expected: "Human confirms at UAT that telemetry age stays <= ~10 s during transfers"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (beacon branch returns before any chunk work, src/image_tx_manager.cpp:101-104; command responses outrank both by loop order, src/main_balloon.cpp:735-745), but the prohibition is judgment-tier with no wired enforcement. Minor note: a due beacon can also be delayed by the enqueue poll's blocking captureThumbnail call (~hundreds of ms) which runs before the beacon check"
  - test: "Prohibition review (02-03 plan, judgment-tier, flagged): MUST NOT report or display an image as transferred, stored, or complete when it is not"
    expected: "Human confirms every COMPLETE row/sidecar is backed by the verified CRC32 result"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS on the finalize path (complete set only after CRC match; SD-degraded fulls report INCOMPLETE) but FAIL on the CR-02 zombie path (stale counters displayed) — that defect is filed as a gap above"
  - test: "Prohibition review (02-03 plan, judgment-tier, flagged): MUST NOT silently delete or overwrite stored images to make SD space"
    expected: "Human fills the card / induces write failure and confirms no existing file disappears"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (zero SD.remove/rmdir/unlink in src/ and include/, verified by grep; degrade() stops storing and warns; FILE_WRITE truncate only ever re-creates the SAME id's partial file)"
  - test: "Prohibition review (02-04 plan, judgment-tier, flagged): MUST NOT fire event-triggered captures while auto-capture is disabled"
    expected: "Human confirms AUTO_CAPTURE_DISABLE stops interval AND event captures"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (single 'enabled' guard at src/auto_capture.cpp:130 covers the interval branch and processEvents)"
---

# Phase 2: Image Transmission Verification Report

**Phase Goal:** Transfer images from balloon to base station over LoRa with thumbnails
**Verified:** 2026-08-18T22:01:04Z
**Status:** gaps_found
**Re-verification:** No — initial verification

**Note on mode:** ROADMAP.md marks Phase 2 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all three Phase 1 verifications).

## Verification Basis

Verified independently of all four SUMMARYs and of 02-REVIEW.md: both transfer modules, the wire contract, the SD module, the event engine, both receive dispatchers, and both mains were read in full or in their relevant regions; the wire harness was executed by this verifier (46/46 clauses, exit 0); both firmware targets were built by this verifier (2x SUCCESS); every commit claimed in the SUMMARYs was confirmed in git log. The review's three critical findings were re-derived from source before being believed — all three are confirmed as code facts, with severity corrections noted below.

## Goal Achievement

### Observable Truths

Deduplicated: roadmap SCs keep roadmap wording; plan truths that restate an SC are folded into its row.

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1: Captured images are chunked and transmitted over LoRa with sequence tracking | ✓ VERIFIED | 200-byte chunks with chunkIndex sequence + presence-bitmap reassembly; serializers big-endian via writeUint16/32 (command_protocol.cpp:296-535); framing/transmit wired to the E32 driver; verifier-run harness 46/46 exit 0. RF delivery itself is the hardware-UAT family carried from Phase 1 |
| 2 | SC-2: Thumbnail preview displays on base station within 10 seconds of capture | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Full path wired: push state machine (image_tx_manager.cpp:362-459) → reassembly + CRC32 (image_rx_manager.cpp:491-549,:556-576) → GET /img/{id}_t.jpg (main_basestation.cpp:1502-1575) → UI card (JS :504-510). Timing needs radios (assumptions A1/A3 flagged in plan) |
| 3 | SC-3: Full image transfers complete in background without blocking telemetry | ✗ FAILED | "Without blocking telemetry" verified (beacon early-return + loop order); "complete" defeated by three structural defects — see Gap 1. passCount never resets (7 occurrences, zero resets); pushPriority unbounded (tx:354-385); drop-oldest frees the pull target (tx:281-295) |
| 4 | SC-4: Images successfully saved to SD card with metadata | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | SdStorage complete at code level (seek+offset writes, once-at-finalization sidecar, D-30 fields, stop-storing-and-warn); SD wiring unconfirmed hardware (Q2/A4). Metadata half degraded by WR-05 sidecar collision — see Gap 4 |
| 5 | SC-5: Telemetry continues to update at 5-second rate during image transfers | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Beacon wired from live Sensors() (image_tx_manager.cpp:115-167), fixed-priority arbiter verified structurally; runtime interleaving needs two radios |
| 6 | SC-6: Event-based auto-capture triggers on altitude/location changes | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Engine fully code-verified (auto_capture.cpp:158-224); needs live GPS. Note eventsEnabled defaults OFF — operator must opt in via UI toggle |
| 7 | 02-01: createThumbnail leaves no dangling buffer on ANY failure path (CR-04) | ✓ VERIFIED | camera_manager.cpp: capture-then-allocate; both early returns null buffer + clear valid + restore settings; estimateImageSize gone (grep 0 hits) |
| 8 | 02-01: Both receivers dispatch on type byte before body arithmetic; unknown types discarded (WR-12) | ✓ VERIFIED | command_handler.cpp:795-798 (0x10 only); command_sender.cpp:310-334 (0x11/0x12/0x13/0x14 with per-type arithmetic; only RESPONSE touches the tracked-command table); harness img-g PASS |
| 9 | 02-01: Both targets build green with correct module exclusion | ✓ VERIFIED | Verifier-run `pio run`: 2x SUCCESS; platformio.ini balloon env excludes image_rx_manager+sd_storage, base env excludes auto_capture+image_tx_manager |
| 10 | 02-02: Full announced once after thumbnail; chunks served ONLY via window requests (D-17) | ✓ VERIFIED | State machine: ANNOUNCE_FULL entered only from completedThumbState (:465-468); announce once → ANNOUNCED (:508); chunks only via armed window (:593-631) |
| 11 | 02-02: FIFO; a new capture never abandons an in-progress pull (D-19) | ✗ FAILED | FIFO ordering itself correct (enqueueSeq everywhere), but overflow drop-oldest frees the FIFO pull target and CR-01's misdirected eviction kills active pulls — see Gaps 1 and 2 |
| 12 | 02-02: Priority: command response, then beacon when due, then at most one chunk (PRI-01) | ✓ VERIFIED | main_balloon.cpp:735-745 loop order enforced; beacon branch consumes the pass and returns (tx:101-104); beacon XOR chunk per pass |
| 13 | 02-02: Oversize fulls (>50000) skip transfer with logged warning; thumbnail still pushes (PRI-03/Q4) | ✓ VERIFIED | Enqueue gate tx:201-207 logs id+size, parks at THUMB_PUSHED; base never sees a manifest. UXGA confirmation is the planned hardware-UAT item |
| 14 | 02-02: Window requests idempotent (Pitfall 10) | ✓ VERIFIED | Arming re-points cursor at startChunk, no ID allocation, no queue mutation (tx:577-584); same-id re-request evicts nothing (strictly-older comparison) |
| 15 | 02-03: 3-pass bound finalizes incomplete: kept on SD, flagged in sidecar, slot freed (D-24) | ✓ VERIFIED | finalizeIncomplete (rx:632-654): terminal flag, sidecar complete:false, releaseSlotWork, FIFO advance. Bound itself works as coded — the defect is what charges passes (Gap 1) |
| 16 | 02-03: Completed image verifies end-to-end CRC32 over stored bytes before complete (D-23) | ✓ VERIFIED | Thumbnails: esp_rom_crc32_le over RAM buffer (rx:567-576); fulls: chained read-back in 256-byte pieces with length cross-check (rx:795-828); SD-degraded fulls report INCOMPLETE/notStored, never fabricated |
| 17 | 02-03: SD naming IMG_{id}.JPG / IMG_{id}_T.JPG / IMG_{id}.JSON, %05u, flat /images (D-29/31/32) | ✓ VERIFIED | sd_storage.cpp:81-95; names built only from numeric ids (strtol-parsed route ids) |
| 18 | 02-03: Sidecar records the D-30 field set | ✗ FAILED (partial) | Field set complete (+kind, rssi:null with documented reason) but the thumbnail's record is destroyed by the same id's full finalization (WR-05) — see Gap 4 |
| 19 | 02-03: Thumbnail with holes falls back to the same window-pull path (D-22) | ✗ FAILED | Mechanism present but structurally cannot heal thumbnails and can kill the active pull — see Gap 2 |
| 20 | 02-03: Transfer panel derives percent/chunks from real accounting only (D-20) | ✗ FAILED (partial) | Normal path verified; CR-02 zombie slot displays stale counters under slot pressure — see Gap 3 |
| 21 | 02-03: SD-full/init failure stops storing, warns in /status+UI, never deletes (Q5) | ✓ VERIFIED | degrade() (sd_storage.cpp:337-353) closes handles, stops persisting, surfaces via getStatus; storage chip three-state (main_basestation.cpp:1479-1500); zero removal APIs repo-wide (grep) |
| 22 | 02-03: FIFO pull order, one active full pull at a time (D-19 base) | ✓ VERIFIED | findActivePull/activateNextPull earliest-arrivalSeq (rx:288-296,:693-720); pullActive single-owner |
| 23 | 02-04: Captures fire on altitude/distance deltas and first-entry LAUNCH/APEX/PARACHUTE_DESCENT/LANDING (D-25) | ✓ VERIFIED | auto_capture.cpp:158-224: seen-mask all 8 phases, 4 triggering; hysteresis baselines reset only on fire; TinyGPSPlus::distanceBetween |
| 24 | 02-04: Thresholds configurable via SET_EVENT_THRESHOLDS with ACK, reported via GET_STATUS (D-26) | ✓ VERIFIED | Handler case + NACK_PARAM bounds (command_handler.cpp:185,:684-723 area); GET_STATUS populates the four event fields (:661); ResponseStatusData field-sum 28 (harness img-h); base route POST /set-event-thresholds with full-long validation (:704) + Event Capture card (:944) |
| 25 | 02-04: Event resets interval baseline; no double-capture; single image-ID sequence (D-27/CR-05) | ✓ VERIFIED | Shared lastCaptureTime advanced by every fire (:245); interval branch requires elapsed >= intervalMs AND >= minSpacing (:143); allocateImageId single sequence (:267-271) |
| 26 | 02-04: Global min spacing (default 20 s, configurable) gates ALL automatic captures (D-28) | ✓ VERIFIED | fire() gate (:234-237) wraps interval and every event branch; setEventConfig re-validates 10..5000 / 10..50000 / 5..3600 in-module (:104-108) |
| 27 | 02-04: AUTO_CAPTURE_DISABLE stays master off-switch; GPS-valid required for deltas | ✓ VERIFIED | Single `enabled` guard above both branches (:130); satellites==0 returns before delta evaluation (:185) |

**Score:** 18/27 truths verified (4 present-but-behavior-unverified; 5 failed, of which 2 partial)

### Deferred Items

None. No later milestone phase addresses the ARQ defects; Phase 3 (web UI/telemetry/maps/gallery) assumes working image storage but does not cover transfer reliability.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| include/image_protocol.h | Complete wire contract | ✓ VERIFIED | All types/enums/constants/bodies present; 0x12/0x13/0x14; 27/17-byte bodies; note: PayloadImageWindowRequest lacks a kind field (Gap 2 root cause) |
| src/command_protocol.cpp | Serializers + type-owning factories | ✓ VERIFIED | serialize/deserialize for all three bodies, all multi-byte fields big-endian; createManifestPacket/createChunkPacket/createTelemetryBeaconPacket assign type first |
| src/camera_manager.cpp | CR-04/WR-11 capture-then-allocate | ✓ VERIFIED | Every failure path nulls buffer, clears valid, restores settings; quality 20; estimate deleted |
| src/image_tx_manager.cpp | Push + window servicing + arbiter | ✓ VERIFIED (presence/wiring) | All promised behavior present and wired; behavioral defects filed as Gaps 1-2 |
| src/image_rx_manager.cpp | Reassembly + window ARQ + snapshot | ✓ VERIFIED (presence/wiring) | All promised behavior present and wired; behavioral defects filed as Gaps 1-3 |
| src/sd_storage.cpp | SD persistence with naming/sidecar/degrade policy | ✓ VERIFIED (presence/wiring) | Instance-overload SD.begin, seek+offset, sidecar-once, never-delete; WR-05/WR-06 defects noted |
| src/auto_capture.cpp | Event engine inside AutoCapture | ✓ VERIFIED | Full D-25..D-28 semantics confirmed by reading the whole module |
| src/main_balloon.cpp / src/main_basestation.cpp | Loop wiring / routes / UI | ✓ VERIFIED | Ordering, beacon, /img routes, /status fields, cards, storage chip all present |
| scripts/verify_protocol_roundtrip.mjs | Harness with teeth | ✓ VERIFIED | Verifier-run: 46/46 PASS, exit 0. (SUMMARY says 47 clauses — actual count 46; trivial discrepancy) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| image_tx_manager process() | auto_capture getLastImageId() | CR-05 single-authority poll | ✓ WIRED | tx:87-92; note level-poll race (WR-07): two captures completing in one loop pass silently drop the older image |
| createManifestPacket/createChunkPacket | both receive dispatchers | type byte at buffer[2], per-type arithmetic | ✓ WIRED | Both dispatchers verified; harness img-g |
| main_basestation /img/{id}_t route | image_rx latest thumbnail | retained RAM + SD fallback, image/jpeg, numeric-only id | ✓ WIRED | :1502-1575 |
| command_handler IMAGE_WINDOW_REQUEST case | ImageTx handleWindowRequest | ACK/NACK via createResponsePacket | ✓ WIRED | :182,:694-723 |
| image_rx process() | command_sender sendCommand | tracked-command machinery, WINDOW timeout class | ✓ WIRED | rx:722-760; sender ackTimeoutFor:50 returns CMD_ACK_TIMEOUT_WINDOW_MS (15000) |
| image_rx chunk receive | sd_storage writeChunk | seek(chunkIndex*chunkSize) + immediate write | ✓ WIRED | rx:531; sd:166-203 |
| /status JSON | getTransferSnapshot | transfers[] rows + storage{} | ✓ WIRED | :1460,:1479 |
| auto_capture fire() | camera_manager setLastCaptureSource | source stamped before captureImage | ✓ WIRED | ac:241 |
| /set-event-thresholds route | SET_EVENT_THRESHOLDS case | full-long validation, 7-byte BE payload, tracked command | ✓ WIRED | :704 + handler:185 |
| auto_capture process() | system_state getFlightPhase | seen-mask first-entry | ✓ WIRED | ac:164-180 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| Base UI Latest Capture card | latestThumbId / thumbImg.src | ImageRx retained verified thumbnail via /status poll | Yes (CRC-gated retention; null-honest before first) | ✓ FLOWING |
| Transfers panel | transfers[] | getTransferSnapshot from bitmap/pass/terminal | Yes on normal path; stale under CR-02 zombie (Gap 3) | ⚠️ STATIC (edge) |
| Telemetry object | telemetry{} | 0x14 beacon frames → onTelemetryBeaconFrame | Yes; null-honest when no beacon ever received | ✓ FLOWING |
| Sidecar JSON | meta fields | manifest fields + latest beacon + chunk counters | Yes; thumbnail record destroyed by WR-05 (Gap 4) | ⚠️ STATIC (edge) |
| Event chip | eventThresholds{} | 30 s GET_STATUS poll latch (getStatusData) | Yes; null until first STATUS | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite | node scripts/verify_protocol_roundtrip.mjs | 46 PASS / 0 FAIL, exit 0 | ✓ PASS |
| Both firmware targets compile+link | pio run -e esp32-s3-balloon -e esp32-s3-basestation | 2x SUCCESS | ✓ PASS |
| Full-image transfer completion under documented cadence | (structural code analysis; runtime needs hardware) | passCount never resets + unbounded push priority + drop-oldest kills pull target | ✗ FAIL (Gap 1) |
| Thumbnail hole healing via window re-request | (structural code analysis) | Balloon serves only FULL bytes for window requests; kind not addressable | ✗ FAIL (Gap 2) |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no scripts/*/tests/probe-*.sh exist. SKIPPED.

### Requirements Coverage

Plan frontmatter union: 02-01 [IMG-01, IMG-02, IMG-04], 02-02 [IMG-01, IMG-03, PRI-01, PRI-03], 02-03 [IMG-03, IMG-04, IMG-05, PRI-03], 02-04 [CTRL-05] — exactly the roadmap's Phase 2 set. No orphans; REQUIREMENTS.md maps no additional IDs to Phase 2.

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|---------------------|----------|
| IMG-01 | 02-01, 02-02 | Captured images transmitted over LoRa | ✓ SATISFIED (code) | Push + pull machinery wired end-to-end; RF runtime in UAT family |
| IMG-02 | 02-01 | Thumbnail preview displays immediately | ✓ SATISFIED (code) | Path complete; 10-s window + loss recovery are the open halves (UAT; Gap 2) |
| IMG-03 | 02-02, 02-03 | Full resolution images transfer in background after thumbnail | ✗ BLOCKED (partial) | Wired and FIFO-ordered, but completion structurally defeated under the documented cadence — Gap 1 |
| IMG-04 | 02-01, 02-03 | Images chunked for reliable LoRa transmission | ✗ BLOCKED (partial) | Chunking + window ARQ exist (harness-proven), but thumbnails have NO working loss recovery (Gap 2) and fulls die under load (Gap 1) — the "reliable" half is not yet met |
| IMG-05 | 02-03 | Base stores received images on SD card | ✓ SATISFIED (code) | Module complete; hardware wiring UAT; metadata loss WR-05 |
| PRI-01 | 02-02 | Telemetry priority over image data | ✓ SATISFIED (code) | Structural priority verified; runtime confirmation UAT |
| PRI-03 | 02-01..03 | Gracefully handles LoRa bandwidth limitations | ✓ SATISFIED (code) | Degrade-never-halt holds everywhere (bounded passes, honest INCOMPLETE, oversize skip) — note graceful degradation is achieved, but the design point itself is infeasible per Gap 1 |
| CTRL-05 | 02-04 | Event-based auto-capture triggers | ✓ SATISFIED (code) | Engine + thresholds + reporting verified; live-GPS confirmation UAT |

Note: REQUIREMENTS.md already marks all eight as "Complete" — that traceability is ahead of reality for IMG-03 and IMG-04 until the gaps close.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/image_rx_manager.cpp | 336-341 | Slot returned after eviction without reset (CR-02) | 🛑 Blocker (gap 3) | Zombie slot, fabricated accounting, silently lost image |
| src/image_tx_manager.cpp | 354-385, 281-295 | Unbounded push priority + drop-oldest of pull target (CR-03) | 🛑 Blocker (gap 1) | Full pulls systematically INCOMPLETE under documented cadence |
| src/image_rx_manager.cpp | 125, 175 | passCount monotonic, never reset on progress | 🛑 Blocker (gap 1) | Any 3 healed stalls kill a pull |
| include/image_protocol.h + tx/rx | 146-150 / tx:533,604 | Window request cannot address thumbnails (CR-01) | 🛑 Blocker (gap 2) | D-22 unimplementable as wired; can kill active pulls |
| src/sd_storage.cpp | 93-95 | Thumbnail/full sidecar name collision (WR-05) | ⚠️ Warning (gap 4) | Thumbnail D-30 records systematically destroyed |
| src/sd_storage.cpp | 136-141 | Same-id re-open leaks FATFS handle (WR-06) | ⚠️ Warning | Bounded handle table exhaustion under repeated re-push |
| src/image_tx_manager.cpp | 419-421, 508-510 | Manifest transmits one-shot, state advances on failure (WR-03) | ⚠️ Warning | Single lost manifest loses the whole transfer silently |
| command_handler/sender processIncomingByte | reset branches | 0xAA resync flaw (WR-04) | ⚠️ Warning | Noise-preceded frames lost; interacts with WR-03 |
| src/command_handler.cpp | 72-125 | Second complete command in one drain silently overwritten (WR-01) | ⚠️ Warning | Recovery only via base retry cycle |
| src/main_balloon.cpp | 376-401 | Balloon RX buffer not enlarged (WR-02, asymmetric with base) | ⚠️ Warning | Two frames during a blocking execute can corrupt |
| src/image_tx_manager.cpp | 87-92 | Level-poll can miss same-pass captures (WR-07) | ⚠️ Warning | Older image silently never transferred |
| src/main_balloon.cpp | 475-479 | False camera health-check failure every boot (WR-08) | ℹ️ Info | Standing false alarm in boot diagnostics |
| include/base_station_config.h | 170 | Malformed BACKUP macro (IN-01) | ℹ️ Info | Unused landmine |
| camera_manager createThumbnail | — | No JPEG validation on thumbnail frames (IN-06) | ℹ️ Info | Corrupt-but-CRC-consistent thumbnails display broken |
| platformio.ini | 76 | Pre-existing TODO on lora_comm.cpp exclusion (predates Phase 2) | ℹ️ Info | Legacy, not phase debt |

No TBD/FIXME/XXX markers in any phase-modified file; no placeholder/stub data paths found; all UI values wired to computed state.

### Review Cross-Check (02-REVIEW.md)

All 3 criticals independently confirmed in source. Severity corrections: CR-02's "base stops receiving images entirely until reboot" is overstated (allocateSlot step 2 recycles terminal zombie slots later) but the silent image loss and fabricated accounting are real; CR-03's exact wall-clock arithmetic depends on flagged hardware assumptions (A1/A3/A6), but the structural facts (passCount never resets, unbounded push priority, drop-oldest of the pull target, entries never freed on completion) are verified code facts that hold regardless. The 02-03 SUMMARY's "Known Limitations" already concedes the D-22 behavior (thumbnail re-requests get FULL bytes) but frames it as a hardware-UAT measurement; the review's deeper finding — that the misdirected request can evict the active pull's entry — is additionally confirmed (tx:561).

### Human Verification Required

Status is gaps_found, so these carry forward alongside gap closure (all are hardware-UAT class, consistent with the plans' explicit deferrals):

1. **Thumbnail within 10 s over real radios (SC-2)** — capture, watch Latest Capture card; validates A1 air-rate and A3 thumbnail-size assumptions.
2. **SD wiring + card contents (SC-4)** — microSD on GPIO 12/13/11/10, FAT32; pull card and inspect IMG_{id}.JPG/_T.JPG/.JSON; SD-absent run stays honest-degraded.
3. **Telemetry age during a transfer (SC-5 / PRI-01 prohibition)** — /status telemetry.ageMs <= ~10 s throughout a full-image pull.
4. **Event triggers with live GPS (SC-6)** — enable events (default OFF), drive deltas, verify single captures, master-disable stops everything.
5. **UXGA oversize cap (PRI-03/Q4)** — thumbnail arrives, full skipped with the logged warning naming id+size.
6. **Four flagged judgment-tier prohibitions** — see frontmatter human_verification entries; this verifier's non-authoritative code-level judgments are recorded there (3 PASS, 1 FAIL-on-one-path).

### Gaps Summary

The phase built, wired, and harnessed everything it promised at the structural level: the wire contract is locked and regression-tested, the thumbnail push path is complete end-to-end, SD persistence follows every naming/policy decision, the beacon and its arbitration are real, and the CTRL-05 event engine is exact. Both targets build green and the harness is green — task completion is high.

What fails is the transfer state machines' cross-module behavior — precisely the layer no grep can certify and no harness covers:

1. **Full-image pulls do not survive their own design point** (Gap 1). The balloon's unbounded push priority and the base's never-resetting pass counter are contradictory assumptions about the same 8-second window, and the drop-oldest queue deletes the entry the base is pulling. Under the documented 20 s capture cadence and the plans' own airtime numbers, the offered load meets or exceeds capacity, so pulls systematically finalize INCOMPLETE.
2. **Thumbnail loss recovery is structurally absent** (Gap 2). The D-22 fallback issues requests the balloon can only answer with FULL-image bytes — the wire contract has no way to ask for thumbnail bytes — and the misdirected request can evict the active pull's entry, converting a thumbnail hole into a dead full transfer. With one-shot manifests (WR-03) and the resync flaw (WR-04), any lost thumbnail chunk is unrecoverable.
3. **Slot-pressure accounting corrupts** (Gap 3) and **thumbnail metadata is systematically destroyed** (Gap 4) — two bounded-scope defects with one-line-ish fixes.

Gaps 1 and 2 are wire-contract-adjacent (the kind byte) and state-machine reworks; they should be planned as a gap-closure plan before Phase 3 builds a gallery on top of image storage. Nothing here requires reworking the Phase 1 transport, the framing, or the module boundaries — the defects are in policy (priority, pass accounting, eviction, naming), not architecture.

---

_Verified: 2026-08-18T22:01:04Z_
_Verifier: Claude (gsd-verifier)_

---
phase: 02-image-transmission
verified: 2026-08-19T00:06:50Z
status: gaps_found
score: 22/27 must-haves verified
behavior_unverified: 3 # SC-2/SC-5/SC-6 present + wired; runtime behavior needs hardware UAT (SC-4 reclassified from this list to FAILED — see gaps)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 18/27
  gaps_closed:
    - "Gap 1 (SC-3 policy defects): passCount reset on accepted-chunk progress (rx:573, sole zero-writer verified by grep); pushPending preemption for starved armed windows (tx:417-422, before findActiveEntry, 5000 ms < 8000 ms stall); class-ranked overflow eviction tx:304-338 + evictionClassOf tx:180-194 (ANNOUNCE_FULL in class 4 per Deviation 1 — correct, prevents the nullptr crash)"
    - "Gap 2 (D-22 thumbnail heal): PayloadImageWindowRequest is 6 bytes with imageKind at offset 2 (header + harness img-f exact-byte clause + tx:595-598 decode + handler 6-byte guard/echo); balloon serves THUMBNAIL windows from thumbBuffer (tx:626-631, 726-728); evictEntriesOlderThan single call site inside if (!thumbWindow) (tx:670); base gates (no-active-pull + full-manifest-arrived + 24 s oversize fallback, rx:180-186) and heal-window chunk routing (rx:245-246)"
    - "Gap 3 (D-20 zombie slot): allocateSlot step 3 now performs *oldest = ImageRxTransfer{} after the slot-pressure finalizeIncomplete (rx:369-373)"
    - "Gap 4 (D-30 sidecar collision): sidecarPath(out, cap, id, kind) emits IMG_%05u_T.JSON for thumbnails (sd:93-105); declaration updated (sd_storage.h:128); writeSidecar passes meta.kind (sd:263)"
  gaps_remaining: [] # all four original gaps are closed at code level
  regressions: [] # diff scope b5e41eb..7cd2169 touches only the 8 declared files; previously-verified truths rest on unchanged code (camera/auto_capture/mains/command_protocol/command_sender)
gaps:
  - truth: "Full image transfers complete in background (SC-3 / IMG-03)"
    status: failed
    reason: "All four prior gap-closure fixes verified sound, but two NEW defects in the SD persistence pipeline (fresh 02-REVIEW.md CR-01/CR-02, both independently confirmed by this verifier) re-defeat the 'complete' half in the phase's documented operating mode (periodic captures overlapping an active pull). (CR-01) startTransfer() calls SDStorage().openTransfer() UNCONDITIONALLY for every manifest (image_rx_manager.cpp:496) before the QUEUED decision (:506); openTransfer for a different id CLOSES the previous partial file of that kind (sd_storage.cpp:146-151); writeChunk never reopens and silently drops (sd:186-194, return value ignored at rx:563). So a newer capture's FULL manifest arriving mid-pull — the normal case at the D-28 20 s cadence with 40-75-chunk pulls — closes the active pull's write handle; the pull fills its RAM bitmap, but verifyStoredCrc32 reads a short file, total != totalSize (rx:860-865) and the image finalizes INCOMPLETE despite 100% chunk reception. (CR-02) verifyStoredCrc32 reads through a SECOND handle (serveFile FILE_READ) BEFORE the write handle is closed/flushed (finalizeImage's close at sd:228-231 runs after verification at rx:616-648); arduino-esp32 VFSFileImpl::write is stdio fwrite and seek is fseek (verified in local toolchain source, vfs_api.cpp:379-385/:404-410) — each writeChunk's seek flushes the PREVIOUS write, so the final chunk's bytes (up to 200 B) are unflushed at read-back time: even a single, uninterrupted full-image pull cannot verify and finalizes INCOMPLETE."
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "openTransfer called unconditionally at :496 before the QUEUED/active-pull decision; writeChunk return ignored at :563; verifyStoredCrc32 at :616 runs before any flush/close of the write handle"
      - path: "src/sd_storage.cpp"
        issue: "openTransfer closes the previous partial file on id change (:146-151); writeChunk refuses rather than reopens when handleId != imageId (:186-194); no flush API exists; finalizeImage's close (:228-231) is the only flush point and runs after verification"
    missing:
      - "Open the SD file lazily — only when a FULL transfer actually becomes the active pull (move openTransfer into activateNextPull / the manifest-while-idle branch), and/or make writeChunk reopen the (id, kind) file when handleId != imageId instead of silently dropping (review CR-01 fix)"
      - "Flush (or close) the kind's write handle before the read-back verification — e.g. SdStorage::flushTransfer(id, kind) called at the top of finalizeTransfer, or reorder so finalizeImage's close precedes verifyStoredCrc32 (review CR-02 fix)"
  - truth: "Images successfully saved to SD card with metadata (SC-4 / IMG-05, full-image half)"
    status: failed
    reason: "Same two root causes as SC-3 (review CR-01/CR-02, confirmed). The metadata half is now CORRECT — Gap 4 closed: thumbnail and full D-30 sidecars persist as separate files (IMG_{id}_T.JSON vs IMG_{id}.JSON) and neither truncates the other. But the image-bytes half is broken: under CR-01 a mid-pull full's file stops growing the moment a newer manifest steals the handle (partial file on disk, complete:false sidecar), and under CR-02 even an uninterrupted full is short by the final chunk's unflushed bytes. Fully-received full images are therefore not durably saved to finalization in the common case. Thumbnail bytes are unaffected at the COMPLETE-decision level (RAM CRC), though a heal window competing with a newer thumbnail push can leave that thumbnail's SD copy short (retained-RAM serving still works — warning, not gap)."
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "Persistence lifecycle (open/verify/finalize order) defeats full-image storage; see Gap 1 artifacts"
      - path: "src/sd_storage.cpp"
        issue: "Single per-kind write handle with close-on-id-change and no reopen/flush path"
    missing:
      - "Lazy open at pull activation or writeChunk reopen (CR-01 fix) and pre-verification flush/close (CR-02 fix) — one sd_storage/image_rx_manager change set closes both SC-3 and SC-4 gaps"
deferred: [] # Phase 3 (gallery/UI) consumes image storage; no later phase addresses the persistence pipeline
behavior_unverified_items:
  - truth: "Thumbnail preview displays on base station within 10 seconds of capture (SC-2 / IMG-02)"
    test: "Capture on the balloon (manual + interval) over real E32 radios; watch the Latest Capture card and /status latestThumbId"
    expected: "Verified thumbnail renders within 10 s; QQVGA/quality-20 thumbnail size lands in the assumed 2.5-4 KB band (A3); per-packet airtime matches the 9.6 kbps assumption (A1)"
    why_human: "RF delivery timing and real JPEG sizes need physical radios and camera; host harness proves the wire format only"
  - truth: "Telemetry continues to update at 5-second rate during image transfers (SC-5 / PRI-01)"
    test: "Start a full-image transfer; watch /status telemetry ageMs across the transfer"
    expected: "telemetry age stays <= ~10 s throughout the transfer (beacon outranks chunks at every transmit opportunity, including under the new 5 s window preemption)"
    why_human: "Runtime arbitration over the half-duplex link needs two radios; code-level priority (loop order + beacon early-return + preemption-inside-chunk-branch) is verified but interleaving behavior is physical"
  - truth: "Event-based auto-capture triggers on altitude/location changes (SC-6 / CTRL-05)"
    test: "Enable events via the UI toggle (eventsEnabled defaults OFF); drive altitude/distance deltas with live GPS; fire AUTO_CAPTURE_DISABLE mid-flight"
    expected: "Event-stamped captures at the deltas (defaults 150 m / 500 m / 20 s); zero automatic captures while disabled; one capture near an interval deadline (no double-capture)"
    why_human: "Needs live GPS and the flight-phase machine on real hardware; engine logic is code-verified end to end"
human_verification: # status is gaps_found; these carry forward alongside gap closure
  - test: "Prohibition review (02-05 plan, judgment-tier, flagged): MUST NOT let image chunk traffic (push work, window service, or the new preemption) delay or displace the telemetry beacon or pending command responses (PRI-01)"
    expected: "Human confirms at UAT that telemetry age stays <= ~10 s during transfers"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (process() beacon early-return at tx:101-104 precedes pushPending incl. preemption; command responses outrank both by loop order, main_balloon.cpp:735/745 — re-confirmed at this HEAD)"
  - test: "Prohibition review (02-05 plan, judgment-tier, flagged): MUST NOT report or display an image as transferred, stored, or complete when it is not"
    expected: "Human confirms every COMPLETE row/sidecar is backed by the verified CRC32 result"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS on the false-COMPLETE direction (complete set only after CRC match; the new gaps produce false INCOMPLETE, the honest-but-broken direction; no fabricated success anywhere)"
  - test: "Prohibition review (02-05 plan, judgment-tier, flagged): MUST NOT let a new capture (its push work or its queue slot) abandon an in-progress pull"
    expected: "Human confirms an active pull survives new captures"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment: queue/arbitration halves PASS (class-5 last resort, preemption bound, thumbnail never evicts), but the SD-handle theft (review CR-01, Gap 1) still lets a new capture's full manifest break the active pull's completion outcome — that defect is independently filed as a hard gap above, not absorbed here"
  - test: "Prohibition review (02-05 plan, judgment-tier, flagged): MUST NOT add a parallel best-effort thumbnail recovery path"
    expected: "Human confirms heals ride the same D-22/D-24 machinery"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (heal uses issueWindowRequest + the same 3-pass bound; passCount reset applies only to accepted chunks — verified rx:180-206, :573)"
  - test: "Prohibition review (02-05 plan, judgment-tier, flagged): MUST NOT introduce any SD file-removal or recycling call"
    expected: "Human confirms the sidecar naming change was additive only"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (grep over src/ + include/: zero SD.remove/rmdir/unlink calls; sidecarPath change is a pure naming change)"
---

# Phase 2: Image Transmission Verification Report

**Phase Goal:** Transfer images from balloon to base station over LoRa with thumbnails
**Verified:** 2026-08-19T00:06:50Z
**Status:** gaps_found
**Re-verification:** Yes — after gap closure (02-05, commits d728108..7cd2169, all four commit hashes confirmed in git log)

**Note on mode:** ROADMAP.md marks Phase 2 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as the previous verification).

## Verification Basis

Re-verification mode: the four previous gaps received full three-level verification (code read in full, wiring traced, builds/harness executed by this verifier); the previously-passed truths received regression checks scoped to the gap-closure diff (`git diff b5e41eb..7cd2169` touches exactly the 8 declared files plus STATE.md — camera_manager, auto_capture, both mains, command_protocol, and command_sender are unchanged, so prior evidence stands). This verifier independently executed: `node scripts/verify_protocol_roundtrip.mjs` (47/47 PASS, exit 0 — one clause more than the previous 46, the new img-f exact-byte assertion) and `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (2x SUCCESS, own run at this HEAD). The fresh 02-REVIEW.md (2 Critical / 10 Warning / 8 Info, commit 5aec751, post-dates the gap closure) was treated as unverified claims: both criticals were re-derived from source before being believed, including the arduino-esp32 stdio claim, which was checked against the actual framework source in the local toolchain (`~/.platformio/packages/framework-arduinoespressif32/libraries/FS/src/vfs_api.cpp`).

## Goal Achievement

### Observable Truths

Deduplicated as before: roadmap SCs keep roadmap wording; plan truths fold into their SC row. Same 27-truth basis as the previous verification (plans 02-01..02-05 union; nothing missing from any plan's must_haves).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | SC-1: Captured images are chunked and transmitted over LoRa with sequence tracking | ✓ VERIFIED | Regression: framing/serializers unchanged by gap closure; verifier-run harness 47/47 exit 0 |
| 2 | SC-2: Thumbnail preview displays on base station within 10 seconds of capture | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Path wired end-to-end and now includes a working hole-recovery half (truth 19); timing needs radios (A1/A3) |
| 3 | SC-3: Full image transfers complete in background without blocking telemetry | ✗ FAILED | "Without blocking telemetry" verified (truth 12). "Complete" re-defeated by NEW SD-pipeline defects — review CR-01 (unconditional openTransfer rx:496 steals the active pull's handle when a newer full manifest arrives mid-pull — the documented D-28 cadence makes this the normal case) + CR-02 (verifyStoredCrc32 rx:616 reads through a second handle before the write handle is flushed; VFS write=fwrite/seek=fseek in local toolchain source, final chunk unflushed). All five links of both chains independently confirmed — see Gaps Summary |
| 4 | SC-4: Images successfully saved to SD card with metadata | ✗ FAILED (was ⚠️) | Metadata half NOW CORRECT (Gap 4 closed: kind-suffixed sidecars verified). Image-bytes half broken by the same CR-01/CR-02 chain: fully-received fulls finalize INCOMPLETE with partial files; not "successfully saved" even with working SD hardware. Reclassified from behavior-unverified because the defeat is code-level, not hardware uncertainty |
| 5 | SC-5: Telemetry continues to update at 5-second rate during image transfers | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Beacon + arbitration re-confirmed incl. the new preemption living inside the chunk branch (tx:101-104 → pushPending tx:406+); runtime interleaving needs two radios |
| 6 | SC-6: Event-based auto-capture triggers on altitude/location changes | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | auto_capture.cpp untouched by gap closure; engine code-verified previously; needs live GPS |
| 7 | 02-01: createThumbnail leaves no dangling buffer on ANY failure path (CR-04) | ✓ VERIFIED | Regression: camera_manager.cpp outside diff scope |
| 8 | 02-01: Both receivers dispatch on type byte before body arithmetic (WR-12) | ✓ VERIFIED | Regression: command_handler.cpp changes confined to the 6-byte window-request guard (:685) and ACK echo (:705-706); dispatch switch intact (read) |
| 9 | 02-01: Both targets build green with correct module exclusion | ✓ VERIFIED | Verifier-run pio build at this HEAD: esp32-s3-balloon SUCCESS, esp32-s3-basestation SUCCESS |
| 10 | 02-02: Full announced once after thumbnail; chunks served ONLY via window requests (D-17) | ✓ VERIFIED | Re-read: ANNOUNCE_FULL only from completedThumbState; announce-once → ANNOUNCED (tx:578); chunks only via armed window; oversize parks at THUMB_PUSHED |
| 11 | 02-02: FIFO; a new capture never abandons an in-progress pull (D-19, queue level) | ✓ VERIFIED | Gap-1 queue fixes confirmed: class-ranked eviction tx:304-338 (active-pull class 5 = last resort, logged), evictionClassOf tx:180-194, THUMBNAIL requests never evict (evictEntriesOlderThan single call site inside if (!thumbWindow), tx:670), preemption bounds push starvation (tx:417-422). NOTE: the SD-handle theft (Gap CR-01) still breaks the active pull's completion OUTCOME — filed under truths 3/4, root-caused to the persistence layer, not the queue |
| 12 | 02-02: Priority: command response, then beacon when due, then at most one chunk (PRI-01) | ✓ VERIFIED | Re-confirmed: process() beacon early-return tx:101-104 returns before pushPending; main_balloon.cpp:735 CmdHandler before :745 ImageTx; the new preemption is inside the chunk branch only |
| 13 | 02-02: Oversize fulls (>50000) skip transfer with logged warning; thumbnail still pushes (PRI-03/Q4) | ✓ VERIFIED | Regression re-read: enqueue gate tx:224-230; THUMB_PUSHED park now also heal-servable |
| 14 | 02-02: Window requests idempotent (Pitfall 10) | ✓ VERIFIED | Re-read: arming re-points cursor, no ID allocation, no queue mutation (tx:687-702); SERVED→ANNOUNCED re-arm on FULL tail re-requests |
| 15 | 02-03: 3-pass bound finalizes incomplete: kept on SD, flagged in sidecar, slot freed (D-24) | ✓ VERIFIED | finalizeIncomplete rx:669-691; bound now correct by construction — passes charge only on unhealed stalls (truth 19/Gap 1) |
| 16 | 02-03: Completed image verifies end-to-end CRC32 over stored bytes before complete (D-23) | ✓ VERIFIED | Guarantee direction holds: complete is set ONLY on CRC match; SD-degraded fulls report INCOMPLETE/notStored, never fabricated (rx:604-632, :835-868). The new gaps violate the OPPOSITE direction (false INCOMPLETE) — impact filed under truths 3/4 |
| 17 | 02-03: SD naming IMG_{id}.JPG / IMG_{id}_T.JPG / IMG_{id}.JSON, %05u, flat /images (D-29/31/32) | ✓ VERIFIED | Regression: imagePath sd:81-91 unchanged; sidecar naming now kind-split (sd:93-105) |
| 18 | 02-03: Sidecar records the D-30 field set | ✓ VERIFIED | Gap 4 CLOSED: sidecarPath(out, cap, id, kind) — IMG_%05u_T.JSON for thumbnails (sd:93-105), sd_storage.h:128 declaration, writeSidecar passes meta.kind (sd:263); both records persist independently; full field set re-confirmed in writeSidecar body |
| 19 | 02-03: Thumbnail with holes falls back to the same window-pull path (D-22) | ✓ VERIFIED | Gap 2 CLOSED end-to-end: 6-byte kind-carrying request (rx:768-772 encodes kind @2; tx:595-598 decodes; handler guard+echo 6 bytes; harness img-f pins literal bytes 00 2A 01 00 07 10); balloon serves THUMBNAIL windows from thumbBuffer incl. THUMB_PUSHED parks (tx:626-631, :726-728, findWindowServiceEntry :390-391); kind byte validated against the enum before any buffer access (tx:603-610); base gates — no active pull + same-id FULL slot or 24 s oversize fallback (rx:180-186); heal-window chunk routing before FULL precedence (rx:245-246). RETRYING display rides passCount>0 in the locked five-state vocabulary (rx:917). Residual: thumbnail SD copy can go short when a heal races a newer push (RAM CRC still completes the row) — warning, not gap |
| 20 | 02-03: Transfer panel derives percent/chunks from real accounting only (D-20) | ✓ VERIFIED | Gap 3 CLOSED: allocateSlot step 3 resets the evicted slot with *oldest = ImageRxTransfer{} after slot-pressure finalizeIncomplete (rx:369-373) — no zombie accounting; snapshot derivation unchanged and bitmap-derived |
| 21 | 02-03: SD-full/init failure stops storing, warns in /status+UI, never deletes (Q5) | ✓ VERIFIED | Regression: degrade() sd:347-363 unchanged; zero removal APIs repo-wide (grep re-run at this HEAD) |
| 22 | 02-03: FIFO pull order, one active full pull at a time (D-19 base) | ✓ VERIFIED | Regression: activateNextPull rx:730-757 unchanged |
| 23 | 02-04: Captures fire on altitude/distance deltas and first-entry flight phases (D-25) | ✓ VERIFIED | Regression: auto_capture.cpp outside diff scope |
| 24 | 02-04: Thresholds configurable via SET_EVENT_THRESHOLDS with ACK, reported via GET_STATUS (D-26) | ✓ VERIFIED | Regression: handler/UI untouched by gap closure |
| 25 | 02-04: Event resets interval baseline; no double-capture; single image-ID sequence (D-27/CR-05) | ✓ VERIFIED | Regression: unchanged |
| 26 | 02-04: Global min spacing gates ALL automatic captures (D-28) | ✓ VERIFIED | Regression: unchanged |
| 27 | 02-04: AUTO_CAPTURE_DISABLE master off-switch; GPS-valid required for deltas | ✓ VERIFIED | Regression: unchanged |

**Score:** 22/27 truths verified (3 present-but-behavior-unverified; 2 failed — both from the same NEW root cause)

### Required Artifacts (02-05 gap-closure set)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| include/image_protocol.h | imageKind in PayloadImageWindowRequest (6-byte), IMG_WINDOW_SERVICE_PREEMPT_MS, IMG_THUMB_HEAL_IDLE_MS | ✓ VERIFIED | Struct :164-172 with layout comment; constants :84, :95 with load-bearing comments |
| src/image_tx_manager.cpp | Kind-aware handleWindowRequest, windowKind-aware serviceWindowChunk, push preemption, SERVED, windowEverArmed, class-ranked eviction | ✓ VERIFIED | All present and wired; read in full this pass (587-710, 712-769, 406-422, 173-194, 304-338) |
| src/image_rx_manager.cpp | passCount reset, 6-byte request, gated heal, heal routing, step-3 slot reset | ✓ VERIFIED | :573 (sole zero-writer, grep), :768-774, :180-206, :245-256, :369-373 |
| src/sd_storage.cpp | kind-suffixed sidecarPath | ✓ VERIFIED | :93-105 + h:128 + writeSidecar :263 |
| scripts/verify_protocol_roundtrip.mjs | 6-byte codecs + img-f clause | ✓ VERIFIED | :666-676 codecs; :933-956 exact-byte + boundary round-trips; verifier-run 47/47 exit 0 |
| include/image_tx_manager.h / include/sd_storage.h / src/command_handler.cpp | SERVED/windowKind/windowEverArmed; sidecarPath decl; 6-byte guard+echo | ✓ VERIFIED | h:48-53,105-114; sd_storage.h:128; handler :685, :705-706 |

### Key Link Verification (02-05 set)

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| rx issueWindowRequest() | tx handleWindowRequest() | 6-byte PayloadImageWindowRequest (id BE16 @0, kind @2, start BE16 @3, count @5) | ✓ WIRED | Encoder rx:768-772 matches decoder tx:595-598; handler guard+ACK echo 6 bytes (ch:685,:705-706); harness img-f pins the literal bytes |
| tx pushPending() preemption | findWindowServiceEntry() | IMG_WINDOW_SERVICE_PREEMPT_MS age check before findActiveEntry | ✓ WIRED | tx:417-422 precedes the push-work block at :430; 5000 < 8000 stall bound |
| sd writeSidecar() | sidecarPath(out, cap, id, kind) | meta.kind derives the name | ✓ WIRED | sd:263 → :93-105; thumbnail and full records no longer collide |
| beacon early-return | all chunk work incl. preemption | process() branch order | ✓ WIRED | tx:101-104 return before :108 pushPending — PRI-01 non-regression confirmed |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| Transfers panel | transfers[] | getTransferSnapshot from bitmap/pass/terminal | Yes — zombie path closed; RETRYING derived from real passCount | ✓ FLOWING |
| Full-image SD file | writeChunk stream | acceptChunk commits | RAM bitmap fills, but SD write silently stops on handle theft (CR-01) and tail never flushed (CR-02) | ✗ DISCONNECTED (edge → common case) |
| Sidecar JSON (both kinds) | meta fields | manifest + latest beacon + counters | Yes; both files now persist independently | ✓ FLOWING |
| Thumbnail heal rows | windowActive/passCount | gated stall loop | Yes; real ARQ machinery, not a parallel path | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite | node scripts/verify_protocol_roundtrip.mjs | 47 PASS / 0 FAIL, exit 0 (verifier-run) | ✓ PASS |
| Both firmware targets compile+link | pio run -e esp32-s3-balloon -e esp32-s3-basestation | 2x SUCCESS (verifier-run at this HEAD) | ✓ PASS |
| Full-image finalization COMPLETE with SD present | static chain analysis (CR-01 five links + CR-02 ordering + framework fwrite/fseek) | Newer manifest mid-pull closes active handle → short file → INCOMPLETE; even single pulls miss the unflushed final chunk | ✗ FAIL |
| Thumbnail heal end-to-end | static wiring (kind byte → matcher → thumbBuffer serve → routing → gates) | All links present and wired; runtime heal timing needs real chunk loss | ✓ PASS (code) / UAT |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no scripts/*/tests/probe-*.sh exist. SKIPPED.

### Requirements Coverage

Plan frontmatter union: 02-01 [IMG-01, IMG-02, IMG-04], 02-02 [IMG-01, IMG-03, PRI-01, PRI-03], 02-03 [IMG-03, IMG-04, IMG-05, PRI-03], 02-04 [CTRL-05], 02-05 [IMG-02, IMG-03, IMG-04, IMG-05, PRI-01, PRI-03] — exactly the roadmap's Phase 2 set. No orphans.

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|---------------------|----------|----------|
| IMG-01 | 02-01, 02-02 | Captured images transmitted over LoRa | ✓ SATISFIED (code) | Push + pull machinery wired; RF runtime UAT |
| IMG-02 | 02-01, 02-05 | Thumbnail preview displays immediately | ✓ SATISFIED (code) | Path complete AND loss-recovery now real (Gap 2 closed); 10 s window is UAT |
| IMG-03 | 02-02, 02-03, 02-05 | Full resolution images transfer in background after thumbnail | ✗ BLOCKED (partial) | Background transfer + queue survival restored (Gap 1 closed), but completion re-defeated by CR-01/CR-02 — Gap 1 of this verification |
| IMG-04 | 02-01, 02-02, 02-03, 02-05 | Images chunked for reliable LoRa transmission | ✓ SATISFIED (code) | Chunking + window ARQ + kind-addressable heal all sound at the transfer layer now; persistence failure is filed under IMG-03/IMG-05 |
| IMG-05 | 02-03, 02-05 | Base stores received images on SD card | ✗ BLOCKED (partial) | Module complete, sidecars now correct (Gap 4 closed), but fulls' bytes don't survive to finalization (CR-01/CR-02) — Gap 2; hardware wiring also UAT |
| PRI-01 | 02-02, 02-05 | Telemetry priority over image data | ✓ SATISFIED (code) | Beacon early-return precedes all chunk work incl. the new preemption; runtime UAT |
| PRI-03 | 02-01..03, 02-05 | Gracefully handles LoRa bandwidth limitations | ✓ SATISFIED (code) | Bounded passes now bound only consecutive unhealed stalls; honest INCOMPLETE everywhere; oversize skip |
| CTRL-05 | 02-04 | Event-based auto-capture triggers | ✓ SATISFIED (code) | Engine code-verified; live-GPS UAT |

Note: REQUIREMENTS.md marks all eight "Complete" — still ahead of reality for IMG-03 and IMG-05 until the SD-pipeline gaps close.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/image_rx_manager.cpp / src/sd_storage.cpp | 496 / 146-151, 186-194 | Unconditional openTransfer steals the active pull's write handle; writeChunk drops silently (review CR-01 — confirmed) | 🛑 Blocker (Gap 1/2) | Fully-received fulls finalize INCOMPLETE in the documented interleaved capture mode |
| src/image_rx_manager.cpp / src/sd_storage.cpp | 616-648 / 228-231 | Stored-CRC verification reads through a second handle before flush/close of the writer (review CR-02 — confirmed incl. framework fwrite/fseek) | 🛑 Blocker (Gap 1/2) | Final chunk unflushed → single-pull fulls also cannot verify |
| src/image_tx_manager.cpp | 490, 578 | Manifests still one-shot: state advances on transmit failure (review WR-01 — confirmed by read) | ⚠️ Warning | A lost manifest still strands that image; interacts with real-link loss |
| src/image_rx_manager.cpp | 788 | windowActive set on queue, ACK/NACK outcome never observed (review WR-02 — confirmed by read) | ⚠️ Warning | Phantom windows burn passes (recoverable via stall re-request); BUSY-heavy interleaving can false-finalize |
| src/sd_storage.cpp | 186-194 | Thumbnail heal racing a newer push leaves the healed thumbnail's SD copy short (CR-01 thumbnail variant — derived by this verifier) | ⚠️ Warning | Row still COMPLETEs via RAM CRC; SD fallback serving degraded only |
| src/command_protocol.cpp | 53-77 | serializeCommand accepts payloadLength 201-224, emits malformed packet (review WR-04 — advisory, not re-verified) | ⚠️ Warning | Currently unreachable via factories; defense-in-depth gap; harness mirrors the boundary |
| src/sd_storage.cpp | 331-341 | serveFile refuses reads after degrade — stored files unservable until reboot (review WR-05 — advisory) | ⚠️ Warning | Contradicts "existing files kept" in spirit; UI links 404 |
| src/camera_manager.cpp | 262 | Full-JPEG copy from internal DRAM, not PSRAM (review WR-07 — advisory) | ⚠️ Warning | SXGA/UXGA captures can fail on heap pressure |
| remaining review warnings/infos | — | WR-03/06/08/09/10, IN-01..08 (incl. IN-02 allocateSlot kind unused — confirmed by read; IN-06 id-wrap to reserved 0) | ℹ️ Info / ⚠️ advisory | None block the phase goal; classify as follow-up hardening |

No TBD/FIXME/XXX markers in any phase-modified file (grep re-run); no placeholder/stub data paths; all new code paths live.

### Review Cross-Check (fresh 02-REVIEW.md, commit 5aec751)

The review post-dates the gap closure and reviewed the current code. Its 2 criticals are both INDEPENDENTLY CONFIRMED by this verifier as code facts and filed as the two gaps above — they are code gaps, not advisories: CR-01's five-link chain (unconditional openTransfer rx:496 → close-on-id-change sd:146-151 → writeChunk refuse sd:186-194 → ignored return rx:563 → short-file verify rx:860-865) is statically complete with no runtime assumptions, and CR-02's mechanism was verified against the actual arduino-esp32 framework source in the local toolchain (VFSFileImpl::write → fwrite; seek → fseek; C-standard fseek flushes prior buffered writes, so the final chunk's fwrite is the unflushed one). The 10 warnings are classified advisory (three confirmed by direct read: WR-01 one-shot manifests, WR-02 phantom windows, WR-04 noted; the rest recorded as review findings — real-looking but not independently re-derived); the 8 infos are follow-up hardening. Notably, the gap-closure work itself withstood re-review: none of the review's findings dispute the four closures.

### Human Verification Required

Status is gaps_found, so these carry forward alongside gap closure (all hardware-UAT class, plus the five flagged judgment-tier prohibitions whose non-authoritative code-level judgments are recorded in frontmatter — 4 PASS, 1 split):

1. **Thumbnail within 10 s over real radios (SC-2)** — capture, watch Latest Capture card; validates A1/A3 assumptions; now also exercise a link-loss heal (RETRYING → COMPLETE).
2. **SD wiring + card contents (SC-4, after the CR-01/CR-02 fix)** — microSD on GPIO 12/13/11/10, FAT32; pull card and inspect IMG_{id}.JPG, IMG_{id}_T.JPG, IMG_{id}.JSON, IMG_{id}_T.JSON after a capture whose both kinds finalized; SD-absent run stays honest-degraded.
3. **Telemetry age during a transfer (SC-5 / PRI-01 prohibition)** — /status telemetry.ageMs <= ~10 s throughout a full-image pull, including under push pressure.
4. **Event triggers with live GPS (SC-6)** — enable events (default OFF), drive deltas, single captures, master-disable stops everything.
5. **UXGA oversize cap (PRI-03/Q4)** — thumbnail arrives, full skipped with the logged warning naming id+size; oversize thumbnail heals after the 24 s idle fallback.
6. **Five flagged prohibitions (02-05 frontmatter)** — see frontmatter human_verification entries for the per-item expectations.

### Gaps Summary

The re-verification confirms ALL FOUR previous gaps are closed at code level and without regression: the transfer state machines now survive their documented operating point at the policy layer (progress retires passes; push work preempts for starved windows at 5 s; overflow eviction ranks the active pull last; window requests are kind-addressable and harness-locked; slot pressure yields clean accounting; both D-30 sidecars persist). Both targets build green and the harness is 47/47 — verified by this verifier's own runs, not taken from the SUMMARY.

What still fails the goal is a DIFFERENT layer that the previous verification's structural checks and the wire harness cannot see, surfaced by the fresh code review and confirmed here link-by-link: the SD persistence pipeline. (1) Every manifest — including a QUEUED full's — opens the SD transfer file immediately, and opening a different id closes the previous partial file of that kind; the base's single write handle is therefore stolen by each newer capture's full manifest while a pull is mid-flight, and the silently-dropped writes mean a pull that receives 100% of its chunks verifies against a short file and finalizes INCOMPLETE. Under the phase's own 20 s capture cadence and 40-75-chunk pulls, this is the normal mode, not an edge. (2) Even a single uninterrupted pull finalizes INCOMPLETE because the stored-CRC read-back runs through a second file handle before the write handle is ever flushed — the final chunk's fwrite sits in the stdio buffer (framework-verified: write is fwrite, per-chunk fseek flushes only the PREVIOUS write). The honest-failure direction is preserved throughout (no fabricated COMPLETE — D-23 and the 02-03 prohibition hold), but "full image transfers complete" (SC-3) and "images successfully saved" (SC-4) are defeated.

Both gaps share one fix surface (sd_storage.cpp + the openTransfer call site in image_rx_manager.cpp): open lazily at pull activation and/or reopen on writeChunk id mismatch, and flush/close the write handle before the read-back verification. Nothing else in the phase needs rework — the gap-closure plan's transfer-layer fixes withstood adversarial re-review, and the remaining review warnings are advisory hardening for Phase 3 or a follow-up.

---

_Verified: 2026-08-19T00:06:50Z_
_Verifier: Claude (gsd-verifier)_

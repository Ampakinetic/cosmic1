---
phase: 01-command-protocol-control
verified: 2026-08-24T02:58:12Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3, NARROWED: the resolution clause is now hardware-proven (CIF/VGA/SVGA honest re-init with proportionate sizes, zero FB-OVF); the pairwise visible-effect judgments + QVGA restore + reboot boot-resolution clauses remain operator-unjudged (01-12 series-B steps d/e/f unexecuted, recorded in G-01-8's missing list)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "G-01-8 (round #5's structured gap 2): RESOLVED — f51bad6 (allocatedFrameSize bound + two-path setFrameSize + mandatory recovery + cached-settings re-init; verifier-read at src/camera_manager.cpp :20/:117/:129-132/:410-478, src/camera_manager.h). Operator bench session 4 (2026-08-24, balloon4.log/base4.log): CIF/VGA/SVGA each executed via 'Camera: framesize growth requires re-init' at balloon4.log:1791/:2784/:3588 (verifier-confirmed at exactly those line numbers) with session-total FB-OVF 4 — all four at the series-A thumbnail path :353-354/:396-397, ZERO at any settings step (baseline: 4,379); CAPTURE_NOW 6 SUCCESS / 0 FAILED (verifier-counted); camera never died. Unexercised clauses (QVGA restore d, reboot boot-resolution e, SC-3 pairs f, values 11-13) honestly recorded in G-01-8's missing list, riding Test 3"
    - "Round #5's gap 1 (G-01-7) levers landed exactly as planned (1064480, verifier-read: pendingHealThumbnail hold at image_rx_manager.cpp:826/:584, evictEntriesOlderThan mid-service guard at image_tx_manager.cpp:829-835, IMG_WINDOW_RX_SETTLE_MS 500 both sides at image_rx_manager.cpp:121 / image_tx_manager.cpp:756-758) and provably engaged at bench (hold 3x base4.log:521/:1127/:2336; zero mid-service evictions — 5 supersede-evictions all post-terminal, defer form never needed; verifier-confirmed in raw logs) — but the acceptance truth FAILED, so the gap stays OPEN rescoped (see gaps)"
  gaps_remaining:
    - "G-01-7 rescoped open (series-A acceptance failed on three distinct residual mechanisms)"
    - "G-01-9 NEW open (thumb/corruption complex — opened this round)"
  regressions: [] # harness exit 0 (verifier-run this round); ZERO source drift since f51bad6 (git diff f51bad6..HEAD touches .planning docs only — the commit-time 2/2 builds and 49/49 harness apply to HEAD's source exactly); CR-03 terminal-state guard intact (command_sender.cpp:410/:492); delegated section#capture submit listener intact (:1529); resolution select intact with all 9 advertised values 5-13 (:2324-2335); galleryCountSeen latch intact (D-36); pacing invariants hold (PASSES=3, STALL=8000, PREEMPT=5000, SETTLE=500 new, HEAL_IDLE=24000=3x stall, TTL=900000 — verifier-grepped at HEAD); prior closures G-01-1/3/4/5/6 all stand (ledger-consistent); WR-01 safety watch green session 4 (no 'Critical battery'; lone 'Emergency Shutdown: Enabled' at balloon4.log:56 is the documented boot config echo)
gaps:
  - truth: "A capture triggered while the previous image's transfer/heal is still pending does not prevent that pending transfer's completion — concurrent captures cannot starve or evict an in-flight thumbnail heal or full pull (G-01-7 in 01-UAT.md, WINDOWS entry 3)"
    status: failed
    reason: "01-12 levers (1064480) are in, substantive, and bench-proven engaged (hold fired 3x base4.log:521/:1127/:2336; zero mid-service evictions — all 5 supersede-evictions targeted post-terminal entries, verifier-confirmed) — but the series-A acceptance truth ('3 unspaced captures finalize every kind COMPLETE') FAILED with 3 INCOMPLETE verdicts, each a DISTINCT mechanism: (a) image 7 thumb INCOMPLETE 35/36 (base4.log:634) — its heal window requests were NACK-deferred by the PRE-EXISTING one-at-a-time BUSY guard (balloon4.log:1154/:1161, verifier-read; the code is pre-1064480 at image_tx_manager.cpp:700-712) while image 8's window was mid-service, and the D-24 pass budget consumed the deferred+retried requests; (b) image 7 full received all 36/36 chunks yet failed stored-bytes CRC (base4.log:854-855) — G-01-9 defect B, balloon-source-side; (c) image 8 full silently never transferred — manifest sent (balloon4.log:722, verifier-read) but never received (0 'manifest image 8 kind 1' in base4.log, verifier-counted), no re-announce path exists (G-01-9 defect C). Chunk-level outcomes improved over the session-2 baseline (thumb 35/36 vs 6/8; full 36/36-received vs 15/52) but the verdict-level truth still fails. Interim mitigation unchanged: space captures"
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "D-24 pass accounting charges passes for BUSY-deferred window requests — defer-aware pass charging is the rescoped primary lever (a NACK-deferred request had no transfer opportunity)"
      - path: "src/image_tx_manager.cpp"
        issue: "announceFullManifest (:585-603) sets state ANNOUNCED unconditionally, including on transmit failure — a single lost FULL manifest strands the full forever (shared with G-01-9 defect C; review CR-02)"
    missing:
      - "Defer-aware D-24 pass accounting: a window request NACK'd by the one-at-a-time BUSY deferral must not consume a heal pass"
      - "Bounded FULL-manifest re-announce so a single lost manifest cannot silently drop a full (shared with G-01-9 defect C)"
      - "The G-01-9 thumb-sizing fix — full-sized thumbs inflate push/heal exposure 4-18x and compounded the series-A INCOMPLETEs"
      - "Bench re-verification: series-A re-run (3 unspaced captures) completes without INCOMPLETE verdicts"
  - truth: "Every capture's THUMBNAIL is a genuine QQVGA-sized image, every finalized-COMPLETE full's stored bytes match its manifest CRC by construction (no balloon-source-side corruption), and no full is silently dropped for a lost FULL manifest (G-01-9 in 01-UAT.md, WINDOWS entry 5 — NEW this round)"
    status: failed
    reason: "Bench session 4 evidence, verifier-re-confirmed in the raw logs: (A) 4 of 6 captures produced FULL-SIZED thumbnails — images 7/8/9/11 enqueue thumb bytes == full bytes ('enqueued image 7 (full 7157 B, thumb 7157 B / 36 chunks)' balloon4.log:356, :399, :1820, :3620; image 11's thumb 28808 B / 145 chunks is LARGER than its 28771 B / 144-chunk full) passing the c67e1a5 dimension-only guard; correct thumbs only for images 6 (1411 B) and 10 (1343 B). (B) 2 fulls received 100% of chunks yet failed stored-bytes CRC (base4.log:854 FCAFC250 vs 9DE9EFA0 at 36/36; :3267 CC9DADC2 vs D043406D at 144/144) — with 0 frame-level CRC FAIL all session, the corruption is upstream of the framer. (C) image 8's full silently lost. The fresh review (01-REVIEW.md, 23726b1) names root causes this verifier independently confirmed in source: CR-01 — ImageChunkBody carries NO imageKind byte (include/image_protocol.h:154-159) and onChunkFrame's routing heuristic (image_rx_manager.cpp:253-284) routes late thumbnail-heal stragglers into the active FULL slot once the thumb slot finalizes (thumbnail bytes occupy full indices; the real full chunks are then discarded as bitmap duplicates — right-length-wrong-content, exactly the 36/36-received CRC-mismatch signature); CR-02 — the one-shot manifest is consumed even on transmit failure; CR-03 — the thumbnail guard checks fb->width/height but never fb->len (malloc(fb->len) happily takes a ~28 KB payload). RIDE-ALONG: CommandSender retry-bound overrun (WINDOWS entry 6) — 13x 'attempt 4/3' (verifier-counted in base4.log), whose late retries re-arm finalized kind-0 windows post-terminal (14 exact-string 'chunk for finalized image 11 kind 0 ignored' lines at :3286-3310, 16 broad 'ignored' — the traffic is correctly ignored, but it should not exist)"
    artifacts:
      - path: "include/image_protocol.h"
        issue: "ImageChunkBody (:154-159) lacks an imageKind byte — chunk routing is an imageId-only heuristic (CR-01; the fix is a wire change: serializer + deserializer + onChunkFrame kind validation + harness together)"
      - path: "src/image_rx_manager.cpp"
        issue: "onChunkFrame (:253-284) fallback-to-FULL routing after thumbnail terminal — the misroute that corrupts fully-received fulls; interim hardening possible without a wire change (route to FULL only when pullActive && windowActive)"
      - path: "src/image_tx_manager.cpp"
        issue: "announceFullManifest (:585-603) — consume ANNOUNCED only on transmit success, bound the retries, free fullBuffer at the bound (CR-02 / defect C)"
      - path: "src/camera_manager.cpp"
        issue: "thumbnail guard (:361-376) — bound fb->len (e.g. THUMB_MAX_BYTES 8192) and/or JPEG-SOF parse, plus drain-and-discard one frame after the QQVGA downshift (fb_count 2 / GRAB_LATEST stale-frame race) (CR-03 / defect A)"
      - path: "src/command_sender.cpp"
        issue: "retry counter off-by-one — one retry fires beyond the D-05/D-07 3-attempt bound (WINDOWS entry 6, deferred-items.md; pre-existing, no 01-12 diff touches this file)"
    missing:
      - "Defect A fix: payload-size/JPEG-dimension verification in the thumbnail guard, or drain-and-recapture after the downshift (log fb->len at bench first as the discriminator)"
      - "Defect B fix: kind-tag the chunk frame (CR-01) with onChunkFrame kind validation — or the interim pullActive&&windowActive hardening — plus kind-tagged balloon window-chunk logging to discriminate remaining source-side candidates"
      - "Defect C fix: bounded FULL-manifest re-announce (CR-02)"
      - "Fix the CommandSender retry off-by-one and re-verify the D-05 3-retry bound"
      - "Bench re-verification: series A + CIF/SVGA-class captures with correctly-sized thumbs completing both kinds COMPLETE"
deferred:
  - truth: "Full-resolution gallery image viewer (operator bench wish; presentation-only)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md (verifier-verified on disk)"
  - truth: "Gallery thumb number overlay + Incomplete-badge explanatory copy (operator bench friction, session 4)"
    addressed_in: "Phase 3 backlog (gallery UX)"
    evidence: ".planning/todos/pending/2026-08-24-gallery-thumb-number-overlay.md and 2026-08-24-incomplete-badge-on-renderable-images.md (verifier-verified on disk; the badge itself verified CORRECT — it honestly flags byte-corrupt-but-renderable JPEGs, root cause is G-01-9)"
  - truth: "D-13 separate camera-controls page / D-15 accordion settings groups"
    addressed_in: "Phase 3"
    evidence: "Carried from prior verifications; superseded by the delivered single-page dashboard (03-01, WEB-04)"
behavior_unverified_items:
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "At a fixed resolution, adjust ONE setting between two captures and compare the pair (brightness -2 vs +2, plus at least one other class); then SET_RESOLUTION QVGA restore and a reboot boot-resolution check"
    expected: "Visible pairwise differences per exercised setting; captures succeed at boot-equivalent sizing after restore, and the first capture after reboot is QVGA-sized"
    why_human: "The resolution clause is NOW hardware-proven (CIF executed at 400x296 class — image 9 full 11223 B / 57 chunks = 1.57x the QVGA 7157 B / 36-chunk baseline, COMPLETE 57/57 base4.log:1464; VGA 97/97 :1938 with a correctly-sized 1343 B thumb :1581; SVGA executed) — but the pairwise visible-effect judgments were never operator-judged (01-12 series-B steps d/e/f unexecuted: no QVGA restore, no reboot check, no SC-3 pairs), and sensor acceptance/visual assessment need the physical camera. Rides the next bench round under Test 3"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #6, after gap closure 01-12)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-24T02:58:12Z
**Status:** gaps_found
**Re-verification:** Yes — #6, after gap closure (plan 01-12: code 1064480/f51bad6, tracking 6d749c8, close-out 4692bc1, fresh review 23726b1 — all verifier-confirmed in git log with exactly the claimed file sets; zero source drift since f51bad6)

## Goal Achievement

Round #5's gap G-01-8 is RESOLVED on operator bench evidence this verifier re-checked line-by-line against the raw logs: CIF/VGA/SVGA each executed via the new honest re-init path, session-total FB-OVF fell from 4,379 to 4 (all four on the series-A thumbnail path, none at any settings step), all 6 captures succeeded, and the camera never died. The G-01-7 levers landed exactly at their trace-named sites and engaged exactly as designed (hold 3x, zero mid-service evictions) — but the series-A acceptance truth honestly FAILED, and the project correctly refused to force a close: the gap is rescoped open with three named next levers, and a NEW gap G-01-9 (full-sized thumbnails / source-side CRC corruption / lost FULL manifest) is opened with a fresh root-cause code review whose three criticals this verifier independently confirmed in source.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — remains achieved and hardware-proven, now strengthened: settings-path resolution changes are truthful (the SC-3 resolution clause is newly hardware-proven), and every session-4 capture executed. What keeps the phase open is the same class as rounds #4/#5: image-reliability residuals in the Phase-2 requirement domain (IMG-02/IMG-03) that this phase's UAT surfaced — now with a sharper diagnosis: the dominant corruption mechanism is a provable wire-protocol routing defect (chunk frames carry no imageKind; the base's routing heuristic misroutes late thumbnail-heal stragglers into the active FULL transfer — CR-01), not an SD or air-loss flake.

This verifier independently: read both code fixes in source (each substantive, at its plan-named site, with the prohibitions held — the eviction-guard scope boundary verified via diff-hunk analysis: evictionClassOf and sweepExpiredEntries bodies untouched); ran the wire harness (exit 0); confirmed zero source drift since f51bad6 (the commit-time 2/2 builds therefore apply to HEAD's source exactly); re-verified every load-bearing bench quote against balloon4.log/base4.log (line numbers and counts match: growth re-inits :1791/:2784/:3588; FB-OVF total 4 at :353-354/:396-397; CAPTURE_NOW 6/0; hold 3x at :521/:1127/:2336; INCOMPLETE verdicts :634/:855/:1297/:3268; CRC mismatches :854/:3267; manifest-lost image 8 at balloon4.log:722 with 0 base receipts; BUSY deferrals :1154/:1161; retry overrun 13x; thumb sizes 4-of-6 full-sized). One minor tally note: the exact string 'chunk for finalized image 11 kind 0 ignored' counts 14 (SUMMARY said 16; the broad 'ignored' count is 16 — the two extra lines are the same traffic class). Not load-bearing.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression: 01-12 touched no UI file (diff-confirmed); resolution select intact with all 9 advertised values 5-13 (:2324-2335, verifier-read); delegated section#capture submit listener intact (:1529); js-capture-msg message path intact; 480px media query + 'No commands yet' copy present; no debt markers |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED | Session 4: commands flowed both directions — 6/6 CAPTURE_NOW SUCCESS (verifier-counted), settings commands ACKed at every step (CIF/VGA/SVGA SUCCESS lines), 1007 balloon chunk sends / 0 FAILED, beacons TX 253 / RX 238 with cadence held; harness green (verifier-run, exit 0) |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | NARROWED: the RESOLUTION clause is now hardware-proven — SET_RESOLUTION growth executed via honest re-init at three sizes with proportionate outputs (CIF 11223 B/57 chunks = 1.57x QVGA baseline COMPLETE 57/57; VGA 97/97 with correctly-sized thumb; SVGA executed), zero FB-OVF at settings steps, G-01-8 resolved. The pairwise visible-effect judgments (brightness/saturation/quality), QVGA restore, and reboot boot-resolution clauses remain operator-unjudged (series-B steps d/e/f unexecuted). See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Hardware UAT test 2 PASS stands; CR-03(01-06) terminal-state guard re-confirmed intact at HEAD (command_sender.cpp:410/:492). Note: WINDOWS entry 6 (retry-bound off-by-one, 13x 'attempt 4/3') is an honestly-routed open deviation from the D-05/D-07 3-attempt bound — termination still occurs, the user-facing retry+notify truth holds; rides the G-01-9 round |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED | Hardware UAT test 4 PASS stands; NVS-backed single image-ID sequence intact (IDs 6-11 sequential in session 4); all captures executed end-to-end |

**Score:** 4/5 truths verified (1 present, behavior-unverified — narrowed this round; the resolution clause of SC-3 upgraded from blocked to hardware-proven)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer (bench wish; presentation-only) | Phase 3 backlog | Todo on disk: .planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md |
| 2 | Gallery thumb number overlay + Incomplete-badge explanatory copy (session-4 operator friction) | Phase 3 backlog (gallery UX) | Todos on disk (verifier-verified); the badge itself verified CORRECT — honest flagging of byte-corrupt-but-renderable JPEGs, root cause G-01-9 |
| 3 | D-13/D-15 page-layout preferences | Phase 3 | Carried; superseded by the delivered single-page dashboard (WEB-04) |

### Required Artifacts (this round's gap plan 01-12)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/image_rx_manager.cpp` (1064480) | pendingHealThumbnail helper + hold at BOTH activation sites + base settle gate | ✓ VERIFIED | Helper :361-385 (used/non-terminal/THUMBNAIL/holed/same-id-FULL — the gate-2 proof, earliest-by-arrival); activateNextPull hold :816-834 with log-once latch (healHoldLoggedId, init :56); manifest-while-idle gate extended at :584 (`findActivePull()==nullptr && pendingHealThumbnail()==nullptr`); settle gate nested INSIDE the window-complete branch :121-123 (no pass charged, stall branch cannot fire during settle) |
| `include/image_rx_manager.h` (1064480) | pendingHealThumbnail declaration + healHoldLoggedId member | ✓ VERIFIED | Both present (diff hunks :160/:171) |
| `src/image_tx_manager.cpp` (1064480) | evictEntriesOlderThan mid-service guard + arming sets windowArmedAtMs + balloon settle gate | ✓ VERIFIED | Guard :829-835 (skip + defer log, scope comment names the boundary); arming :729; settle :756-758 (first-chunk-only, re-armed spans re-settle) |
| `include/image_tx_manager.h` (1064480) | ImageTxEntry.windowArmedAtMs | ✓ VERIFIED | Field added (diff hunk :115); reset in freeEntry (:926) |
| `include/image_protocol.h` (1064480) | IMG_WINDOW_RX_SETTLE_MS = 500 + invariant comment | ✓ VERIFIED | :97 with discriminator comment; verifier-grepped chain SETTLE 500 < PREEMPT 5000 < STALL 8000 holds; all five standing constants unchanged |
| `src/camera_manager.h` + `src/camera_manager.cpp` (f51bad6) | allocatedFrameSize tracking, re-init-with-recovery setFrameSize, cached-settings initCamera | ✓ VERIFIED | Member beside currentFrameSize; constructor init :20; initCamera refresh :117 (written ONLY there — never a second identity); cached setters :129-132 (set_saturation(s, currentSaturation) exactly once); two-path setFrameSize :410-478 — sensor-only <= allocation (monotonic-order comment), growth re-init with growth log :459, MANDATORY recovery (restore saved values + re-init + recovery log :471) before false; both-fail terminal case leaves initialized==false (honest dead camera, never silent half-alive) |
| `01-UAT.md` evidence blocks | G-01-8 resolved / G-01-7 UPDATE rescoped / G-01-9 opened / Test 3 note | ✓ VERIFIED | Ledger read in full; every discriminator quote this verifier re-checked against balloon4.log/base4.log — text, line numbers, and counts match (one tally nuance: 14 exact-string vs 16 broad post-terminal ignores, noted above) |
| `.planning/WINDOWS.md` | Entries 3 rescoped-open / 4 fixed / 5+6 opened; counts 3 open / 3 fixed / 6 total | ✓ VERIFIED | Table and JSON copies consistent; /gsd-ship correctly still blocked |
| `deferred-items.md` + 2 todos | Retry overrun routed; gallery UX todos | ✓ VERIFIED | On disk (verifier-verified) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| pendingHealThumbnail | Both full-pull activation sites | hold reorders priority between existing D-21 triggers | ✓ WIRED | Bench-proven: hold fired 3x and in every case the thumb heal ran to its D-24 terminal verdict BEFORE the full pull activated (image 7: heal requests seq 20-24 then INCOMPLETE :634, only then full pull seq 25+); heal loop untouched (gate 1 passes while no pull active); bounded by D-24 (observed: image-7 thumb finalized INCOMPLETE and released the hold) |
| evictEntriesOlderThan guard | supersede path only | guard predicate == BUSY-check predicate; overflow ranking + TTL sweep unchanged | ✓ WIRED | Diff-hunk analysis: no hunk touches evictionClassOf (:203) or sweepExpiredEntries (:843); bench: 5 supersede-evictions all post-terminal, defer form never fired, no queue wedge (T-01-12-02 mitigated) |
| windowArmedAtMs -> serviceWindowChunk settle | first-chunk-after-(re)arm only | settle delays only the FIRST chunk of a freshly armed window | ✓ WIRED | Mid-window continuation and preempt clock unaffected (code :756-758 reads windowNextIndex==windowStart); beacon early-return and command-response order untouched (no main-file diffs) — PRI-01 held at bench (beacons interleaved with window service, TX 253 consecutive) |
| allocatedFrameSize -> setFrameSize bound -> reinitialize() recovery | initCamera refresh (single writer) | currentFrameSize stays the identity; allocatedFrameSize is derived capacity | ✓ WIRED | Bench-proven at three growth sizes; recovery branch never fired (no honest-NACK needed — every exercised size succeeded); thumbnail QQVGA pair always <= allocation (sensor-only path, no re-init storm — image 10 thumb 1343 B/7 chunks at VGA allocation confirms) |
| Delegated section#capture submit listener (regression) | fetch POST + pollOnce | unchanged by this round | ✓ WIRED | Listener :1529; raw literals intact |
| galleryCountSeen latch (D-36, regression) | /gallery count-driven refetch | untouched | ✓ WIRED | 4 references intact; no UI file in any 01-12 diff |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| SET_RESOLUTION verdict | allocatedFrameSize bound + reinitialize() | real fb-buffer allocation state | Yes | ✓ FLOWING — verdict now tracks true buffer capacity; bench: three growth re-inits with proportionate image sizes, zero FB-OVF at settings steps |
| Thumbnail payload | fb->len via dimension-guarded capture | esp32-camera fb (fb_count 2, GRAB_LATEST) | Partial | ✗ DISCONNECTED (payload vs metadata) — G-01-9 defect A: metadata says 160x120 while bytes can be a stale full-size frame (4/6 captures); the honest Incomplete badge displays the true state |
| Full-image stored bytes | chunk routing by (imageId, heuristic kind) | wire chunks -> slot bitmap -> SD | Partial | ✗ DISCONNECTED UNDER MISROUTE — G-01-9 defect B / CR-01: late thumbnail-heal stragglers land at full indices after thumb terminal; 36/36-received fulls fail stored-bytes CRC |
| Full-manifest availability | announceFullManifest -> ANNOUNCED state | one-shot transmit | No | ✗ HOLLOW — G-01-9 defect C / CR-02: state consumed even on transmit failure; a lost manifest strands the full silently (image 8) |
| Image-ID sequence (regression) | NVS namespace imgid | persisted per issue | Yes | ✓ FLOWING — IDs 6-11 sequential across the session (carried closure intact) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite (incl. PRI order gates) | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run this round) | All PASS, exit 0 | ✓ PASS |
| Both firmware targets compile | `pio run` (commit-time at 6d749c8, on record) | 2 succeeded; zero source drift f51bad6..HEAD (verifier-verified: docs-only diff) — applies to HEAD exactly | ✓ PASS |
| G-01-8 closure evidence vs raw logs | grep balloon4.log base4.log | growth re-inits :1791/:2784/:3588; FB-OVF total 4 all at :353-354/:396-397 (thumbnail path), 0 at settings steps; CAPTURE_NOW 6/0; CIF 1.57x size COMPLETE 57/57; VGA thumb 1343 B correct | ✓ PASS |
| G-01-7 lever engagement | grep base4.log balloon4.log | hold 3x :521/:1127/:2336; supersede-evictions 5 (all post-terminal); defer form 0 (never needed); BUSY deferrals :1154/:1161 | ✓ PASS (levers function) |
| G-01-7 series-A acceptance | grep base4.log finalize verdicts | image 7 thumb INCOMPLETE 35/36 :634; image 7 full INCOMPLETE 36/36-recv :855 (CRC :854); image 8 full ABSENT (manifest lost balloon4.log:722 / 0 receipts) | ✗ FAIL (gap G-01-7 — honestly routed, rescoped) |
| G-01-9 defect evidence | grep balloon4.log base4.log | thumbs: 4/6 full-sized (:245/:355/:398/:1819/:2812/:3619 — 7157/7160/11223/28808 == full sizes); CRC mismatches :854/:3267 at 100% chunks; retry overrun 'attempt 4/3' = 13 | ✗ FAIL (gap G-01-9 — new, honestly routed) |
| CR-01/CR-02/CR-03 root-cause claims (fresh review) | verifier source reads | ImageChunkBody has no kind byte (:154-159); onChunkFrame heuristic fallback (:253-284); announceFullManifest sets ANNOUNCED unconditionally (:585-603); thumb guard checks width/height only, malloc(fb->len) unbounded (:361-376) | ✓ CONFIRMED (all three criticals real at HEAD) |
| WR-01 safety watch (regression) | grep 'Critical battery' balloon4.log | 0 lines; lone 'Emergency Shutdown: Enabled' at :56 is the documented boot config echo | ✓ PASS |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists in the repo. The declared verification commands (harness + both builds) — harness verifier-run above; builds on record with the zero-drift proof. SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-02..01-07, 01-10, 01-12 | Trigger camera capture from base station web UI | ✓ SATISFIED | Session 4: 6/6 CAPTURE_NOW SUCCESS from the dashboard, IDs 6-11, images transferred |
| CTRL-02 | 01-03/04/06/07, 01-12 | Adjust all camera settings remotely | ✓ SATISFIED, clauses open | G-01-8 RESOLVED: the resolution clause is fixed (every advertised value now executes honestly or NACKs honestly — three growth sizes bench-proven; REQUIREMENTS.md 'Complete' is now defensible on the exercised truth). Open clauses ride Test 3: pairwise visible-effect judgments, QVGA restore, reboot boot-resolution, values 11-13 untested |
| CTRL-03 | 01-03/04/05/07 | Manual + automatic capture modes | ✓ SATISFIED | UAT test 4 PASS; unchanged this round |
| CTRL-04 | 01-03/04/05/07 | Automatic capture fixed interval timing | ✓ SATISFIED | UAT test 4 PASS (exact cadence incl. >30 s value) |
| CTRL-06 | 01-03/04/05/07 | Failed commands retried with timeout | ✓ SATISFIED | UAT test 2 PASS; terminal guard intact; WINDOWS entry 6 (retry-bound off-by-one) is a routed deviation, not a truth failure |
| PRI-02 | 01-02, 01-06 | Retry mechanism with timeout | ✓ SATISFIED | Same evidence as CTRL-06 |
| IMG-02/IMG-03 (gap-plan anchors) | 01-08..01-12 | Thumbnail displays immediately; fulls transfer in background | ⚠ SATISFIED WITH OPEN RESIDUALS (G-01-7 rescoped + G-01-9 open) | Spaced/manual captures complete (session 4: images 6/9-full/10 both kinds COMPLETE); unspaced series A failed 3 verdicts; thumb payload integrity defective 4/6 (G-01-9 A); full-content integrity corrupted by the CR-01 misroute (G-01-9 B); manifest loss silently drops fulls (G-01-9 C). Phase-2-mapped requirements — honestly routed (WINDOWS 3/5 open, ship blocked); flagged for the milestone audit as prior rounds did |
| IMG-01/04/05/06, PRI-03 (carried anchors) | 01-08..01-11 | Transmit / chunked ARQ / SD storage / gallery / bandwidth handling | ✓ SATISFIED | Carried closures stand; session 4 corroborates (57/57 and 97/97 CRC-complete transfers at CIF/VGA; gallery grows; windowed ARQ with pass bounds held under elevated air loss — 61 END MARKER MISS all bounded) |

Orphaned requirements: none. The six Phase-1-mapped IDs are all claimed across plans and evidenced; the additional IDs claimed by gap plans are Phase-2-mapped requirements whose hardware truths surfaced in Phase 1's UAT, with flips carrying the honest residual status.

### Plan Prohibition Verdicts (judgment/backstop-tier — autonomous, NON-AUTHORITATIVE)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-12 | MUST NOT fabricate image-transfer or config success | PASS (log level, non-authoritative) — flagged, human review recommended | G-01-8 flipped only on operator-observed quotes (this verifier re-checked them all against the raw logs); G-01-7 kept OPEN despite functioning levers — the exact case the prohibition guards; G-01-9 opened rather than absorbed |
| 01-12 | MUST NOT mark G-01-7 or G-01-8 resolved on code-level evidence alone | PASS (non-authoritative) — flagged, human review recommended | G-01-8 closure cites bench series-B evidence; G-01-7 NOT closed on its code-level lever proof |
| 01-12 | MUST NOT add a parallel best-effort thumbnail mechanism (D-22 single path) | PASS (code level, non-authoritative) — flagged, human review recommended | Verifier-read: the hold reorders priority between two existing D-21 triggers; the heal loop and its gates are byte-untouched; no new mechanism |
| 01-12 | MUST NOT apply the mid-service guard to the overflow path or TTL sweep | PASS (machine-checkable) — diff-verified | No diff hunk touches evictionClassOf or sweepExpiredEntries; guard lives only in evictEntriesOlderThan with the scope comment |
| 01-12 | MUST NOT touch the beacon early-return or command-response loop order | PASS (machine-checkable) | No main-file diffs in either 01-12 code commit; harness order gates green (verifier-run); beacon cadence held at bench (TX 253 consecutive, interleaved with window service) |
| 01-12 | MUST NOT leave the camera dead after a failed framesize re-init | PASS (code + bench, non-authoritative) — flagged, human review recommended | Recovery re-init mandatory before false return (verifier-read :464-478); both-fail terminal case leaves initialized==false — honest, never silent; recovery branch never fired at bench (every exercised size succeeded) |
| 01-12 | MUST NOT change the /gallery refresh contract (D-36) | PASS (machine-checkable) | No UI file touched; galleryCountSeen latch intact (4 references) |

### Anti-Patterns Found

No TBD/FIXME/XXX in any of the seven files modified by 01-12 (verifier grep clean). Items below are from the fresh 01-REVIEW.md (23726b1, 3 Critical / 5 Warning / 7 Info) — this verifier confirmed all three criticals in source (see Behavioral Spot-Checks); the criticals ARE the routed G-01-9/G-01-7-residual mechanisms, not new unrouted debt.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| include/image_protocol.h + src/image_rx_manager.cpp | :154-159 / :253-284 | CR-01: chunk frames carry no imageKind; routing heuristic misroutes late thumb-heal stragglers into the active FULL slot (root cause of the 36/36 stored-bytes CRC mismatches) | 🛑 Gap (routed — G-01-9 defect B) | Wire fix (kind byte + validation + harness) or interim pullActive&&windowActive hardening; the review's duplicate-drop signature matches base4.log:821-847 |
| src/image_tx_manager.cpp | :585-603 | CR-02: one-shot FULL manifest consumed on transmit failure — no re-announce path | 🛑 Gap (routed — G-01-9 defect C, also feeds G-01-7) | Bounded re-announce + free fullBuffer at the bound |
| src/camera_manager.cpp | :361-376 | CR-03: thumbnail guard checks fb->width/height only — fb->len never bounded (full-size payloads pass as "thumbnails") | 🛑 Gap (routed — G-01-9 defect A) | Bound fb->len / JPEG-SOF check + drain-one-frame after downshift |
| src/image_tx_manager.cpp | :500-515 | WR-01: failed thumb-manifest transmit still advances push state — thumbnail permanently undeliverable | ⚠️ Warning | Same family as CR-02 on the push path; fold into the G-01-9 round |
| src/image_tx_manager.cpp | :203-210, :327-361 | WR-02: overflow eviction ranks an in-flight thumb heal as FIRST victim (the G-01-7 lever-2 guard covers only the supersede path) | ⚠️ Warning | Named lever for the G-01-9 round: mid-service skip in the overflow victim scan |
| src/main_balloon.cpp + command_handler + auto_capture | :884-895 | WR-03: manual + auto capture in the same loop pass silently drop the first image | ⚠️ Warning | Race window one 100 ms pass per collision; fix = advance AutoCap spacing reference on manual capture |
| src/image_rx_manager.cpp + command_sender | :186-233 | WR-04: stall re-requests duplicate still-tracked window requests (feeds the CR-01 re-arm churn and the retry overrun) | ⚠️ Warning | Fold into the G-01-9 round's CommandSender-adjacent work (entry 6) |
| src/command_sender.cpp + main_basestation.cpp | :439-449 / :104-122 | WR-05: NACK_BUSY terminalized as FAILED and poisons the link-truth LED ('No link' while chunks flow) | ⚠️ Warning | BUSY is routine deferral; retry-within-budget or separate BUSY classification |
| src/command_sender.cpp | :524-537 | IN-01: 'attempt 4/3' label off-by-one (the WINDOWS entry 6 surface) | ℹ️ Info | Label fix rides entry 6 |
| Carried Info items | — | IN-02..IN-07 (false health-check warnings, dead previousMode, mode-before-verify, unused VLA, dead code, stale byte-count comments) | ℹ️ Info | None in a Phase-1 must-have domain; several overlap the pending /gsd-secure-phase 1 gate |

### SUMMARY vs Reality

1. 01-12-SUMMARY claims reproduce on this verifier's own reads, greps, harness run, and log checks: commits 1064480 (5 files, +137/-6), f51bad6 (2 files, +88/-17), 6d749c8 (STATE.md), 4692bc1 (docs), 23726b1 (01-REVIEW.md rewrite) all exist with exactly the claimed file sets.
2. Every load-bearing bench quote is verbatim-accurate against balloon4.log/base4.log, including line numbers (growth re-inits :1791/:2784/:3588; hold :521/:1127/:2336; INCOMPLETE :634/:855/:1297/:3268; CRC :854/:3267; manifest :722; BUSY deferrals :1154/:1161) and counts (FB-OVF 4; CAPTURE_NOW 6/0; retry overrun 13; supersede-evictions 5). One tally nuance: 'chunk for finalized image 11 kind 0 ignored' exact-counts 14 (SUMMARY said 16; broad 'ignored' = 16) — same traffic class, not load-bearing.
3. The closure is honest and the non-closure is honest: G-01-8 flipped on mechanism-specific (zero FB-OVF) AND truth-specific (all captures succeed at every exercised size) evidence; G-01-7 stayed open despite its levers provably engaging — the acceptance verdict governs, and the failure was decomposed into three named mechanisms rather than monolithic "still broken".
4. The G-01-8 resolution explicitly records its unexercised clauses (steps d/e/f, values 11-13) rather than claiming full coverage — the resolution is scoped to the named defect, which is closed.
5. 01-REVIEW.md (23726b1) critical claims were re-derived by this verifier in source: all three confirmed real at HEAD (CR-01 kind-less chunk body + heuristic routing; CR-02 unconditional ANNOUNCED; CR-03 unbounded fb->len). The review's base-side SD-storage clearance is consistent with this verifier's understanding of the flush-before-verify path.
6. REQUIREMENTS.md statuses: CTRL-02 'Complete' is now defensible (G-01-8 resolved; open clauses are spot-check residuals under Test 3, noted above); IMG-02/IMG-03 'Complete' carries the honest G-01-7/G-01-9 residuals in the WINDOWS ledger, which keeps /gsd-ship blocked — same defensible-only-because-the-ledger-governs pattern flagged for the milestone audit in round #5.
7. STATE.md, WINDOWS.md, deferred-items.md, and the two todos are mutually consistent and match the git record.

### Gaps Summary

Two structured gaps, both real, both verified at code level AND log level, both honestly routed by the project (01-UAT.md entries, WINDOWS 3/5/6 open, /gsd-ship blocked):

1. **G-01-7 (rescoped) — concurrent-transfer completion.** The 01-12 levers are in and functioning (hold serializes heals ahead of fulls; no mid-service eviction; settle gates both sides), but unspaced captures still produced 3 INCOMPLETE verdicts via three distinct mechanisms: BUSY-deferral pass burn (the pre-existing one-at-a-time guard consumes D-24 passes for requests that had no transfer opportunity), balloon-source-side stored-bytes corruption (now root-caused as the CR-01 chunk misroute), and the silently lost FULL manifest (CR-02). Named next levers: defer-aware pass accounting, bounded manifest re-announce, and the G-01-9 thumb fix. Interim mitigation: space captures.
2. **G-01-9 (new) — thumb/corruption complex.** Full-sized thumbnails passing the metadata-only guard (4/6 captures — payload must be validated, not fb->width/height); fully-received fulls corrupted by the CR-01 kind-less chunk-routing misroute; one-shot manifests consumed on transmit failure. The fresh review turns what bench session 4 could only candidate-name ("balloon-source-side corruption") into confirmed root causes with named fixes. Ride-along: CommandSender retry-bound off-by-one (WINDOWS entry 6).

Everything else is green: 4/5 SCs verified (SC-3's resolution clause upgraded to hardware-proven), all 01-12 artifacts substantive and wired, all prohibitions held (scope boundaries diff-verified), harness exit 0 (verifier-run), builds 2/2 on record with a zero-drift proof carrying them to HEAD exactly, no debt markers, all prior closures regression-intact, pacing invariants held, PRI-01 held at bench.

The phase goal — bidirectional LoRa command/control with ACK, retry, manual + auto capture — is achieved and hardware-proven on current firmware, now with truthful settings-path resolution changes. What remains is image-reliability residuals in the Phase-2 requirement domain plus the unjudged SC-3 spot-check clauses — all routed, none absorbed into a false close.

Recommended next step: `/gsd-plan-phase 1 --gaps` (G-01-9 CR-01/CR-02/CR-03 fixes + G-01-7 defer-aware pass accounting, with the series-B d/e/f and SC-3 visible-effect ride-alongs under Test 3), then `/gsd-secure-phase 1` — the deliberately-pending security gate is the separate remaining phase-close gate (its scope should absorb the review's WR-04/WR-05/IN-class CommandSender and link-truth findings where they overlap).

---

_Verified: 2026-08-24T02:58:12Z_
_Verifier: Claude (gsd-verifier)_

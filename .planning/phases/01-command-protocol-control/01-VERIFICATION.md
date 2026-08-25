---
phase: 01-command-protocol-control
verified: 2026-08-25T03:18:15Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3, NARROWED TO ITS FINAL CLAUSE: the resolution clause is now fully hardware-proven end-to-end (growth at CIF/SVGA, QVGA restore to boot-class sizing, reboot boot-resolution on base-side wire evidence) — only the pairwise visible-effect judgments (brightness -2 vs +2 + one other class) remain operator-unjudged, third consecutive round riding (zero non-RESOLUTION settings commands issued all session, verifier-counted in base5.log)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "G-01-9 defect A (full-sized thumbnails): RESOLVED at bench session 5 — all 7 logged enqueues are genuine QQVGA payloads 1176-1340 B / 6-7 chunks with zero thumb==full byte-equality lines (balloon5.log:198/:242/:699/:1029/:1525/:5014/:6399, verifier-read); the CR-03 drained-stale-frame discriminator fired at every logged capture (:196/:240/:697/:1027/:1523/:5012/:6397, verifier-read). Session 4 baseline was 4-of-6 impostors at 7157-28808 B"
    - "G-01-9 defect B (all-chunks-received stored-bytes CRC mismatch): RESOLVED at bench — zero 'stored-bytes CRC mismatch' lines in either log (verifier-grepped, 0/0) INCLUDING CIF 31-chunk (base5.log:776-777) and SVGA 80/82-chunk (:1243/:2770) fulls, all complete=true; the 01-13 kind-exact routing is live on the wire ('window chunk(image 14 kind 0...' balloon5.log:289)"
    - "WINDOWS entry 6 (retry-bound label overrun): FIXED — all 52 'Retrying command' lines read '(retry K/3)' with K<=3 (43x1/3, 6x2/3, 3x3/3, verifier-counted), zero attempt-ordinal lines in either log, and the one exhausted command terminalized honestly at exactly 3 (base5.log:126 'Command seq=6 timeout after 3 retries'); post-terminal traffic is bounded straggler re-service ended by cancel-on-finalize (25 'chunk for finalized' lines session-wide, worst cluster 14x image 17 kind 1 at :779-807, 'Cancelled command seq=28' at :778 — verifier-counted)"
    - "WINDOWS entry 7 (01-15 heal-site defer log deviation): FIXED — fired as designed exactly once (base5.log:283 'heal deferred - window request seq 12 still in flight for image 15 kind 0'; image 15's thumb then healed to COMPLETE 7/7 at :310); 'stall deferred' 16x all benign (verifier-counted)"
    - "SC-3 resolution-clause extensions: QVGA restore verified by wire (SET_RESOLUTION 6 ACKed balloon5.log:5048/:6384; post-restore image 20 is 4090 B / 21 chunks == boot-QVGA image 14's 4094 B / 21, both kinds COMPLETE base5.log:3148/:3258); reboot clause EXERCISED and verified base-side (beacon seq reset 451->0 amid continuous post-reset image-21 chunk flow base5.log:3341-3343; image 21 NVS-continued 20->21 at QVGA-class 4094 B / 21 chunks, both kinds COMPLETE :3378/:3518) with the detached-balloon-console evidence gap honestly recorded (balloon5.log ends at idle telemetry, last [BCN] seq=452 at :6777, verifier-read)"
  gaps_remaining:
    - "G-01-7 rescoped open (series-A acceptance fails on command survivability under unspaced load + manifest air loss — narrower than round #6's three mechanisms)"
    - "G-01-9 defect C open (air-loss class: TX-success manifest consumes the one-shot announce with no receipt-driven recovery)"
    - "CR-04 NEW unrouted critical (camera-ready gate blocks IMAGE_WINDOW_REQUEST/GET_STATUS during camera-down windows — data-loss risk)"
    - "WR-08 NEW unrouted warning (TX push paths advance cursors on transmit failure)"
  regressions: [] # harness exit 0 (verifier-run at HEAD: 52 PASS / 0 FAIL); ZERO source drift since d546ace (working tree touches only .planning/config.json + phase-03 UAT + build checksum — no src/include/scripts file); pacing invariants hold (PASSES=3, MANIFEST_ATTEMPTS=3 new, STALL=8000, PREEMPT=5000, SETTLE=500, HEAL_IDLE=24000, TTL=900000, manifest body 27, beacon body 19 — all verifier-grepped exactly once); prior closures stand (G-01-8 re-init path intact in camera_manager.cpp; pendingHealThumbnail hold intact 4 refs; kind routing has no heuristic remnants 'Heal-window precedence'=0; evictionClassOf/sweepExpiredEntries bodies untouched by round 7 — diff-hunk verified, the only eviction-adjacent hunks are the overflow-scan restructure inside enqueueCapture and the documented whitespace-only predicate join in evictEntriesOlderThan); delegated section#capture listener + resolution select untouched (main_basestation.cpp absent from every round-#7 commit)
gaps:
  - truth: "A capture triggered while the previous image's transfer/heal is still pending does not prevent that pending transfer's completion — concurrent captures cannot starve or evict an in-flight thumbnail heal or full pull (G-01-7 in 01-UAT.md, WINDOWS entry 3)"
    status: failed
    reason: "Every session-4 failure mechanism is ELIMINATED at bench session 5 (verifier-re-confirmed in raw logs: zero INCOMPLETE verdicts in either log, zero stored-bytes CRC mismatches, zero BUSY deferrals, zero mid-service evictions — all 5 supersede-evictions idle-targeted balloon5.log:813-814/:1062/:1560/:6440 + one TTL :4258, serialization hold 4x with heals completing first :66/:377/:2382/:3356, beacons 484 accepted worst-gap 2 consecutive) — but the series-A acceptance truth STILL fails on a NEW narrower axis: the THIRD unspaced CAPTURE_NOW (seq=6, queued base5.log:55) never reached the balloon. All 4 transmissions were lost under the concurrent image-14/15 chunk storm (retries 1/3 :73, 2/3 :81, 3/3 :92) and the base terminalized honestly (:126 'timeout after 3 retries' — verifier-read at exactly those lines). First two triggers made it (seq=4 ACKED :35, seq=5 retry-2/3 ACKED :70 -> images 14/15); 3 unspaced triggers produced 2 captures. The 16-byte command frame lost 4/4 while 217-byte chunk frames flowed — half-duplex turnaround/priority class. Residual mechanism: command air-priority or command-burst pacing during window service (named in 01-UAT.md G-01-7)"
    artifacts:
      - path: "src/command_sender.cpp"
        issue: "No air-priority/pacing class for command frames competing with in-flight window chunk service — the named next lever (command air-priority or burst pacing during window service)"
      - path: "src/image_tx_manager.cpp"
        issue: "window service keeps the half-duplex channel saturated during unspaced-capture chunk storms; no command-yield gap exists"
    missing:
      - "Command air-priority or command-burst pacing during window service so a concurrent CAPTURE_NOW survives the chunk storm"
      - "Bench re-verification: series-A re-run (3 unspaced captures) yields 3 captures / 6/6 verdicts COMPLETE with zero command timeouts"
      - "SC-3 visible-effect pairs ride the same bench round (third consecutive round unjudged)"
  - truth: "No full is silently dropped for a lost FULL manifest — every enqueued armable full either transfers or its loss is surfaced by a named drop log (G-01-9 defect C in 01-UAT.md, WINDOWS entry 5)"
    status: failed
    reason: "The 01-14 re-announce (bb2c517, verifier-read success-gated at src/image_tx_manager.cpp:660-680) covers ONLY the TX-FAILURE class. Session 5 produced zero TX failures, so that path was never exercised, and the AIR-LOSS class silently dropped a full exactly as session 4 did: image 15's FULL manifest left the balloon with TX-success accounting (balloon5.log:274 'ImageTx: FULL manifest(image 15, 4853 B, 25 chunks) sent' — verifier-read), never arrived (zero 'manifest image 15 kind 1' lines in base5.log, verifier-grepped; only the unrelated kind-0 manifest at :76), consumed the one-shot announce state, and nothing at either console names the loss. The operator's series-A report 'some of the pictures were missed' is this image. Named lever: receipt-driven manifest recovery — base-side nudge when a completed thumb has no FULL manifest within a bound, or balloon-side periodic bounded re-announce while an announced full is head-and-idle, independent of TX verdict"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "announceFullManifest (:623-684) re-announces only on TX-failure verdicts; a TX-success/air-lost manifest still consumes ANNOUNCED with no recovery path"
      - path: "src/image_rx_manager.cpp"
        issue: "no base-side nudge exists (thumb COMPLETE + no FULL manifest within a bound should solicit re-announce)"
    missing:
      - "Receipt-driven FULL-manifest recovery (base-side nudge on thumb-COMPLETE-without-FULL-manifest within a bound, or balloon-side periodic bounded re-announce while head-and-idle, independent of TX verdict)"
      - "Bench re-verification: zero silently absent fulls across a full session"
  - truth: "Image-transfer and status commands are servable whenever the resources they need (ImageTx PSRAM buffers, E32 radio, cached settings) are operational — a camera hardware failure must not block their execution (CR-04 in 01-REVIEW.md round #8, commit d546ace — re-derived and CONFIRMED at HEAD by this verifier)"
    status: failed
    reason: "CommandHandler::executeCommand (src/command_handler.cpp:139-145) rejects EVERY command with NACK_BUSY 'Camera not ready' when camera->isReady() is false — and isReady() is just 'initialized' (src/camera_manager.h:134). On low battery (batteryVoltage < BATTERY_LOW_THRESHOLD) main_balloon.cpp:864 calls Camera().enableCamera(false) -> end() -> initialized=false (camera_manager.cpp:89), while ImageTx().process() keeps running in the same loop (main_balloon.cpp:895 area) with buffers and radio fully operational. From that moment: (1) IMAGE_WINDOW_REQUEST (dispatch :182-183) is refused — every announced full becomes unretrievable for the entire camera-down window, exactly the final images an operator needs to recover before power loss; the stranded data is then destroyed by power-off or TTL eviction; the base's BUSY-deferral retries burn their budget then terminalize, and D-24 finalizes INCOMPLETE. (2) GET_STATUS (:179-180) is also refused, so the base's status poll fails permanently while the balloon still beacons. The refusal reason (camera) is unrelated to the resource the commands need. UNROUTED: WINDOWS.md has no entry for CR-04 (the fresh review 01-REVIEW.md d546ace names it; no ledger or gap entry carries it). Pre-existing (round #7 touched neither command_handler.cpp nor main_balloon.cpp) — not a regression, but a critical data-loss-risk defect confirmed at source this round"
    artifacts:
      - path: "src/command_handler.cpp"
        issue: "executeCommand (:139-145) blanket camera gate ahead of the full dispatch switch — blocks IMAGE_WINDOW_REQUEST/GET_STATUS/AUTO_CAPTURE/SET_EVENT_THRESHOLDS which need no camera hardware"
    missing:
      - "Scope isReady() checks to the camera-touching handlers only (CAPTURE_NOW/SET_RESOLUTION/SET_QUALITY/...); leave IMAGE_WINDOW_REQUEST, GET_STATUS, SET_EVENT_THRESHOLDS, AUTO_CAPTURE_ENABLE/DISABLE ungated"
      - "Route CR-04 in the WINDOWS ledger (and the next gap-closure plan) alongside the G-01-7/G-01-9-C residuals"
  - truth: "A failed chunk transmit does not permanently skip that chunk for its pass or complete a window whose final chunk never left the balloon (WR-08 in 01-REVIEW.md round #8 — re-derived and CONFIRMED at HEAD by this verifier)"
    status: partial
    reason: "Both TX push paths advance their cursor unconditionally after serialize+transmit: pushThumbChunk (src/image_tx_manager.cpp:601 'entry.nextThumbChunk++' — runs when ok==false) and serviceWindowChunk (:881 'entry.windowNextIndex++' likewise). In serviceWindowChunk, when the FAILED chunk is the last of a tail-reaching window, the entry is still marked windowArmed=false and — for FULL windows reaching the tail — SERVED (:883-895), a preferred eviction candidate, despite the final chunk never leaving the balloon; a later heal re-request can then hit UNKNOWN_IMAGE after eviction and terminal-fail INCOMPLETE with the data resident the whole time. Each failed transmit becomes a permanent hole for its pass whose only recovery is a multi-second stall-timeout + re-request round trip. Warning-class: the heal path owns recovery, and session 5 produced zero TX failures (never exercised) — but the defect is real at HEAD and compounds the G-01-7/G-01-9-C residual round. UNROUTED in the ledger"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "pushThumbChunk :601 and serviceWindowChunk :881-895 — cursor advance and SERVED/window-complete transitions not gated on transmit success"
    missing:
      - "On !ok: do not advance the cursor; retry the same index on the next process() pass with a small same-index bound (e.g. 3) before skipping; evaluate the SERVED transition only on a successful final-chunk transmit"
      - "Route WR-08 in the same round as the G-01-7/G-01-9-C/CR-04 levers"
deferred:
  - truth: "Full-resolution gallery image viewer (operator bench wish; presentation-only)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md (verifier-verified on disk)"
  - truth: "Gallery thumb number overlay + Incomplete-badge explanatory copy (operator bench friction)"
    addressed_in: "Phase 3 backlog (gallery UX)"
    evidence: ".planning/todos/pending/2026-08-24-gallery-thumb-number-overlay.md and 2026-08-24-incomplete-badge-on-renderable-images.md (verifier-verified on disk)"
  - truth: "D-13 separate camera-controls page / D-15 accordion settings groups"
    addressed_in: "Phase 3"
    evidence: "Carried from prior verifications; superseded by the delivered single-page dashboard (03-01, WEB-04)"
behavior_unverified_items:
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3 — pairwise visible-effect clause only; every other clause is now hardware-proven)"
    test: "At a fixed resolution, adjust ONE setting between two captures and compare the pair (brightness -2 vs +2, plus at least one other class: contrast, saturation, or quality)"
    expected: "Visible pairwise differences per exercised setting, recorded per pair"
    why_human: "Sensor visible-effect acceptance needs the physical camera and operator judgment — session 5 issued zero non-RESOLUTION settings commands (base5.log queue: 9x CAPTURE_NOW, 4x SET_RESOLUTION, 72x GET_STATUS, 46x IMAGE_WINDOW_REQUEST — verifier-counted), the third consecutive round this clause has ridden. The resolution clause no longer rides: growth (CIF/SVGA), QVGA restore, and reboot boot-resolution are all bench-proven this round (see gaps_closed). A minor balloon-side evidence gap also remains for the reboot clause (console detached before reboot — judged on base-side wire evidence, honestly recorded in 01-UAT.md Test 3)"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #7, after gap closure 01-13..01-16)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-25T03:18:15Z
**Status:** gaps_found
**Re-verification:** Yes — #7, after gap-closure round #7 (plans 01-13/01-14/01-15 code + 01-16 bench; commits 70543c5/394a231/90300b8/bb2c517/ae16991/7dd5dab/45c8b80 code, 8adc0bc/37f405c ledgers, 02142e1 out-of-plan GPS config, d546ace fresh review — all verifier-confirmed in git log; zero source drift d546ace..working-tree)

## Goal Achievement

Round #7 is the strongest round yet and the ledgers are honest. The two gaps that dominated rounds #4-#6 narrowed to their smallest residue: G-01-9's defects A and B are ELIMINATED at bench on evidence this verifier re-checked line-by-line against the raw session-5 logs (zero INCOMPLETE verdicts, zero stored-bytes CRC mismatches including CIF/SVGA-class fulls, all thumbs genuine QQVGA with the drain discriminator at every capture), WINDOWS 6/7 flipped fixed on truthful retry ordinals and the heal-defer log, and SC-3's resolution clause is now proven end-to-end including QVGA restore and reboot boot-resolution. The series-A acceptance truth STILL fails — but on a new, narrower axis (a 16-byte command frame lost 4/4 under the concurrent chunk storm; honest timeout), plus the manifest AIR-LOSS class that the TX-verdict-gated re-announce structurally cannot see. Both residuals are honestly routed open with named levers.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — remains achieved and hardware-proven, now strengthened: the retry bound is label-truthful and termination-exact at bench, settings-path resolution changes are truthful across growth/restore/reboot, and every exercised capture executed end-to-end.

New this round: the fresh code review (d546ace, round #8) found CR-04 (critical) and WR-08 (warning). This verifier re-derived BOTH from source before believing them — both are REAL at HEAD and neither is routed in the WINDOWS ledger. CR-04 is a data-loss-risk defect in the command dispatch gate (camera-down blocks image retrieval and status while the radio and buffers live); it is pre-existing, not a round-#7 regression, but it belongs in the same next round as the G-01-7/G-01-9-C levers.

This verifier independently: read every round-#7 code change at its site (all substantive, at plan-named locations, prohibitions held); ran the wire harness at HEAD (exit 0, 52 PASS / 0 FAIL); re-verified every load-bearing session-5 quote against balloon5.log (6,791 lines) / base5.log (3,658 lines) — line numbers AND counts all match, including the negative claims (0 INCOMPLETE, 0 CRC mismatch, 0 BUSY deferrals, 0 manifest-15 receipts, 0 attempt-ordinal labels). Two non-load-bearing tally nuances noted in SUMMARY vs Reality.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression: NO UI file in any round-#7 commit (diff-verified — main_basestation.cpp absent from 70543c5..d546ace); delegated section#capture submit listener + resolution select untouched; zero debt markers in all round-#7 files (verifier grep clean). The 01-16 dashboard-glance lift was honestly recorded unverified-by-log (operator-did-not-flag, no pass claimed) — no regression surface exists since no UI file changed |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED | Session 5: 9 CAPTURE_NOW queued / 8 executed (1 honest timeout), settings ACKed at every step (CIF/SVGA/QVGA restore SUCCESS — balloon5.log:1004/:1506/:5048 verifier-read), 72 GET_STATUS served, 46 IMAGE_WINDOW_REQUESTs serviced, beacons 484 accepted with worst gap 2 consecutive seqs; kind-stamped chunk wire live (balloon5.log:289); harness green (verifier-run at HEAD, exit 0) |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | NARROWED TO ITS FINAL CLAUSE: resolution clause now FULLY hardware-proven — growth (CIF re-init 6->8 image 17 both kinds COMPLETE base5.log:577/:776; SVGA re-init 8->11 images 18/19 both kinds COMPLETE :1243/:2770), QVGA restore (image 20 QVGA-class 4090 B/21ch COMPLETE :3148/:3258), reboot boot-resolution (image 21 QVGA-class 4094 B/21ch, NVS-continued, both kinds COMPLETE :3378/:3518, base-side wire evidence). ONLY the pairwise visible-effect judgments remain — zero non-RESOLUTION settings commands all session (verifier-counted). See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Bench-proven live this round: seq=6 retried exactly 3x (labels 1/3 :73, 2/3 :81, 3/3 :92) then terminalized honestly (:126) — the bound is now label-truthful AND termination-exact; all 52 retry lines K<=3, zero attempt-ordinal (verifier-counted); the 01-15 BUSY deferral (scoped to IMAGE_WINDOW_REQUEST only, verifier-read :427-439) never needed firing (zero BUSY deferrals). WINDOWS 6 flipped fixed on this evidence |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED | Hardware UAT test 4 PASS stands; 9 manual triggers executed end-to-end session 5; NVS image-ID sequence continued across a mid-session reboot (20->21) — the G-01-6 closure class re-proven |

**Score:** 4/5 truths verified (1 present, behavior-unverified — narrowed this round to the visible-effect clause only)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer (bench wish; presentation-only) | Phase 3 backlog | Todo on disk: .planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md |
| 2 | Gallery thumb number overlay + Incomplete-badge explanatory copy | Phase 3 backlog (gallery UX) | Todos on disk (verifier-verified) |
| 3 | D-13/D-15 page-layout preferences | Phase 3 | Carried; superseded by the delivered single-page dashboard (WEB-04) |

### Required Artifacts (round #7: plans 01-13, 01-14, 01-15, 01-16)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/image_protocol.h` (01-13/01-14) | ImageChunkBody.imageKind + 0x13 6-byte-overhead comments + IMG_MANIFEST_MAX_ATTEMPTS 3 | ✓ VERIFIED | imageKind at :166 with CR-01 comment; chunk-budget comment 6-byte (:229 area); IMG_MANIFEST_MAX_ATTEMPTS=3 at :75 with rationale; all six standing pacing constants + manifest body 27 + beacon body 19 byte-identical (verifier-grepped, each exactly once) |
| `src/command_protocol.cpp` + `include/command_protocol.h` (01-13) | serialize/deserialize kind byte (write, read, validate) + factory signature | ✓ VERIFIED | serializeChunk 6-overhead arithmetic :398, kind written :418; deserializeChunk min-length :438, kind read :458, kind-validation rejecting non-THUMBNAIL/FULL_IMAGE :464-465, frame-arithmetic agreement :477; createChunkPacket(imageId, imageKind, ...) :673/:677, declaration command_protocol.h:306 exactly once |
| `src/command_sender.cpp` (01-13/01-15) | chunk bodyOverhead 6 + BUSY deferral branch + retry-ordinal label | ✓ VERIFIED | bodyOverhead=6 at :325 (manifest/beacon cases untouched); deferral branch at :427-439 — placed AFTER the terminal-state guard (:410-412) and BEFORE response storage (:441), gated NACK_BUSY AND IMAGE_WINDOW_REQUEST AND retryCount<maxRetries, returns with no storage/no decrement/no failure booked; retry label '(retry %d/%d)' at :565, zero attempt-ordinal remnants |
| `src/image_tx_manager.cpp` (01-13/01-14) | both createChunkPacket call sites stamped kind + kind in log lines; success-gated manifest transitions with bounded retry + park-and-free; two-pass overflow scan | ✓ VERIFIED | push stamps THUMBNAIL :580, window stamps entry.windowKind :860; both log lines name kind (:592/:872); pushThumbManifest success-gated (:546 reset / :550-551 increment+bound / :554 thumbnail-drop log) and announceFullManifest success-gated (:660/:663-664/:667 FULL-drop log), ANNOUNCED assigned only in the success branch; freeEntry resets :1006; 'all entries mid-service' fallback log :377; manifestAttempts appears exactly 7x |
| `include/image_tx_manager.h` (01-14) | ImageTxEntry.manifestAttempts | ✓ VERIFIED | Field present with shared-counter comment |
| `src/image_rx_manager.cpp` + `include/image_rx_manager.h` (01-13/01-15) | kind-exact onChunkFrame routing + windowRequestSeq/windowRequestInFlight + defer guards at both stall sites + cancels | ✓ VERIFIED | findTransfer(c.imageId, c.imageKind) :323, no-match drop names kind :326, 'Heal-window precedence' heuristic GONE (grep 0); windowRequestInFlight helper :400-412 (PENDING/SENT = in-flight; IDLE/ACKED/FAILED/TIMEOUT = terminal); stall guard FIRST check :160-165 (log-once latch deferSkipLoggedSeq), heal-site guard :235-240 sharing the latch; seq stored on queue success :973; cancels at window-complete advance :136-137 and both finalize paths :808-809/:839-840 |
| `src/camera_manager.cpp` (01-13) | THUMB_MAX_BYTES 8192 + stale-frame drain + fb->len guard, G-01-8 path untouched | ✓ VERIFIED | constant :12; drain log :360 between downshift and capture; guard condition fb->len > THUMB_MAX_BYTES :393 with bound in the bail log; allocatedFrameSize refs >= 3 (G-01-8 intact); two-path setFrameSize untouched by this round |
| `scripts/verify_protocol_roundtrip.mjs` (01-13) | kind-carrying mirrors + (img-i) kind-rejection negative check | ✓ VERIFIED | (img-i) check at :933/:940; harness exit 0 verifier-run at HEAD (52 PASS / 0 FAIL) |
| `01-UAT.md` / `WINDOWS.md` / `STATE.md` (01-16) | ledger flips on evidence | ✓ VERIFIED | Read in full; every discriminator quote re-checked against balloon5.log/base5.log by this verifier — text, line numbers, and counts all match; WINDOWS counts reconcile (2 open / 5 fixed / 7 total, table and JSON agree); /gsd-ship correctly still blocked |
| `balloon5.log` / `base5.log` (01-16) | retained bench evidence | ✓ VERIFIED | 6,791 / 3,658 lines at repo root (untracked, prior-session convention); every citation this verifier checked reproduces at its exact line |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| Serializer <-> deserializer <-> base dispatch | 6-byte chunk overhead agreement | serializeChunk :398 / deserializeChunk :438/:477 / bodyOverhead :325 | ✓ WIRED | All three agree on the 6-byte overhead; bench-proven (chunk framing worked all session — a mixed pair would have framed zero chunks, which also proves both boards flashed together per the 01-16 prohibition) |
| createChunkPacket callers <-> windowKind | push = THUMBNAIL, window = entry.windowKind | :580 / :860 | ✓ WIRED | Bench-proven on the wire: 'window chunk(image 14 kind 0...' balloon5.log:289; zero kind misroutes possible-and-hidden (every finalize's read-back CRC passed, zero stored-bytes mismatches) |
| windowRequestSeq <-> getCommandState | defer-aware guard correctness | helper :400-412 | ✓ WIRED | Bench: 'stall deferred' 16x + 'heal deferred' 1x fired benignly, every affected transfer still COMPLETE (base5.log:132/:283 et al.) |
| BUSY deferral <-> defer-aware guard | deferred request stays PENDING so the in-flight check extends | command_sender :427-439 | ✓ WIRED (unexercised) | Zero BUSY deferrals at session 5 — the mechanism never fired; structure verifier-read correct, class-scoped, budget-bounded |
| manifestAttempts <-> two manifest phases | reset on success + freeEntry; bounded retries | :546/:560/:660/:674/:1006 | ✓ WIRED (TX-failure path unexercised) | Zero TX failures at session 5 — the re-announce path never fired; the AIR-LOSS gap is precisely that TX-success also consumes the announce (gap 2) |
| Cancels <-> finalize/advance | post-terminal re-arm churn suppression | :136/:808/:839 | ✓ WIRED | Bench: 'Cancelled command seq=28' :778; post-terminal ignores bounded at 25 session-wide vs session-4's overrun-driven clusters |
| Delegated section#capture listener (regression) | fetch POST + pollOnce | unchanged this round | ✓ WIRED | No UI file in any round-#7 diff |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| Thumbnail payload | thumbBuffer via drain+bound-guarded capture | esp32-camera fb | Yes | ✓ FLOWING — bench: 7/7 logged thumbs genuine QQVGA 1176-1340 B, drain discriminator at every capture |
| Full-image stored bytes | kind-exact chunk routing | wire chunks -> slot bitmap -> SD | Yes | ✓ FLOWING — bench: zero stored-bytes CRC mismatches including CIF/SVGA fulls; read-back CRC complete=true |
| Full-manifest availability | announceFullManifest -> ANNOUNCED | one-shot transmit, TX-verdict-gated retry | Partial | ✗ DISCONNECTED UNDER AIR LOSS — image 15: TX-success consumed the announce, no receipt-driven recovery exists (gap 2) |
| Command survivability under load | command frames vs window chunk service | E32 half-duplex channel | Partial | ✗ DISCONNECTED UNDER STORM — seq=6 lost 4/4 while chunks flowed (gap 1) |
| Image-ID sequence (regression) | NVS namespace imgid | persisted per issue | Yes | ✓ FLOWING — IDs 14-21 sequential across the session INCLUDING the reboot (20->21) |
| Window service during camera-down | executeCommand dispatch | camera->isReady() blanket gate | No | ✗ HOLLOW — IMAGE_WINDOW_REQUEST/GET_STATUS refused while ImageTx/radio operational (gap 3, CR-04) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite (incl. PRI order gates + kind validation) | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run at HEAD) | 52 PASS / 0 FAIL, exit 0 | ✓ PASS |
| G-01-9 defect A closure vs raw logs | grep balloon5.log | 7/7 enqueues genuine QQVGA (1231/1340/1329/1332/1283/1176/1304 B, 6-7 chunks), zero thumb==full equality; 7 drain lines at :196/:240/:697/:1027/:1523/:5012/:6397 | ✓ PASS |
| G-01-9 defect B closure vs raw logs | grep base5.log balloon5.log | 0 INCOMPLETE verdicts, 0 stored-bytes CRC mismatches in either log; CIF 31/31 (:776), SVGA 80/80 (:1243) + 82/82 (:2770) COMPLETE | ✓ PASS |
| WINDOWS 6 closure vs raw logs | grep base5.log | 52 retry lines all K<=3 (43/6/3 split); zero attempt-ordinal; honest 'timeout after 3 retries' :126; 25 post-terminal ignores bounded, 'Cancelled command seq=28' :778 | ✓ PASS |
| G-01-7 lever engagement | grep base5.log balloon5.log | hold 4x :66/:377/:2382/:3356; 5 idle-targeted supersede-evictions + 1 TTL; zero BUSY deferrals; beacons 484 accepted worst-gap 2 | ✓ PASS (levers function) |
| G-01-7 series-A acceptance | grep base5.log balloon5.log | seq=6 lost 4/4 under chunk storm (queued :55, retries :73/:81/:92, timeout :126); 3 unspaced triggers -> 2 captures; image 15 full silently lost (manifest sent balloon5:274, 0 base receipts) | ✗ FAIL (gap 1 + gap 2 — honestly routed, rescoped) |
| SC-3 resolution clauses | sed raw logs | CIF re-init 6->8 :1000-1004 + image 17 both COMPLETE; SVGA re-init 8->11 :1502-1506 + images 18/19 both COMPLETE; QVGA restore :5048/:6384 + image 20 QVGA-class COMPLETE :3148/:3258; reboot: beacon reset 451->0 :3341-3343, image 21 QVGA-class both COMPLETE :3378/:3518 | ✓ PASS (resolution clause closed; visible-effect pairs remain unrun — 0 non-RESOLUTION settings commands, verifier-counted) |
| CR-04 root-cause claim (fresh review) | verifier source reads | executeCommand blanket gate :139-145; isReady==initialized (camera_manager.h:134); low-battery enableCamera(false) main_balloon.cpp:864; end() clears initialized :89; ImageTx().process() keeps running :895 | ✓ CONFIRMED REAL (unrouted critical — gap 3) |
| WR-08 root-cause claim (fresh review) | verifier source reads | nextThumbChunk++ unconditional :601; windowNextIndex++ unconditional :881 with SERVED-marking :883-895 reachable on failed tail transmit | ✓ CONFIRMED REAL (unrouted warning — gap 4) |
| WR-01 battery watch (regression) | grep 'Critical battery' balloon5.log | 0 lines; beacons 'batt=valid' on all 484 accepted lines | ✓ PASS |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists. The declared verification commands: harness verifier-run above (exit 0); builds 2/2 on record at 2b197da pre-flight with the zero-drift proof (no src/include/scripts file changed since d546ace). SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-----------|--------|----------|
| CTRL-01 | 01-02..01-07, 01-10, 01-12, 01-16 | Trigger camera capture from base station web UI | ✓ SATISFIED | Session 5: 9 dashboard CAPTURE_NOW triggers, 8 executed (1 honest air-loss timeout), images transferred and stored |
| CTRL-02 | 01-03/04/06/07, 01-12, 01-16 | Adjust all camera settings remotely | ✓ SATISFIED, one clause open | Resolution clause now FULLY hardware-proven (growth + restore + reboot). Open clause: pairwise visible-effect judgments under Test 3 (third round riding) — REQUIREMENTS.md 'Complete' remains defensible on the exercised truth with the residual ledgered |
| CTRL-03 | 01-03/04/05/07 | Manual + automatic capture modes | ✓ SATISFIED | UAT test 4 PASS; unchanged this round |
| CTRL-04 | 01-03/04/05/07 | Automatic capture fixed interval timing | ✓ SATISFIED | UAT test 4 PASS (exact cadence incl. >30 s value); unchanged |
| CTRL-06 | 01-03/04/05/07, 01-15 | Failed commands retried with timeout | ✓ SATISFIED | Bench-proven live at session 5: exactly-3 retries with truthful labels, honest terminal timeout (:126); WINDOWS 6 closed |
| PRI-02 | 01-02, 01-06, 01-15 | Retry mechanism with timeout | ✓ SATISFIED | Same evidence as CTRL-06; the 01-15 deferral class correctly scoped |
| IMG-01/IMG-04 (01-13 anchors) | 01-13 | Transmit / chunked packets | ✓ SATISFIED | Kind-tagged chunk wire bench-live; 131 windows/chunk flows serviced |
| PRI-03 (01-14 anchor) | 01-14 | Graceful degradation under bandwidth limits | ✓ SATISFIED | Bounded manifests with honest drop logs; defer-aware pass accounting degrades honestly |
| IMG-02/IMG-03 (01-13..01-16 anchors) | 01-13..01-16 | Thumbnail immediate / fulls in background | ⚠ SATISFIED WITH OPEN RESIDUALS (G-01-7 rescoped + G-01-9 defect C + CR-04/WR-08 unrouted) | Spaced/manual captures complete both kinds at every exercised size; unspaced series A: 2-of-3 triggers captured with all-run verdicts COMPLETE, but command survivability + manifest air loss remain; phase-2-mapped requirements honestly ledgered (WINDOWS 3/5 open, ship blocked) — flagged for the milestone audit as prior rounds did |

Orphaned requirements: none. All six Phase-1-mapped IDs are claimed across plans and evidenced; the additional IDs claimed by round-#7 plans are Phase-2-mapped requirements whose hardware truths surfaced in Phase 1's UAT, with flips carrying honest residual status.

### Plan Prohibition Verdicts (judgment/backstop-tier — autonomous, NON-AUTHORITATIVE)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-13 | MUST NOT change pacing constants, 0x12 manifest body, or 0x14 beacon body | PASS (machine-checkable) | All anchors grep exactly once (verifier-run); manifest 27 / beacon 19 byte-identical |
| 01-13 | MUST NOT touch beacon early-return or command-response loop order | PASS (machine-checkable) | No main_balloon.cpp diff in round #7; harness PRI-01 order gates green (verifier-run) |
| 01-13 | MUST NOT add dual-format/compatibility mode for kind-less chunks | PASS (machine-checkable) | Kind byte mandatory both directions; deserialize rejects invalid kinds — no shim |
| 01-13 | MUST NOT close G-01-9 on code-level evidence alone | PASS (backstop, non-authoritative) — flagged, human review recommended | Defects A+B flipped ONLY on bench quotes this verifier re-checked against raw logs; C honestly left open |
| 01-14 | MUST NOT change one-transmit-per-pass / beacon early-return / TX arbitration order | PASS (machine-checkable) | Diff-hunk verified; harness green |
| 01-14 | MUST NOT alter evictionClassOf ranking or sweepExpiredEntries TTL path | PASS (machine-checkable) | Diff-hunk analysis: evictionClassOf body untouched (matches are the overflow-scan call move + a comment); sweepExpiredEntries only a comment mention; the one eviction-adjacent hunk in evictEntriesOlderThan is the documented whitespace-only predicate join (verbatim-diffed by this verifier — identical tokens) |
| 01-14 | MUST NOT retry manifests unboundedly | PASS (machine-checkable) | IMG_MANIFEST_MAX_ATTEMPTS 3 hard bound with buffer-free at exhaustion (verifier-read :551/:664) |
| 01-14 | MUST NOT close defect C on code-level evidence alone | PASS (backstop, non-authoritative) | The TX-failure re-announce was never exercised (zero TX failures); air-loss honestly kept open |
| 01-15 | MUST NOT change D-24 bounds or any pacing constant | PASS (machine-checkable) | PASSES 3 / STALL 8000 / SETTLE 500 each exactly once |
| 01-15 | MUST NOT extend defer-skip to terminal-state requests | PASS (backstop, non-authoritative) | windowRequestInFlight checks PENDING/SENT only (verifier-read :400-412); ACKED/FAILED/TIMEOUT/IDLE charge passes on the unchanged clock |
| 01-15 | MUST NOT apply NACK_BUSY deferral beyond IMAGE_WINDOW_REQUEST | PASS (machine-checkable) | Class gate verifier-read :427-429; all other classes fall through to terminal FAILED unchanged |
| 01-15/01-16 | MUST NOT fabricate transfer or config success | PASS (backstop, non-authoritative) — flagged, human review recommended | Every flip cites bench lines this verifier reproduced; series-A failure honestly kept open; image 15 loss surfaced, not absorbed |
| 01-16 | MUST NOT flash only one board | PASS (machine-checkable, indirect) | Chunk framing worked all session on the new 6-byte layout — a mixed pair would frame zero chunks |
| 01-16 | MUST NOT fix newly observed bench defects inside the plan | PASS (backstop) | CR-04/WR-08 routed to the review, no in-plan code fix; 02142e1 is the declared out-of-plan GPS-config operator commit (sensor_pins.h only — image-path code untouched, verifier-confirmed in diff; internally consistent per review IN-09) |

### Anti-Patterns Found

Zero TBD/FIXME/XXX in all nine files modified by round #7 (verifier grep clean); zero TODO/HACK/PLACEHOLDER. Items below are from the fresh review (01-REVIEW.md d546ace: 1 Critical / 2 Warning / 9 Info) — this verifier re-derived the Critical and the new Warning in source; both confirmed real at HEAD. CR-04 and WR-08 are NOT in the WINDOWS ledger — they need routing.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/command_handler.cpp | :139-145 (with main_balloon.cpp:858-867, camera_manager.h:134) | CR-04: blanket camera-ready gate refuses IMAGE_WINDOW_REQUEST/GET_STATUS during low-battery camera-down while ImageTx/radio live — announced fulls unretrievable until power loss/TTL destroys them; status poll fails while beaconing | 🛑 Gap (UNROUTED — gap 3; pre-existing, newly-named) | Scope isReady() to camera-touching handlers; route in WINDOWS + next round |
| src/image_tx_manager.cpp | :601, :881-895 | WR-08: cursors advance on failed transmit; failed tail chunk can mark entry SERVED (preferred eviction candidate) with bytes never offered | 🛑 Gap (UNROUTED — gap 4; warning-class, unexercised at session 5) | Gate advance + SERVED on transmit success with a small same-index retry bound |
| src/command_handler.cpp + src/auto_capture.cpp | :200-237 / :175-178 | WR-03 (carried): manual capture never advances the auto-capture baseline — interval capture can fire moments after a manual one | ⚠️ Warning | Fix = markCaptureBaseline() after successful manual capture; fold into next round |
| src/image_rx_manager.cpp | :549-565 | IN-08 (new, carried): re-manifest restart zeroes window context without cancelling the still-tracked request | ℹ️ Info | Harmless today (last-arm-wins); cancel on restart like finalize does |
| Carried Info items | — | IN-02..IN-07 (health-check false warnings, dead previousMode, mode-before-verify, unused VLA method, dead findOldestCommand, stale 17-byte comment) | ℹ️ Info | None in a Phase-1 must-have domain; several overlap the pending /gsd-secure-phase 1 gate |
| include/sensor_pins.h | :35-38 | IN-09: the out-of-plan 02142e1 GPS pin/baud change — internally consistent, hardware-proven config; test_minimal_hardware.cpp pins stale (out of default build) | ℹ️ Info | No action required |

### SUMMARY vs Reality

1. Every round-#7 code claim reproduces on this verifier's own reads at the plan-named sites (details in Required Artifacts); commits 70543c5/394a231/90300b8/bb2c517/ae16991/7dd5dab/45c8b80/8adc0bc/37f405c/02142e1/d546ace all exist with exactly the claimed file sets.
2. Every load-bearing session-5 bench quote is verbatim-accurate against balloon5.log/base5.log including line numbers and counts: log lengths (6,791/3,658); enqueues and drains at :198..:6399/:196..:6397; seq=6 queued :55, retries :73/:81/:92, timeout :126; image-15 manifest :274 with 0 receipts; holds :66/:377/:2382/:3356; retry tallies 52 with 43/6/3 split; stall-deferred 16 / heal-deferred 1 at :283; beacon reset :3341-3343; image-21 completes :3378/:3518; QVGA restore :5048/:6384 + :3148/:3258; CIF/SVGA re-inits :1000-1004/:1502-1506 + completes :577/:776/:1243/:2770; post-terminal 25/14 with cancel :778; BUSY deferrals 0; beacons 484; HTTP 404 banners 48; balloon log ends at idle telemetry :6777. Negative claims (0 INCOMPLETE, 0 CRC mismatch, 0 byte-equality thumbs) also verifier-grepped true.
3. The closure and non-closure discipline held exactly: A+B flipped on mechanism-specific bench evidence with unexercised paths named; defect C kept open with the air-loss class precisely bounded; series A kept open on the two new axes with named levers; the reboot clause judged base-side with the console-detach gap recorded rather than papered over.
4. Two non-load-bearing tally nuances: (a) 01-13-SUMMARY claims "53 checks green" — the harness at HEAD prints 52 PASS lines (exit 0; the orchestrator's own run also counted 52); (b) 01-16-SUMMARY's self-check cites commit 8b7313b for the summary file — the actual commit is 37f405c (amended; contains the SUMMARY + ROADMAP update). Neither affects any verdict.
5. STATE.md, WINDOWS.md (2 open / 5 fixed / 7 total, table+JSON+frontmatter consistent), 01-UAT.md, and the three todos are mutually consistent and match the git record.
6. The fresh review (d546ace) verified all round-#7 fixes at root — consistent with this verifier's independent source reads; its two NEW findings (CR-04/WR-08) were treated as unverified claims until re-derived here: both confirmed real, both currently unrouted (this report routes them as gaps 3 and 4).

### Gaps Summary

Four structured gaps — two honestly-ledgered residuals carried forward (narrowed), two newly-confirmed unrouted findings from the fresh review:

1. **G-01-7 (rescoped) — series-A acceptance.** All session-4 failure mechanisms eliminated (zero INCOMPLETE/CRC/deferral/mid-service-eviction, holds 4x, beacons held), but 3 unspaced triggers produced 2 captures: the third trigger's command frame lost all 4 transmissions under the concurrent chunk storm (honest timeout). Named lever: command air-priority or burst pacing during window service.
2. **G-01-9 defect C — manifest air loss.** The TX-verdict-gated re-announce cannot see a TX-success/air-lost manifest: image 15's full silently dropped exactly as session 4's image 8. Named lever: receipt-driven recovery (base-side nudge on thumb-COMPLETE-without-FULL-manifest, or balloon-side periodic bounded re-announce while head-and-idle).
3. **CR-04 (NEW, unrouted critical) — camera-ready gate blocks window service and status.** executeCommand refuses EVERY command when the camera is down, including IMAGE_WINDOW_REQUEST and GET_STATUS which need only ImageTx buffers and the radio — announced fulls become unretrievable precisely when an operator must recover them before power loss. Pre-existing; verifier-confirmed at source; needs WINDOWS routing + the dispatch-scoped fix.
4. **WR-08 (NEW, unrouted warning) — cursors advance on failed transmit.** pushThumbChunk/serviceWindowChunk skip failed chunks permanently for the pass and can mark a window SERVED whose final chunk never left the balloon. Unexercised at session 5 (zero TX failures); compounds gap 1's storm behavior.

Everything else is green: 4/5 SCs verified (SC-3 narrowed to its final visible-effect clause), all round-#7 artifacts substantive and wired, all prohibitions held (scope boundaries diff-verified), harness exit 0 verifier-run, zero source drift at HEAD, no debt markers, all prior closures regression-intact, pacing invariants held, PRI-01 held at bench.

The phase goal — bidirectional LoRa command/control with ACK, retry, manual + auto capture — is achieved and hardware-proven on current firmware, now with truthful retry ordinals and fully-proven resolution-path behavior. What remains: two narrow image-reliability residuals in the Phase-2 requirement domain, two unrouted review findings (one critical), the SC-3 visible-effect pairs (third round riding), and the deliberately-pending security gate.

Recommended next step: `/gsd-plan-phase 1 --gaps` (one narrow round: receipt-driven manifest recovery + command survivability/pacing + CR-04 dispatch-scoping + WR-08 cursor gating, with the SC-3 visible-effect pairs and the WR-03 baseline ride-alongs under Test 3), then `/gsd-secure-phase 1` — the separate remaining phase-close gate (its scope should absorb the review's IN-class CommandSender/E32 findings where they overlap).

---

_Verified: 2026-08-25T03:18:15Z_
_Verifier: Claude (gsd-verifier)_

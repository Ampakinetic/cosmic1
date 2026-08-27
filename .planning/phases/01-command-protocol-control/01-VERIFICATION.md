---
phase: 01-command-protocol-control
verified: 2026-08-28T09:00:00Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3, narrowed to its final clause for the SIXTH consecutive round: every resolution-class clause is bench-proven (session 5, no regression surface since — camera_manager.cpp untouched by rounds #8/#10); only the pairwise visible-effect judgments (brightness -2 vs +2 + one other class) remain operator-unjudged. Session 7 issued zero non-RESOLUTION settings commands before the D1 crashes aborted it (verifier-grepped balloon.log/base.log)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "Review round #9 findings routed (previous gap 4): CLOSED — WINDOWS entries 10-14 exist (table AND JSON, counts reconciled), each carrying its confirmed-finding description and evidence class; all five companion code fixes (01-23, 3d0aaea) re-derived at HEAD source by this verifier: WR-01 ownership guard (sd_storage.cpp:316 'm.storedTodSd = (*handleId == meta.imageId) && (*persistedBytes > 0)'), WR-02 second-frame drop guard (command_handler.cpp:887-893, named log), review-WR-03 inter-byte resync in BOTH framers (command_handler.cpp:820-823 + command_sender.cpp:309-312, CMD_FRAME_INTERBYTE_MS), WR-04 RFC-8259 jsonEscape (main_basestation.cpp:3137-3155: \\u00XX below 0x20, >=0x80 dropped), WR-05 CRITICAL-branch camera disable (main_balloon.cpp:858-867 inside !isEmergencyActive(), distinct SYS_INFO). Nuance: the fix-all disposition was AUTO-SELECTED at the 01-23 checkpoint under workflow.auto_advance — honestly recorded in 01-23-SUMMARY with an explicit request for operator confirmation at end-of-phase (surfaced as a human item below)"
  gaps_remaining:
    - "G-01-7 series-A burst full-delivery: STILL OPEN, now UNJUDGEABLE — round #10 shipped both named levers (01-21 balloon receipt-evidence re-announce/eviction + 01-22 base full-arm deadline; all mechanisms verifier-read at source) but bench session #7 FAILED on the D1 crash regression before any burst ran: 0/6 verdicts, zero round-#10 discriminators engaged (verifier-grepped: 0 'budget re-armed', 0 'full-pull activation deadline reached'; the only 3 're-announce held' lines are D2 boot artifacts at :135/:504/:652, each seconds after 'System ready')"
    - "SC-3 visible-effect pairs: SIXTH round riding (session aborted before any settings command)"
    - "WR-03 cadence discriminator: STILL unrun (zero AUTO_CAPTURE_ENABLE)"
  regressions:
    - "D1 CRASH REGRESSION (NEW, G-01-10 / WINDOWS 15, blocker): on round-#10 firmware the balloon hard-crashed twice during series A — crash 1 (balloon.log:301-302 i2cWrite ESP_ERR_INVALID_STATE + 'Core 1 panic'ed (Interrupt wdt timeout on CPU1)', second panic IDLE1 stack canary :313-314, rst:0xc) and crash 2 (:526-528 TG0WDT_SYS_RST with no panic, seconds after image 36's chunk 1/9). Session 6 ran the IDENTICAL unspaced pattern clean on pre-round-#10 firmware (3/3 ACKs, 12 images, zero resets), so the regression is from 01-21/01-22/01-23. Root cause NOT established; both session-7 crashes were first-post-boot capture attempts. Verifier re-read all cited lines in the retained logs — they reproduce verbatim"
gaps:
  - truth: "The balloon survives the start of post-capture radio push (G-01-10 / WINDOWS 15): a capture's enqueue/manifest/chunk/beacon sequence never hard-resets the firmware"
    status: failed
    reason: "Bench session #7 (01-24, round-#10 firmware at ELF SHA256 3d2b351b4, zero source drift 3d0aaea..bench): the balloon hard-crashed at both first-post-boot capture attempts (images 35/36) — crash 1 balloon.log:301-302 + :313-314 (I2C INVALID_STATE -> INT_WDT CPU1 -> IDLE1 stack canary, rst:0xc), crash 2 :526-528 (TG0WDT_SYS_RST, no panic). Downstream: 0/6 series-A verdicts (both buffers died in resets), forbidden CAPTURE_NOW terminals base.log:141/:143, every image-36 window request rejected (balloon.log:670/:673/:728/:785/:845), base NACK_INVALID retries (base.log:167/:199/:236/:273) — all verifier-read in the raw logs. A pattern session 6 ran clean = round-#10 regression; root cause NOT established"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "leading suspect (NOT established): 01-21 pushPending busy-hold gate + budget re-arm fire at the exact crash phase"
      - path: "src/command_handler.cpp"
        issue: "suspect (NOT established): 01-23 hasCommand guard + framer inter-byte resync (receive path active during push)"
      - path: "src/command_sender.cpp"
        issue: "suspect (NOT established): 01-23 framer resync twin (base side)"
    missing:
      - "Debug round: addr2line the crash-1 backtrace against the deployed ELF (SHA256 3d2b351b4, retained in .pio/build), reconcile the I2C INVALID_STATE + INT_WDT CPU1 + TG0WDT signatures into one root cause, fix"
      - "Establish crash determinism (both session-7 crashes were first-post-boot captures) and whether spaced captures avoid it (interim discipline: avoid unspaced capture bursts)"
      - "D2 fix rides the same round (WINDOWS 16 / G-01-11): make the zero-initialized lastInboundWindowRequestMs inert (receipt-ever flag or begin()-time stamp) so 're-announce held' only fires on genuine inbound traffic — verifier confirmed the mechanism at source (image_tx_manager.cpp:36/:533) and the 3 spurious boot fires in balloon.log"
  - truth: "G-01-7 series-A acceptance (01-24-PLAN must-have, backstop): 3 back-to-back unspaced captures on round-#10 firmware — 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls; the round's discriminators prove the levers engaged"
    status: failed
    reason: "UNJUDGEABLE-NOT-DISPROVEN: the session aborted on D1 before any real burst. 2 ACKs / 2 captures happened (images 35/36, balloon.log:292/:509), then crashes; 0/6 verdicts; the forbidden zero-timeouts clause failed (base.log:141/:143). Zero round-#10 discriminators engaged (verifier-grepped: 0 budget re-arms, 0 deadline releases, 0 receipt-evidenced class-5 evictions). The levers ARE in and wired — this verifier read every site: 01-21 global liveness stamp BEFORE kind validation (image_tx_manager.cpp:869 vs :882), busy-hold consuming nothing (:533-539), per-entry re-arm gated ANNOUNCED && !fullWindowEverArmed && attempts>0 (:936-941), eviction class 5 on windowEverArmed || lastWindowRequestMs != 0 (:227); 01-22 FIFO scan hoisted + wrap-safe 20 s deadline + named release log (image_rx_manager.cpp:898-948), manifestArrivedMs stamped on BOTH slot-fill paths (:569). Bench-unvalidated, not disproven"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "levers present and wired (verifier-read); no bench validation exists — D1 precedes any series-A re-run"
      - path: "src/image_rx_manager.cpp"
        issue: "deadline lever present and wired (verifier-read); unvalidated at bench"
    missing:
      - "D1 debug round + fix first (gap 1), then the series-A re-run: 3 unspaced captures, 6/6 COMPLETE, zero timeouts, with the discriminator lines ('re-announce budget re-armed', 'full-pull activation deadline reached', receipt-evidenced class-5 labels) proving lever engagement or naming the next mechanism (burst-admission depth is the named fallback)"
  - truth: "The bench-moment clauses (01-24-PLAN must-haves, backstop): SC-3 visible-effect pairs judged; WR-03 cadence discriminator quoted; CIF-cycle + QVGA-restore clauses exercised; dashboard operator-LOOKED glance"
    status: partial
    reason: "NONE ran — sixth consecutive round riding: the session aborted before any settings or auto-capture command (verifier-grepped zero SET_* and zero AUTO_CAPTURE lines in balloon.log/base.log); the dashboard glance is recorded baseline-only in the Test 3 note. All are honestly recorded in 01-UAT.md (Test 3 note + Test 4 note + G-01-7 update) and routed in STATE.md Next Steps to the post-D1 bench moment"
    artifacts:
      - path: ".planning/phases/01-command-protocol-control/01-UAT.md"
        issue: "Test 3 stays 'issue'; the visible-effect-pair clause is its last open settings clause (CIF/QVGA-restore clauses also riding again — session-5-proven, camera_manager.cpp untouched by rounds #8/#10, but the 01-24 re-exercise never ran)"
    missing:
      - "Post-D1 bench session: SC-3 pairs (brightness -2/+2 plus one other class at fixed QVGA), WR-03 cadence discriminator (AUTO_CAPTURE_ENABLE 20 s + manual capture, quote both 'Captured image ID' timestamps — also discharges the SC-5 coincidental-reliance advisory), CIF cycle + QVGA restore with zero FB-OVF, dashboard LOOK glance incl. the d8ba14e gallery-detail fields and all UI-SPEC lifts"
  - truth: "Confirmed review findings are routed — every confirmed finding from the fresh review (7d96a98, 0 Critical / 3 Warnings / 11 Info) lives in the WINDOWS ledger or a todo before phase completion (round-#7 house convention, encoded in 01-23's own must-have)"
    status: failed
    reason: "All three warnings re-derived and CONFIRMED at HEAD source by this verifier, NONE routed — WINDOWS.md has no entry for any of them (ledger holds exactly 16 entries, none referencing 7d96a98), no todo carries them, STATE.md's advisory line references the OLD review (fa921a7). Confirmed: review-WR-01 ps_malloc failure for the full buffer returns early at image_tx_manager.cpp:282 BEFORE the thumbnail branch (:294) — PSRAM exhaustion drops the cheapest highest-value payload the degradation ladder was designed to keep (inconsistent with :266 and :306); review-WR-02 main_balloon.cpp:574-576 fires 'Camera system health check failed' on every HEALTHY active camera (the negation was lost when the check was stubbed; an absent camera produces no warning at all — carried across two review cycles); review-WR-03 main_basestation.cpp:2207-2210 truncates the uint32 commandsAcked to uint16 for the ackedAtLastPoll comparison — each 65536-ACK wrap mis-orders the LED-truth refresh. None blocks the phase goal; all three are real. The 11 Info items live in the review file itself (prior-cycle convention)"
    artifacts:
      - path: ".planning/WINDOWS.md"
      - issue: "no entries for the 7d96a98 review warnings (ledger stands at 3 open / 13 fixed / 16 total)"
    missing:
      - "Route review-WR-01..03 as WINDOWS entries or todos with evidence classes (natural companions to the D1 debug round — all three sites are in files that round will touch)"
      - "Decision point for the human: route-and-fix-now vs route-and-waive-with-reason per finding"
deferred:
  - truth: "Full-resolution gallery image viewer (operator bench wish; presentation-only)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md (verifier-verified on disk)"
  - truth: "Gallery thumb number overlay + Incomplete-badge explanatory copy (operator bench friction)"
    addressed_in: "Phase 3 backlog (gallery UX)"
    evidence: ".planning/todos/pending/2026-08-24-gallery-thumb-number-overlay.md and 2026-08-24-incomplete-badge-on-renderable-images.md (verifier-verified on disk)"
  - truth: "Thumb-first image delivery + antenna-pointing overlay (operator requests, captured at 898fcc6)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-27-thumb-first-image-delivery-with-on-demand-full-retrieval.md and 2026-08-27-antenna-pointing-overlay-for-base-web-ui.md (verifier-verified on disk)"
behavior_unverified_items:
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3 — pairwise visible-effect clause only)"
    test: "At fixed QVGA, adjust ONE setting between two captures and compare the pair (brightness -2 vs +2, plus at least one other class: contrast, saturation, or quality 10 vs 30)"
    expected: "Visible pairwise differences per exercised setting, recorded per pair"
    why_human: "Sensor visible-effect acceptance needs the physical camera and operator judgment — SIX consecutive bench rounds have issued zero non-RESOLUTION settings commands (session-7 queue mix verifier-grepped; the session aborted on D1 before any settings command). The resolution clause no longer rides: growth/restore/reboot were bench-proven at session 5 and neither round #8 nor round #10 touched camera_manager.cpp (verifier diff-verified via commit scopes), so no regression surface exists for those closures"
coincidental_reliance_items:
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5)"
    reason: "fixture-only"
    harden: "SC-5's auto-capture half rests on Test 4's bench PASS, which predates the last two auto_capture.cpp changes (02-04 event triggers; 01-19 baseline advance) — non-interference is established by code reading, not by a bench run on current HEAD (zero AUTO_CAPTURE_ENABLE at sessions 6 AND 7). The WR-03 cadence discriminator at the post-D1 bench moment is precisely the hardening step — already routed in STATE.md Next Steps. Session 7 additionally re-proved the manual half only through capture (2 captures executed); both crashed at push, so no end-to-end manual evidence exists on HEAD firmware either until the post-D1 session"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #9, after gap closure 01-21..01-24)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-28T09:00:00Z
**Status:** gaps_found
**Re-verification:** Yes — #9, after gap-closure round #10 (plans 01-21/01-22/01-23 code + 01-24 bench; commits 866087b/84f8aea/3d0aaea code, eae8bb6/8ea79c1 routing, bb5932e failed-bench ledger record, 88c9257/967373d docs, 7d96a98 fresh review — all verifier-confirmed in git log; zero source drift HEAD..working-tree in src/ and include/)

## Goal Achievement

Round #10 closed the ONE gap it could close on code evidence — the round-#9 review findings are routed (WINDOWS 10-14) with all five companion fixes verifier-re-derived at HEAD source — and shipped the two G-01-7 burst full-delivery levers the round was built around (01-21 receipt-evidence re-announce + eviction ranking on the balloon, 01-22 full-arm deadline on the base; every behavioral site read by this verifier, correctly wired, zero wire-format bytes moved, harness green). The routing half of the prior verification's gap 4 is done.

The bench half FAILED, honestly. Session #7 (the round's acceptance surface) never got to judge the levers: the balloon HARD-CRASHED at both first-post-boot capture attempts — I2C INVALID_STATE escalating to an INT_WDT CPU1 panic with an IDLE1 stack-canary second panic, then a silent TG0WDT reset after the reboot — 0/6 series-A verdicts, both images' buffers dead in the resets, the two crash-window CAPTURE_NOWs terminalized at the forbidden 'timeout after 3 retries'. Session 6 ran the identical unspaced pattern clean on pre-round-#10 firmware, so this is a REGRESSION from this round's own changes (01-21/01-22/01-23 are the named suspects; root cause NOT established — the backtrace is undecoded, ELF retained). This is the phase's most serious finding: there is currently NO clean bench evidence on HEAD firmware at all, and the crash sits in the capture-push path the phase goal is about. It is routed as a blocker (G-01-10 / WINDOWS 15) with a debug round named ahead of any bench re-run, and the ledgers flipped NOTHING on the failed session — exactly the honesty this project's convention demands.

What also did NOT happen, again: the SC-3 visible-effect pairs (sixth round riding — the session aborted before any settings command), the WR-03 cadence discriminator, the CIF/QVGA-restore re-exercise, and the operator-LOOKED dashboard glance. All routed to the post-D1 bench moment.

New since the last verification: a fresh adversarial review (7d96a98, 0 Critical / 3 Warnings / 11 Info). This verifier re-derived ALL three warnings at HEAD source — all REAL (PSRAM-alloc failure drops the thumbnail the degradation ladder was designed to keep; the inverted camera health check that warns on every healthy boot; the uint16 ACK-counter truncation in the LED-truth path). None blocks the phase goal; none is routed — WINDOWS, todos, and STATE carry no entry for any of them. Per this project's own convention (round #7 precedent, and 01-23's own must-have encoding it), unrouted confirmed findings are a verification gap. This is the one gap the ledgers do NOT carry.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; the round's own 01-24-PLAN truths are assessed separately below).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression check: round #10's commit scopes touch no UI file (866087b: image_tx trio; 84f8aea: image_rx pair; 3d0aaea: command/sd/power files + WINDOWS — verifier read the stats); the last UI change remains d8ba14e (gallery detail), verifier-checked at #8. Multi-session operator evidence stands. Session 7's glance was baseline-only (recorded honestly) — the LOOK glance rides the post-D1 bench |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED (regression caveat: D1) | Even the FAILED session shows the ACK path live on HEAD firmware: both crash-window CAPTURE_NOWs were ACKed and executed ('CommandHandler: Command CAPTURE_NOW - SUCCESS' balloon.log:295; 'Captured image ID 35' :292, image 36 :509), window requests serviced pre-crash, NACK_INVALID returned honestly for post-reset orphans. Harness exit 0 all PASS (verifier-run). CAVEAT: command reliability under capture-push contention on HEAD is exactly what D1 degrades (2 forbidden timeouts base.log:141/:143) — tracked by the D1 gap, not hidden here |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | NARROWED TO ITS FINAL CLAUSE, unchanged from #8: resolution-class clauses bench-proven session 5 (growth/restore/reboot) with no regression surface since (camera_manager.cpp absent from every round-#8 AND round-#10 commit — verifier diff-verified). ONLY the pairwise visible-effect judgments remain — zero non-RESOLUTION settings commands for the SIXTH consecutive round (session aborted on D1 before any settings command; verifier-grepped). See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Session 7 incidentally re-proved the machinery ON HEAD firmware: seq 5/6 terminalized honestly at exactly 'timeout after 3 retries' (base.log:141/:143 — verifier-read) with the queue panel the operator was watching; zero beyond-bound labels (regression check: 01-15 label code untouched by round #10). The timeouts themselves are D1's symptom, not a retry-machinery defect |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED (coincidental-reliance) | 2 manual triggers executed through capture on HEAD (images 35/36 — the ACK/capture half of the loop), though both crashed at push (D1); the full manual-trigger evidence (queue->ACK->capture->transfer) stands on sessions 4/5/6. Test 4 PASS stands (interval cadence, disable semantics, shared ID sequence). ADVISORY persists: the auto-capture half's bench evidence predates 01-19 and now 02-04-era changes; non-interference code-proven only; the WR-03 discriminator has still never run on current HEAD (zero AUTO_CAPTURE_ENABLE at sessions 6 AND 7). See coincidental_reliance_items |

**Score:** 4/5 truths verified (1 present, behavior-unverified — the visible-effect clause, sixth round riding)

### 01-24 Round Truths (the round's own acceptance)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Series-A: 3 unspaced captures, 3 ACKs, 3 captures, 6/6 COMPLETE, zero timeouts, discriminators prove lever engagement | ✗ FAILED (unjudgeable) | 2 ACKs / 2 captures then D1 crashes; 0/6 verdicts; forbidden timeouts :141/:143; zero discriminators engaged (verifier-grepped 0/0/0; the 3 're-announce held' lines are D2 boot artifacts :135/:504/:652, each seconds after 'System ready' :131/:500/:648 — verifier-read). Levers wired at source, bench-unvalidated |
| 2 | G-01-9-class honesty: zero silently absent fulls | ✓ VERIFIED (vacuously) | Both images' losses are NAMED at both consoles (crash resets + post-reset reject/NACK lines quoted in G-01-10) — nothing silently absent; the 01-17 re-announce machinery was never reached |
| 3 | SC-3 visible-effect pairs judged | ✗ NOT RUN | Sixth round riding; zero settings commands (verifier-grepped); routed to the post-D1 bench |
| 4 | WR-03 cadence discriminator quoted | ✗ NOT RUN | Zero AUTO_CAPTURE_ENABLE; routed |
| 5 | CIF-cycle + QVGA-restore clauses exercised | ✗ NOT RUN | Rode again (session-5-proven, no regression surface — camera_manager.cpp untouched); routed |
| 6 | Dashboard glance operator-LOOKED | ✗ NOT RUN | Baseline-only per the Test 3 note (honest); the d8ba14e gallery fields remain unlooked-at — routed |
| 7 | WINDOWS 10-14 end in decided terminal states with evidence classes honored | ✓ VERIFIED | All five fixed with unexercised-at-bench recorded BY NAME in each entry's reason (verifier read all five); code fixes re-derived at source (see re_verification.gaps_closed). NUANCE: the fix-all disposition was AUTO-SELECTED under YOLO auto_advance at the 01-23 checkpoint — recorded verbatim in 01-23-SUMMARY with an explicit request for end-of-phase operator confirmation (human item below) |
| 8-15 | UI-SPEC lifts (7 explicit + LED-truth backstop) | ⚠️ UNVERIFIED-BY-LOG | No dashboard session occurred; nothing observed either way — no pass claimed, all ride the post-D1 bench glance |
| 16 | Ledger honesty: flips only on operator-observed evidence; failed series left open with residual named | ✓ VERIFIED | ZERO flips on the failed session (verifier: WINDOWS open ids exactly {3,15,16}, 3/13/16 reconciles across front matter / table / JSON; REQUIREMENTS untouched — requirements-completed: [] honored); G-01-7 updated UNJUDGEABLE-not-disproven; D1/D2 routed with suspects, debug path, and discriminator named |

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer | Phase 3 backlog | Todo on disk (verifier-verified) |
| 2 | Gallery thumb number overlay + Incomplete-badge copy | Phase 3 backlog | Todos on disk (verifier-verified) |
| 3 | Thumb-first image delivery + antenna-pointing overlay | Phase 3 backlog | Todos on disk (verifier-verified, captured at 898fcc6) |

Step 9b check: no later milestone phase exists to defer the riding clauses to — Phases 2 and 3 are already executed and Phase 1 closes last. The D1 debug round + post-D1 bench moment are Phase-1 closeout activities routed in STATE.md Next Steps, not later-roadmap deferrals, so they remain gaps.

### Required Artifacts (round #10: plans 01-21, 01-22, 01-23, 01-24)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/image_tx_manager.cpp` + `include/image_tx_manager.h` + `include/image_protocol.h` (01-21) | receipt-informed re-announce (busy-hold + per-entry re-arm), receipt-evidence eviction ranking, three named discriminator lines | ✓ VERIFIED (bench-unvalidated) | IMG_FULL_REANNOUNCE_BUSY_MS=15000 (image_protocol.h:116); global liveness stamp BEFORE kind validation (cpp:869, trust boundary documented); busy-hold consuming nothing (cpp:533-539, latch :534-536); per-entry stamp + re-arm gated exactly ANNOUNCED && !fullWindowEverArmed && attempts>0 (:935-941, named log); evictionClassOf class 5 on windowEverArmed \|\| lastWindowRequestMs != 0 (:226-227) with class-3 label reserved for never-requested (:390); freeEntry reset (:1243); IMG_FULL_REANNOUNCE_MAX terminal path intact (:833-836). Commit scope exactly 3 files (+106/-5). WIRED but bench-unvalidated (D1) — honestly recorded in WINDOWS 3 |
| `src/image_rx_manager.cpp` + `include/image_rx_manager.h` (01-22) | deadline-bounded serialization hold, manifestArrivedMs clock, named release log | ✓ VERIFIED (bench-unvalidated) | IMG_FULL_ARM_DEADLINE_MS=20000 (header:57); FIFO scan hoisted before the hold branch (cpp:898-906); wrap-safe deadline check (:930-931); one-shot hold log intact inside the deadline (:933-937); named release log (:941); activation tail unchanged (no new D-21 trigger); manifestArrivedMs stamped on BOTH slot-fill paths incl. re-manifest restart (:569). Commit scope exactly 2 files |
| 01-23 fix files (5 fixes, 8 source files + WINDOWS) | ownership guard, second-frame drop, both framer resyncs, jsonEscape, CRITICAL camera disable | ✓ VERIFIED | Each fix re-derived at source (line-cited in re_verification.gaps_closed); CMD_FRAME_INTERBYTE_MS in command_protocol.h; harness exit 0 all PASS (verifier-run, includes truncated-stream and bogus-length reset checks). All five WIRED-unexercised at bench, recorded BY NAME in WINDOWS 10-14 |
| `balloon.log` / `base.log` (01-24, retained session evidence) | every citation locatable | ✓ VERIFIED | 1,125 / 429 lines exact (matches the summary). Verifier re-read every load-bearing citation: crash-1 lines (:295-:315 area incl. i2cWrite :301, INT_WDT panic :302, EXCCAUSE 0x6, IDLE1 canary :313-314), crash-2 (:517-:528 chunk 1/9, beacon start, rst:0x7), forbidden terminals (base.log:141/:143), D2 boot fires (:135/:504/:652 vs 'System ready' :131/:500/:648), captures (:292/:509), discriminator zero-counts (0 budget re-arms, 0 deadline releases) — all reproduce |
| `01-UAT.md` / `WINDOWS.md` / `STATE.md` (01-24) | honest failed-session record, zero flips, D1/D2 routed | ✓ VERIFIED | Read in full: G-01-10 (blocker) and G-01-11 (minor) carry truth/status/reason/root_cause/artifacts/missing/started; G-01-7 session-7 update UNJUDGEABLE with D2 evidence-pollution note; WINDOWS 15/16 open in table AND JSON, counts 3/13/16 reconcile exactly; STATE.md Next Steps item 1 is the D1 debug round with suspects, ELF SHA, addr2line path, and interim discipline |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| handleWindowRequest (any inbound request) | lastInboundWindowRequestMs stamp | busy-hold gate in pushPending | ✓ WIRED (bench-unvalidated) | Stamp before kind validation (cpp:869 <- :882); gate at :533 before findReannounceCandidate (:550); session 7 saw only the D2 boot-epoch artifact of this link, never genuine traffic through it |
| handleWindowRequest (matched target) | lastWindowRequestMs + reannounceAttempts reset | evictionClassOf protected ranking | ✓ WIRED (bench-unvalidated) | :935-941 -> :227; zero re-arms logged at session 7 (crashes preceded) |
| onManifestFrame slot fill | manifestArrivedMs | activateNextPull deadline release | ✓ WIRED (bench-unvalidated) | :569 -> :930-:943; zero deadline-reached lines at session 7 |
| Previously-verified mechanisms (quiet gate, camera gate, baseline) | unchanged through round #10 | regression greps | ✓ WIRED | CMD_TX_CHANNEL_QUIET_MS=750 / HOLD_MAX=30000 + channelQuietForTx/canTransmitNow + both call sites (:237/:280) intact in command_sender.cpp; commandRequiresCamera scoped gate intact (:141/:177) in command_handler.cpp; markCaptureBaseline wired exactly once (:268 -> auto_capture.cpp:128) |
| Framer resync <-> quiet-gate latch | independent axes | resync keys on per-byte timing, latch on completed frame types | ✓ WIRED | command_sender.cpp:309-312 does not touch the 0x13 latch (:396-:403 region unchanged); harness truncated-stream checks pass |

### Data-Flow Trace (Level 4)

Not applicable this round — plans 01-21/01-22 are scheduling changes with zero wire-format bytes; 01-23's fixes are guard/escape/logic paths with no new rendered data; 01-24 is a bench/ledger plan. (The jsonEscape output feeds /api/state's ssid field — verified present at the emit site main_basestation.cpp:3339.)

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Wire-format regression harness | `node scripts/verify_protocol_roundtrip.mjs` | exit 0, all checks PASS (verifier-run at HEAD 7d96a98) | ✓ PASS |
| Session-7 crash citations | sed exact lines in balloon.log | i2c :301, INT_WDT panic :302, canary :313-314, rst:0x7 :526-528 all verbatim | ✓ PASS |
| Forbidden-terminal / D2 / capture citations | sed base.log:141/:143; balloon.log:135/:504/:652/:292/:509 | all reproduce; discriminator zero-counts confirmed (0 budget re-arms, 0 deadline releases) | ✓ PASS |
| Source drift | `git diff --stat HEAD -- src/ include/` | empty — working tree touches only .pio checksum, .planning docs, untracked logs | ✓ PASS |
| Round-#10 commit scopes | `git show --stat` 866087b / 84f8aea / 3d0aaea | exactly the plan's files; UI and camera_manager untouched | ✓ PASS |
| WINDOWS integrity | full-file read + count | 16 entries, open ids exactly {3,15,16}, front matter 3/13/16 agrees with table (16 rows) and JSON | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` probes exist in this project (scripts/ holds embed_web_assets.mjs and verify_protocol_roundtrip.mjs only); the phase's executable verification is the wire-format harness (run above) — SKIPPED (no probes declared).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| CTRL-01 | 01-24 (+prior) | User can trigger camera capture from base station web interface | ✓ SATISFIED (regression note) | Trigger capability proven across sessions 1-6; session 7 re-proved trigger->ACK->capture on HEAD (2/2 attempts) but both crashed at push — the regression is D1's, routed as the phase's top blocker; on HEAD firmware no clean end-to-end bench evidence exists until D1 closes |
| CTRL-02 | 01-24 (+prior) | User can adjust all camera settings remotely | ✓ SATISFIED (riding note) | All 7 settings execute for real (01-04 sensor setters + ACKs on the wire; resolution class bench-proven sessions 4/5); the visible-effect pair judgment rides (SC-3 clause, sixth round) |
| CTRL-03 | 01-24 (+prior) | Manual + automatic capture modes | ✓ SATISFIED (riding note) | Manual bench-proven through session 6; auto-capture Test 4 PASS (WR-03 discriminator rides) |
| CTRL-04 | 01-24 (+prior) | Fixed interval timing | ✓ SATISFIED | AutoCapture wraparound-safe timer, bounds re-validated in-module; Test 4 cadence PASS |
| CTRL-06 | 01-24 (+prior) | Failed commands retried with timeout | ✓ SATISFIED | Session 7 re-proved on HEAD: honest terminalization at exactly 3 retries with operator-visible notification (base.log:141/:143) |
| PRI-02 | 01-24 (+prior) | Retry mechanism with timeout for failed transmissions | ✓ SATISFIED | Same evidence as CTRL-06; D-05/D-07 pacing verified at source and bench; quiet-gate constants intact through round #10 |

No orphaned requirements: REQUIREMENTS.md maps exactly CTRL-01..04, CTRL-06, PRI-02 to Phase 1 (all marked Complete; CTRL-05 is a Phase-2 ID). REQUIREMENTS.md was NOT touched by 01-24 (requirements-completed: [] honored — correct for a failed session).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (all round-#10 source files) | - | TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER scan | — | CLEAN — zero matches across all 11 touched source/header files (verifier-run) |
| src/image_tx_manager.cpp | 276-283 | full-buffer ps_malloc failure early-returns before the thumbnail branch — drops the degradation ladder's cheapest payload (review 7d96a98 WR-01, verifier-confirmed) | ⚠️ Warning | PSRAM exhaustion loses BOTH payloads; inconsistent with :266 and :306; UNROUTED (see gaps) |
| src/main_balloon.cpp | 573-577 | inverted camera health check warns on every healthy boot (review 7d96a98 WR-02, verifier-confirmed; carried across two review cycles) | ⚠️ Warning | Operator warning-channel noise masks the real failure case; UNROUTED |
| src/main_basestation.cpp | 2206-2210 | uint16 truncation of the uint32 ACK counter mis-orders LED-truth refresh at each 65536-ACK wrap (review 7d96a98 WR-03, verifier-confirmed) | ⚠️ Warning | One missed refresh per wrap, LED-truth path; UNROUTED |
| src/main_balloon.cpp | 1085-1102 | commented-out event dispatcher (dead scaffolding; inert by the 01-23 decision, recorded in WINDOWS 14) | ℹ️ Info | Maintained in the review's Info list |

### Human Verification Required

These items need the physical bench / operator judgment; all are routed (STATE.md Next Steps: D1 debug round then the bench moment), not dropped:

### 1. SC-3 visible-effect pairs (sixth round riding)

**Test:** At fixed QVGA: SET_BRIGHTNESS -2, capture; SET_BRIGHTNESS +2, capture; judge the pair visibly different. Repeat for one other class (contrast -2/+2, saturation -2/+2, or quality 10 vs 30).
**Expected:** Visible pairwise differences, per-pair verdicts recorded in Test 3.
**Why human:** Sensor visible-effect acceptance needs the physical camera and operator judgment; six bench rounds have issued zero non-RESOLUTION settings commands.

### 2. WR-03 cadence discriminator

**Test:** AUTO_CAPTURE_ENABLE at 20 s; let one interval capture fire; trigger a manual CAPTURE_NOW; compare the next interval capture's timestamp to the manual one.
**Expected:** Next interval capture lands ~20 s AFTER the manual capture (both 'Captured image ID' timestamps quoted). Also discharges the SC-5 coincidental-reliance advisory.
**Why human:** Needs the running firmware pair and wall-clock observation; never run on current HEAD.

### 3. G-01-7 series-A re-run (after D1 fix)

**Test:** 3 back-to-back unspaced captures on post-debug-round firmware.
**Expected:** 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls, with the round-#10 discriminator lines proving lever engagement.
**Why human:** RF-pair behavior under real half-duplex load; session 7 never reached a burst.

### 4. Dashboard LOOK glance + UI-SPEC lifts + CIF/QVGA clauses

**Test:** Exercise the dashboard (a UI change, d8ba14e, has been unlooked-at for two sessions); CIF cycle (SET_RESOLUTION 8) with both kinds COMPLETE at CIF sizing, QVGA restore with wire-evidence sizing, zero FB-OVF.
**Expected:** All seven UI-SPEC considerations + LED-truth backstop hold; the new gallery detail fields render; settings clauses all judged.
**Why human:** Browser observations leave no console evidence; operator-looked (not did-not-flag) is the bar.

### 5. 01-23 disposition confirmation + new-review routing decision (human decision points)

**Test:** Confirm the auto-selected fix-all disposition of round-#9's WR-01..WR-05 (01-23-SUMMARY explicitly requests end-of-phase confirmation); decide route-and-fix-now vs route-and-waive-with-reason for the fresh review's WR-01..03 (7d96a98).
**Expected:** Each decision recorded in the ledger with its evidence class.
**Why human:** Acceptance of robustness-edge-case risk is a maintainer judgment; the verifier confirms the defects are real and (for the new three) unrouted.

### Gaps Summary

Four structured gaps, honestly ledgered except the last:

1. **D1 crash regression (G-01-10 / WINDOWS 15, BLOCKER)** — HEAD firmware hard-crashes at post-capture radio push start (2/2 first-post-boot attempts at session 7); round-#10 regression; root cause NOT established; debug round named with retained ELF + suspects. D2 (boot-epoch spurious hold, WINDOWS 16) rides the same round. This is the phase's top blocker: no clean bench evidence exists on HEAD until it closes.
2. **G-01-7 series-A burst full-delivery** — the round's levers are in and wired (verifier-read at source) but bench-unvalidated: session 7 aborted at 0/6 before any discriminator engaged. Unjudgeable-not-disproven; re-run gated behind D1.
3. **The riding bench-moment clauses** — SC-3 visible-effect pairs (sixth round), WR-03 cadence discriminator, CIF/QVGA-restore re-exercise, operator-LOOKED dashboard glance. All routed; all need one clean post-D1 session.
4. **Three unrouted review warnings (7d96a98)** — all confirmed real at HEAD by this verifier (PSRAM-alloc thumbnail drop, inverted camera health check, uint16 ACK truncation), none goal-blocking, none routed anywhere. This is the one gap the ledgers do NOT carry — the same class of gap round #9's routing closed, reopened by a new review.

SUMMARY vs Reality: no divergences found this round — every load-bearing claim in 01-21/01-22/01-23/01-24-SUMMARY that this verifier checked (code sites, commit scopes, harness, log line numbers, counts, ledger integrity, zero-flip honesty, log-naming deviation handling) reproduces. The 01-24 record is a model of a failed-session writeup: nothing salvaged, everything routed, provenance deviations documented.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — remains achieved and hardware-proven through session 6, and the command path re-proved live (ACK + honest terminalization) even inside the failed session 7. But the current HEAD carries a crash regression in the capture-push path with root cause unknown, the burst-delivery optimization remains unjudged, one settings clause remains operator-unjudged for six rounds, and three fresh confirmed review findings are unrouted. The honest verdict is gaps_found with a clear next action: the D1 debug round, then one clean bench session that judges everything at once.

---

_Verified: 2026-08-28T09:00:00Z_
_Verifier: Claude (gsd-verifier)_

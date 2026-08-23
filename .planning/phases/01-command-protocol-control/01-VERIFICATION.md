---
phase: 01-command-protocol-control
verified: 2026-08-23T22:45:00Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3: capture-command execution hardware-proven (all three post-fix sessions); the settings visible-effect + CIF 400x296 clauses remain operator-unjudged and are now BLOCKED by open gap G-01-8 (the 01-11 settings-series attempt surfaced it — SET_RESOLUTION VGA false-SUCCESS + FB-OVF)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "G-01-5 (round #4's structured gap): RESOLVED — f265556 (branch c) stops booking FAILED on a missed AUX-low after a COMPLETE write (short-write and pre-write readiness stay true failures; verifier-read at src/e32_lora.cpp transmit()); ride-along c67e1a5 (R2 QQVGA guard) closes the thumbnail-sizing quirk. Operator-verified across three post-fix bench sessions 2026-08-24: phantom chunk FAILED 0/0/0 vs ~41/capture baseline, benign 'AUX-low missed after complete write' 31/0/90 — every quoted line and count re-verified against the raw logs by this verifier (balloon.log/base.log, balloon2/base2, balloon3/base3)"
    - "G-01-6 (escalated sibling, opened after round #4): RESOLVED — 678d4f1 (branch e) persists the image-ID counter to NVS in AutoCapture (restore in begin(), persist-before-issue in allocateImageId, fail-open; verifier-read at src/auto_capture.cpp). Operator-verified: IDs 1..5 sequential across three reboots (balloon2.log:106/:614, balloon3.log:104/:455), gallery index grows 2->3->4->5 with IMG_00002_*..IMG_00005_* appended (base2.log:243/:370, base3.log:84/:302/:528) — log-verified by this verifier"
    - "WR-01 (folded into 01-10): closed at c9770b1 — zero dummy PowerData (grep '0.1f, 85' == 0, 'batteryPercentage = 85' == 0), batteryReadingValid() at 3 sites, safety branches validity-gated; runtime watch GREEN (0 'Critical battery' across all balloon logs; the lone 'Emergency Shutdown: Enabled' at balloon.log:56 is the documented power_manager.cpp:706 boot config echo)"
    - "Ride-alongs verified: R1 91bee03 (STATUS_LED 39->41, GPIO41 confirmed free against the base pin map, edge-gated updateLED — zero __digitalWrite/IO-39 lines in all post-fix base logs) and R2 c67e1a5 (thumbnail QQVGA guard — correct sizing 1362-1703 B / 7-9 chunks every session; the guard's bail path never fired, consistent with correct sizes)"
  gaps_remaining: [] # round #4's gap set is fully closed
  regressions: [] # harness exit 0 (verifier-run, all clauses); both pio targets SUCCESS (verifier-run, 2 succeeded); CR-03 terminal-state guard intact (command_sender.cpp early-return for ACKED/FAILED/TIMEOUT before storage/decrements); CR-05 gates clean (isTimeToCapture 0 project-wide; captureImage live callers only auto_capture.cpp:280 + command_handler.cpp:212; allocateImageId single authority at auto_capture.cpp:300, both caller paths now NVS-backed); raw literal integrity 2 openers / 2 closers; delegated submit listener intact (main_basestation.cpp:1529); single-lever discipline held (image_protocol.h: PASSES=3, STALL=8000, PREEMPT=5000, HEAL_IDLE=24000 all unchanged); no E32_TARGET_TX_POWER (branch b correctly not selected); galleryCountSeen latch untouched (branch d correctly not selected)
gaps:
  - truth: "A capture triggered while the previous image's transfer/heal is still pending does not prevent that pending transfer's completion — concurrent captures cannot starve or evict an in-flight thumbnail heal or full pull (G-01-7 in 01-UAT.md, WINDOWS entry 3)"
    status: failed
    reason: "Operator bench finding (01-11 post-fix session 2, unspaced back-to-back captures): image 2's thumbnail finalized INCOMPLETE 6/8 after 3 passes (base2.log:296) — its heal starved behind image 2's own full pull, then image 3's window request evicted it (balloon2.log:656 'window request for image 3 supersedes older entry image 2; evicted' — this verifier confirmed both lines in the raw logs and the eviction path at image_tx_manager.cpp:798-801); image 3's full then finalized INCOMPLETE 15/52 (base2.log:498) after five tail-window re-requests (seq 53 initial + 55-59). Session 3's SPACED captures completed 4/4 kinds COMPLETE — concurrency is the discriminated mechanism, not air loss. Interim mitigation documented as operating discipline: space captures (wait for both finalize lines between triggers)"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "supersede/eviction rule (evictEntriesOlderThan :798, 'supersedes older entry ... evicted' :801) — a new capture's FULL window request evicts the previous image's pending-heal entry (:693); the never-evict-a-pending-heal lever"
      - path: "src/image_rx_manager.cpp"
        issue: "heal gating behind the active full pull (gates 1/2, :190-216) + immediate window re-request cadence — the serialization / inter-window RX-settle levers for the tail-chunk turnaround friction (13..14 / 14..14 / 49..52-class windows, pass-bounded today)"
    missing:
      - "Serialize thumbnail completion (push + heal) before the full pull starts, and/or stop evicting a pending heal entry for a new capture (operator-proposed lever, named in 01-UAT.md)"
      - "Optional inter-window RX-settle gap for tail-chunk turnaround friction (half-duplex immediate-retransmit collisions)"
      - "Bench re-verification: back-to-back unspaced captures complete without INCOMPLETE verdicts"
  - truth: "SET_RESOLUTION to any advertised framesize either works or NACKs honestly — a size larger than the boot-allocated frame buffer must never leave the camera in a state that fails all subsequent captures (G-01-8 in 01-UAT.md, WINDOWS entry 4)"
    status: failed
    reason: "Operator bench finding (01-11 session 1 settings series): SET_RESOLUTION value 9 (VGA) returned SUCCESS (balloon.log:582) then flooded 4,379 'cam_hal: FB-OVF' lines and failed all 4 subsequent CAPTURE_NOW commands until reboot — this verifier confirmed all counts in balloon.log. Code defect confirmed: CameraManager::setFrameSize (camera_manager.cpp:397-416) changes only the sensor framesize (s->set_framesize) — no buffer reallocation, no size bound; the PSRAM fb buffers (fb_count=2) stay sized for the boot framesize (QVGA). Latent since 01-04, exposed by the deferred settings series. Scope: the UI advertises values 5-13 (main_basestation.cpp:2324-2335) — values 8-13 (CIF, VGA, SVGA, XGA, SXGA, UXGA) are all larger than the QVGA boot buffer, so the Phase-1 UAT Test 3 CIF 400x296 clause sits inside this defect's blast radius (untested but presumed affected)"
    artifacts:
      - path: "src/camera_manager.cpp"
        issue: "setFrameSize (:397-416) — sensor-only framesize change without reallocation or an allocated-size bound; the fix site (NACK above the allocated buffer, or re-init the camera on size change)"
      - path: "include/command_protocol.h"
        issue: "FrameSize wire codes (:65-77) — the NACK bound derives from the boot-allocated buffer size; values 8-13 are all above it at QVGA boot"
      - path: "src/main_basestation.cpp"
        issue: "Resolution select advertises values 5-13 (:2324-2335) — the UI currently advertises six values the camera cannot safely accept"
    missing:
      - "Bound SET_RESOLUTION to the allocated buffer (NACK above it) or re-init/realloc the camera on a size change"
      - "Settings visible-effect + CIF 400x296 spot-checks (UAT Test 3 clauses still operator-unjudged — re-run after this fix; also closes this report's one behavior-unverified truth SC-3)"
      - "Bench re-verification: VGA (and ideally values 10-13) either works or NACKs honestly; boot resolution unaffected after the command"
deferred:
  - truth: "Full-resolution gallery image viewer (operator bench wish; presentation-only)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md (verifier-verified on disk; the /img/{id} SD-streaming data path is hardware-proven)"
  - truth: "D-13 separate camera-controls page / D-15 accordion settings groups"
    addressed_in: "Phase 3"
    evidence: "Carried from prior verifications; Phase 3 delivered the single-page dashboard layout (03-01, WEB-04) — informational, superseded in practice"
behavior_unverified_items:
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "Send SET_RESOLUTION / brightness / saturation / quality from the UI in-page, trigger spaced captures, and compare images pairwise; exercise the CIF 400x296 option after G-01-8's fix lands"
    expected: "Visible differences per setting in captured images; the CIF option executes and captures at 400x296 (currently blocked: CIF 400x296 exceeds the QVGA boot buffer — same defect class as the proven VGA failure)"
    why_human: "Capture-command execution is hardware-proven end-to-end (all three 01-11 post-fix sessions: CAPTURE_NOW SUCCESS, images transferred and rendered), but the settings-adjustment visible-effect clauses were never operator-judged — the 01-11 attempt hit the G-01-8 defect (SET_RESOLUTION VGA bricked captures until reboot) before any pairwise comparison could be recorded. Sensor acceptance and visual assessment need the physical camera; the UAT routes these clauses to the G-01-8 closure round"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #5, after gap closure 01-10/01-11)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-23T22:45:00Z
**Status:** gaps_found
**Re-verification:** Yes — #5, after gap closure (plans 01-10 c9770b1/3fc35a7, 01-11 f265556/678d4f1/91bee03/c67e1a5; tracking 734baab; docs 04e3047/c1aac84/b513bce/48d217e; fresh review de6c813 — all verifier-confirmed in git log with exactly the claimed file sets)

## Goal Achievement

Round #4's structured gap G-01-5 is RESOLVED, its escalated sibling G-01-6 is RESOLVED, and WR-01 is closed — all on operator bench evidence this verifier re-checked line-by-line against the raw logs. The re-verification series then honestly surfaced two NEW open gaps: G-01-7 (concurrent-transfer starvation under unspaced captures) and G-01-8 (SET_RESOLUTION to any framesize above the boot-allocated buffer falsely succeeds and fails all captures until reboot). Both are recorded open in 01-UAT.md and WINDOWS entries 3/4 (which correctly keep /gsd-ship blocked), with named levers and missing lists; the ROADMAP routes them to a Phase-1 remediation round before phase complete.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — is achieved and hardware-proven on the CURRENT firmware: commands flowed both directions in all three post-fix sessions (CAPTURE_NOW SUCCESS, images transferred, responses ACKed), the gallery grows across reboots for the first time, and the phantom-failure noise that masked real behavior is gone. What keeps the phase open is the same pattern as round #4: image-reliability/settings residuals in the Phase-2 requirement domain (IMG-02/03 reliability, CTRL-02 resolution clause + visible-effect) that this phase's UAT surfaced, plus the SC-3 spot-checks they block.

This verifier independently: read all five code fixes in source (each substantive, at its trace-named site); ran the wire harness (exit 0) and both firmware builds (2 succeeded) at this HEAD; re-ran every regression gate from prior rounds; and re-verified every load-bearing bench quote against the six untracked raw logs — line numbers and counts match exactly (FB-OVF 4,379; chunk FAILED 0/0/0; benign AUX 31/0/90; END MARKER MISS 5 in session 3; IDs 1..5 sequential; gallery 2->3->4->5; the eviction line at balloon2.log:656; 157 sends / 0 FAILED in session 3).

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression: section#capture forms intact (resolution form at :2320-2337 with all 9 advertised values); delegated in-page submit listener intact (main_basestation.cpp:1529, form.getAttribute('action')); raw literal integrity 2 openers / 2 closers |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED | Refreshed on current firmware: commands flowed both directions in all three post-fix sessions (balloon.log:255 CAPTURE_NOW SUCCESS et al.; responses returned — the benign-AUX reclassification did not disturb the ACK path); harness wire clauses all green (verifier-run, exit 0) |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Capture-command execution hardware-proven (three post-fix sessions: captures executed, images transferred CRC-verified, rendered); 7 sensor setters + name-mapped SET path intact (frameSizeFromEsp :892, used at :653). The settings visible-effect + CIF clauses remain operator-unjudged — the 01-11 attempt surfaced G-01-8 (SET_RESOLUTION VGA false-SUCCESS + 4,379 FB-OVF + captures dead until reboot) before any pairwise comparison. CIF 400x296 is inside the same defect class (400x296 > boot QVGA buffer). See behavior_unverified_items and gap G-01-8 |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Hardware UAT test 2 PASS (degraded link incl. duplicate-ACK edge) stands; CR-03 terminal-state guard re-confirmed intact at this HEAD (command_sender.cpp early-return for ACKED/FAILED/TIMEOUT before response storage, decrements, statistics) |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED | Hardware UAT test 4 PASS stands; CR-05 gates re-run clean (isTimeToCapture 0; live captureImage callers only auto_capture.cpp:280 + command_handler.cpp:212; allocateImageId single authority, now NVS-durable — the one-sequence truth is STRENGTHENED: IDs persist across reboots, bench-proven 1..5) |

**Score:** 4/5 truths verified (1 present, behavior-unverified — blocked by open gap G-01-8; no truth carries an un-routed code gap)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer (bench wish; presentation-only) | Phase 3 backlog | Todo on disk: .planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md |
| 2 | D-13/D-15 page-layout preferences | Phase 3 | Carried; superseded by the delivered single-page dashboard (WEB-04) |

### Required Artifacts (this round's gap plans 01-10/01-11)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/main_balloon.cpp` (c9770b1, WR-01) | Three dummy PowerData sites replaced with validity-gated PowerMgr reads; batteryReadingValid() helper; one-shot validity-transition warning | ✓ VERIFIED | Helper mirrors the beacon idiom ([1.8,8.0] V AND nonzero raw ADC); updateSystemState lowPowerMode gated on valid; processPowerManagement CRITICAL/LOW branches act only inside `if (powerData.valid)` — triggerEmergency/camera-disable unreachable from a floating line; sendTelemetryData reads PowerMgr directly; thresholds ride VOLTS (documented unit-fix deviation). Gates: '0.1f, 85' = 0, 'batteryPercentage = 85' = 0, batteryReadingValid = 3 |
| `platformio.ini` (3fc35a7) | CDC_ON_BOOT=0 in both production envs (bench observability deviation) | ✓ VERIFIED | Both envs flipped; test envs keep CDC on; documented Rule-3 deviation — balloon3.log demonstrably carries the full ImageTx/CommandHandler discriminator set |
| `src/e32_lora.cpp` (f265556, branch c / G-01-5) | Missed AUX-low after a COMPLETE write no longer books FAILED; short-write and pre-write readiness stay true failures; benign log line | ✓ VERIFIED | Short-write gate (sent != length) added BEFORE the AUX handshake; missed AUX-low logs 'treated as sent (bytes left the radio)' and returns true; AUX-high timeout and readiness failure untouched; commit message quotes the discriminating lines (per the trace-named-site clause) |
| `src/auto_capture.cpp` (678d4f1, branch e / G-01-6) | Image-ID counter persisted to NVS across reboot, fail-open | ✓ VERIFIED | Preferences begin(namespace "imgid", readOnly=false); restore lastImageId in begin(); allocateImageId persists BEFORE handing out the ID (a reboot can never re-issue); fail-open guards logged; e-site deviation (auto_capture.cpp, not plan-anticipated sd_storage.cpp) recorded and trace-forced |
| `include/base_station_config.h` + `src/main_basestation.cpp` (91bee03, R1) | STATUS_LED remapped off SDMMC-owned GPIO 39; edge-gated writes | ✓ VERIFIED | STATUS_LED_PIN 41 (:47) — verifier confirmed 41 named nowhere else on the base pin map (LoRa 14/48/19/20/21, SD_MMC 39/38/40, OLED I2C 1/2); pinMode at :2067; updateLED writes only on level change (static lastWrittenLevel); post-fix base logs contain ZERO __digitalWrite/IO-39 lines (baseline 3,513-9,563/session) |
| `src/camera_manager.cpp` (c67e1a5, R2) | Thumbnail frame verified QQVGA-sized before enqueue | ✓ VERIFIED | fb->width/height != 160/120 bails through the existing no-thumbnail failure branch (fb returned, thumbnail.buffer nulled — CR-04 pattern preserved, thumbnail.valid = false, settings restored, distinctive log line); never pushes a full-size frame as a "thumbnail" |
| `01-UAT.md` evidence blocks | G-01-5/G-01-6 resolved with verbatim evidence; G-01-7/G-01-8 opened; Test 3 note updated | ✓ VERIFIED | All discriminator_evidence/verified_by quotes re-checked against the raw logs — exact text, line numbers, and counts |
| `.planning/WINDOWS.md` | Entry 2 fixed with evidence; entries 3 (G-01-7) / 4 (G-01-8) open | ✓ VERIFIED | Table and JSON copies consistent; /gsd-ship correctly still blocked |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| E32LoRa::transmit verdict | All TX callers (command responses, image chunks, beacons) | complete-write + missed-AUX-low -> true; short-write -> false | ✓ WIRED | Bench-proven: 0 chunk FAILED across all post-fix sessions while responses were ACKed and every spaced capture completed; real air loss still covered by END-MARKER/CRC + the D-22 window heal on the base (pass-bounded — the bound's edge is honestly routed as G-01-7's tail-chunk friction) |
| NVS restore in AutoCapture::begin | lastImageId -> allocateImageId (persist-before-issue) -> both capture authorities (auto_capture.cpp:281, command_handler.cpp:218) | single image-ID authority (CR-05 preserved), now durable | ✓ WIRED | Bench-proven: IDs 1..5 sequential across three reboots; gallery index 2->3->4->5 with IMG_00002_*..IMG_00005_* appended (no overwrite) |
| computeLinkTruth | updateLED edge-gated digitalWrite on GPIO 41 | level written only when it changes | ✓ WIRED | 0 HAL error lines in all post-fix base logs (was thousands/session, ~7-10 ms blocking each) |
| createThumbnail QQVGA guard | enqueue's no-thumbnail failure branch | dimension check bails honestly (buffer nulled, settings restored) | ✓ WIRED | Correct sizes observed all sessions (1362-1703 B / 7-9 chunks); the guard firing is code-verified only (never needed at bench — expected, noted) |
| PowerMgr().update() 1s cadence | batteryReadingValid() -> lowPowerMode + CRITICAL/LOW safety branches | validity gate precedes every safety action | ✓ WIRED | 0 'Critical battery' / no Emergency entry / no camera auto-disable across all balloon logs; beacons batt=valid |
| Delegated section#capture submit listener (01-07, regression) | fetch POST to each form's action + pollOnce | unchanged by this round | ✓ WIRED | Listener at :1529; raw literals intact 2/2 |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| Image-ID sequence | lastImageId | NVS namespace imgid (restored at boot, persisted per issue) | Yes | ✓ FLOWING — measured on hardware across three reboots (IDs 2/3/4/5 on boots 2/3; gallery index reflects the appended files) |
| TX accounting verdict | transmit() return | serial write completeness + AUX handshake | Yes | ✓ FLOWING — verdict now tracks bytes-actually-written; the phantom signal (booked FAILED with data received) is eliminated 0/0/0 vs 41/capture |
| Telemetry battery fields | batteryVoltage/Current/Percentage | PowerMgr real reads (was fabricated 3.7/0.1/85) | Yes | ✓ FLOWING — beacons carry batt=valid verdicts; the fabricated 85% is gone (grep 0/0) |
| Gallery grid | index count | SD_MMC index rebuild after each persist | Yes | ✓ FLOWING — index advances 2->3->4->5 on the fixed firmware (was locked at 6 by the ID-reset overwrite) |
| Thumbnail render (unspaced captures) | IMG_{id}_T.JPG completeness | push burst + heal, gated behind the active full pull, subject to supersede-eviction | Partial | ✗ DISCONNECTED UNDER CONCURRENCY — G-01-7: with spacing every thumbnail completes (4/4); unspaced, the heal starves and is evicted (INCOMPLETE 6/8, eviction line balloon2.log:656); the honest Incomplete badge displays the true state |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite (all clauses incl. type-byte teeth, PRI order gates) | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run) | All PASS, exit 0 | ✓ PASS |
| Both firmware targets compile at this HEAD | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (verifier-run) | 2 succeeded (25.9 s / 25.4 s) | ✓ PASS |
| 01-10/01-11 grep gates | dummy PowerData / batteryReadingValid / STATUS_LED / pacing constants / E32_TARGET_TX_POWER / galleryCountSeen | 0 / 0 / 3 / all four unchanged / 0 matches / latch untouched | ✓ PASS |
| G-01-5 closure evidence vs raw logs | grep balloon*.log base*.log | phantom FAILED 0/0/0; benign AUX 31/0/90; session-3 finalize verdicts 4/4 COMPLETE at :75/:237/:290/:524; 157 sends / 0 FAILED; END MARKER MISS 5 all recovered | ✓ PASS |
| G-01-6 closure evidence vs raw logs | grep balloon2/3.log base2/3.log | IDs 2/3/4/5 at :106/:614/:104/:455; gallery 2->3->4->5 at :243/:370/:84/:302/:528; IMG_00002.JPG complete=true at :232 | ✓ PASS |
| G-01-8 defect reproduction trail | grep balloon.log | SET_RESOLUTION SUCCESS :582; FB-OVF 4,379; CAPTURE_NOW 1x SUCCESS then 4x FAILED | ✗ FAIL (gap G-01-8 — the defect is real, open, and honestly routed) |
| G-01-7 defect evidence | grep balloon2.log base2.log | eviction line :656; INCOMPLETE 6/8 at :296 and 15/52 at :498; five tail re-requests seq 53/55-59 | ✗ FAIL (gap G-01-7 — real, open, honestly routed) |
| WR-01 safety watch | grep 'Critical battery' balloon*.log | 0 / 0 / 0; lone 'Emergency Shutdown: Enabled' at balloon.log:56 is the documented boot config echo | ✓ PASS |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists in the repo. The declared verification commands (harness + both builds) were executed by this verifier above. SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-02..01-07, 01-10 | Trigger camera capture from base station web UI | ✓ SATISFIED | All three post-fix bench sessions: CAPTURE_NOW SUCCESS from the dashboard, images transferred and rendered |
| CTRL-02 | 01-03/04/06/07, 01-10 | Adjust all camera settings remotely | ⚠ PARTIAL — G-01-8 open | 7/7 forms/routes/handlers execute remotely (sensor-level SET path proven, commands ACKed); BUT the resolution clause is defective for advertised values 8-13 (CIF..UXGA all exceed the boot QVGA buffer; VGA proven to brick captures until reboot, false SUCCESS) and the visible-effect clauses are operator-unjudged. Rides the G-01-8 closure round (this verifier's disconfirmation finding: REQUIREMENTS.md marks CTRL-02 Complete — over-optimistic while G-01-8/WINDOWS entry 4 is open) |
| CTRL-03 | 01-03/04/05/07 | Manual + automatic capture modes | ✓ SATISFIED | Hardware UAT test 4 PASS; unchanged by this round (CR-05 gates clean) |
| CTRL-04 | 01-03/04/05/07 | Automatic capture fixed interval timing | ✓ SATISFIED | Hardware UAT test 4 PASS (exact cadence incl. >30 s value) |
| CTRL-06 | 01-02/03/06 | Failed commands retried with timeout | ✓ SATISFIED | Hardware UAT test 2 PASS; CR-03 guard intact at HEAD |
| PRI-02 | 01-02, 01-06 | Retry mechanism with timeout | ✓ SATISFIED | Same evidence as CTRL-06 |
| IMG-01/04/05 (gap-plan anchors) | 01-08/09/10/11 | Transmit / chunked ARQ / SD storage | ✓ SATISFIED | Hardware-proven: transfers complete CRC-verified; IMG_00002_*..IMG_00005_* persisted complete=true on SD |
| IMG-02/IMG-03 (flipped Complete by 01-11) | 01-08..01-11 | Thumbnail displays immediately; full transfers in background | ⚠ SATISFIED WITH OPEN RESIDUAL (G-01-7) | Proven under spaced captures (4/4 kinds COMPLETE, session 3); unspaced captures can starve/evict (2 INCOMPLETE, session 2). Phase-2-mapped requirements — the residual is honestly routed (WINDOWS entry 3 open, ship blocked); the 01-11 flip is defensible only because the ledger governs ship. Noted for the milestone audit |
| IMG-06 (flipped Complete by 01-11) | 01-10/01-11 | Gallery displays all received images with pagination | ✓ SATISFIED | Gallery grows across reboots for the first time (2->3->4->5); index rebuild after each persist verified in logs |
| PRI-03 (flipped Complete by 01-11) | 01-08..01-11 | Gracefully handles LoRa bandwidth limitations | ✓ SATISFIED | 9.6 kbps enforcement, windowed ARQ with pass bounds, real air loss measured tiny and self-healed; the operating note (space captures) is documented |

Orphaned requirements: none. The six Phase-1-mapped IDs are all claimed across plans and evidenced. The additional IDs claimed by the gap plans (IMG-01..06, PRI-03) are Phase-2-mapped requirements whose hardware truths surfaced in Phase 1's UAT — their flips are recorded above with the honest residual status.

### Plan Prohibition Verdicts (judgment/backstop-tier — autonomous, NON-AUTHORITATIVE)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-10 | MUST NOT fabricate image-transfer or config success | PASS (log level, non-authoritative) — flagged, human review recommended | Every tally/verdict quote re-verified against the raw logs by this verifier (counts exact). The two operator-only observations are attributed as operator-reported in the SUMMARY |
| 01-10 | MUST NOT mark G-01-5/G-01-6 resolved on code-level evidence alone | PASS (non-authoritative) — flagged, human review recommended | Discipline held: both gaps stayed OPEN after 01-10 (evidence round only); closure happened in 01-11 only after operator bench re-verification |
| 01-10 | MUST NOT let an invalid battery reading trigger emergency or camera-disable | PASS (code + bench watch, non-authoritative) — flagged, human review recommended | Gate precedes every safety action (verifier-read); watch green: 0 'Critical battery' across all logs. Known residual honestly flagged in UAT: a full-scale float (raw 4095) passes the gate — flight-config item, cannot cause a false emergency (reads as healthy, never critical) |
| 01-11 | MUST NOT change image pacing constants before the discriminator names the mechanism | PASS (machine-checkable) — flagged per autonomous disposition | All four constants unchanged at HEAD (verifier grep: PASSES=3, STALL=8000, PREEMPT=5000, HEAL_IDLE=24000); single-lever discipline held |
| 01-11 | MUST NOT add a parallel best-effort thumbnail mechanism (D-22 single heal path) | PASS (code level, non-authoritative) — flagged, human review recommended | No new mechanism: R2 is a guard that bails through the existing failure branch; the heal remains the single reliability path |
| 01-11 | MUST NOT refetch /gallery on every poll (D-36) | PASS (machine-checkable) | Branch d not selected; galleryCountSeen latch untouched (:1618-1645); refetch stays count-driven |
| 01-11 | MUST NOT fabricate image-transfer or config success | PASS (log level, non-authoritative) — flagged, human review recommended | Both status flips cite operator-observed console lines; this verifier re-verified the quotes against the raw logs |
| 01-11 | MUST NOT mark G-01-5/G-01-6 resolved on code-level evidence alone | PASS (non-authoritative) — flagged, human review recommended | Three operator bench sessions on fixed firmware precede the flips; residuals NOT force-closed — routed as new open gaps G-01-7/G-01-8 |

### Anti-Patterns Found

No TBD/FIXME/XXX in any file modified by 01-10/01-11 (verifier grep clean). The one TODO (platformio.ini:75, lora_comm.cpp rewrite note) is git-blamed to 2e10e834 (2026-06-28) — predates the phase, informational. Items below are from the fresh 01-REVIEW.md (de6c813, 0 Critical / 8 Warning / 17 Info); WR-07/WR-08 are the known-open gaps. None blocks the phase goal beyond the structured gaps.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/image_tx_manager.cpp | 693, 798-801 | G-01-7: FULL window request evicts older entries mid-flight — concurrent-transfer starvation | 🛑 Gap (routed) | Structured gap above; interim mitigation documented (space captures) |
| src/camera_manager.cpp | 397-416 | G-01-8: setFrameSize sensor-only — no buffer realloc/bound | 🛑 Gap (routed) | Structured gap above; UI advertises 6 unsafe values (8-13) |
| src/command_protocol.cpp | 165-179 | WR-06 (new): serializeResponse silently drops an oversize payload while the header advertises it (latent — createResponsePacket clamps to 50) | ⚠️ Warning | Public protocol API asymmetry vs the fixed serializeCommand; harness should grow the rejection clause. Recommend folding into the next gap round or /gsd-secure-phase 1 |
| src/main_balloon.cpp | 574-577 | WR-01(review): inverted camera health check — warns when camera healthy (persists) | ⚠️ Warning | False diagnostic only |
| include/sensor_pins.h | 52 | WR-02(review): GPIO4 double-assigned (battery ADC vs camera SCCB SDA on ESP32S3_EYE) | ⚠️ Warning | Undocumented hardware collision; note — with WR-01(c9770b1) the 1 Hz analogRead now feeds real safety logic, raising this pin's stakes; fix before flight |
| platformio.ini | (basestation env) | WR-03(review): basestation env missing board_build.partitions — 1.2 MB default app partition vs growing web surface | ⚠️ Warning | Builds today (verifier-run); will fail as assets grow |
| src/e32_lora.cpp | 591-609 | WR-04(review): exitConfigMode ignores saved previousMode (dead local) | ⚠️ Warning | Correct today only because every caller enters from NORMAL |
| src/main_basestation.cpp | 978-979 | WR-05(review): canvas-fallback centering offsets identically zero | ⚠️ Warning | Cosmetic |
| Carried Info items | — | IN-01..IN-17 from 01-REVIEW (dead code, stale comments, seq-wrap edges, control-char JSON escaping, RX buffering, at-least-once re-execution, blocking AUX waits, oversize-drop latent edge, weak AP default, etc.) | ℹ️ Info | None in a Phase-1 must-have domain; IN-10/weak-AP overlap the pending /gsd-secure-phase 1 gate |

### SUMMARY vs Reality

1. 01-10/01-11 SUMMARY claims reproduce on this verifier's own reads, greps, harness run, builds, and log checks: all seven commits exist with exactly the claimed file sets (c9770b1 main_balloon.cpp only; 3fc35a7 platformio.ini only; f265556 e32_lora.cpp only; 678d4f1 auto_capture.cpp only; 91bee03 the two base files; c67e1a5 camera_manager.cpp only; 734baab STATE.md only).
2. Every load-bearing bench quote is verbatim-accurate against the raw logs, including line numbers (SET_RESOLUTION :582; eviction :656; IDs :106/:614/:104/:455; finalize verdicts :68/:186/:75/:237/:290/:524; gallery index :243/:370/:84/:302/:528) and counts (FB-OVF 4,379; FAILED 0/0/0; AUX-missed 31/0/90; END MARKER MISS 5; sends 157; IO-39 flood 0). The tallies reconciled in the SUMMARY's Issues section (46 vs "48 chunks", 5+1 vs "six re-requests") match the logs' measured counts.
3. The two closures are honest: G-01-5 flipped only after mechanism-specific (phantom FAILED -> 0) AND truth-specific (spaced captures 4/4 COMPLETE) evidence; G-01-6 flipped after three-reboot persistence proof. The two new residuals were NOT absorbed into the closures — they are new open gaps with named levers.
4. Deviations are all recorded, none silent: the volts-vs-percentage unit fix (c9770b1), the CDC log consolidation (3fc35a7, Rule 3), the two e-site deviations (trace-named-site clause), the operator-approved deferral of the settings series to 01-11, and the separation-series replacement (B5 falsified by close-range completions — no residual left for separation to explain).
5. 01-REVIEW.md (de6c813) claims were spot-re-derived: WR-01-closure sighting matches this verifier's code read; WR-06's oversize-drop confirmed at command_protocol.cpp:165-179 (latent, clamped upstream); WR-07/WR-08 are the routed gaps — consistent.
6. REQUIREMENTS.md marks CTRL-01..04/CTRL-06/PRI-02 Complete — accurate except CTRL-02 is over-optimistic while G-01-8 keeps its resolution clause defective (see Requirements Coverage); IMG-02/IMG-03 flips carry the honest G-01-7 residual in the ledger.

### Gaps Summary

Two gaps, both real, both verified at code level AND log level, both already honestly routed by the project (01-UAT.md entries, WINDOWS 3/4 open, ROADMAP promises a remediation round before phase complete):

1. **G-01-7 — concurrent-transfer starvation.** A new capture's window traffic supersedes/evicts the previous image's pending heal (balloon2.log:656) and the heal starves behind its own full pull; unspaced captures produced 2 INCOMPLETE verdicts while spaced captures completed 4/4 — concurrency discriminated as the mechanism. Lever named: serialize transfers / never evict a pending heal; interim mitigation documented (space captures).
2. **G-01-8 — SET_RESOLUTION buffer-realloc defect.** setFrameSize changes only the sensor framesize; values above the boot QVGA buffer (advertised 8-13, VGA proven) falsely SUCCEED, flood FB-OVF, and fail all captures until reboot. Blocks UAT Test 3's settings visible-effect + CIF clauses — the same clauses that keep SC-3 behavior-unverified.

Everything else is green: 4/5 SCs verified (round #4's gap G-01-5 and sibling G-01-6 closed on operator evidence this verifier re-checked against the raw logs; WR-01 closed with a green safety watch), all artifacts substantive and wired, all key links connected, harness exit 0, builds 2/2, no debt markers, all prior closures regression-intact, single-lever discipline held (pacing constants untouched — the discriminator-driven routing was respected exactly).

The phase goal — bidirectional LoRa command/control with ACK, retry, manual + auto capture — is achieved and hardware-proven on current firmware. What remains is the same class as round #4's residual: image-reliability/settings residuals in the Phase-2 requirement domain that this phase's UAT surfaced, honestly routed with the ledger keeping /gsd-ship blocked, plus the SC-3 spot-checks they block.

Recommended next step: `/gsd-plan-phase 1 --gaps` (G-01-7 serialization + G-01-8 buffer bound, with the SC-3 ride-along spot-checks), then `/gsd-secure-phase 1` (WR-06 + IN-10 scope; consider WR-02 GPIO4 and the ADC full-scale float note for flight config).

---

_Verified: 2026-08-23T22:45:00Z_
_Verifier: Claude (gsd-verifier)_

---
phase: 01-command-protocol-control
verified: 2026-08-28T10:35:00Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3, narrowed to its final clause for the SEVENTH consecutive round: resolution-class clauses remain bench-proven (sessions 4/5; camera_manager.cpp untouched by rounds #8/#10/#11 — verifier diff-verified; session 8 even added a live SET_RESOLUTION wire-10 whose SVGA-class payload sizing, 27377 B / 137 chunks vs QVGA's 7152 B / 36, is wire evidence the setting took); only the pairwise visible-effect judgments (brightness -2 vs +2 + one other class) remain operator-unjudged — session 8 issued zero non-RESOLUTION settings commands before the D1 crash aborted it (verifier-grepped balloon2.log/base2.log)
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "Review 7d96a98 routing (previous gap 4): CLOSED — WINDOWS entries 17-19 exist (table AND JSON, counts reconciled at 2/17/19), all three dispositioned FIXED per the auto-selected option-a; every fix re-derived at HEAD source by this verifier: WR-01 ps_malloc fall-through (image_tx_manager.cpp:355-367 — nullptr/0/0 full, named 'full dropped, thumbnail still pushes' log, falls through to the unchanged thumb branch at :380), WR-02 honest health-check branch (main_balloon.cpp:577-585 — active camera warns only on absent esp_camera_sensor_get handle; inactive gets the skipped note), WR-03 uint32 end-to-end (main_basestation.cpp:81 member + :2208-2212 poll site; negative grep zero 'static_cast<uint16_t>(acked'). WR-02 additionally BENCH-VERIFIED at session 8 (zero 'health check failed' lines in the whole console; honest skipped notes :109-110/:1328-1329 at both boots)"
    - "D2 spurious boot hold (G-01-11 / WINDOWS 16): CLOSED on hardware evidence — the 01-25 receipt-ever flag (inboundWindowRequestSeen: header :242, ctor init :39, begin reset :79, gate :623, stamp :964 — all five sites verifier-read) is provably in-image (warm-up lines balloon2.log:102/:1321) and BOTH session-8 boot windows are clean of the spurious fire; the session's exactly-2 fires are genuine by timestamp correlation (:335 mid-service of image 37's window; :1389 five lines after the rejected-but-received request at :1384-1388 — stamped by that very request). G-01-11 flipped resolved with quoted evidence"
    - "The 01-25 debug round landed as planned (previous gap 1's first half): every session-7 crash address decoded against the proven deployed ELF (doc §1), all four suspect classes dispositioned (§2), one root cause ranked (§3), determinism + discriminators named (§4) — and this verifier INDEPENDENTLY re-ran the session-8 decode: sha256sum of .pio/build/esp32-s3-balloon/firmware.elf reproduces c53635e1819a6ca5107b1437450dbc324e5848e685b50de73ea6a4387c8e0553 exactly, and xtensa-esp32s3-elf-addr2line -pfiaC -e on 0x40376430 resolves to 'tick_hook at esp-idf/components/esp_system/int_wdt.c:111' with 0x403c88b8 unresolvable — the doc's decode table reproduces verbatim"
  gaps_remaining:
    - "D1 crash regression (G-01-10 / WINDOWS 15) RE-OPENED with new evidence: the 01-25 PSRAM-warm-up fix is DISCONFIRMED as sufficient — session 8's spaced smoke SURVIVED (the session-7 crash-1 pattern is dead: image 37 first-post-boot push both kinds COMPLETE, base2.log:74-75/:238-239) but the balloon reset at the FIRST CHUNK of the SEVENTH re-armed FULL window of the second image (balloon2.log:1220 armed 96..109 -> :1227 chunk 1/14 sent -> :1228-1231 rst:0x7 TG0WDT_SYS_RST, Saved PC:0x40376430 = tick_hook, zero project frames) with [MEM] healthy throughout (:611 enqueue, :1175 last pre-reset) — new named axis: task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service; debug round #2 required"
    - "G-01-7 series-A burst full-delivery: SEVENTH round unjudgeable — the series never ran (session 8's two CAPTURE_NOWs were minutes apart, i.e. spaced; verifier-grepped zero round-#10 discriminators: 0 'budget re-armed', 0 'evicting class 5', 0 base 'full-pull activation deadline reached')"
    - "SC-3 visible-effect pairs (seventh round), WR-03 cadence discriminator (seventh round), CIF-cycle-as-specified + QVGA restore (the executed SET_RESOLUTION was wire-10/SVGA, not wire-8/CIF, and its full never completed), dashboard operator-LOOKED glance + UI-SPEC lifts — all NOT RUN (session aborted at image 38; zero settings commands beyond the one SET_RESOLUTION, zero AUTO_CAPTURE — verifier-grepped both consoles)"
  regressions:
    - "NONE new in code: round #11's code work (01-25 warm-up/[MEM]/receipt-ever flag, 01-26 three review fixes) is verified wired and harness-green at HEAD f410b4e; zero source drift; camera_manager.cpp and image_rx_manager.cpp untouched since verification #9 (verifier diff-verified — no regression surface for the bench-proven settings clauses). The D1 recurrence is the SAME open gap evolved, not a new code regression: one crash class (first-post-boot push) is now bench-fixed, a second class (mid-service WDT starvation) is newly discriminated and routed"
gaps:
  - truth: "The balloon survives post-capture radio push through sustained FULL-window service (G-01-10 / WINDOWS 15, the phase's top blocker): a capture's enqueue/manifest/chunk/beacon/window sequence never hard-resets the firmware, so buffers survive to serve the transfer"
    status: failed
    reason: "Bench session #8 (01-27, round-#11 firmware at ELF SHA256 c53635e181 — verifier-confirmed the SHA and independently reproduced the addr2line decode): the spaced smoke SURVIVED (image 37, the exact session-7 crash-1 pattern, both kinds COMPLETE) but the balloon reset at the first chunk of re-armed FULL window 96..109 of the SECOND image — balloon2.log:1220 armed -> :1227 'window chunk(image 38 kind 1, 1/14, 200 B) sent' -> :1228-1231 'rst:0x7 (TG0WDT_SYS_RST)' 'Saved PC:0x40376430' (= esp-idf tick_hook, int_wdt.c:111, ZERO project frames). [MEM] healthy at enqueue (:611) and at the last pre-reset sample (:1175) — the 01-25 warm-up hypothesis is DISCONFIRMED as sufficient; zero mojibake/zero Guru Meditation/zero canary and both i2cWrite errors non-adjacent BMP280 noise — session-7's corruption corroboration ABSENT. Downstream: image 38 full INCOMPLETE 109/137 after 3 passes (base2.log:651 — crash-caused), every post-reboot window request rejected (:1385/:1398). New named axis: task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service (the serviceWindowChunk -> E32 transmit path: AUX polling, UART flush, a kernel lock held across a blocking wait, or a cache-suspended stretch). NOT established: any application frame, any memory-corruption mechanism visible this session"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "serviceWindowChunk -> E32 transmit path is the new suspect axis (NOT established); the [MEM] instrumentation correctly stays in until this gap closes (removal condition named in-source :135-136)"
      - path: ".planning/debug/d1-crash-regression-push-start.md"
        issue: "§6 carries the full session-8 evidence and the debug-round-#2 brief — the round-#12 input"
    missing:
      - "Debug round #2 (round #12): audit the serviceWindowChunk -> E32 transmit path for WDT-period blocks against the deployed ELF c53635e181 with Saved PC 0x40376430 = tick_hook (int_wdt.c:111) and the [MEM]-healthy session-8 evidence; determine whether session-7's spinlock wedge and session-8's stall are one mechanism or two; fix, then bench-verify"
      - "Establish crash determinism on the new axis (session 8's captures were SPACED and the second still crashed mid-transfer — the avoid-unspaced-bursts discipline is no longer sufficient; the crash class is not confined to bursts)"
  - truth: "G-01-7 series-A acceptance (burst full-delivery): 3 back-to-back unspaced captures — 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls, with the round-#10 discriminators proving lever engagement (or the residual mechanism named)"
    status: failed
    reason: "UNJUDGEABLE — SEVENTH round: session 8 aborted at image 38's FULL service on the D1 recurrence before any unspaced burst (the two CAPTURE_NOWs at balloon2.log:208/:604 are minutes apart). 2 ACKs / 2 captures / 3 of 4 kind-verdicts COMPLETE (image 37 both kinds; image 38 thumb) with ZERO command timeouts — the spaced behavior is clean — but zero round-#10 discriminators engaged (verifier-grepped 0/0/0). Image 38's kind-1 INCOMPLETE 109/137 is CRASH-CAUSED, not burst-delivery (verifier-read: base2.log:651 vs the crash at balloon2.log:1227-1231). The 01-21/01-22 levers remain unverified, not disproven; the D2 discriminator pollution is now FIXED so the next series-A run reads clean"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "01-21 levers present and wired (verifier-read in prior rounds; untouched by round #11); no bench validation exists — D1 recurrence precedes any series-A re-run"
    missing:
      - "D1 debug round #2 + fix first, then the series-A re-run: 3 unspaced captures, 6/6 COMPLETE, zero timeouts, with the discriminator lines ('re-announce budget re-armed', receipt-evidenced class-5 eviction labels, base 'full-pull activation deadline reached') proving lever engagement or naming the next mechanism"
  - truth: "The bench-moment clauses: SC-3 visible-effect pairs judged; WR-03 cadence discriminator quoted; CIF-cycle (wire-8) + QVGA-restore clauses exercised; dashboard operator-LOOKED glance + UI-SPEC lifts"
    status: partial
    reason: "NONE ran — SEVENTH consecutive round riding: session 8 aborted at image 38 before any settings command beyond the operator's own SET_RESOLUTION wire-10 (SVGA — a deviation from the specified CIF wire-8, honestly recorded; its capture's full never completed so even that clause is unsatisfied) and before any AUTO_CAPTURE command (verifier-grepped zero SET_BRIGHTNESS/contrast/saturation/quality and zero AUTO_CAPTURE in both consoles). The dashboard glance was not reached (/api/state serving base2.log:527/:532 but no LOOK verdicts). All honestly recorded in 01-UAT.md (Test 3/Test 4 notes) and routed in STATE.md Next Steps"
    artifacts:
      - path: ".planning/phases/01-command-protocol-control/01-UAT.md"
      issue: "Test 3 stays 'issue' — the visible-effect-pair clause is its last open settings clause (CIF/QVGA-restore clauses also riding: session-5-proven, camera_manager.cpp untouched by rounds #8/#10/#11, but the re-exercise never ran)"
    missing:
      - "Post-D1 bench session: SC-3 pairs (brightness -2/+2 plus one other class at fixed QVGA), WR-03 cadence discriminator (AUTO_CAPTURE_ENABLE 20 s + manual capture, quote both 'Captured image ID' timestamps — also discharges the SC-5 coincidental-reliance advisory), CIF cycle (wire-8) + QVGA restore with zero FB-OVF, dashboard LOOK glance incl. the d8ba14e gallery-detail fields and all UI-SPEC lifts"
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
    why_human: "Sensor visible-effect acceptance needs the physical camera and operator judgment — SEVEN consecutive bench rounds have issued zero non-RESOLUTION settings commands (session-8 queue mix verifier-grepped; the session aborted on the D1 recurrence before any settings command). The resolution clause no longer rides: growth/restore/reboot were bench-proven at sessions 4/5 (and session 8 added live SET_RESOLUTION wire-10 wire evidence: image 38's 27377 B / 137 chunks vs QVGA image 37's 7152 B / 36 — the setting demonstrably took effect on the sensor), and no round since has touched camera_manager.cpp (verifier diff-verified through HEAD f410b4e)"
coincidental_reliance_items:
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5)"
    reason: "fixture-only"
    harden: "The manual half now has clean CURRENT-firmware evidence — session 8's image 37 is a full end-to-end manual-trigger proof on round-#11 firmware (queue -> ACK -> capture -> thumb 8/8 + full 36/36 COMPLETE, base2.log:74-75/:238-239) — but the auto-capture half still rests on Test 4's bench PASS, which predates the 01-19 baseline advance and 02-04 event triggers; non-interference is established by code reading only (zero AUTO_CAPTURE_ENABLE at sessions 6, 7, AND 8 — seventh round). The WR-03 cadence discriminator at the post-D1 bench moment is precisely the hardening step — already routed in STATE.md Next Steps. The D1 recurrence also bounds the manual half's currency: image 38's manual trigger died mid-full at the crash, so one clean end-to-end proof exists on this firmware generation, not a clean session"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #10, after gap closure 01-25..01-27)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-28T10:35:00Z
**Status:** gaps_found
**Re-verification:** Yes — #10, after gap-closure round #11 (plans 01-25 D1 debug + D2 fix, 01-26 review 7d96a98 routing + companion fixes, 01-27 bench session #8; commits ed1ec47/8539c4c/0e84ca1/8041b27/a2a14a7/f410b4e — all verifier-confirmed in git log; zero source drift HEAD..working-tree in src/ and include/)

## Goal Achievement

Round #11 closed both halves of what it could close on evidence — and, for the first time in three rounds, closed real HARDWARE truths: D2 (G-01-11 / WINDOWS 16) is resolved on bench evidence (both session-8 boot windows clean of the spurious hold; the session's exactly-2 fires genuine by timestamp correlation — the receipt-ever flag working as designed), and WR-02 is bench-verified (zero health-check-failure lines at both healthy boots, one of them the crash-reboot stress boot). The prior verification's gap 4 (three unrouted review warnings) is fully closed: WINDOWS 17-19 exist with decided dispositions and all three fixes verifier-re-derived at HEAD source. The 01-25 debug round is a model of the genre — and this verifier INDEPENDENTLY reproduced its most load-bearing artifact: the ELF SHA matches and the addr2line decode of the fatal Saved PC reproduces exactly (tick_hook, int_wdt.c:111, zero project frames).

The bench half FAILED again, honestly — but differently, and the difference is progress: session 8's spaced smoke SURVIVED (the session-7 crash-1 pattern — first-post-boot capture push — is dead at this firmware), the session ran 3/4 kind-verdicts COMPLETE with ZERO command timeouts, and the crash that ended it landed somewhere new: mid-service of the SECOND image's SEVENTH re-armed FULL window, with healthy [MEM] throughout. That evidence DISCONFIRMS the 01-25 PSRAM-warm-up hypothesis as sufficient, removes session-7's corruption corroboration (zero mojibake, zero panic, zero canary, both i2cWrite errors non-adjacent BMP280 noise), and names a sharper root-cause axis for round #12: task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service. Nothing was forced: D1 re-opened with the new dump decoded and appended to the debug doc, G-01-7 stayed unjudgeable with the series-attempt count honestly incremented, and the ledgers flipped exactly one truth (D2) plus one entry's bench evidence (WR-02).

What also did NOT happen, again: the SC-3 visible-effect pairs (SEVENTH round riding), the WR-03 cadence discriminator (seventh), the CIF-cycle-as-specified + QVGA-restore clauses (the operator's SET_RESOLUTION wire-10 was a recorded deviation whose full never completed), and the operator-LOOKED dashboard glance. All routed to the post-D1 bench moment.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; the round's own 01-25/01-26/01-27-PLAN truths are assessed separately below).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression check: round #11's commit scopes touch no UI markup (8539c4c: image_tx trio; 8041b27: image_tx + main_balloon health-check + main_basestation LED-truth C++ logic only; 0e84ca1: ledgers) — verifier read the stats; the last UI change remains d8ba14e. Session 8 used the dashboard throughout (both CAPTURE_NOWs + SET_RESOLUTION queued from it). The LOOK glance rides the post-D1 bench |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED (regression caveat: D1) | Session 8's cleanest command showing of the last three sessions: both CAPTURE_NOWs ACKed + executed (balloon2.log:207-213/:604-609), SET_RESOLUTION wire-10 ACKed + executed (:589-592), window requests serviced pre-crash, post-reboot requests honestly rejected + NACKed, ZERO command timeouts in base2.log (verifier-counted — unlike session 7's forbidden terminals). Harness exit 0 at HEAD (verifier-run). CAVEAT: the balloon still dies mid-session on the D1 axis — tracked by the D1 gap, not hidden here |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | NARROWED TO ITS FINAL CLAUSE, unchanged from #9: resolution-class clauses bench-proven (sessions 4/5; session 8 adds live wire evidence — SET_RESOLUTION wire-10 produced a 27377 B / 137-chunk full vs QVGA's 7152 B / 36, the setting demonstrably taking effect on the sensor) with no regression surface since (camera_manager.cpp absent from every round-#8/#10/#11 commit — verifier diff-verified). ONLY the pairwise visible-effect judgments remain — zero non-RESOLUTION settings commands for the SEVENTH consecutive round (session aborted on the D1 recurrence first; verifier-grepped). See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Zero command timeouts at session 8 (verifier-grepped base2.log — no regression, no forbidden terminals); the retry machinery's exercise evidence stands on sessions 5/6/7 (honest terminalization at exactly 3 retries, held-command lifecycles to ACKED). The D1 crash is a firmware-death defect, not a retry-machinery defect |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED (coincidental-reliance) | The manual half gained CURRENT-firmware evidence: session 8's image 37 is a clean end-to-end manual-trigger proof on round-#11 firmware (queue -> ACK -> capture -> thumb 8/8 + full 36/36 COMPLETE, base2.log:74-75/:238-239) — the first clean full-loop proof since the regression appeared. Test 4 PASS stands (interval cadence, disable semantics, shared ID sequence; NVS ID sequence continued 37->38->39 across the crash reboot, balloon2.log:1318). ADVISORY persists: the auto-capture half's bench evidence predates 01-19/02-04 changes; the WR-03 discriminator has still never run on current HEAD (zero AUTO_CAPTURE_ENABLE at sessions 6/7/8). See coincidental_reliance_items |

**Score:** 4/5 truths verified (1 present, behavior-unverified — the visible-effect clause, seventh round riding)

### 01-25 Round Truths (the round's own acceptance)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Every crash address decoded against the proven deployed ELF | ✓ VERIFIED | Debug doc §1 with the provenance cross-check first (ELF SHA 3d2b351b4 + the two inline-symbol reproductions); ROM-range and CORRUPTED terminals recorded unresolvable-with-reason. This verifier re-verified the doc exists with all four sections and independently reproduced the round's OWN decode methodology on the session-8 ELF (see spot-checks) |
| 2 | One root cause ranked, every suspect dispositioned | ✓ VERIFIED (then honestly superseded by bench) | §2 dispositions all four suspect classes with file:line; §3 ranks internal-RAM corruption with evidence for/against and the stated limit. Session 8 then disconfirmed the ranked mechanism as SUFFICIENT — recorded in §6 with the new axis; that is the debug process working, not a plan failure |
| 3 | Crash determinism analyzed; bench discriminator named | ✓ VERIFIED | §4 answers first-post-boot mechanically and names the D1/D2 discriminators; session 8's [MEM] evidence then ANSWERED the determinism question (not first-post-boot-deterministic) — recorded in G-01-10's missing list |
| 4 | D1 fix implements the ranked lever; protected surfaces untouched | ✓ VERIFIED (bench-disconfirmed, honestly held open) | Warm-up at image_tx_manager.cpp:89-98 + [MEM] instrumentation :130-147/:300 with the removal condition named in-source; wire format/quiet gate/D-05-D-07 untouched (CMD_TX_CHANNEL_QUIET_MS=750 intact at command_sender.h:25; harness exit 0 — verifier-run). WINDOWS 15 stayed open per the plan's own prohibition; the bench then re-opened it further |
| 5 | D2 receipt-ever flag makes the zero stamp inert | ✓ VERIFIED (bench-proven) | All five source sites verifier-read (header :242, ctor :39, begin :79, gate :623, stamp :964); the hold line NOT muted (fires live at :335/:1389); bench-proven at both session-8 boots — G-01-11 RESOLVED / WINDOWS 16 fixed |
| 6 | Both targets build green; harness exit 0 on the fix commit | ✓ VERIFIED | Executor pre-flight on record; the retained ELF (Aug 28 09:49) matches the deployed build banner (balloon2.log:120 'Build: Aug 28 2026 09:48:55' — verifier-read); harness re-run by this verifier at HEAD f410b4e, exit 0 |
| 7 | Ledger honesty: 15/16 stay open pending bench | ✓ VERIFIED | Both stayed open after 01-25 (front-matter counts unchanged then); session 8 then flipped 16 legitimately on quoted hardware evidence and extended 15 — zero code-level flips |

### 01-26 Round Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | WR-01..03 routed as WINDOWS 17-19 in both copies, counts reconciled | ✓ VERIFIED | Table + JSON entries 17/18/19 present citing review 7d96a98 + re-verification #9; counts 2 open / 17 fixed / 19 total reconcile across front matter, table, and JSON (verifier re-parsed) |
| 2 | Dispositions DECIDED (fix/waive+reason), operator's at the checkpoint | ✓ VERIFIED (auto-advanced, pending confirmation) | Option-a (fix all three) auto-selected under workflow.auto_advance per the 01-23 house convention, recorded in all three entries' reasons with the provenance; joins 01-23's and 01-25's selections as the THIRD pending end-of-phase operator confirmation (human item below) |
| 3 | WR-01 ps_malloc fall-through | ✓ VERIFIED | image_tx_manager.cpp:355-367 — nullptr/0/0 full with the renamed unconditional 'full dropped, thumbnail still pushes' log, falls through to the unchanged thumb branch (:380); both-empty guard intact; oversize/thumb-failure branches byte-identical. Closed on code evidence with the unstageable trigger (on-demand PSRAM exhaustion) named — entries 8-14 convention |
| 4 | WR-02 honest health-check branch | ✓ VERIFIED (bench-proven) | main_balloon.cpp:577-585 — active camera warns only on absent esp_camera_sensor_get handle (:578-581, allPassed cleared); inactive camera gets the skipped note (:584). Bench: zero 'health check failed' lines in the whole balloon2.log (verifier-grepped count 0); honest skipped notes at :109-110/:1328-1329; second boot = the crash-reboot stress boot |
| 5 | WR-03 uint32 end-to-end | ✓ VERIFIED | main_basestation.cpp:81 (uint32 member), :2208-2212 (uint32 compare + store, no casts); negative grep zero 'static_cast<uint16_t>(acked'; counter/cadence/thresholds untouched. Closed on code evidence with the unstageable trigger (65536-ACK wrap) named |
| 6 | Builds + harness on the approved subset | ✓ VERIFIED | Executor verify chain on record; harness re-run by this verifier, exit 0 |

### 01-27 Round Truths (the bench session's acceptance)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | D1 acceptance: spaced smoke AND series A with zero crash signatures | ✗ FAILED (re-opened) | Smoke SURVIVED (image 37 both kinds COMPLETE — the session-7 crash-1 pattern dead); series A never reached; crash at window 96..109 chunk 1/14 — rst:0x7 + Saved PC 0x40376430 (tick_hook), [MEM] healthy. Signature counts (verifier-reproduced): i2cWrite=2 (non-adjacent BMP280 noise), Guru=0, canary=0, rst:0x7=1 (the fatal), rst:0xc=0, U+FFFD=0. The 01-25 fix DISCONFIRMED as sufficient; debug round #2 routed |
| 2 | D2 discriminator: boot windows clean before first genuine inbound request | ✓ VERIFIED | Boot 1 (:85-135) zero hold lines before the first inbound at :260; boot 2 (:1238-:1383) zero before :1384; the session's exactly-2 fires traffic-correlated (:335 mid-window-service with chunks :329-333 immediately prior; :1389 five lines after the rejected-but-received request :1384-1388). G-01-11 RESOLVED / WINDOWS 16 fixed |
| 3 | G-01-7 series-A acceptance | ✗ NOT RUN (unjudgeable) | Series never ran (two CAPTURE_NOWs minutes apart — verifier-verified by log position :208 vs :604); zero round-#10 discriminators (verifier-grepped 0/0/0); image-38 INCOMPLETE crash-caused. Seventh round riding |
| 4 | SC-3 visible-effect pairs judged | ✗ NOT RUN | Zero SET_BRIGHTNESS/contrast/saturation/quality commands (verifier-grepped both consoles); seventh round riding |
| 5 | WR-03 cadence discriminator quoted | ✗ NOT RUN | Zero AUTO_CAPTURE commands; seventh round riding; SC-5 advisory NOT discharged |
| 6 | CIF-cycle (wire-8) + QVGA-restore clauses | ✗ NOT RUN | The executed SET_RESOLUTION was wire-10/SVGA (operator deviation, recorded); its full never completed (crash); no QVGA restore |
| 7 | Dashboard glance operator-LOOKED | ✗ NOT RUN | Session aborted at image 38; /api/state serving (base2.log:527/:532) but no LOOK verdicts |
| 8 | WR-02 bench discriminator: healthy boots log no camera failure | ✓ VERIFIED | Zero 'health check failed' lines in the whole console; honest skipped notes at both boots (verifier-grepped + sed) |
| 9 | Ledger honesty: flips only on operator-observed evidence; failed series left open | ✓ VERIFIED | Exactly ONE gap flipped (G-01-11, on quoted boot-window + timestamp-correlation evidence); G-01-10 re-opened with the dump decoded + appended; G-01-7 updated unjudgeable; WINDOWS 16 flipped / 15 extended / 3 annotated; counts 2/17/19 reconcile (verifier re-parsed); REQUIREMENTS untouched (requirements-completed: [] honored — correct for a failed session) |
| 10-16 | UI-SPEC lifts (7 explicit + LED-truth backstop) | ⚠️ UNVERIFIED-BY-LOG | No dashboard session occurred; nothing observed either way — no pass claimed, all ride the post-D1 bench glance |

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer | Phase 3 backlog | Todo on disk (verifier-verified) |
| 2 | Gallery thumb number overlay + Incomplete-badge copy | Phase 3 backlog | Todos on disk (verifier-verified) |
| 3 | Thumb-first image delivery + antenna-pointing overlay | Phase 3 backlog | Todos on disk (verifier-verified, captured at 898fcc6) |

Step 9b check: no later milestone phase exists to defer the riding clauses to — Phases 2 and 3 are already executed and Phase 1 closes last. The D1 debug round #2 + post-D1 bench moment are Phase-1 closeout activities routed in STATE.md Next Steps, not later-roadmap deferrals, so they remain gaps.

### Required Artifacts (round #11: plans 01-25, 01-26, 01-27)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `.planning/debug/d1-crash-regression-push-start.md` (01-25) | decode table, suspect dispositions, ranked root cause, determinism + discriminators | ✓ VERIFIED | 447 lines, six sections (§1-§5 from 01-25; §6 session-8 recurrence appended by 01-27 — commit a2a14a7 +129 lines, verifier-read); the addr2line invocation and address list recorded; every load-bearing claim this checker sampled reproduces (ELF SHA exact; 0x40376430 decode reproduces verbatim under this verifier's own toolchain run) |
| `src/image_tx_manager.cpp` + `include/image_tx_manager.h` (01-25) | warm-up + [MEM] instrumentation + receipt-ever flag | ✓ VERIFIED | Warm-up :89-98 (ps_malloc + memset 0xA5 + free, result log); [MEM] watch :130-147 (1 s throttle, monotone new-low only, removal condition named) + logMemDiagnostic :300 + enqueue call :322; flag at header :242 / ctor :39 / begin :79 / gate :623 / stamp :964. Commit 8539c4c scope exactly 3 files (+122/-9) |
| 01-26 fix files (3 fixes) | WR-01 fall-through, WR-02 honest branch, WR-03 uint32 | ✓ VERIFIED | Each re-derived at source (line-cited above); commit 8041b27 scope exactly 4 files; WR-01/03 closed on code evidence with unstageable triggers named BY NAME; WR-02 bench-verified |
| `balloon2.log` / `base2.log` (01-27, retained session evidence) | every citation locatable | ✓ VERIFIED | 1,645 / 739 lines. Verifier re-read every load-bearing citation: the fatal window (:1220-1231), [MEM] (:611/:1175), warm-up (:102/:1321), boot banners (:120/:1242 'Build: Aug 28 2026 09:48:55' matching the retained ELF mtime), smoke capture (:207-213), SET_RESOLUTION (:589-592), image-38 capture (:604-609), enqueues (:217/:614), D2 fires (:335/:1389) with their traffic contexts, first-inbound lines (:260/:1384), base finalize lines (:74-75/:238-239/:290-291/:651-652), post-reboot rejects (:1385/:1398), NVS next-ID (:1318) — all reproduce. Naming deviation from balloon13/base8 recorded, files never renamed (the 01-24 convention) |
| `01-UAT.md` / `WINDOWS.md` / `STATE.md` (01-27) | honest failed-session record, one legitimate flip, D1 re-opened | ✓ VERIFIED | Read in full: G-01-10 carries the session-8 root_cause extension (disconfirmation + new axis) with the missing list updated (debug-round item DONE, determinism ANSWERED, round-#2 item OPEN); G-01-11 resolved with verbatim verified_by quotes; G-01-7 unjudgeable update; WINDOWS 2/17/19 reconciled; STATE.md Current Status + Next Steps name the debug round #2 brief and /gsd-secure-phase 1 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| handleWindowRequest (any inbound) | inboundWindowRequestSeen=true + stamp | busy-hold gate in pushPending | ✓ WIRED (bench-proven) | Stamp :960 + flag :964 (before kind validation, trust-boundary comment intact) -> gate :623-630; session 8 exercised the link live: the rejected request at :1384 stamped the flag and the hold correctly fired at :1389 |
| begin() | PSRAM warm-up + instrumentation baselines | first-use frontier moved to boot | ✓ WIRED (bench-disconfirming) | :89-98 + :103-105; ran at both boots (:102/:1321 — the fix provably in-image); the crash recurred anyway on healthy [MEM] — the lever is wired but the hypothesis it encodes is disproven as sufficient (honestly recorded, instrumentation stays) |
| enqueueCapture full-alloc failure | nullptr/0/0 full + named degradation log | thumbnail branch fall-through | ✓ WIRED (bench-unexercised) | :356-367 -> :380; unstageable trigger (on-demand PSRAM exhaustion) named in WINDOWS 17 |
| esp_camera_sensor_get (boot health check) | warn only on absent handle | honest warning channel | ✓ WIRED (bench-proven) | :577-585; discriminated live at both session-8 boots (zero failures, honest skipped notes) |
| getCommandsAcked (uint32) | ackedAtLastPoll (uint32) | LED-truth refresh compare | ✓ WIRED (bench-unexercised) | :2208-2212, zero truncating casts; unstageable trigger (65536-ACK wrap) named in WINDOWS 19 |
| Previously-verified mechanisms (quiet gate, camera gate, baseline, framers) | unchanged through round #11 | regression greps + commit scopes | ✓ WIRED | CMD_TX_CHANNEL_QUIET_MS=750 / channelQuietForTx (:575-579) + hold max (:603-607) intact; commandRequiresCamera untouched; framers untouched (8539c4c/8041b27 scopes exclude command_handler/command_sender); harness exit 0 |

### Data-Flow Trace (Level 4)

Not applicable this round — 01-25's changes are boot-time warm-up + diagnostics + a gating flag; 01-26's fixes are degradation/warning/type-widening paths with no new rendered data; 01-27 is a bench/ledger plan. (The [MEM] diagnostic's own data flows from real heap APIs — esp_get_minimum_free_heap_size / uxTaskGetStackHighWaterMark / psram stats — verifier-read at :300-315, and its live output is in balloon2.log:611/:1175.)

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Wire-format regression harness | `node scripts/verify_protocol_roundtrip.mjs` | exit 0, all checks PASS (verifier-run twice at HEAD f410b4e) | ✓ PASS |
| Deployed-ELF provenance | `sha256sum .pio/build/esp32-s3-balloon/firmware.elf` | c53635e1819a6ca5107b1437450dbc324e5848e685b50de73ea6a4387c8e0553 — exact match to the debug doc §6.1 | ✓ PASS |
| Fatal-address decode (independent) | `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf 0x40376430 0x403c88b8` | `tick_hook at esp-idf/components/esp_system/int_wdt.c:111` / `?? ??:0` — reproduces the doc's decode table verbatim | ✓ PASS |
| Session-8 crash citations | sed balloon2.log:1220-1231; base2.log:651-652 | armed 96..109 -> chunk 1/14 sent -> rst:0x7 + Saved PC:0x40376430 all verbatim; INCOMPLETE 109/137 after 3 passes verbatim | ✓ PASS |
| D2 boot-window + fire citations | grep 're-announce held' balloon2.log; sed contexts | exactly 2 fires (:335, :1389); :335 follows window chunks :329-333; :1389 follows the rejected request :1384-1388; first inbound :260 — all reproduce | ✓ PASS |
| Crash-signature counts | grep -c per signature, whole balloon2.log | i2cWrite=2 (:885/:1577, non-adjacent, each followed by a successful BMP280 read), Guru=0, canary=0, rst:0x7=1, rst:0xc=0, U+FFFD=0 — matches the ledger claims exactly | ✓ PASS |
| Riding-clause NOT-RUN confirmation | grep SET_*/AUTO_CAPTURE/discriminators, both logs | 0 settings commands beyond SET_RESOLUTION, 0 AUTO_CAPTURE, 0/0/0 round-#10 discriminators, 2 CAPTURE_NOWs total (spaced) | ✓ PASS |
| Source drift + commit scopes | `git diff --stat HEAD -- src/ include/`; `git show --stat` 8539c4c/8041b27/0e84ca1 | drift empty; scopes exactly the plans' files; camera_manager/image_rx/UI markup untouched (0 commits since 921013d) | ✓ PASS |
| WR-03 negative check | grep 'static_cast<uint16_t>(acked' src/main_basestation.cpp | zero matches | ✓ PASS |
| WINDOWS integrity | full-file read + count | 19 entries, open ids exactly {3,15}, 2/17/19 reconciles across front matter / table / JSON | ✓ PASS |
| Zero command timeouts | grep -c 'timeout after' base2.log | 0 (session 7 had 2 forbidden terminals) | ✓ PASS |
| WR-02 discriminator | grep -c 'health check failed' balloon2.log | 0; honest skipped notes at :109-110/:1328-1329 | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` probes exist in this project (scripts/ holds embed_web_assets.mjs and verify_protocol_roundtrip.mjs only); the phase's executable verification is the wire-format harness (run above) — SKIPPED (no probes declared).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| CTRL-01 | 01-25/01-27 (+prior) | User can trigger camera capture from base station web interface | ✓ SATISFIED (regression note) | Session 8: 2/2 dashboard triggers -> ACK -> capture on round-#11 firmware, one (image 37) fully end-to-end COMPLETE — the first clean full-loop proof since the regression; the D1 crash bounds the session, routed as the phase's top blocker |
| CTRL-02 | 01-26/01-27 (+prior) | User can adjust all camera settings remotely | ✓ SATISFIED (riding note) | All 7 settings execute for real (01-04 sensor setters; ACKs on the wire); resolution class re-proven live at session 8 (wire-10 produced SVGA-class payload sizing); the visible-effect pair judgment rides (SC-3 clause, seventh round) |
| CTRL-03 | 01-27 (+prior) | Manual + automatic capture modes | ✓ SATISFIED (riding note) | Manual bench-proven on current firmware (image 37 end-to-end); auto-capture Test 4 PASS (WR-03 discriminator rides) |
| CTRL-04 | 01-27 (+prior) | Fixed interval timing | ✓ SATISFIED | AutoCapture wraparound-safe timer; Test 4 cadence PASS; NVS ID sequence held across the crash reboot (37->38->39, balloon2.log:1318) |
| CTRL-06 | 01-25/01-26/01-27 (+prior) | Failed commands retried with timeout | ✓ SATISFIED | Zero timeouts at session 8 (no regression); honest-terminalization evidence stands on sessions 5/6/7; retry-ordinal bound unchanged |
| PRI-02 | 01-25/01-27 (+prior) | Retry mechanism with timeout for failed transmissions | ✓ SATISFIED | Same evidence as CTRL-06; quiet-gate constants intact through round #11 (verifier-read) |

Riding IDs per the project's gap-closure convention (marked Complete in REQUIREMENTS.md from their owning phases; no new claims this round): IMG-01/IMG-03/IMG-04/PRI-03 (01-25), IMG-02/IMG-03 (01-26), IMG-02/IMG-03 + the six above (01-27 — requirements-completed: [] on the failed session, the 01-24 convention). No orphaned requirements: REQUIREMENTS.md maps exactly CTRL-01..04, CTRL-06, PRI-02 to Phase 1, all Complete; REQUIREMENTS.md was NOT touched by 01-27 (honored).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (all round-#11 source files) | - | TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER scan | — | CLEAN — zero matches across all 4 touched source/header files (verifier-run, per-file counts 0) |
| src/image_tx_manager.cpp | 130-147, 300-315 | [MEM] diagnostic instrumentation (temporary by design) | ℹ️ Info | Bounded (per-enqueue + 1 s monotone new-low), removal condition named in-source AND its stay-extended condition is correct (G-01-10 did not close — WINDOWS 15 records it); not a stub |
| src/main_balloon.cpp | 570 | 'Method doesn't exist yet' commented communication health check (pre-existing, WR-02's neighbor) | ℹ️ Info | Pre-dates this round; the honest skipped-note now prints for it; carried in prior rounds' Info lists |

### Decision Coverage

CONTEXT.md decisions for this phase were translated in earlier rounds and re-checked at prior verifications; round #11 introduced no new decisions (its three checkpoint decisions are recorded in SUMMARYs/ledger entries above). No drift found.

### Human Verification Required

These items need the physical bench / operator judgment; all are routed (STATE.md Next Steps: debug round #2 then the bench moment), not dropped:

### 1. SC-3 visible-effect pairs (seventh round riding)

**Test:** At fixed QVGA: SET_BRIGHTNESS -2, capture; SET_BRIGHTNESS +2, capture; judge the pair visibly different. Repeat for one other class (contrast -2/+2, saturation -2/+2, or quality 10 vs 30).
**Expected:** Visible pairwise differences, per-pair verdicts recorded in Test 3.
**Why human:** Sensor visible-effect acceptance needs the physical camera and operator judgment; seven bench rounds have issued zero non-RESOLUTION settings commands.

### 2. WR-03 cadence discriminator

**Test:** AUTO_CAPTURE_ENABLE at 20 s; let one interval capture fire; trigger a manual CAPTURE_NOW; compare the next interval capture's timestamp to the manual one.
**Expected:** Next interval capture lands ~20 s AFTER the manual capture (both 'Captured image ID' timestamps quoted). Also discharges the SC-5 coincidental-reliance advisory.
**Why human:** Needs the running firmware pair and wall-clock observation; never run on current HEAD.

### 3. G-01-7 series-A re-run (after debug round #2)

**Test:** 3 back-to-back unspaced captures on post-fix firmware.
**Expected:** 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls, with the round-#10 discriminator lines proving lever engagement. The D2 discriminator now reads clean (fixed at session 8), so the busy-hold lines this series will watch are truthful by construction.
**Why human:** RF-pair behavior under real half-duplex load; the series has been aborted/unreached seven rounds running.

### 4. Dashboard LOOK glance + UI-SPEC lifts + CIF/QVGA clauses

**Test:** Exercise the dashboard (d8ba14e gallery detail still unlooked-at after three sessions); CIF cycle (SET_RESOLUTION wire-8) with both kinds COMPLETE at CIF sizing, QVGA restore with wire-evidence sizing, zero FB-OVF.
**Expected:** All seven UI-SPEC considerations + LED-truth backstop hold; gallery detail fields render; settings clauses all judged as specified.
**Why human:** Browser observations leave no console evidence; operator-looked (not did-not-flag) is the bar; the wire-8 CIF clause has never run as specified.

### 5. Three pending auto-advanced disposition confirmations (human decision points)

**Test:** Confirm (or route back) the three auto-selected dispositions now pending end-of-phase operator confirmation: 01-23's fix-all (WINDOWS 10-14), 01-25's fix-shape option-a + instrumentation ride-along, and 01-26's option-a fix-all (WINDOWS 17-19). Note 01-25's lever choice is now bench-disconfirmed as sufficient — the confirmation question for it is whether the warm-up stays (harmless, first-use frontier still worth moving) while round #12's fix supersedes it.
**Expected:** Each confirmation (or route-back) recorded in the ledger.
**Why human:** Acceptance of robustness-edge-case risk and fix-shape selection is a maintainer judgment; all three were auto-advanced under workflow.auto_advance and honestly recorded as pending.

### Gaps Summary

Three structured gaps, all honestly ledgered:

1. **D1 crash regression RE-OPENED (G-01-10 / WINDOWS 15, BLOCKER)** — the 01-25 fix is disconfirmed as sufficient: the first-post-boot crash class is dead (spaced smoke survived), but a mid-service class is newly discriminated — TG0WDT reset at the first chunk of a re-armed FULL window with healthy [MEM], zero corruption corroboration, Saved PC in the WDT tick machinery, zero project frames. New axis named (task-watchdog starvation / loopTask-or-kernel block during SUSTAINED FULL window service); debug round #2 is the routed next step with the retained ELF + decode + §6 evidence. The crash class is NOT confined to bursts (session-8 captures were spaced). This remains the phase's top blocker: no complete clean bench session exists on HEAD until it closes.
2. **G-01-7 series-A burst full-delivery** — seventh round unjudgeable: the series never ran; image 38's INCOMPLETE is crash-caused. Levers wired, unvalidated; re-run gated behind D1 debug round #2 (the D2 discriminator pollution it suffered is now fixed, so the next run reads clean).
3. **The riding bench-moment clauses** — SC-3 visible-effect pairs (seventh round), WR-03 cadence discriminator, CIF-cycle-as-specified + QVGA restore, operator-LOOKED dashboard glance + UI-SPEC lifts. All routed; all need one clean post-D1 session.

The prior verification's gap 4 (unrouted review warnings) is CLOSED — the only gap class the ledgers did not carry last round is now carried, dispositioned, fixed, and (for WR-02) bench-verified.

SUMMARY vs Reality: no divergences found this round — every load-bearing claim in 01-25/01-26/01-27-SUMMARY that this verifier checked (all five D2 flag sites, both D1 levers, all three review fixes, the ELF SHA, the addr2line decode — independently re-run, every crash/hold/[MEM]/finalize/finalize-INCOMPLETE citation, all grep counts, the spaced-capture fact, the discriminator zero-counts, ledger integrity, the one legitimate flip, naming-deviation handling) reproduces. The 01-27 record again honors the failed-session convention: nothing forced, one independently-proven truth closed on quoted evidence, the blocker re-opened with decoded evidence.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — remains achieved and hardware-proven (sessions 1-6; and session 8 re-proved the command loop live on round-#11 firmware with the cleanest command showing of the post-regression era plus one full end-to-end manual capture). But HEAD firmware still hard-crashes during sustained FULL-window service with the root cause narrowed yet unfixed, the burst-delivery optimization remains unjudged after seven attempts, one settings clause remains operator-unjudged for seven rounds, and the riding clauses accumulate. The honest verdict is gaps_found with a clear next action: debug round #2 (the WDT-starvation audit), then one clean bench session that judges everything at once.

---

_Verified: 2026-08-28T10:35:00Z_
_Verifier: Claude (gsd-verifier)_

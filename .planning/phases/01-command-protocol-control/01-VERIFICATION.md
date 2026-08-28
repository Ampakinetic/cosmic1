---
phase: 01-command-protocol-control
verified: 2026-08-28T14:10:00Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3, narrowed to its final clause for the EIGHTH consecutive round: resolution-class clauses remain bench-proven (sessions 4/5; camera_manager.cpp untouched by rounds #8/#10/#11/#12 — commit scopes verifier-read, zero source drift at HEAD 269f6b7); session 9 issued zero commands of ANY settings class (verifier-grepped balloon3.log/base3.log: SET_BRIGHTNESS/contrast/saturation/quality = 0, SET_RESOLUTION = 0) — only the pairwise visible-effect judgments remain operator-unjudged
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed: [] # NONE of the three #10 gaps closed on hardware: D1 re-crashed (third expression), the series never ran, the riding clauses never ran. The routed CODE half of gap 1 did land and is verifier-confirmed: debug round #2's §7 audit exists in full (WDT topology pinned, window reconstructed, all 8 project-code candidates eliminated, ONE-mechanism-family verdict, ranked cause with the honest limit), Lever A (yielding bounded TX-drain) + B1/B2 discriminators are wired at HEAD (e32_lora.cpp:230-256, image_tx_manager.cpp:153/:198, image_protocol.h:129), builds 2/2 SUCCESS and harness exit 0 both VERIFIER-RE-RUN this round — but the bench disconfirmed the fix as sufficient, so the gap truth itself did not move
  gaps_remaining:
    - "D1 crash regression (G-01-10 / WINDOWS 15) — THIRD EXPRESSION: the 01-28 Lever-A fix is DISCONFIRMED as sufficient a second time. Session 9's crash MOVED from mid-window-service (session 8) to the INTER-WINDOW LULL after a cleanly completed FIRST window: balloon3.log:421 armed 0..15 -> :427-:491 all 16/16 chunks on air -> lull :495-:519 (GET_STATUS, [BCN] seq=23, [MEM] healthy, loop Max 1277 ms) -> :520 i2cWrite ESP_ERR_INVALID_STATE WITH the session-7 corruption-class mojibake ON the line -> :523 rst:0x7 TG0WDT_SYS_RST -> :524 Saved PC 0x4037c7fa. Verifier independently reproduced the ELF provenance (SHA256 e09dd034a5ab... exact, mtime 12:06 matching the deployed banner) and the addr2line decode (esp_vApplicationTickHook freertos_hooks.c:34 — one frame above session-8's tick_hook, same tick-ISR chain, zero project frames). The crash-adjacent on-line-mojibake i2cWrite BREAKS the sessions-7/8 'non-adjacent BMP280 noise' classification. Debug round #3 routed (debug doc §8.6: lull-phase candidate audit — the §7.3 arithmetic never covered the lull set; the I2C+mojibake+WDT convergence question; why the crash point moved)"
    - "G-01-7 series-A burst full-delivery: EIGHTH round unjudgeable — the series never ran (session 9's single CAPTURE_NOW is the spaced smoke itself; aborted at protocol step 3). Verifier-grepped zero round-#10 discriminators (0 'budget re-armed', 0 'full-pull activation deadline reached', 0 'evicting class 5'). Image 39's INCOMPLETE 15/36 (base3.log:194) is crash-caused (chunk 7 ordinary RF loss + post-crash NACK_INVALID x3), not burst-delivery"
    - "The riding bench-moment clauses — EIGHTH round: SC-3 visible-effect pairs (zero settings commands), WR-03 cadence discriminator (zero AUTO_CAPTURE; SC-5 advisory NOT discharged), CIF-cycle wire-8 + QVGA restore (zero SET_RESOLUTION — not even the session-8 wire-10 deviation this time), dashboard operator-LOOKED glance + UI-SPEC lifts. All routed to the post-D1 bench moment"
  regressions:
    - "NONE new in code: round #12's code work (Lever A + B1/B2) is verified wired at HEAD 269f6b7 with builds 2/2 and harness exit 0 (both verifier-re-run); zero source drift; commit scopes exact (6ca9a36 = 7 files e32/image_tx/protocol + 2 ledgers; f25b41b/a5dd90b/269f6b7 docs-only). camera_manager.cpp and image_rx_manager.cpp untouched (no regression surface for the bench-proven settings clauses). The D1 recurrence is the SAME open gap evolved to a third expression — a bench failure of the fix hypothesis, not a new code regression. D2 (WINDOWS 16) and WR-02 (WINDOWS 18) regression watches both GREEN on the round-#12 firmware (2 genuine hold fires :492/:683; zero health-check-failed) — the previously-closed entries held"
gaps:
  - truth: "The balloon survives post-capture radio push through sustained FULL-window service AND its inter-window lulls (G-01-10 / WINDOWS 15, the phase's top blocker): a capture's enqueue/manifest/chunk/window/lull sequence never hard-resets the firmware, so buffers survive to serve the transfer"
    status: failed
    reason: "Bench session #9 (01-29, round-#12 firmware with Lever A + B1/B2 provably in-image — warm-up :102/:614, [BOOT] :103/:615): the smoke's FIRST window served CLEAN (image 39 thumb COMPLETE 9/9 base3.log:79; FULL window 1 armed balloon3.log:421, all 16/16 chunks on air :427-:491) but the balloon reset at t=124026 ms in the INTER-WINDOW LULL — :520 i2cWrite ESP_ERR_INVALID_STATE carrying the session-7 corruption-class mojibake bytes ON the line (replacing the T of STATE, hex-verified; the corrected §7.4 grep — the UTF-8-replacement-char grep returns 0, the twice-documented trap) DIRECTLY ADJACENT to :523 rst:0x7 TG0WDT_SYS_RST, :524 Saved PC:0x4037c7fa = esp_vApplicationTickHook freertos_hooks.c:34 (verifier decode reproduces verbatim vs ELF e09dd034). The 01-28 fix is DISCONFIRMED as sufficient a SECOND time. The round-#12 discriminators ANSWERED cleanly and informatively (§8.3): B2 [LOOP] = ZERO fires with loopTask demonstrably healthy (Max 1277 ms < the 2500 ms latch; loop cycled to the final second :518-:519) — the session-8 CPU0-side signature reproduced AND the Lever-A loop-hang class disconfirmed; B1 = TASK_WDT named by the ROM reason register at boot 2 (:615); [MEM] healthy a third session (:372/:511). Crash-signature counts (verifier-reproduced): rst:0x7=1, rst:0xc=0, Guru=0, canary=0, [LOOP]=0, i2cWrite=1 (the adjacent one), mojibake=1 (on :520 itself). Downstream: image 39 full INCOMPLETE 15/36 after 3 passes (base3.log:194) — crash-caused (chunk 7 ordinary RF loss, retransmits hit the crashed balloon, NACK_INVALID x3 :162/:171/:181 vs honest post-reboot rejections balloon3.log:679/:692/:713); zero command timeouts"
    artifacts:
      - path: "src/e32_lora.cpp"
        issue: "Lever A (yielding bounded TX-drain, :230-256) is wired and green but disconfirmed as sufficient — correctly kept (hazard removal); not the cure"
      - path: ".planning/debug/d1-crash-regression-push-start.md"
        issue: "§8 carries the full session-9 evidence + the round-#3 brief — §8.6 names the unaudited lull-phase candidate set (Wire-0 transactions, GPS UART1 reads, NVS/flash ops) and the I2C+mojibake+WDT convergence question"
    missing:
      - "Debug round #3 (round #13): audit the INTER-WINDOW-LULL candidate set with the §7.3 arithmetic it never had, against the deployed ELF e09dd034 with Saved PC 0x4037c7fa = esp_vApplicationTickHook; answer §8.6's three questions (lull-vs-service starver fit; what the adjacent on-line-mojibake I2C failure implies — driver-state corruption co-effect vs bus wedge with interrupts masked vs coincidence; why the corruption signature returned ADJACENT this time); consider §8.6's discriminator menu (CPU0 idle-observability counter, I2C health instrumentation, TWDT stage-0 subscription check); fix, then bench-verify"
      - "Re-establish crash-context determinism: the crash point MOVED across three expressions (first-post-boot push -> mid-service of re-armed window -> inter-window lull) — the only constants are TG0WDT stage-1, PC in the tick-ISR chain, zero project frames, loopTask healthy, [MEM] healthy. Even SPACED single captures are not safe (session 9 crashed on the FIRST image's lull)"
  - truth: "G-01-7 series-A acceptance (burst full-delivery): 3 back-to-back unspaced captures — 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls, with the round-#10 discriminators proving lever engagement (or the residual mechanism named)"
    status: failed
    reason: "UNJUDGEABLE — EIGHTH round: session 9 aborted at protocol step 3 on the D1 third expression before any unspaced burst (the single CAPTURE_NOW at balloon3.log:364/:370 is the spaced smoke). Commands clean (seq 1/2/3 ACKed base3.log:28/:61/:89; zero command timeouts) and image 39's thumb COMPLETE 9/9 — but zero round-#10 discriminators engaged (verifier-grepped 0/0/0). Image 39's kind-1 INCOMPLETE 15/36 is CRASH-CAUSED, not burst-delivery (verifier-read: window 1 served 16/16 clean; chunk 7 ordinary RF loss; retransmits hit the crashed balloon). The 01-21/01-22 levers remain unverified, not disproven"
    artifacts:
      - path: "src/image_tx_manager.cpp"
        issue: "01-21/01-22 levers present and wired (untouched by round #12 — commit scope verifier-read); no bench validation exists — D1 recurrence precedes any series-A re-run for the third consecutive session"
    missing:
      - "D1 debug round #3 + fix first, then the series-A re-run: 3 unspaced captures, 6/6 COMPLETE, zero timeouts, with the discriminator lines ('re-announce budget re-armed', receipt-evidenced class-5 eviction labels, base 'full-pull activation deadline reached') proving lever engagement or naming the next mechanism"
  - truth: "The bench-moment clauses: SC-3 visible-effect pairs judged; WR-03 cadence discriminator quoted; CIF-cycle (wire-8) + QVGA-restore clauses exercised; dashboard operator-LOOKED glance + UI-SPEC lifts"
    status: partial
    reason: "NONE ran — EIGHTH consecutive round riding: session 9 aborted at protocol step 3 before ANY settings or auto-capture command (verifier-grepped: 0 SET_BRIGHTNESS/contrast/saturation/quality, 0 SET_RESOLUTION, 0 AUTO_CAPTURE in BOTH consoles — not even session 8's wire-10 deviation repeated). The dashboard glance was not reached (commands flowed through it but no LOOK verdicts). All honestly recorded in 01-UAT.md (Test 3/Test 4 session-9 updates verifier-read) and routed in STATE.md Next Steps"
    artifacts:
      - path: ".planning/phases/01-command-protocol-control/01-UAT.md"
      issue: "Test 3 stays 'issue' — the visible-effect-pair clause is its last open settings clause (CIF/QVGA-restore clauses also riding: session-5-proven, camera_manager.cpp untouched through HEAD, but the re-exercise never ran)"
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
    why_human: "Sensor visible-effect acceptance needs the physical camera and operator judgment — EIGHT consecutive bench rounds have issued zero non-RESOLUTION settings commands (session-9 command mix verifier-grepped: 1 CAPTURE_NOW, 3 IMAGE_WINDOW_REQUEST-class, GET_STATUSs; the session aborted on the D1 recurrence before any settings step). The resolution clause no longer rides: growth/restore/reboot were bench-proven at sessions 4/5 and no round since has touched camera_manager.cpp (commit scopes verifier-read through HEAD 269f6b7)"
coincidental_reliance_items:
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5)"
    reason: "fixture-only"
    harden: "The manual half has current-firmware evidence at BOTH bench sessions of the fix era: session 8's image 37 full end-to-end COMPLETE (queue -> ACK -> capture -> thumb + full COMPLETE) and session 9's image 39 through thumb COMPLETE 9/9 on round-#12 firmware (queue -> ACK -> capture -> thumb loop; its FULL died at the crash, so the current-firmware through-full proof remains session 8's). The auto-capture half still rests on Test 4's bench PASS, which predates the 01-19 baseline advance and 02-04 event triggers; non-interference is established by code reading only (zero AUTO_CAPTURE_ENABLE at sessions 6, 7, 8, AND 9 — eighth round). The WR-03 cadence discriminator at the post-D1 bench moment is precisely the hardening step — routed in STATE.md Next Steps
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #11, after gap closure 01-28 + 01-29)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-28T14:10:00Z
**Status:** gaps_found
**Re-verification:** Yes — #11, after gap-closure round #12 (plans 01-28 D1 debug round #2 + fix, 01-29 bench session #9; commits 60315b5/de6a278/6ca9a36/9261b9e/f25b41b/a5dd90b/269f6b7 — all verifier-confirmed in git log; zero source drift HEAD..working-tree in src/ and include/)

## Goal Achievement

Round #12 executed its two halves exactly as planned, and both halves are verifier-confirmed — including the half that failed. The code half (01-28) is a complete debug round of the genre's best shape: §7 pins the deployed build's watchdog topology from its own config artifacts (TG0WDT_SYS_RST = the TASK watchdog's stage-1 10 s silent hardware backstop; in this firmware only IDLE0 feeds the TWDT, so every crash proves IDLE0 starved >=10 s on CPU0 while both cores' tick chains stayed alive), reconstructs the post-chunk window (~1102 ms/pass), eliminates ALL EIGHT project-code starver candidates by duration/lock/interrupt arithmetic (the unbounded serial->flush() busy-spin is the sole non-yielding stretch but holds no kernel lock and runs on CPU1), reconciles sessions 7/8 as ONE mechanism family, ranks the CPU0 scheduler/interrupt-delivery stall with the honest limit stated, and lands the evidence-supported fix: Lever A (yielding bounded TX-drain, e32_lora.cpp:230-256 — verifier-read at HEAD with the G-01-10-citing comment and honest hazard-removal-not-cure scope) plus discriminators B1 ([BOOT] reset-cause, image_tx_manager.cpp:153) and B2 ([LOOP] slow pass, :198, IMG_LOOP_SLOW_PASS_MS 2500 at image_protocol.h:129). This verifier independently re-ran BOTH gates: builds 2/2 SUCCESS and the wire harness exit 0.

The bench half (01-29) FAILED again, honestly — and the discriminators WORKED: session 9's smoke first window served 16/16 chunks clean, then the board reset in the TX-light INTER-WINDOW LULL with a crash-adjacent i2cWrite error carrying the session-7 corruption-class mojibake ON ITS OWN LINE. The fix is disconfirmed as sufficient a second time (D1's third expression), but the round's instrumentation answered exactly what it was built to answer: B2's zero fires with a healthy loop (Max 1277 ms) reproduces the session-8 CPU0-side signature AND disconfirms the Lever-A loop-hang class; B1 named TASK_WDT from the ROM reason register; [MEM] stayed healthy a third session. The decode (0x4037c7fa = esp_vApplicationTickHook freertos_hooks.c:34, one frame ABOVE session-8's tick_hook in the same tick-ISR chain, zero project frames) reproduces under this verifier's own toolchain run against the SHA-verified deployed ELF. Nothing was forced: zero ledger flips, requirements-completed: [], the naming deviation (balloon3/base3 vs balloon14/base9) recorded, the incoming evidence packet's mojibake=0 corrected to 1 (the twice-documented grep trap, caught by the continuation executor and re-verified by this verifier at the raw-byte level), and debug round #3 routed with a sharpened brief.

What also did NOT happen, again: the SC-3 visible-effect pairs (EIGHTH round riding — zero settings commands of any class this session), the WR-03 cadence discriminator (eighth), the CIF-cycle-as-specified + QVGA-restore clauses (eighth), and the operator-LOOKED dashboard glance. All routed to the post-D1 bench moment.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; the round's own 01-28/01-29-PLAN truths are assessed separately below).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression check: round #12's commit scopes touch no UI markup (6ca9a36: e32_lora + image_tx + protocol headers + 2 ledgers; f25b41b/a5dd90b/269f6b7: docs only — verifier read the stats); the last UI change remains d8ba14e. Session 9 drove the dashboard throughout (commands seq 1/2/3 queued from it, ACKed base3.log:28/:61/:89). The LOOK glance rides the post-D1 bench |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED (regression caveat: D1) | Session 9's command loop is clean: seq 1 (GET_STATUS-class), seq 2 (CAPTURE_NOW — the smoke), seq 3 (window request) all ACKed; zero command timeouts in base3.log (verifier-counted); post-crash retransmit requests honestly rejected + NACKed against the rebooted balloon. Harness exit 0 AND builds 2/2 at HEAD 269f6b7 (both verifier-run this round). CAVEAT: the balloon still dies mid-session on the D1 axis — tracked by the D1 gap, not hidden here |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | NARROWED TO ITS FINAL CLAUSE, unchanged from #9/#10: resolution-class clauses bench-proven (sessions 4/5) with no regression surface since (camera_manager.cpp absent from every round-#8/#10/#11/#12 commit — verifier diff-verified; zero source drift at HEAD). ONLY the pairwise visible-effect judgments remain — zero non-RESOLUTION settings commands for the EIGHTH consecutive round (session aborted on the D1 recurrence at protocol step 3, before any settings step; verifier-grepped). See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Zero command timeouts at session 9 (verifier-grepped base3.log — no regression, no forbidden terminals); the retry machinery's exercise evidence stands on sessions 5/6/7 (honest terminalization at exactly 3 retries, held-command lifecycles to ACKED). The D1 crash is a firmware-death defect, not a retry-machinery defect |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED (coincidental-reliance) | The manual half added a THIRD current-generation proof point: session 9's image 39 ran queue -> ACK -> capture -> thumb COMPLETE 9/9 on round-#12 firmware (base3.log:79) before the crash bounded its full (the through-full proof remains session 8's image 37). Test 4 PASS stands (interval cadence, disable semantics, shared ID sequence; NVS sequence continued 38->39 across the crash reboot, balloon3.log:366 with image 39 the first capture). ADVISORY persists: the auto-capture half's bench evidence predates 01-19/02-04 changes; the WR-03 discriminator has still never run on current HEAD (zero AUTO_CAPTURE_ENABLE at sessions 6/7/8/9). See coincidental_reliance_items |

**Score:** 4/5 truths verified (1 present, behavior-unverified — the visible-effect clause, eighth round riding)

### 01-28 Round Truths (the debug round's own acceptance)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | WDT topology determined from the deployed build and recorded in §7.1 | ✓ VERIFIED | §7.1 read in full: TG0WDT_SYS_RST = TWDT (MWDT0/TG0) stage-1 RESET_SYSTEM @10 s with NO print path (vs stage-0 INT @5 s printing unconditionally) — answering silent-vs-panic structurally; IWDT = MWDT1/TG1 300/600 ms fed only from tick_hook with the CPU1 interlock; only IDLE0 feeds the TWDT (loopTask unsubscribed, pinned CPU1 — the arduino main.cpp:111/:113 provenance quoted). Every config/framework claim carries its path (sdkconfig:2172-2180, task_wdt_impl_timergroup.c:28-33/:124-126, int_wdt.c:32-41/:104-127, task_wdt.c:491, port_systick.c:199/:223-224, firmware.map object provenance) |
| 2 | Post-chunk execution window reconstructed (§7.2) | ✓ VERIFIED | The :1226-1227 ordering proof (the send log prints after the full transmit handshake), the ~1102 ms/pass sustained-service cadence (Performance :915->:1189 arithmetic), 8-9 passes + beacon seq 47 inside the 10 s window, and the terminal-emptiness observation (not even one BMP280 line) — all present with citations |
| 3 | Candidate-block arithmetic complete (§7.3) | ✓ VERIFIED | All EIGHT candidates dispositioned with duration/masks-interrupts/kernel-lock/cache-suspend columns; the minimum set from the plan fully covered (flush 226 ms spin but yielding semaphore + CPU1; AUX waits delay(10)-yield; OLED ~100 ms driver-based; sensors/beacon/memcpy/printf ms-class); the unbounded-flush NB (latent loopTask hang that still cannot produce TG0 stage-1 because IDLE1 is not subscribed) is the honest gem that motivated Lever A |
| 4 | One-mechanism-or-two question ANSWERED (§7.4) | ✓ VERIFIED | VERDICT: ONE mechanism family, two expressions, with four named discriminating evidence items (same tick-ISR chain both sessions; same TG0WDT class; the silence-proves-interrupt-death inference; session-7 corruption as honestly-labeled INFERENCE co-effect). The §6.4 mojibake correction (balloon2.log:155 IS one corruption-class line) and the session-6 weak point (logs not retained) both recorded honestly |
| 5 | One root cause RANKED with honest limit (§7.5) | ✓ VERIFIED | CPU0 scheduler/interrupt-delivery stall below project-code visibility; the 01-25 §3 convention invoked explicitly (elimination + blast radius ONLY; the stall's inner structure not observable pre-bench) |
| 6 | Fix implements the Task-2-selected lever at the evidence-named site; protected surfaces byte-identical | ✓ VERIFIED (bench-disconfirmed, honestly held open) | Lever A at e32_lora.cpp:230-256 (delay(1)-polled uart_ll_is_tx_idle(UART_LL_GET_HW(uartPort)) bounded 1000 ms; uartPort defaulted 2 in begin(), both call sites unchanged — verifier-read); B1 at image_tx_manager.cpp:153 (16-case local resetReasonName map — no esp_reset_reason_to_name in IDF 5.5.4 prebuilt headers, verified claim); B2 at :198 (latch-guarded, re-armed on normal pass). CMD_TX_CHANNEL_QUIET_MS = 750 intact (command_sender.h:25); [MEM] instrumentation present (7 sites); round-#10 discriminator lines present (:703 hold, :1110 budget-re-arm); D2 receipt-ever flag intact (5 sites); the two remaining serial->flush() calls are boot-time config paths (:377/:686 — verifier-read contexts). Harness exit 0 (verifier-run) |
| 7 | Both targets build green; harness exit 0 on the fix commit | ✓ VERIFIED | VERIFIER-RE-RUN at HEAD 269f6b7: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` = 2 succeeded (55.6 s); `node scripts/verify_protocol_roundtrip.mjs` = exit 0 (all checks PASS) |
| 8 | Ledger honesty: WINDOWS 15 / G-01-10 extended, stay OPEN | ✓ VERIFIED | Both read: WINDOWS entry 15 carries the ROUND-#12 UPDATE in table + JSON, status open, counts 2/17/19; 01-UAT G-01-10 root_cause carries the ROUND-#12 DETERMINATION with the missing-list debug-round item marked DONE; neither claims the crash fixed at code level. The Task-2 checkpoint record exists in §7 (option (a)+(b), auto-advanced, the FOURTH pending operator confirmation) |

### 01-29 Round Truths (the bench session's acceptance)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | D1 acceptance: spaced smoke AND sustained full-service AND series A, zero crash signatures, discriminators quoted | ✗ FAILED (third expression) | The smoke's FIRST window survived (16/16 on air, :427-:491) but the balloon reset in the INTER-WINDOW LULL: :520 crash-adjacent i2cWrite with on-line mojibake -> :523 rst:0x7 -> :524 Saved PC 0x4037c7fa = esp_vApplicationTickHook (verifier decode reproduces verbatim). Signature counts (verifier-reproduced): rst:0x7=1, rst:0xc=0, Guru=0, canary=0, [LOOP]=0, i2cWrite=1, mojibake=1 (raw-byte grep). The 01-28 fix DISCONFIRMED as sufficient a second time; debug round #3 routed |
| 2 | D2 discriminator regression watch (fixed entry 16) | ✓ VERIFIED | Exactly 2 're-announce held' fires (:492 immediately after chunk 16/16; :683 four lines after the rejected-but-received request :679-:682), both genuine by timestamp correlation; zero boot-window spurious fires at either boot — the receipt-ever flag truthful on the round-#12 firmware |
| 3 | G-01-7 series-A acceptance (eighth attempt) | ✗ NOT RUN (unjudgeable) | Series never ran (single CAPTURE_NOW = the spaced smoke, :364/:370); zero round-#10 discriminators (verifier-grepped 0/0/0); image 39 INCOMPLETE crash-caused. Eighth round riding |
| 4 | SC-3 visible-effect pairs judged | ✗ NOT RUN | Zero SET_BRIGHTNESS/contrast/saturation/quality AND zero SET_RESOLUTION commands (verifier-grepped both consoles); eighth round riding |
| 5 | WR-03 cadence discriminator quoted | ✗ NOT RUN | Zero AUTO_CAPTURE commands; eighth round riding; SC-5 advisory NOT discharged |
| 6 | CIF-cycle (wire-8) + QVGA-restore clauses | ✗ NOT RUN | Zero SET_RESOLUTION commands — not even a deviation this session; eighth round riding |
| 7 | Dashboard glance operator-LOOKED | ✗ NOT RUN | Commands flowed (seq 1/2/3 ACKed) but no LOOK verdicts recorded before the abort |
| 8 | WR-02 bench discriminator (fixed entry 18) | ✓ VERIFIED | Zero 'health check failed' lines whole console (verifier-grepped); healthy boots + the crash-reboot boot both clean |
| 9 | Ledger honesty: flips only on operator-observed evidence; failed session recorded per convention | ✓ VERIFIED | ZERO flips; WINDOWS 15/3 extended/annotated in table + JSON, counts 2/17/19 reconcile (verifier re-parsed both copies); G-01-10 SESSION-9 EXTENSION + G-01-7 session-9 note verifier-read in 01-UAT.md; Test 3/Test 4 session-9 updates present; STATE.md Current Status/Next Steps carry the round story + the /gsd-secure-phase 1 gate; requirements-completed: [] honored; the naming deviation + the mojibake-count correction recorded as deviations, files never renamed |
| 10-17 | UI-SPEC lifts (7 explicit + LED-truth backstop) | ⚠️ UNVERIFIED-BY-LOG | No dashboard observation session occurred; nothing observed either way — no pass claimed, all ride the post-D1 bench glance |

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer | Phase 3 backlog | Todo on disk (verifier-verified) |
| 2 | Gallery thumb number overlay + Incomplete-badge copy | Phase 3 backlog | Todos on disk (verifier-verified) |
| 3 | Thumb-first image delivery + antenna-pointing overlay | Phase 3 backlog | Todos on disk (verifier-verified, captured at 898fcc6) |

Step 9b check: no later milestone phase exists to defer the riding clauses to — Phases 2 and 3 are already executed and Phase 1 closes last. The D1 debug round #3 + post-D1 bench moment are Phase-1 closeout activities routed in STATE.md Next Steps, not later-roadmap deferrals, so they remain gaps.

### Required Artifacts (round #12: plans 01-28, 01-29)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `.planning/debug/d1-crash-regression-push-start.md` (01-28 + 01-29) | §7 (six subsections + checkpoint record) + §8 (session-9 recurrence) | ✓ VERIFIED | 916 lines; §7 at :451 (7.1-7.6 + FIX SHAPE SELECTED), §8 at :727 (8.1-8.6). Every load-bearing framework/config claim carries a re-derivable path; this verifier sampled and reproduced the round's OWN most load-bearing artifacts (ELF SHA exact; the 0x4037c7fa AND 0x40376430 AND 0x403c88b8 decodes all reproduce verbatim) |
| The fix: `src/e32_lora.cpp` + `include/e32_lora.h` (Lever A) | yielding bounded TX-drain replacing the flush busy-spin | ✓ VERIFIED | :230-256 drain block with G-01-10/§7.6-citing comment; begin(uartPort=2) signature (e32_lora.h:129); call sites unchanged both boards; the remaining flush() calls are config-path only |
| The discriminators: `src/image_tx_manager.cpp` + `include/image_protocol.h` (B1/B2) | [BOOT] reset-cause + [LOOP] slow-pass, latched, with removal conditions | ✓ VERIFIED | B1 :146-154, B2 :181-205, IMG_LOOP_SLOW_PASS_MS 2500 at image_protocol.h:129 with rationale; both removal conditions named in-source; both PROVED IN-IMAGE at bench (:103/:615 warm-up+BOOT lines live in balloon3.log) |
| `balloon3.log` / `base3.log` (01-29, retained session evidence) | every citation locatable | ✓ VERIFIED | 804 / 221 lines. Verifier re-read every load-bearing citation: the fatal lull+crash (:495-:524), [BOOT]s (:103/:615), warm-ups (:102/:614), [MEM] (:372/:511), Performance (:514), capture/enqueue (:366/:375), window arm/serve (:421/:427-:491), D2 fires (:492/:683), post-reboot rejects (:679/:692/:713), no-valid-buffer (:647), boot banner (:23/:535 matching the retained ELF mtime), the bootloader entry (:530), base ACKs (:28/:61/:89), thumb COMPLETE (:79), manifest (:81 CRC 447D30AB), window queue (:84), retransmits + NACK_INVALIDs (:138/:162/:167/:171/:177/:181), INCOMPLETE (:194) — all reproduce verbatim. Naming deviation from balloon14/base9 recorded, files never renamed. Note: 'CAPTURE_NOW' greps 2 lines but they are the receipt+result of ONE command — consistent with the single-smoke claim |
| `01-UAT.md` / `WINDOWS.md` / `STATE.md` (01-29) | honest failed-session record, zero flips, debug round #3 routed | ✓ VERIFIED | Read: G-01-10 SESSION-9 EXTENSION + missing-list ANSWERED/DONE items; G-01-7 session-9 unjudgeable note; Test 3/Test 4 eighth-round notes; WINDOWS entries 15/3 session-9 updates in table AND JSON with counts 2/17/19 reconciled; STATE.md Current Status + Progress + Next Steps name the round-#3 brief and /gsd-secure-phase 1 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| E32LoRa::transmit (runtime TX path) | yielding bounded TX-drain | delay(1)-polled uart_ll_is_tx_idle | ✓ WIRED (bench-exercised) | e32_lora.cpp:228 write -> :244-256 drain -> short-write guard; the fatal-path lever provably ran in the deployed image (the window served 16/16 chunks through this code on air) |
| begin() | B1 boot reset-cause | esp_reset_reason() + local 16-case map | ✓ WIRED (bench-proven) | :146-154; fired at BOTH session-9 boots — POWERON :103 and TASK_WDT :615 (the ROM reason register independently confirming the §7.1 TG0WDT = task-watchdog read) |
| process() top | B2 slow-pass latch | passGap vs IMG_LOOP_SLOW_PASS_MS | ✓ WIRED (bench-exercised, zero fires) | :181-205; zero [LOOP] lines in balloon3.log with loopTask healthy (Max 1277 ms) — the discriminator's designed negative answer, which §7.6's read converts into the CPU0-side signature |
| getCommandsAcked (uint32) / D2 flag / [MEM] / round-#10 levers | unchanged through round #12 | commit-scope + grep evidence | ✓ WIRED | ackedAtLastPoll uint32 intact; receipt-ever flag 5 sites (:42/:111/:699/:1040 + header :242); [MEM] 7 mentions; budget-re-arm :1110; quiet gate CMD_TX_CHANNEL_QUIET_MS 750; zero source drift at HEAD |
| The audit <-> the deployed build | decode BEFORE any code change | retained ELF SHA cross-check | ✓ WIRED | §7 ran against c53635e181 (session 8's build) and committed BEFORE the fix (60315b5 precedes 6ca9a36); §8's decode ran against e09dd034 (the round-#12 deployed build, SHA verifier-reproduced, mtime matching the boot banner to the minute) |

### Data-Flow Trace (Level 4)

Not applicable this round — Lever A is a timing/yield restructure of an existing byte path (same write, same AUX handshake, same wire bytes — harness-proven), and B1/B2 are bounded diagnostic lines whose data sources are real hardware/framework registers (esp_reset_reason(), millis(), the loop pass clock). Their live outputs are in balloon3.log (:103/:615 and the zero-[LOOP] whole-console count).

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| -------- | ------- | ------ | ------ |
| Dual-target build (independent) | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` | 2 succeeded in 55.6 s (verifier-run at HEAD 269f6b7) | ✓ PASS |
| Wire-format regression harness | `node scripts/verify_protocol_roundtrip.mjs` | exit 0, all checks PASS (verifier-run) | ✓ PASS |
| Deployed-ELF provenance | `sha256sum .pio/build/esp32-s3-balloon/firmware.elf` | e09dd034a5ab7b904278902106a0348d12c800ad88b03daac5d29029ad0cb5f7 — exact match to §8.1; mtime 2026-08-28 12:06 matching the deployed boot banner 12:06:09 | ✓ PASS |
| Fatal-address decode (independent) | `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf 0x4037c7fa 0x403c88b8 0x40376430` | `esp_vApplicationTickHook at ...freertos_hooks.c:34 (discriminator 1)` / `?? ??:0` / `tick_hook at ...int_wdt.c:111` — reproduces §8.2's decode table AND session-8's decode verbatim | ✓ PASS |
| Session-9 crash citations | sed balloon3.log:421/:427/:491/:505/:511/:514/:518-524; base3.log:79/:81/:194 | armed 0..15 -> 1/16..16/16 sent -> [BCN] 23 -> [MEM] healthy -> Max 1277 -> beacon TX -> i2cWrite(mojibake) -> ROM banner -> rst:0x7 -> Saved PC:0x4037c7fa; thumb COMPLETE 9/9; manifest CRC 447D30AB; INCOMPLETE 15/36 after 3 passes — all verbatim | ✓ PASS |
| Crash-signature counts | grep -c per signature, whole balloon3.log | rst:0x7=1, rst:0xc=0, Guru=0, canary/watchpoint=0, [LOOP]=0, i2cWrite=1, health-check-failed=0 — matches the ledger claims exactly | ✓ PASS |
| Mojibake corrected method | raw-byte grep `e2 88 a9 e2 94 90 e2 95 9c` | balloon3.log=1 (ON :520 — visible in the verbatim line), base3.log=0 — the §7.4 trap correction verified at byte level | ✓ PASS |
| Riding-clause NOT-RUN confirmation | grep SET_*/AUTO_CAPTURE/discriminators, both logs | 0 settings commands (incl. 0 SET_RESOLUTION), 0 AUTO_CAPTURE, 0/0/0 round-#10 discriminators, 1 CAPTURE_NOW (the smoke) | ✓ PASS |
| B1/B2 discriminator engagement | grep '\[BOOT\] reset-cause' / '\[LOOP\]' balloon3.log | :103 POWERON, :615 TASK_WDT; [LOOP]=0 whole console — the §7.6 discriminating read answered | ✓ PASS |
| Source drift + commit scopes | `git status --short -- src/ include/`; `git show --stat` 6ca9a36/f25b41b/a5dd90b/269f6b7 | drift empty; 6ca9a36 = exactly 7 files; 01-29 commits docs-only; camera_manager/image_rx/UI markup untouched | ✓ PASS |
| WINDOWS integrity | full-file read + count | 19 entries, open ids exactly {3, 15}, 2/17/19 reconciles across front matter / table / JSON | ✓ PASS |
| Zero command timeouts | grep -c 'timeout after' base3.log | 0 | ✓ PASS |
| Protected-surface greps | CMD_TX_CHANNEL_QUIET_MS / [MEM] / budget-re-armed / D2 flag | 750 intact; [MEM] present; :1110 discriminator present; flag at all 5 sites | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` probes exist in this project; the phase's executable verification is the wire-format harness + dual-target build (both run above) — SKIPPED (no probes declared).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| ----------- | ---------- | ----------- | ------ | -------- |
| CTRL-01 | 01-28/01-29 (+prior) | User can trigger camera capture from base station web interface | ✓ SATISFIED (regression note) | Session 9: dashboard trigger -> ACK -> capture on round-#12 firmware, thumb COMPLETE 9/9 (image 39); the D1 crash bounded the session before the full completed — routed as the phase's top blocker |
| CTRL-02 | 01-29 (+prior) | User can adjust all camera settings remotely | ✓ SATISFIED (riding note) | All 7 settings execute for real (01-04 sensor setters; ACKs on the wire at prior sessions); the visible-effect pair judgment rides (SC-3 clause, eighth round) |
| CTRL-03 | 01-29 (+prior) | Manual + automatic capture modes | ✓ SATISFIED (riding note) | Manual bench-proven current-generation (session 8 image 37 end-to-end; session 9 image 39 through thumb); auto-capture Test 4 PASS (WR-03 discriminator rides) |
| CTRL-04 | 01-29 (+prior) | Fixed interval timing | ✓ SATISFIED | AutoCapture wraparound-safe timer; Test 4 cadence PASS; NVS ID sequence held across the session-9 crash reboot (38->39) |
| CTRL-06 | 01-28/01-29 (+prior) | Failed commands retried with timeout | ✓ SATISFIED | Zero timeouts at session 9 (no regression); honest-terminalization evidence stands on sessions 5/6/7; retry-ordinal bound unchanged; quiet-gate constants intact through round #12 (verifier-read) |
| PRI-02 | 01-28/01-29 (+prior) | Retry mechanism with timeout for failed transmissions | ✓ SATISFIED | Same evidence as CTRL-06 |

Riding IDs per the project's gap-closure convention (marked Complete in REQUIREMENTS.md from their owning phases; no new claims this round): IMG-03 (01-28), IMG-02/IMG-03 (01-29) — both plans honored requirements-completed: [] on their records (01-28 made no bench claims; 01-29 is a failed session; the 01-24/01-27 convention). No orphaned requirements: REQUIREMENTS.md maps exactly CTRL-01..04, CTRL-06, PRI-02 to Phase 1, all Complete; REQUIREMENTS.md was NOT touched by round #12 (honored).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (all round-#12 source files) | - | TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER scan | — | CLEAN — zero matches across all 5 touched source/header files (verifier-run, per-file counts 0) |
| src/image_tx_manager.cpp | 146-205 | B1/B2 diagnostic instrumentation (temporary by design) | ℹ️ Info | Bounded (once per boot / once per episode), removal conditions named in-source; correctly retained — G-01-10 did not close (WINDOWS 15 records it); not a stub |
| src/e32_lora.cpp | 377, 686 | Two remaining serial->flush() calls | ℹ️ Info | Boot-time config-path flushes (3-6 bytes, ms-class) outside the runtime TX path — dispositioned in §7.3/SUMMARY; not a hazard on the fatal path |

### Decision Coverage

CONTEXT.md decisions for this phase were translated in earlier rounds and re-checked at prior verifications; round #12 introduced no new CONTEXT decisions (its Task 2 checkpoint selection is recorded in §7's FIX SHAPE SELECTED subsection and the ledgers). The auto-advance convention was applied correctly and recorded with provenance. No drift found.

### Human Verification Required

These items need the physical bench / operator judgment; all are routed (STATE.md Next Steps: debug round #3 then the bench moment), not dropped:

### 1. SC-3 visible-effect pairs (eighth round riding)

**Test:** At fixed QVGA: SET_BRIGHTNESS -2, capture; SET_BRIGHTNESS +2, capture; judge the pair visibly different. Repeat for one other class (contrast -2/+2, saturation -2/+2, or quality 10 vs 30).
**Expected:** Visible pairwise differences, per-pair verdicts recorded in Test 3.
**Why human:** Sensor visible-effect acceptance needs the physical camera and operator judgment; eight bench rounds have issued zero non-RESOLUTION settings commands.

### 2. WR-03 cadence discriminator

**Test:** AUTO_CAPTURE_ENABLE at 20 s; let one interval capture fire; trigger a manual CAPTURE_NOW; compare the next interval capture's timestamp to the manual one.
**Expected:** Next interval capture lands ~20 s AFTER the manual capture (both 'Captured image ID' timestamps quoted). Also discharges the SC-5 coincidental-reliance advisory.
**Why human:** Needs the running firmware pair and wall-clock observation; never run on current HEAD.

### 3. G-01-7 series-A re-run (after debug round #3)

**Test:** 3 back-to-back unspaced captures on post-fix firmware.
**Expected:** 3 ACKs, 3 captures, 6/6 verdicts COMPLETE, zero command timeouts, zero silently absent fulls, with the round-#10 discriminator lines proving lever engagement. The D2 discriminator reads clean (held at sessions 8/9), so the busy-hold lines the series watches are truthful by construction.
**Why human:** RF-pair behavior under real half-duplex load; the series has been aborted/unreached eight rounds running.

### 4. Dashboard LOOK glance + UI-SPEC lifts + CIF/QVGA clauses

**Test:** Exercise the dashboard (d8ba14e gallery detail still unlooked-at after four sessions); CIF cycle (SET_RESOLUTION wire-8) with both kinds COMPLETE at CIF sizing, QVGA restore with wire-evidence sizing, zero FB-OVF.
**Expected:** All seven UI-SPEC considerations + LED-truth backstop hold; gallery detail fields render; settings clauses all judged as specified.
**Why human:** Browser observations leave no console evidence; operator-looked (not did-not-flag) is the bar; the wire-8 CIF clause has never run as specified.

### 5. FOUR pending auto-advanced disposition confirmations (human decision points)

**Test:** Confirm (or route back) the four auto-selected dispositions now pending end-of-phase operator confirmation: 01-23's fix-all (WINDOWS 10-14), 01-25's fix-shape option-a + instrumentation ride-along, 01-26's option-a fix-all (WINDOWS 17-19), and 01-28's option (a)+(b) (Lever A + B1/B2). Note TWO of the four levers are now bench-disconfirmed as sufficient (01-25's warm-up at session 8; 01-28's Lever A at session 9) — the confirmation questions for those are whether the levers STAY (both are hazard removals/diagnostics whose retention is harmless and arguably load-bearing for round #3) while the next fix supersedes them.
**Expected:** Each confirmation (or route-back) recorded in the ledger.
**Why human:** Acceptance of robustness-edge-case risk and fix-shape selection is a maintainer judgment; all four were auto-advanced under workflow.auto_advance and honestly recorded as pending.

### Gaps Summary

Three structured gaps, all honestly ledgered — the same three as verification #10, evolved:

1. **D1 crash regression, THIRD EXPRESSION (G-01-10 / WINDOWS 15, BLOCKER)** — the 01-28 Lever-A fix is disconfirmed as sufficient a second time. Progress inside the failure is real and verifier-confirmed: the WDT topology is pinned, all project-code starver candidates are eliminated, the round's own discriminators answered cleanly (CPU0-side signature reproduced; loop-hang class dead; [MEM] healthy a third session), and the crash's third expression carries NEW discriminating evidence (crash-adjacent I2C failure with the corruption-class mojibake ON the line — the 'non-adjacent BMP280 noise' classification breaks; the crash point moved to the inter-window lull, killing the 'sustained TX-heavy service' constant). Debug round #3 is routed with a precise brief (§8.6). This remains the phase's top blocker: no complete clean bench session exists on HEAD until it closes.
2. **G-01-7 series-A burst full-delivery** — eighth round unjudgeable: the series never ran; image 39's INCOMPLETE is crash-caused. Levers wired, unvalidated; re-run gated behind D1 debug round #3.
3. **The riding bench-moment clauses** — SC-3 visible-effect pairs (eighth round), WR-03 cadence discriminator (eighth), CIF-cycle-as-specified + QVGA restore (eighth), operator-LOOKED dashboard glance + UI-SPEC lifts. All routed; all need one clean post-D1 session.

SUMMARY vs Reality: no divergences found this round — every load-bearing claim in 01-28/01-29-SUMMARY that this verifier checked (the §7 subsection structure and framework-source paths, all three fix components at the cited lines, both ledger gates re-run independently, the ELF SHA, the addr2line decode — independently re-run, every fatal-window/lull/crash/boot/D2/[MEM]/ACK/manifest/retransmit/finalize citation, all grep counts including the corrected mojibake method at raw-byte level, the discriminator engagement/absence evidence, ledger integrity, the zero-flip convention, requirements-completed: [], naming-deviation handling) reproduces. The 01-29 record again honors the failed-session convention at its best: nothing forced, the third expression routed with decoded evidence, the incoming evidence packet's grep error caught and corrected, and the two previously-fixed entries' regression watches verified green rather than assumed.

The phase goal itself — bidirectional LoRa command/control with ACK, retry, manual + auto capture — remains achieved and hardware-proven (sessions 1-6; sessions 8/9 re-proved the command loop live on successive fix generations, with session 9 adding a clean queue->ACK->capture->thumb loop and zero command timeouts). But HEAD firmware still hard-crashes during the post-capture transfer lifecycle (now in the inter-window lull) with the root cause narrowed twice yet unfixed, the burst-delivery optimization remains unjudged after eight attempts, one settings clause remains operator-unjudged for eight rounds, and the riding clauses accumulate. The honest verdict is gaps_found with a clear next action: debug round #3 (the lull-phase audit + the I2C/mojibake convergence question), then one clean bench session that judges everything at once.

---

_Verified: 2026-08-28T14:10:00Z_
_Verifier: Claude (gsd-verifier)_

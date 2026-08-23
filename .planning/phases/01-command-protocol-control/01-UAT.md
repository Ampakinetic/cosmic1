---
status: diagnosed
phase: 01-command-protocol-control
source: [01-VERIFICATION.md]
started: 2026-08-18T11:14:05Z
updated: 2026-08-24T10:30:00Z
---

## Current Test
[testing complete]

## Tests

### 1. End-to-end command round trip over real radios
expected: ACK within 2 s; queue row updates to terminal state; counters advance
result: pass
note: re-test after debug fix 1223f46 (balloon boot abort — three stacked causes); original failure logged in Gaps as G-01-1

### 2. Retry/TIMEOUT on degraded link including the duplicate-ACK edge
expected: With the link degraded (range/antenna removed), a retried command shows
retry progress and reaches TIMEOUT/FAILED per the locked vocabulary; the pending
count returns to 0 and stays there — including after a late duplicate ACK arrives
for a command already ACKED by an earlier retry (the 01-06 terminal-state guard is
what makes this safe; prior verifier noted this test is only meaningful now)
result: pass

### 3. Camera settings on the physical sensor
expected: Adjusting camera settings from the web UI produces visible changes in
captured images (brightness/contrast/saturation/exposure/WB/quality/frame size);
the "CIF 400x296" option succeeds (post-01-06 relabel — previously always NACK'd)
result: issue
reported: "Excellent, the data is working, and the image capture is working. However the thumbnail image looks corrupted, and there is a 'Incomplete' message over it. The full image looks good." (01-09 bench session, iteration 5)
severity: major
note: G-01-3's blocking halves (Storage Unavailable; ALL transfers timing out) RESOLVED on hardware at the 01-09 bench — Storage tile OK, command round-trip ACK, one capture completed end-to-end with the full image CRC-verified and rendered intact from SD. Residual narrowed to the thumbnail push burst (new gap G-01-5); the settings-visibility + CIF spot-check clauses ride the G-01-5 re-test round (images only now flow). UPDATE (01-10 round): the discriminator round landed BOTH kinds COMPLETE on v2/v3 fresh captures (thumbnails intact both times — see G-01-5 discriminator_evidence), but the standing corrupt-thumbnail symptom is attributed to the phantom-failure + overwrite mechanisms, so Test 3 stays 'issue' until 01-11's remediation is bench-verified. WR-01 regression watch across all 01-10 sessions: GREEN (0 'Critical battery' lines, no Emergency entry, no camera auto-disable; beacons batt=valid — quotes in G-01-5 discriminator_evidence). Settings visible-effect + CIF 400x296 spot-checks DEFERRED to 01-11 Task 3's bench re-verification (operator-approved deviation — that task re-runs the series on remediated firmware). UPDATE (01-11 remediation + bench re-verification, 2026-08-24): G-01-5 and G-01-6 RESOLVED on the bench — phantom-failure accounting fixed (0 chunk FAILED across all three post-fix sessions vs ~41/capture baseline; 31/0/90 benign AUX-missed lines), gallery grows across reboots (index 2->3->4->5, IDs 2/3/4/5 sequential; quotes in the gap entries). The settings series then surfaced a NEW defect: SET_RESOLUTION value 9 (VGA) floods 'cam_hal: FB-OVF' (4,379 lines) and fails all subsequent captures until reboot (G-01-8), and the unspaced-capture sessions exposed concurrent-transfer starvation (G-01-7). Test 3 stays 'issue' pending G-01-7/G-01-8 closure; with capture spacing, all four kinds COMPLETE (session 3).

### 4. Auto-capture cadence + disable
expected: AUTO_CAPTURE_ENABLE captures at exactly the commanded interval (try a
value above 30 s to prove no legacy interleave); after an ACKed AUTO_CAPTURE_DISABLE
zero automatic captures occur; image IDs form one sequence across manual + interval
captures (GET_STATUS lastImageId truthful)
result: pass
note: G-01-4 resolved by 01-07 (754f834, delegated in-page AJAX submit for all section#capture forms); runtime truth confirmed at the 01-09 bench — the operator triggered captures from the dashboard across the session with no raw-JSON navigation or Back-button behavior reported

### 5. Prohibition review — no fabricated state on the wire or UI
expected: Judgment-tier prohibition from plan 01-06: the operator is never shown
protocol or camera state that does not reflect reality (no ACK for unexecuted
actions, no green link LED while failed, GET_STATUS fields match actual balloon
state)
result: pass

## Summary

total: 5
passed: 4
issues: 1
pending: 0
skipped: 0
blocked: 0

## Gaps

- gap_id: G-01-1
  truth: "ACK within 2 s; queue row updates to terminal state; counters advance"
  status: resolved
  reason: "User reported: There are currently problems with the balloon node not sending data nor using it's OLED screen"
  severity: blocker
  test: 1
  resolved_by: "1223f46 (debug session balloon-no-data-oled-blank — three stacked causes)"
  resolved_at: 2026-08-22
  verified_by: "UAT re-test pass on hardware"
  artifacts: []
  missing: []

- gap_id: G-01-3
  truth: "Adjusting camera settings from the web UI produces visible changes in captured images; CIF 400x296 option succeeds; base Storage section reflects the mounted 4GB FAT32 SD; picture transmissions complete without timing out"
  status: resolved
  reason: "User reported: There are other problems with the camera, the Storage section says 'Unavailable' but there is a 4Gb FAT32 formatted SD in the base station module. All the picture transmissions are failing with timeouts."
  severity: blocker
  test: 3
  root_cause: "Two INDEPENDENT causes. (A) Storage 'Unavailable' is a real SD.begin() mount failure, not a UI bug — wiring doesn't match the constants in base_station_config.h (SCK=12/MISO=13/MOSI=11/CS=10); 02-03-PLAN explicitly deferred pin confirmation to hardware UAT. (B) Image-transfer timeouts are an E32 transport failure specific to the system's only multi-RF-packet frames: 216-byte 0x13 chunk frames sub-pack into 4x58-byte RF packets (~1.0-1.4 s air time at factory-default 2.4 kbps) while every working frame class is a single RF packet. Ranked mechanisms: B1 air-rate config makes chunk TX exceed the 5-s AUX bound (e32_lora.cpp:187); B2 per-packet loss x4 defeats the 3-pass ARQ (base logs type=13 CRC FAIL); B3 30 dBm PA current sag corrupts ~1 s transmissions; B4 sustained push streams saturate the channel so window requests/GET_STATUS collide. One bench run of existing UART0 traces discriminates."
  artifacts:
    - path: "src/sd_storage.cpp"
      issue: "SD.begin() genuinely fails on hardware; begin()/pin code is correct — physical wiring/card issue"
    - path: "include/base_station_config.h"
      issue: "SD pin constants (SCK 12/MISO 13/MOSI 11/CS 10) to audit against actual wiring"
    - path: "src/e32_lora.cpp"
      issue: "transmit() AUX handshake bounds (1 s low / 5 s high) modeled without 58-byte sub-packing / 2.4 kbps default air-time reality"
    - path: "src/image_tx_manager.cpp"
      issue: "timing margins (5 s preemption / 8 s stall / 15 s window ACK / 3 passes) are the levers if mechanism is saturation/loss"
    - path: "src/image_rx_manager.cpp"
      issue: "RX logic exonerated; SD-independent by design (SD-down yields INCOMPLETE/notStored, never timeouts)"
    - path: "src/command_sender.cpp"
      issue: "where unACKed window requests become the 'Timeout' queue rows the user sees"
  missing:
    - "Audit SD breakout wiring (4 signal wires + 3V3/GND) against base_station_config.h constants; confirm via boot line 'SdStorage: card mounted'"
    - "Retry mount with a second known-good FAT32 card to rule out card-contact/module-power issues"
    - "Bench discriminator run: one capture attempt watching balloon serial (ImageTx chunk sent|FAILED, E32 Transmit timeout) and base UART0 (type=13 CRC FAIL, manifest, queue rows)"
    - "Raise E32 air data rate via module config (8-25x less time-on-air) and/or fix PA power per B1/B3"
    - "If saturation (B4) persists: pace captures or adjust chunk size/stall windows"
  debug_session: .planning/debug/storage-unavailable-image-tx-timeout.md
  resolved_by: "01-09 bench session: SD_MMC 1-bit transport switch (794df00, operator datasheet finding — built-in slot CLK=39/CMD=38/DATA=40, not the assumed SPI breakout) + both boards at 9.6 kbps (01-08 ce24cd8). The interim base-only flash severing the pair (balloon still at factory 2.4k — no telemetry, window requests TIMEOUT) confirmed the air-rate enforcement is real and load-bearing; balloon re-flash restored the link exactly as the 01-08 tripwire scenario predicted"
  resolved_at: 2026-08-23
  verified_by: "Operator bench observation: Storage tile OK (green); command round-trip ACK; one capture completed end-to-end — full image CRC-verified and rendered intact from SD. See 01-09-SUMMARY.md for the evidence table"
  artifacts_final:
    - "include/base_station_config.h + include/sd_storage.h + src/sd_storage.cpp (794df00): SD_MMC 1-bit on the built-in slot — the root-cause-A fix"
    - "src/e32_lora.cpp (01-08, e16845a/ce24cd8): real register protocol + boot-time 9.6k enforcement — the root-cause-B fix"

- gap_id: G-01-5
  truth: "A triggered capture's THUMBNAIL arrives intact and renders uncorrupted in the gallery (or its D-22 kind-addressed window heal recovers it to COMPLETE) while the full completes — no corrupt-preview, Incomplete-badged captures at bench range"
  status: resolved
  reason: "Operator bench report (01-09 iteration 5): 'the thumbnail image looks corrupted, and there is a 'Incomplete' message over it. The full image looks good.'"
  severity: major
  test: 3
  root_cause: "Thumbnail PUSH-burst residual loss (debug-matrix B2/B4 class) — NOT a transfer-logic fault. The full image (windowed ARQ pull: per-window verification, passCount reset on accepted progress) recovered everything at 9.6 kbps, eliminating B1 (chunks transmit and complete) and making B3 unlikely (the full's longer sustained multi-RF-packet stream verified end-to-end; PA sag would hit it hardest). The thumbnail streams as a blind push burst immediately after its manifest; holes are discovered only post-hoc and healed by the D-22 kind-addressable window path AFTER the full pull finishes (gate 1, image_rx_manager.cpp:190-196) — bounded to 3 passes (image_rx_manager.cpp:198-199, 'thumbnail push stalled; passes exhausted'). The partial IMG_{id}_T.JPG streams to SD during reception (acceptChunk writes every chunk), so the corrupt render + D-48 Incomplete badge is the designed honest degradation, working as built. Balloon-side servicing verified present by code read (kind-split window arming slices thumbBuffer; buffers retained post-SERVED for a 15-min TTL) — the heal was servable. Whether the heal windows were lost on air (B2/B4) or a timing/margin edge exists at the new air rate requires the serial discriminator below. UPDATE (01-10 bench discriminator round, mechanism NAMED): the dominant 'loss' is an E32 AUX handshake phantom transmit-failure — balloon-side TX accounting defect, NOT air loss. E32LoRa::transmit() (src/e32_lora.cpp:207-233) writes the bytes to UART FIRST (:223 serial->write + flush), THEN waits for AUX-low confirmation (:227, 1000 ms) and returns false on timeout — the data has already left the radio when the 'FAILED' verdict is booked. ~50% of chunk transmissions hit the timeout while the base received every chunk (v3: balloon tallied 37 sent / 41 FAILED, base finalized both kinds COMPLETE 36/36). The transfer/heal machinery is driven by this false loss signal: spurious window requests (4 [E32TX] cmd=30 rows in v2, 5 in v3) trigger re-transmission of data that already arrived. Real air loss is tiny and self-heals: END MARKER MISS totals 6 (v1 flood session) / 1 (v2) / 2 (v3), CRC FAIL 0 in every session, all recovered within one ARQ pass. B5 (close-range 30 dBm RX-overload/desense): NOT REPRODUCED — downgraded; every close-range capture on fresh-boot firmware completed with ~zero real loss, leaving B5 no unexplained loss to account for. The escalated 'fulls also failing at close range' symptom is attributed to the G-01-6 overwrite interaction (fresh-boot ID re-allocation overwriting card files) plus the v1-session phantom-failure flood, not to RF impairment."
  artifacts:
    - path: "src/image_rx_manager.cpp"
      issue: "thumbnail heal loop (gates 1/2 at :190-196, 3-pass bound + finalizeIncomplete at :198-199) — pass bound and stall margins are the levers"
    - path: "src/image_tx_manager.cpp"
      issue: "kind-split window arming (:634-732) + thumbBuffer retention — servicing verified present in code; bench-trace confirmation pending"
    - path: "include/image_protocol.h"
      issue: "IMG_WINDOW_STALL_MS 8000 / IMG_THUMB_HEAL_IDLE_MS 24000 / IMG_RETRANSMIT_MAX_PASSES 3 — pacing levers for the follow-up round (01-08's deferred follow-up condition)"
  missing:
    - "CLOSED by the 01-10 discriminator round: serial discriminator captured on both consoles (see discriminator_evidence below) — mechanism class named: E32 AUX handshake phantom transmit-failure (balloon-side TX accounting defect at src/e32_lora.cpp:223/:227 write-then-wait), not heal-lost-on-air"
    - "CLOSED by the 01-10 round: second-capture re-test — v2 AND v3 were both fresh captures on fresh-boot firmware; thumbnails arrived intact both times (8/8 and 36/36 COMPLETE first-try) — per-burst loss disproven as the standing mechanism"
    - "CLOSED by 01-11 (f265556): AUX-handshake accounting fixed — a missed AUX-low after a COMPLETE write no longer books FAILED ('E32: AUX-low missed after complete write - treated as sent (bytes left the radio)', 31x balloon.log / 0x-FAILED / 90x balloon3.log — all benign); the false loss signal and its spurious window requests are gone. Thumbnail sizing quirk closed by ride-along R2 (c67e1a5, QQVGA guard): correct sizes all sessions — 1362/1465/1468/1703 B / 7-9 chunks; the v3 7138-B quirk did not recur"
    - "CLOSED by the 01-11 post-fix series (2026-08-24, three sessions): before/after tally recorded — phantom FAILED 0/0/0 across sessions vs 41/capture baseline (v3); real air loss bounded and self-healing (END MARKER MISS 0-6/session, all recovered by pass budget); close-vs-separated quantification superseded — B5 stays downgraded (not reproduced) and the residual is mechanism-named as concurrent-transfer starvation (G-01-7), not air loss; with capture spacing 4/4 kinds COMPLETE (session 3)"
    - "EXECUTED at the 01-11 re-verification: the settings series ran and surfaced the SET_RESOLUTION VGA buffer-realloc defect (G-01-8, new); settings visible-effect + CIF 400x296 clauses stay open under Test 3 via G-01-8 (WR-01 regression-watch portion green since 01-10)"
  discriminator_evidence:
    - "BALLOON v3 (balloon3.log) per-chunk pattern, verbatim: 'E32: Transmit timeout - AUX didn't go low' followed by 'ImageTx: chunk(image 1, 1/36, 200 B) FAILED' / 'ImageTx: window chunk(image 1, 1/16, 200 B) FAILED' — while the base received everything (see finalize lines below)"
    - "BALLOON v3 tallies (balloon3.log): push burst 17 chunk sent / 19 FAILED (chunks 1-36); window service 18 sent / 22 FAILED (4 windows: 16+2+16+2 chunks); +2 manifests sent => 37 sent / 41 FAILED total; 43 AUX timeouts (the 2 extra attach to beacon/manifest sends — balloon.log v1 shows the same phantom on beacons: '[BCN] seq=40 sent=39 ok=0', '[BCN] seq=60 sent=58 ok=0' while the base accepted every beacon)"
    - "BALLOON v3 enqueue (balloon3.log:132, the thumbnail-sizing quirk): 'ImageTx: enqueued image 1 (full 7138 B, thumb 7138 B / 36 chunks, source 0)' — thumbnail size equals full size; v2's thumbnail was correctly 1465 B / 8 chunks"
    - "BASE v2 (base2.log:866-867): 'ImageRx: image 1 kind 0 finalized COMPLETE (8/8 chunks, 1465 B)' + 'SdStorage: finalized /images/IMG_00001_T.JPG (8/8 chunks, 1465 B persisted, complete=true)' — thumbnail COMPLETE first-try, no heal fired"
    - "BASE v2 (base2.log:2865-2866): 'ImageRx: image 1 kind 1 finalized COMPLETE (36/36 chunks, 7158 B)' + 'SdStorage: finalized /images/IMG_00001.JPG (36/36 chunks, 7158 B persisted, complete=true)' — one window re-request pass served it: base2.log:1857 'ImageRx: window request queued for image 1 kind 1 (chunks 14..15, seq 5, pass 1)'"
    - "BASE v3 (base3.log:3320-3321): 'ImageRx: image 1 kind 0 finalized COMPLETE (36/36 chunks, 7138 B)' + 'SdStorage: finalized /images/IMG_00001_T.JPG (36/36 chunks, 7138 B persisted, complete=true)'; (base3.log:5758-5759): kind 1 'COMPLETE (36/36 chunks, 7138 B)' + IMG_00001.JPG persisted complete=true — 4/4 kinds COMPLETE across v2/v3 despite 41 balloon-booked failures in v3"
    - "REAL AIR LOSS, all sessions (base logs): '[FRAME] type=13 len=216 END MARKER MISS' — 6x in base.log (v1), 1x in base2.log (v2), 2x in base3.log (v3); 'CRC FAIL' count 0 in every session; every miss recovered within one ARQ pass (approx 1-2 chunks per 16)"
    - "SPURIOUS WINDOW REQUESTS (the false-loss signal driving the heal machinery): '[E32TX] cmd=30' rows — 4 in base2.log (v2), 5 in base3.log (v3), per single capture each"
    - "CODE (src/e32_lora.cpp:207-233): :223 'size_t sent = serial->write(data, length); serial->flush();' executes BEFORE :227 'if (!waitForAuxLow(1000))' which prints :229 'E32: Transmit timeout - AUX didn't go low' and returns false — the bytes have already left the radio; the FAILED accounting is phantom"
    - "OPERATOR-REPORTED (live console, not retained in saved excerpts): 'CommandHandler: Response failed' ACK false-failures — same AUX mechanism hitting command-response writes; consistent with the 2 non-chunk AUX timeouts retained in balloon3.log"
    - "SECONDARY FINDING (base hot-loop error flood, NOT the loss mechanism — everything arrived despite it; cleanup routed to 01-11): '[3819832][E][esp32-hal-gpio.c:185] __digitalWrite(): IO 39 is not set as GPIO. Execute digitalMode(39, OUTPUT) first.' — 9,563 lines in base.log (v1), 3,513 in base2.log, 6,122 in base3.log, constant while idle and transferring (observed spacing ~22 ms). Cause: STATUS_LED_PIN 39 (src/main_basestation.cpp:40) is double-booked with SD_CLK_PIN 39 (include/base_station_config.h, SDMMC host owns the pin after SDStorage().begin()); updateLED()'s READY/NO_LINK branches call digitalWrite(STATUS_LED_PIN) EVERY loop pass (main_basestation.cpp:3672/3676 — only the 1 Hz blink branch is throttled), each triggering the HAL validation error + a synchronous UART print. The GPIO39 coexistence itself was documented at 01-09 ('cannot disturb the SD clock' — confirmed: all data arrived); the per-pass console flood and its ~7-10 ms blocking are the new finding. Fix shape: remap STATUS_LED_PIN off GPIO 39 + throttle the per-pass writes"
    - "WR-01 REGRESSION WATCH (Task 3 portion, GREEN): 0 'Critical battery' lines, no Emergency-mode entry, no camera auto-disable across all balloon logs; beacons carried a valid battery verdict throughout — base3.log verbatim: 'ImageRx: beacon seq=3 alt=0.0m temp=24.0C gps=no-fix batt=valid'. Operator-reported boot observations (live console; the retained balloon logs do not include the boot window — balloon2.log's capture ends at 'Sensor pins initialized'): boot banner 'Emergency Shutdown: Enabled' is power_manager.cpp:706 config echo, not a trigger; 'Battery monitoring active (raw reading: 4095)' (main_balloon.cpp:688) is ADC full-scale on the USB-powered bench unit's floating sense line — the [1.8,8.0]V validity gate cannot detect a full-scale float; expected at bench, flagged for flight config"
  debug_session: .planning/debug/storage-unavailable-image-tx-timeout.md
  resolved_by: "01-11 f265556 (branch c, operator-confirmed routing): E32LoRa::transmit() no longer books FAILED when a complete write is followed by a missed AUX-low — the benign case logs 'E32: AUX-low missed after complete write - treated as sent (bytes left the radio)'; short-write and pre-write readiness remain true failures. Ride-along R2 c67e1a5 (thumbnail QQVGA frame-dimension guard) closes the sizing quirk"
  resolved_at: 2026-08-24
  verified_by: "Operator bench re-verification on fixed firmware, three sessions (2026-08-24), all with consolidated UART0 logging. Session 1 (balloon.log/base.log): pre-resolution-incident capture PASSED the entire before/after table — 46 ImageTx chunk/window sends / 0 FAILED / 31 benign AUX-missed; base.log:68 'ImageRx: image 1 kind 0 finalized COMPLETE (7/7 chunks, 1362 B)' + :186 'ImageRx: image 1 kind 1 finalized COMPLETE (35/35 chunks, 6931 B)'; IO-39 flood ZERO lines (R1 verified). Session 3, the Task 3 discriminator (base3/balloon3, SPACED captures — operator waited for both finalize lines between triggers): base3.log:75 'ImageRx: image 4 kind 0 finalized COMPLETE (8/8 chunks, 1465 B)', :237 'kind 1 finalized COMPLETE (52/52 chunks, 10225 B)', :290 'image 5 kind 0 finalized COMPLETE (9/9 chunks, 1703 B)', :524 'image 5 kind 1 finalized COMPLETE (53/53 chunks, 10431 B)' — 4/4 kinds COMPLETE, tail 49..52 recovered on pass 2 (:491/:512); balloon3.log 157 chunk sends / 0 FAILED / 90 benign AUX-missed; base3.log 5 END MARKER MISS all recovered. Verdict: the named mechanism (phantom TX-failure accounting) is fixed and transfers complete reliably with capture spacing; the residual under unspaced captures is a DIFFERENT mechanism, routed as G-01-7"
  artifacts_final:
    - "src/e32_lora.cpp (f265556): complete-write-then-missed-AUX-low reclassified benign — the G-01-5 fix at the trace-named site"
    - "src/camera_manager.cpp (c67e1a5): thumbnail QQVGA guard — the sizing-quirk closure"

- gap_id: G-01-6
  truth: "The dashboard Image Gallery grows as new captures persist — grid, pager, and count reflect every finalized image (12 per page, newest first)"
  status: resolved
  reason: "Operator report (2026-08-23, continued bench after the 01-09 session): 'The gallery is not expanding to show new pictures, it's locked at 6 pictures.'"
  severity: major
  test: 3
  root_cause: "Undiagnosed — captured for the gap-planning round. Code-read triage: the server path looks sound (handleGalleryList main_basestation.cpp:3461-3523 pages 12/req via SD_GALLERY_PAGE_SIZE; index cap is 1000 with newest-window eviction sd_storage.cpp:532-545; lazy rebuild keyed on indexVersion sd_storage.cpp:569-573). The UI refresh contract is the leading suspect surface: '/api/state's galleryCount is the ONLY refresh signal' for the grid (main_basestation.cpp:1603-1615, D-36) — if the served galleryCount stops advancing (or advances on fewer paths than persistence does), the grid never re-fetches and stays frozen at whatever it last rendered. Competing mechanism: later captures finalize INCOMPLETE and never persist files to SD (index legitimately never grows) — plausible given G-01-5's escalated loss rate, in which case G-01-6 is a symptom of G-01-5, not a separate defect. UPDATE (01-10 bench discriminator round, branch CONFIRMED): persistence OVERWRITE, not refresh-stale — the balloon's image-ID counter resets on every reboot, so a fresh-boot balloon re-allocates image ID 1 ('CommandHandler: Captured image ID 1', balloon3.log:127) and the resulting IMG_00001_* filenames OVERWRITE the card's existing image-1 files instead of appending; the gallery index count cannot grow. Overwrite-not-append proven twice on hardware (v2 and v3 both persisted IMG_00001_T.JPG + IMG_00001.JPG onto a card whose index already held 6 images, and the index stayed at 6 after each persist). Compounding input: in the earlier failing session (v1) later captures never finalized at all (zero finalize lines in base.log), so historically both mechanisms starved the gallery — the failing-transfer half is explained by G-01-5's phantom-failure/overwrite interaction, and the durable half is the ID-reset overwrite, which persists even with perfect transfers (v2/v3 proved transfers can be perfect and the count still stays 6)."
  artifacts:
    - path: "src/main_basestation.cpp"
      issue: "gallery refresh signal — galleryCount served in /api/state (D-36, :3317) vs the footer script's galleryCountSeen comparison (:1603-1615); verify count source and every path that bumps it"
    - path: "src/sd_storage.cpp"
      issue: "indexVersion bump sites — every persist path (full, thumb, sidecars, finalize) must mark the index stale or ensureIndexCurrent() never rebuilds (:569-573)"
    - path: "src/image_rx_manager.cpp"
      issue: "finalize path — do INCOMPLETE captures persist indexed files (partials streamed during reception) or none? Determines whether G-01-6 can be a G-01-5 symptom"
  missing:
    - "CLOSED by the 01-10 discriminator round: zero-tooling discriminator run — operator fetched http://192.168.4.1/gallery?page=1 in the browser and the JSON total EQUALLED the dashboard grid count (both 6) => persistence branch (refresh signal is NOT stale; the served count faithfully reflects an index that never grows)"
    - "CLOSED by the 01-10 round: finalize verdicts named from the base console — the failing session (v1, base.log) has ZERO finalize lines for post-6th captures (transfer-slot stall, G-01-5 class), while v2/v3 fresh-boot sessions DID finalize and persist (quotes below) yet the index still read 6 — naming the overwrite, not a finalize defect, as the durable mechanism"
    - "CLOSED by 01-11 (678d4f1, branch e): image-ID counter persisted to NVS in AutoCapture (e-site deviation: trace named src/auto_capture.cpp, not the plan-anticipated src/sd_storage.cpp); re-verified on the bench — the gallery count GROWS (see verified_by)"
  discriminator_evidence:
    - "OPERATOR-OBSERVED (browser, Step 1): /gallery?page=1 JSON total == dashboard grid count == 6 => persistence branch named"
    - "BASE v2 (base2.log:49, :940, :2912): 'SdStorage: gallery index built — 6 image(s)' — printed at boot AND AGAIN after persisting IMG_00001_T.JPG (:867) and again after IMG_00001.JPG (:2866): the freshly persisted files land on names the card already holds, so the rebuilt index still counts 6"
    - "BASE v3 (base3.log:3492, :5893): 'SdStorage: gallery index built — 6 image(s)' — same pattern after the v3 thumb (:3321) and full (:5759) persists; overwrite-not-append proven twice"
    - "BALLOON v3 (balloon3.log:127): 'CommandHandler: Captured image ID 1' — fresh-boot balloon re-allocates ID 1 (the ID counter is RAM-only and resets on reboot), which is why every bench session's filenames are IMG_00001_*"
    - "BASE v1 (base.log): zero 'ImageRx: ... finalized' lines in the whole session — the earlier 'locked at 6' report's captures never finalized (failing-transfer half); v2/v3 prove the overwrite half persists even with 4/4 COMPLETE finalizes"
  opened_at: 2026-08-23
  resolved_by: "01-11 678d4f1 (branch e): AutoCapture persists the image-ID counter to NVS so a fresh-boot balloon continues the sequence instead of re-allocating ID 1 (fail-open — an NVS failure falls back to the RAM counter); e-site deviation: the fix landed in src/auto_capture.cpp per the trace, not the plan-anticipated src/sd_storage.cpp"
  resolved_at: 2026-08-24
  verified_by: "Operator bench re-verification on fixed firmware (2026-08-24): NVS persistence PROVEN across reboots — balloon2.log:106 'CommandHandler: Captured image ID 2' on the second boot (vs the pre-fix 'ID 1' every boot), :614 'CommandHandler: Captured image ID 3'; balloon3.log:104 'Captured image ID 4', :455 'Captured image ID 5' — IDs 1..5 sequential across three sessions/reboots. GALLERY GROWS for the first time: base2.log:243 'SdStorage: gallery index built — 2 image(s)' advancing to :370 '— 3 image(s)'; base3.log:84 '— 4 image(s)' advancing to :302/:528 '— 5 image(s)', with fresh-boot captures persisting IMG_00002_* .. IMG_00005_* (e.g. base2.log:232 'SdStorage: finalized /images/IMG_00002.JPG (51/51 chunks, 10172 B persisted, complete=true)') — no more overwrite of IMG_00001_*"
  artifacts_final:
    - "src/auto_capture.cpp (678d4f1): NVS-persisted image-ID counter — the G-01-6 fix"

- gap_id: G-01-4
  truth: "Pressing Auto Capture Enable or Trigger Camera Capture submits in-page (AJAX) and the operator stays on the admin page with the result reflected in the queue/status UI"
  status: resolved
  reason: "User reported: When I press the Auto Capture Enable, or the 'Trigger Camera Capture' button, I end up looking at the JSON result string and have to go back to see the admin page."
  severity: major
  test: 4
  root_cause: "The capture/auto-capture/settings forms are native HTML form posts with no client-side submit interception — in-page AJAX submission was never implemented for these buttons (the fetch+preventDefault pattern exists in the footer script only for the Phase 3 alerts-form and wifi-form). Handlers reply with bare application/json via sendResponse, so the browser does a default full-page POST to the raw JSON string. Latent since plan 01-01 (bd50a30), not a regression; not the 1223f46 script-truncation class (footer script demonstrably loads and executes)."
  artifacts:
    - path: "src/main_basestation.cpp"
      issue: "lines 2196-2360: plain form action + type=submit markup for capture, 7 settings, auto-capture enable/stop, event thresholds"
    - path: "src/main_basestation.cpp"
      issue: "lines 640-1839: footer script — submit listeners attach only to alerts-form (1412) and wifi-form (1477); zero fetch references to /capture, /auto-capture, or /set-* routes"
    - path: "src/main_basestation.cpp"
      issue: "lines 3582-3593: sendResponse returns bare JSON with no redirect/HTML — the page the browser lands on"
  missing:
    - "Mirror the in-repo alerts-form/wifi-form pattern (lines 1412-1441, 1477-1509): one delegated submit listener on section#capture covering all 10 control forms (no new ids needed)"
    - "preventDefault, serialize form elements to x-www-form-urlencoded, fetch(form.action, {method:'POST'})"
    - "Render returned {status,message} into a per-card message div"
    - "Call pollOnce() after submit so Command Queue panel, autocapture-chip, and link LED reflect the queued command"
    - "Additions ride the existing PROGMEM-streamed footer script (respect prior truncation-fix constraints)"
  debug_session: .planning/debug/capture-buttons-raw-json-navigation.md
  resolved_by: "01-07 (754f834): one delegated in-page AJAX submit listener covering all 11 section#capture control forms"
  resolved_at: 2026-08-23
  verified_by: "01-09 bench session runtime confirmation — operator triggered captures from the dashboard across the session, command ACKed, no raw-JSON navigation or Back-button behavior reported (WINDOWS ledger entry 1 closed)"

- gap_id: G-01-7
  truth: "A capture triggered while the previous image's transfer/heal is still pending does not prevent that pending transfer's completion — concurrent captures cannot starve or evict an in-flight thumbnail heal or full pull"
  status: open
  reason: "Operator bench finding (01-11 post-fix session 2, 2026-08-24): back-to-back captures left image 2's thumbnail INCOMPLETE 6/8 (heal starved behind image 2's own full pull, then evicted by image 3's window request) and image 3's full INCOMPLETE 15/52 after 3 passes — while session 3's SPACED captures completed 4/4 kinds COMPLETE, isolating concurrency as the discriminator"
  severity: major
  test: 3
  root_cause: "Concurrent-transfer starvation: a new capture's manifest/window traffic supersedes the previous image's pending work. Verbatim: balloon2.log:656 'ImageTx: window request for image 3 supersedes older entry image 2; evicted' — image 2's pending thumbnail heal was evicted when image 3 arrived, after base2.log:296 'ImageRx: image 2 kind 0 finalized INCOMPLETE (thumbnail push stalled; passes exhausted): 6/8 chunks after 3 passes' (the heal had been starved behind image 2's own full pull, base2.log:231 'kind 1 finalized COMPLETE (51/51 chunks, 10172 B)'). Image 3's full then hit bounded tail-chunk turnaround friction: base2.log:415/:431 'window request queued for image 3 kind 1 (chunks 13..14, seq 55/56, pass 1/2)' and :451/:462/:484 'chunks 14..14, seq 57/58/59, pass 1/2/3' — five re-requests for the 13..14/14..14 tail windows; the balloon armed and sent each time yet the chunks never arrived (6 END MARKER MISS in base2.log) — half-duplex immediate-retransmit turnaround-collision class; base2.log:498 'finalized INCOMPLETE (retransmit passes exhausted): 15/52 chunks after 3 passes'. Session 3 (spaced: operator waited for both finalize lines between triggers) completed everything — image 4 thumb 8/8 + full 52/52, image 5 thumb 9/9 + full 53/53 with the 49..52 tail recovered on pass 2 (base3.log:491/:512/:524) — proving the residual is concurrency, not air loss. Named lever (operator-proposed, routed to a future round): serialize transfers — a thumbnail completes fully (push + heal) before its full pull starts, and/or a pending heal entry is never evicted for a new capture; secondary: an inter-window RX-settle gap for the tail-chunk friction (bounded by the 3-pass budget today). NOT a regression from 01-11's commits — removing the phantom-failure noise exposed this pre-existing residual. Interim mitigation (documented operating discipline): space captures until serialization lands"
  artifacts:
    - path: "src/image_tx_manager.cpp"
      issue: "new-capture supersede/eviction rule (the 'window request for image N supersedes older entry' path) — the pending-heal eviction is the primary lever"
    - path: "src/image_rx_manager.cpp"
      issue: "heal gating behind the active full pull (gates 1/2, :190-216) + immediate window re-request cadence — the serialization / RX-settle levers"
  missing:
    - "Serialize thumbnail completion (push + heal) before the full pull starts, and/or stop evicting a pending heal entry for a new capture"
    - "Inter-window RX-settle gap for tail-chunk turnaround friction (13..14 / 14..14 / 49..52-class windows)"
    - "Bench re-verification: back-to-back unspaced captures complete without INCOMPLETE verdicts"
  opened_at: 2026-08-24

- gap_id: G-01-8
  truth: "SET_RESOLUTION to any advertised framesize either works or NACKs honestly — a size larger than the boot-allocated frame buffer must never leave the camera in a state that fails all subsequent captures"
  status: open
  reason: "Operator bench finding (01-11 post-fix session 1, 2026-08-24): after SET_RESOLUTION value 9 (VGA 640x480) returned SUCCESS (balloon.log:582 'CommandHandler: Command SET_RESOLUTION - SUCCESS'), the balloon console flooded 4,379 'cam_hal: FB-OVF' lines (from :588 onward) and all subsequent CAPTURE_NOW commands FAILED (4x 'CommandHandler: Command CAPTURE_NOW - FAILED') until reboot restored the boot resolution"
  severity: major
  test: 3
  root_cause: "Camera PSRAM frame buffers (fb_count=2, allocated at init for PSRAM) are sized at esp_camera_init for the boot framesize (QVGA); CameraManager::setFrameSize (src/camera_manager.cpp:398-412) changes only the sensor framesize (s->set_framesize) and never reallocates — a larger framesize overflows the fixed buffers (FB-OVF) and every fb capture fails until reboot. Latent since 01-04; NOT a regression from the 01-11 commits (which touched e32_lora.cpp, auto_capture.cpp, base config/UI, camera_manager's thumbnail guard only); exposed by this round's deferred settings visible-effect series. Fix class: NACK framesizes above the allocated buffer, or re-init the camera on a size change. Values 10-13 (SVGA..UXGA, include/command_protocol.h:72-76) are presumably worse; untested"
  artifacts:
    - path: "src/camera_manager.cpp"
      issue: "setFrameSize (:398-412) — sensor-only framesize change without buffer reallocation; the fix site"
    - path: "include/command_protocol.h"
      issue: "FrameSize wire codes (:65-77; FRAMESIZE_VGA = 9 at :72) — the NACK bound derives from the boot-allocated buffer size"
  missing:
    - "Bound SET_RESOLUTION to the allocated buffer (NACK above it) or re-init/realloc the camera on a size change"
    - "Settings visible-effect + CIF 400x296 spot-checks (Test 3 clauses still unjudged — re-run after this fix)"
    - "Bench re-verification: VGA (and ideally values 10-13) either works or NACKs honestly; boot resolution unaffected after the command"
  opened_at: 2026-08-24

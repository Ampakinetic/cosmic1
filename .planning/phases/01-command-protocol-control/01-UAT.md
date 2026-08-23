---
status: diagnosed
phase: 01-command-protocol-control
source: [01-VERIFICATION.md]
started: 2026-08-18T11:14:05Z
updated: 2026-08-23T01:50:00Z
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
note: G-01-3's blocking halves (Storage Unavailable; ALL transfers timing out) RESOLVED on hardware at the 01-09 bench — Storage tile OK, command round-trip ACK, one capture completed end-to-end with the full image CRC-verified and rendered intact from SD. Residual narrowed to the thumbnail push burst (new gap G-01-5); the settings-visibility + CIF spot-check clauses ride the G-01-5 re-test round (images only now flow).

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
  status: failed
  reason: "Operator bench report (01-09 iteration 5): 'the thumbnail image looks corrupted, and there is a 'Incomplete' message over it. The full image looks good.'"
  severity: major
  test: 3
  root_cause: "Thumbnail PUSH-burst residual loss (debug-matrix B2/B4 class) — NOT a transfer-logic fault. The full image (windowed ARQ pull: per-window verification, passCount reset on accepted progress) recovered everything at 9.6 kbps, eliminating B1 (chunks transmit and complete) and making B3 unlikely (the full's longer sustained multi-RF-packet stream verified end-to-end; PA sag would hit it hardest). The thumbnail streams as a blind push burst immediately after its manifest; holes are discovered only post-hoc and healed by the D-22 kind-addressable window path AFTER the full pull finishes (gate 1, image_rx_manager.cpp:190-196) — bounded to 3 passes (image_rx_manager.cpp:198-199, 'thumbnail push stalled; passes exhausted'). The partial IMG_{id}_T.JPG streams to SD during reception (acceptChunk writes every chunk), so the corrupt render + D-48 Incomplete badge is the designed honest degradation, working as built. Balloon-side servicing verified present by code read (kind-split window arming slices thumbBuffer; buffers retained post-SERVED for a 15-min TTL) — the heal was servable. Whether the heal windows were lost on air (B2/B4) or a timing/margin edge exists at the new air rate requires the serial discriminator below."
  artifacts:
    - path: "src/image_rx_manager.cpp"
      issue: "thumbnail heal loop (gates 1/2 at :190-196, 3-pass bound + finalizeIncomplete at :198-199) — pass bound and stall margins are the levers"
    - path: "src/image_tx_manager.cpp"
      issue: "kind-split window arming (:634-732) + thumbBuffer retention — servicing verified present in code; bench-trace confirmation pending"
    - path: "include/image_protocol.h"
      issue: "IMG_WINDOW_STALL_MS 8000 / IMG_THUMB_HEAL_IDLE_MS 24000 / IMG_RETRANSMIT_MAX_PASSES 3 — pacing levers for the follow-up round (01-08's deferred follow-up condition)"
  missing:
    - "Serial discriminator on the thumbnail heal phase: base console 'ImageRx: image N kind 0 finalized INCOMPLETE (thumbnail push stalled; passes exhausted): X/Y chunks after 3 passes' + '[E32TX] cmd=30' heal-request rows + '[FRAME] type=13 CRC FAIL' counts during the thumbnail push; balloon console 'ImageTx: THUMBNAIL window armed for image N' + 'window chunk(...) sent|FAILED' — separates heal-served-but-lost-on-air (B2/B4) from heal-not-serviced/not-fired (defect)"
    - "Re-test whether a SECOND capture's thumbnail arrives intact (per-burst vs systematic loss)"
    - "If B4 push-burst saturation persists: pacing levers per 01-08's deferred follow-up condition (image_tx/rx margins, heal pass bound, window size)"
    - "Ride-along closure of Test 3's untested clauses: settings-visibility + CIF 400x296 spot-check (images only now flow end-to-end)"
  debug_session: .planning/debug/storage-unavailable-image-tx-timeout.md

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

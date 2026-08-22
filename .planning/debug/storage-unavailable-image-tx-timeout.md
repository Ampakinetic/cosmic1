---
status: diagnosed
trigger: "UAT Test 3 (G-01-3): 'There are other problems with the camera, the Storage section says Unavailable but there is a 4Gb FAT32 formatted SD in the base station module. All the picture transmissions are failing with timeouts.'"
created: 2026-08-23T00:00:00Z
updated: 2026-08-23T00:00:00Z
---

## Current Focus
<!-- OVERWRITE on each update - reflects NOW -->

hypothesis: FINAL. Two INDEPENDENT causes (one per symptom; no AND-gate between them):
  A) Storage "Unavailable" = REAL SD.begin() mount failure on the base's dedicated HSPI bus at pins SCK=12/MISO=13/MOSI=11/CS=10. The UI is truthful; the code pattern is correct arduino-esp32 usage. The mount failure itself is physical: the SD wiring/pins were an explicitly UNVERIFIED assumption (02-03-PLAN "user_setup" note, confirmation deferred to hardware UAT — exactly where it failed). Verify with the boot log line + wiring audit + known-good card.
  B) Image-transfer timeouts = transport-layer failure specific to the system's ONLY large frames: 216-byte 0x13 chunk frames. Per the official E32-900T30D manual (58-byte max RF packet, automatic sub-packing, 512-B buffer, default 2.4 kbps air rate), each chunk = 4 back-to-back RF packets ≈ 1.0-1.4 s of air time, in sustained streams; commands/ACKs/beacons/manifests (<=38 B) are single-packet — exactly matching the observed works/doesn't-work split. The entire transfer logic is exonerated by code verification.
test: (done) Exhaustive code-joint verification; E32 datasheet grounding.
expecting: One bench run of the existing UART0 traces discriminates B's physical mechanism: balloon "ImageTx: chunk ... FAILED"+"E32: Transmit timeout" => AUX/air-rate bound (B1); base "[FRAME] type=13 CRC FAIL" => long-frame RF integrity/PA power sag (B2/B3); manifests arrive + partial chunks + "Image Window ... Timeout" queue rows => channel saturation by the push stream starving window ARQ (B4, aggravated by auto-capture cadence).
next_action: RETURN DIAGNOSIS (goal: find_root_cause_only). Fix direction A: audit wiring vs constants (or edit constants to match wiring), confirm FAT32, re-flash; verify boot line "SdStorage: card mounted". Fix direction B: capture the UART0 traces from one capture attempt and match against the discriminator matrix; likely mitigations: raise E32 air rate via config (58-B sub-packing still applies but time-on-air shrinks), pace captures, or shrink chunk payload.

reasoning_checkpoint:
  hypothesis: "Storage Unavailable is a truthful report of a real SD mount failure (physical wiring/config vs unverified pin constants); image-transfer timeouts are independent and isolate to E32 transport of 216-byte chunk frames (4x sub-packed at 58 B, ~1.2 s air time each at default 2.4 kbps) — not any protocol/logic fault, not the SD failure"
  confirming_evidence:
    - "storage.state=UNAVAILABLE renders IFF sd.initFailed, set ONLY when SD.begin() returns false (main_basestation.cpp:3229-3231, sd_storage.cpp:52-63)"
    - "02-03-PLAN.md lines 23/75/115: pins 12/13/11/10 are an editable user_setup instruction flagged as an unverified assumption with confirmation deferred to hardware UAT"
    - "image RX accounting is SD-independent (sd_storage.h Degrade-never-halt contract; openTransfer/writeChunk return false when degraded with 'accounting continues') — verified every call site"
    - "chunk wire format serializer/parser symmetric (command_protocol.cpp:388-473 vs command_sender.cpp:322-326); receive buffers 240 B >= 216 B; base UART ring enlarged to 1024 (main_basestation.cpp:2019); balloon ID handoff advances the polled counter (auto_capture.cpp:267-271)"
    - "E32 official manual: 58-byte max single RF packet w/ auto sub-packing, 512-B buffer, default 2.4 kbps air rate, AUX low while busy (cdebyte.com E32-900T30D user manual)"
  falsification_test: "A: if wiring matches 12/13/11/10 exactly AND a second known-good FAT32 card still fails to mount, the wiring-mismatch cause is wrong (board-level fault instead). B: if balloon serial shows every chunk 'sent' (not FAILED) and base shows zero CRC FAILs yet chunks still never accumulate, the transport-layer hypothesis is wrong — return to investigation"
  fix_rationale: "A: align physical wiring with the editable constants (or vice versa) — the mount call is correct, only the physical layer can be wrong. B: the driver/protocol margins (5 s AUX high, 8 s stall, 15 s window ACK, 3 ARQ passes) were modeled without the E32's 58-byte sub-packing and 2.4 kbps default time-on-air; fixing requires either faster air-rate config or protocol margin/throughput adjustment, discriminated by the trace matrix"
  blind_spots: "No hardware observation was possible in this session — B's specific physical mechanism (air-rate/AUX bound vs RF integrity vs PA power sag vs channel saturation) is ranked, not confirmed; the user's E32 modules' actual configuration (air rate, FEC, channel) is unknown; assumes both boards run current firmware"
  candidate_causes:
    - "environment: SD breakout wiring does not match pins 12/13/11/10 (symptom A — leading)"
    - "environment: card/module power/contact or board-level SPI fault (symptom A — secondary)"
    - "environment: E32 air-rate/module config makes 216-B chunk transmits exceed the driver's 5 s AUX bound (symptom B — B1)"
    - "environment: RF-layer loss of 4x-sub-packed long frames defeats the 3-pass ARQ; 30 dBm PA current sag on long transmissions (symptom B — B2/B3)"
    - "config: sustained push stream (auto-capture cadence) saturates the half-duplex channel, starving window ARQ and GET_STATUS (symptom B — B4)"
    - "code: none found — the entire transfer/SD logic class exonerated by joint-by-joint verification"
  and_gate: "Across symptoms: NO — each symptom has its own independent cause (SD mount failure cannot stall transfers in this code; verified). Within symptom B: plausibly YES (e.g., slow air rate AND push-stream saturation co-occur) — the trace matrix separates them"

## Symptoms
<!-- Written during gathering, then IMMUTABLE -->

expected: "Adjusting camera settings from the web UI produces visible changes in captured images; CIF 400x296 option succeeds; base Storage section reflects the mounted 4GB FAT32 SD; picture transmissions complete without timing out"
actual: "Web UI Storage section shows 'Unavailable' despite 4GB FAT32 SD physically present in base station module. All picture transmissions fail with timeouts. Camera settings changes cannot be visually verified because no images arrive."
errors: Storage section shows "Unavailable"; image transfers time out
reproduction: Test 3 in Phase 01 UAT (hardware in the loop), 2026-08-22
started: Discovered during UAT 2026-08-22. Context: day earlier, commit 1223f46 (balloon boot abort fix, three stacked causes) landed touching main_balloon.cpp, main_basestation.cpp, sensor_manager.cpp, image_rx_manager.cpp, command_sender.cpp, status_display.cpp, include/sensor_pins.h. Link fault re-tested PASS (commands round-trip ACK < 2s). New symptoms appeared testing camera capture/settings over the now-working link.

## Eliminated
<!-- APPEND only - prevents re-investigating -->

- hypothesis: 1223f46 (previous day's fix) regressed the SD/image paths
  evidence: git show diff — image_rx_manager.cpp gained only a Serial0 [BCNRX] trace; sensor_pins.h rename is balloon I2C-only; main_basestation.cpp changes are page-streaming + traces. No SD/transfer behavior changed. Also Phase 02 hardware UAT never ran before this session, so there was no working baseline to regress.
  timestamp: 2026-08-23

- hypothesis: Storage "Unavailable" is a broken UI status query (storage actually mounted)
  evidence: /api/state emits storage.state from SDStorage().getStatus() computed truth; "UNAVAILABLE" iff initFailed which is set only on a real SD.begin() failure. No hardcoded state, no stale-cache path.
  timestamp: 2026-08-23

- hypothesis: Image-transfer timeouts are CAUSED by the SD being unavailable (single shared root cause for both symptoms)
  evidence: The RX pipeline is SD-independent by design and at every call site: openTransfer/writeChunk return false immediately when degraded ("accounting continues, nothing persisted"); finalize reports INCOMPLETE (notStored) rather than stalling; serveFile/buildIndex bail instantly on initFailed. No code path lets SD absence delay or drop a chunk. The two symptoms are independent in this codebase.
  timestamp: 2026-08-23

- hypothesis: Chunk frames fail to parse (Phase-1 CR-01/CR-03 class wire-format mismatch)
  evidence: serializeChunk emits 7+5+dataLen+4; the base parser computes the identical expectedTotal from the header bodyLen; both bound dataLen<=200; 216 <= 240-byte receiveBuffer; framing constants 0xAA 0x55 / 0x0D 0x0A with length-driven framing; beacon body size comment says 17 but code uses the same 19 constant on both sides. Host harness (Phase 2, 47 checks) round-trips these formats.
  timestamp: 2026-08-23

- hypothesis: Manual CAPTURE_NOW captures never enqueue for transmission (ID handoff broken)
  evidence: handleCaptureNow calls AutoCap().allocateImageId() which pre-increments the SAME lastImageId member getLastImageId() returns; ImageTx::process polls that getter every pass. Interval captures use fire() which assigns the same member. Both enqueue paths verified.
  timestamp: 2026-08-23

- hypothesis: Balloon ignores window requests (dispatch/ACK gap in command handler)
  evidence: executeCommand dispatches 0x30 to handleImageWindowRequest; ARMED -> ACK with 6-byte echo; UNKNOWN_IMAGE/INVALID_RANGE/BUSY -> typed NACKs; parser accepts any COMMAND frame payload <= CMD_MAX_PAYLOAD_SIZE. TIMEOUT requires the request or ACK to be LOST on air (no response at all across 3 retries).
  timestamp: 2026-08-23

- hypothesis: UART RX buffer overrun on the base drops chunk bursts
  evidence: initLoRa sets LoRaSerial RX buffer to 1024 bytes (main_basestation.cpp:2019, Pitfall 2) vs 216-byte frames; loop drains CmdSender().process() first every pass. Balloon only receives small command frames (default 256 B sufficient).
  timestamp: 2026-08-23

## Evidence
<!-- APPEND only - facts discovered -->

- timestamp: 2026-08-23
  checked: .planning/debug/knowledge-base.md (Phase 0)
  found: One prior session (balloon-no-data-oled-blank, resolved by 1223f46). Adjacent knowledge: (a) that fix touched image_rx_manager.cpp + main_basestation.cpp; (b) silent-failure pattern (macro collision suppressed by angle-bracket include; String operator+= OOM silently drops appends; optional-sensor init made fatal). Treat as hypothesis candidate, not certainty.
  implication: The regression window is narrow — symptoms appeared while testing over the link fixed by 1223f46. Diff of 1223f46 vs SD/storage paths is a high-value first check. Silent-failure modes are plausible repeats.

- timestamp: 2026-08-23
  checked: git show 1223f46 (full diff)
  found: image_rx_manager.cpp change is ONLY an added Serial0 [BCNRX] trace (no behavior). sensor_pins.h change renames BMP280_ADDRESS->BMP280_I2C_ADDRESS (balloon I2C only). main_basestation.cpp changes: page streaming + UART0 traces. No SD/transfer-logic changes.
  implication: 1223f46 regression ruled out for both symptoms. These are latent Phase-2/03 faults newly EXPOSED by the now-working link (Phase 02 hardware UAT never ran before this — STATE.md confirms).

- timestamp: 2026-08-23
  checked: Storage chip data path: main_basestation.cpp:1817-1821 (UI), :3226-3234 (/api/state JSON), sd_storage.cpp:47-83 (SdStorage::begin)
  found: UI "UNAVAILABLE" renders IFF sd.initFailed == true, which is set ONLY when SD.begin(SD_CS_PIN=10, sdSPI) returns false. Honest computed state — not a broken status query. SD pins: SCK=12 MISO=13 MOSI=11 CS=10 (base_station_config.h), dedicated SPIClass(HSPI) instance, instance-overload SD.begin — the documented-correct arduino-esp32 pattern (02-03-PLAN line 118).
  implication: Storage symptom = REAL mount failure. Root cause is hardware-side (wiring vs constants / card / module power) or a subtler SPI-level issue; NOT a UI bug. Phase-02 plan explicitly flagged wiring as unverified "user_setup" item with pin confirmation deferred to hardware UAT.

- timestamp: 2026-08-23
  checked: Pin-conflict analysis for base SD pins (10/11/12/13) vs all base GPIO uses
  found: Base uses: I2C 1/2 (OLED), LoRa UART2 48/14 + M0/M1/AUX 19/20/21, LED 39, SD 10-13. Camera pins 4-18 are balloon-only. GPIO 10-13 are not strapping pins on ESP32-S3 and not used by common WROOM-1 PSRAM variants.
  implication: No code-level pin conflict for the SD bus.

- timestamp: 2026-08-23
  checked: Image RX pipeline SD-dependency (image_rx_manager.cpp full read)
  found: Chunk accounting is SD-INDEPENDENT by design: SDStorage().openTransfer/writeChunk return false when degraded and "accounting continues, nothing persisted"; transfers finalize INCOMPLETE (notStored) at 100% chunks — never stall or time out from SD absence.
  implication: Symptom B (timeouts) is NOT downstream of symptom A (SD down) in the current code. The two symptoms are independent — UNLESS the SD failure mode on hardware is worse than modeled (e.g., SD.begin left the HSPI bus/crashing — no code path for that; begin() failure path is clean).

- timestamp: 2026-08-23
  checked: Window-request command path: command_sender.cpp (ackTimeoutFor WINDOW=15s, 3 retries -> TIMEOUT), command_handler.cpp handleImageWindowRequest (ARMED->ACK with echo; UNKNOWN_IMAGE/INVALID_RANGE->NACK; BUSY->NACK_BUSY), image_tx_manager.cpp handleWindowRequest (arms window on ANNOUNCED/SERVED fulls, past-push thumbs)
  found: Full handshake wiring present and symmetric. TIMEOUT state appears in the base Command Queue UI only when the balloon sends NO response at all (lost frame) across 3 retries.
  implication: If the user's "timeouts" are the queue rows ("Image Window" -> TIMEOUT), the balloon is not receiving the requests or its ACKs are lost — an RF/transport-level cause, not dispatch logic.

- timestamp: 2026-08-23
  checked: TX pipeline joints: main_balloon.cpp loop (CmdHandler->AutoCap->ImageTx order, one transmit per pass), auto_capture.cpp (allocateImageId advances the same lastImageId that ImageTx polls — manual CAPTURE_NOW captures DO enqueue), image_tx_manager.cpp (thumb manifest->chunks->full announce->ANNOUNCED), camera_manager.cpp (capture + QQVGA thumbnail, both PSRAM-backed)
  found: All wiring present; no gap between capture and enqueue; push/announce/window-service state machine coherent.
  implication: Balloon-side logic fault eliminated at the structural level.

- timestamp: 2026-08-23
  checked: Frame serializer/parser symmetry for 0x12/0x13/0x14 (command_protocol.cpp:300-514) + base parser (command_sender.cpp:278-391) + buffer sizes (command_sender.h:124 receiveBuffer=CMD_MAX_PACKET_SIZE=240)
  found: serializeChunk emits 7+5+dataLen+4; parser computes identical expectedTotal from header bodyLen; bounds reject dataLen>200; 216-byte frame fits 240-byte receiveBuffer. Beacon/manifest formats fixed-size and symmetric. Typed dispatch (WR-12) accepts 0x11/0x12/0x13/0x14 on base.
  implication: Wire-format mismatch (the Phase-1 CR-01/CR-03 class) ELIMINATED — manifests/beacons/chunks all round-trip in the host harness (Phase 2 harness 47 checks green).

- timestamp: 2026-08-23
  checked: E32 driver (e32_lora.cpp): transmit = waitForAuxHigh(1000) + write + flush + waitForAuxLow(1000) + waitForAuxHigh(5000), synchronous; RX = byte-wise from HardwareSerial
  found: TX handshake bounded (max ~7 s/blocking); normal blocking per transmit ~0.3-1.2 s. Balloon loop paces 100 ms; ImageTx does at most ONE transmit per pass; beacon preempts chunk work every 5 s (PRI-01).
  implication: Timing model consistent with design comments (~250-400 ms/chunk at higher air rates; ~1.2 s at 2.4 kbps). The 8 s stall / 5 s preemption margins hold unless the actual E32 air rate or AUX wiring behaves worse than modeled.

- timestamp: 2026-08-23
  checked: E32-900T30D official user manual (cdebyte.com) + E32-T series datasheets (web research)
  found: Max single RF packet = 58 BYTES with AUTOMATIC SUB-PACKAGING beyond it; UART-side buffer 512 B; factory-default air data rate 2.4 kbps (selectable 0.3k..19.2k); AUX is LOW while the module is transmitting/receiving or its buffer is draining, HIGH when idle.
  implication: The system's 216-byte 0x13 chunk frames are the ONLY multi-RF-packet frames (4x 58-B packets, ~1.0-1.4 s air time each chunk at 2.4 kbps); commands/ACKs (14-25 B), beacons (30 B), manifests (38 B) are all single-packet. This matches the observed works/doesn't-work split EXACTLY and explains why Test 1/2 passed while every image transfer failed. Consequences: (a) per-chunk loss probability is ~4x the per-packet loss (any one sub-packet lost -> chunk CRC fails); (b) sustained chunk streams occupy the half-duplex channel ~90% of the time, starving the base's window requests/GET_STATUS -> "Timeout" queue rows and RETRYING->INCOMPLETE transfers; (c) at non-default 0.3k air rate a chunk's air time (~6+ s) would exceed the driver's waitForAuxHigh(5000) bound making every chunk transmit fail on the balloon.

- timestamp: 2026-08-23
  checked: Balloon E32 begin call arg order (main_balloon.cpp:474) vs E32LoRa::begin signature
  found: begin(loraSerial, 48, 14, 19, 20, 21, 9600) maps to rxPin=48/txPin=14/m0=19/m1=20/aux=21 — matches sensor_pins.h and HardwareSerial.begin(baud, config, RX, TX) semantics. Same on base (initLoRa).
  implication: Pin/arg-order bug eliminated (and empirically commands work, so the UART/pin config is correct).

- timestamp: 2026-08-23
  checked: Discriminator matrix against existing UART0 traces (added in 1223f46)
  found: One bench run of a single capture attempt distinguishes every remaining physical mechanism: balloon serial shows "ImageTx: chunk(...) sent|FAILED" + "E32: Transmit timeout - AUX didn't go/low/high"; base UART0 shows "[BCNRX]" (beacons), "[FRAME] type=XX len=N CRC FAIL | END MARKER MISS", "ImageRx: manifest image N ...", "ImageRx: window request queued", "[E32TX] cmd=30 ...ok=" and the queue rows' "Timeout" vocabulary (main_basestation.cpp:3016-3017).
  implication: Root cause for symptom B is narrowed to the E32 transport layer with a definitive, zero-code-change bench discriminator; the transfer/SD logic itself is exonerated.

## Resolution
<!-- OVERWRITE as understanding evolves -->

root_cause: TWO INDEPENDENT causes (no shared root; the AND-gate between symptoms is NO — verified the image pipeline cannot stall on SD absence):
  (A) Storage "Unavailable": a REAL SD.begin() mount failure on the base's dedicated HSPI bus at SCK=12/MISO=13/MOSI=11/CS=10. The web UI truthfully reports it (storage.state=UNAVAILABLE iff initFailed). The code pattern (dedicated SPIClass(HSPI) + instance-overload SD.begin, correct pins, no conflicts) is correct, and the 02-03 plan explicitly recorded the wiring to 11/13/12/10 as an UNVERIFIED user_setup assumption whose confirmation was deferred to hardware UAT — exactly where it failed. The mount failure is therefore physical: breakout wiring does not match the constants (leading), or card contact/power/module-level fault (secondary). Hardware confirmation step: boot log line "SdStorage: SD.begin failed (pins SCK=12 MISO=13 MOSI=11 CS=10)" then audit the four wires + 3V3/GND against base_station_config.h; retry with a second known-good FAT32 card.
  (B) Image-transfer timeouts: an E32 TRANSPORT-LAYER failure specific to the system's only large frames — the 216-byte 0x13 chunk frames. Per the official E32-900T30D manual the module sub-packages transparent-mode data into 58-byte RF packets (512-B buffer, factory-default 2.4 kbps air rate, AUX low while busy), so each chunk is 4 back-to-back RF packets (~1.0-1.4 s air time) delivered in a sustained half-duplex stream, while every frame class that demonstrably works (commands 14-25 B, ACKs, beacons 30 B, manifests 38 B) is a single RF packet. All transfer logic (wire formats, parsers, buffers, slot machinery, window ARQ, SD-degradation paths) verified correct — the failure isolates to long-frame transport. Ranked physical mechanisms, separated by one bench run of the existing UART0 traces: (B1) module air-rate config makes chunk air time exceed the driver's 5-s AUX-completion bound -> every chunk TX fails on the balloon (requires non-default 0.3k rate; look for "E32: Transmit timeout" + "ImageTx: chunk ... FAILED"); (B2) per-RF-packet loss x 4 sub-packets/chunk defeats the 3-pass ARQ -> base "[FRAME] type=13 CRC FAIL", transfers stuck low-%; (B3) 30 dBm PA current sag on ~1 s transmissions corrupting exactly the long frames (check bench supply, works/failed pattern); (B4) sustained push streams (auto-capture cadence) saturate the half-duplex channel so the base's window requests/GET_STATUS never get through -> "Image Window ... Timeout" queue rows + RETRYING->INCOMPLETE transfers.
fix: (diagnosis-only session — none applied) A: align wiring with constants (or edit SD_SCK/MISO/MOSI/CS_PIN in include/base_station_config.h to match actual wiring) and re-verify the boot log. B: run the discriminator matrix, then either raise the E32 air data rate via module config (shrinks time-on-air 8-25x; 58-B sub-packing remains), fix PA power/feed, and/or adjust protocol margins (chunk payload size, stall/ACK windows, capture pacing) once the mechanism is known.
verification: Code-level: every joint verified by direct read (see Evidence/Eliminated); E32 behavior grounded in the official manual. Hardware-level: NOT YET OBSERVED — this session could not run the boards; the two confirmation steps above are the acceptance tests for the fix owners.
files_changed: []

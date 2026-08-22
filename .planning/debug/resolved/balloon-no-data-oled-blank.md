---
status: resolved
trigger: "There are currently problems with the balloon node not sending data nor using it's OLED screen"
created: 2026-08-22T11:02:01Z
updated: 2026-08-23T01:20:00Z
---

## Current Focus
<!-- OVERWRITE on each update - always reflects NOW -->

hypothesis: CONFIRMED (web half): handleRoot built the whole page as ONE Arduino String (~80 KB) — String::operator+= silently drops the append when realloc cannot find a contiguous block, so the 57 KB HTML_FOOTER (the entire dashboard script) was dropped and the page shipped script-less (13,678 header + 9,349 sections = 23,027 bytes observed, byte-exact). Tiles render as static "—", zero polls, no stale badge. Tablet runs modern Chrome (user) — not an engine problem. Fix applied: page now streams in 3 parts from PROGMEM via setContentLength + sendContent (house pattern from handleLeafletJs); dynamic sections String is only ~9 KB
test: fixed build flashed; user reloads on Cosmic1-Diag while capture runs
expecting: [PAGE] streamed ~80,027 bytes; [JSDBG] block1+start+end; [API] /api/state tele=1 every 5s; user sees live tiles
next_action: verify trace + user confirmation, then trim diagnostics, restore production SSID, guardrail, archive
bug_class: Bohrbug (deterministic — every boot reproduces; failure visible at boot, not runtime-dependent)
reasoning_checkpoint:
  hypothesis: "Macro collision: sensor_pins.h defines BMP280_ADDRESS=0x76; Adafruit_BMP280.h (included AFTER sensor_pins.h in sensor_manager.cpp, via angle brackets so the redefinition warning is silenced) redefines BMP280_ADDRESS=(0x77). At the call site `bmp280->begin(BMP280_ADDRESS)` expands to 0x77 — the library probes 0x77 where nothing lives, while the real BMP280 ACKs at 0x76 with chip id 0x58. begin() returns false at detected() (sensorID stays 0x00, heap-zeroed — read8 never ran, proven by library trace `Address 0x77 Not detected`), SensorManager::begin() false, setup() aborts, loop() parks on delay(1000) forever → no 0x14 beacons (base sees no data) AND StatusOLED().begin() never called (OLED completely dark)"
  confirming_evidence:
    - "Library's own trace with -D DEBUG_SERIAL=Serial0: `Address 0x77 Not detected` immediately before `[BMP] begin failed: sensorID=0x00` — direct observation of the wrong-address probe (captured 3 deterministic runs)"
    - "Pre-scan on the same bus, same boot, ~150 ms earlier: `0x3C 0x76` ACK; manual 0xD0 register read returns 0x58 (correct BMP280 chip id); identical repeated-start transaction shape works (ec=0 got=1 id=0x58)"
    - "Adafruit_BMP280.h:34 `#define BMP280_ADDRESS (0x77)` vs sensor_pins.h:16 `#define BMP280_ADDRESS 0x76`; sensor_manager.h includes sensor_pins.h, sensor_manager.cpp then includes <Adafruit_BMP280.h> — last definition wins at line 92"
    - "Boot-order audit: Sensors().begin() is the ONLY false-returning pre-OLED init (Debug/PowerMgr/initializeHardware unconditional true, initGPS always true) — matches OLED-dark + no-beacon both stemming from this one abort"
  falsification_test: "Fix the address (begin at 0x76 explicitly) and flash: if boot still aborts at sensor manager, hypothesis wrong. Conversely re-flashing the colliding build must reproduce `Address 0x77 Not detected` + abort — already captured 3x"
  fix_rationale: "(1) Remove the collision: rename the project macro so the library's 0x77 default can never silently win — addresses the exact mechanism. (2) Try both legal BMP280 addresses 0x76 then 0x77 — protects the class (breakout strapping variance). (3) Degrade gracefully on absent BMP (mirror camera pattern, null out bmp280 so existing guards engage) — removes the AND-gate contributor that turns any sensor hiccup into total loss of telemetry+OLED. Fixes the cause, not the symptom: nothing here fakes sensor data or forces the address only for this boot"
  blind_spots: "PacketMgr().begin()/SysState().begin() have never executed on this hardware (boot never got past sensors) — a second latent abort would surface as boot stopping at a LATER stage (OLED would show the frozen stage; Serial0 abort traces localize it). E32 RF TX success is asserted by the E32 AUX handshake, not by the base actually receiving — final link verification is the human-verify checkpoint. GPS/antenna/range issues may still exist behind this fix"
  candidate_causes:
    - "code/config: BMP280_ADDRESS macro name collision between sensor_pins.h (0x76) and the Adafruit library header (0x77) — CONFIRMED primary"
    - "code/design: boot treats optional-sensor init failure as fatal (abort) — CONFIRMED amplifier via AND-gate; without it a sensor failure would cost only the BMP rows, not the whole telemetry+OLED mission"
    - "environment/hardware: sensor absent/miswired/dead-bus/0x77-strapped — REFUTED by bus scan + chip id + post-fail scan"
    - "data: part is a BME280 (id 0x60) — REFUTED by chip id 0x58"
  and_gate: "YES — failure required (macro collision making the probe hit 0x77) AND (device physically at 0x76) AND (fatal-abort design converting init failure into total boot failure). Fix addresses all three legs: rename macro, probe both addresses, degrade gracefully"
tdd_checkpoint: null

## Symptoms
<!-- Written during gathering, then immutable -->

expected: Balloon node boots, drives its OLED status screen, and transmits the ~5 s telemetry beacon (0x14) which the base station receives and displays in its web UI
actual: Base station works well but receives NO data from the balloon; the balloon's OLED is completely dark (no pixels at all)
errors: Not yet observed — balloon serial monitor has not been checked (user will capture boot output during the session)
reproduction: Power on the balloon node while the base station runs; observe no OLED pixels and no telemetry arriving at the base web UI
started: Worked before with this hardware (regression). Flashed build is committed 9916e7c (OLED status screens for link bring-up). The working tree has UNCOMMITTED changes to main_balloon.cpp and status_display.cpp on top of 9916e7c that are NOT flashed

## Eliminated
<!-- APPEND only - prevents re-investigating after /clear -->

## Evidence
<!-- APPEND only - facts discovered during investigation -->

- timestamp: 2026-08-22T11:02:01Z
  checked: git state vs flashed build
  found: Flash = committed 9916e7c; working tree carries uncommitted main_balloon.cpp (+17/-2) and status_display.cpp (+23/-4) NOT on the device
  implication: Debug against the 9916e7c code, not the working tree; tree changes may be an in-flight fix attempt for this very fault

- timestamp: 2026-08-22T11:32:00Z
  checked: uncommitted diff content (git diff src/main_balloon.cpp src/status_display.cpp)
  found: Diff is pure instrumentation, NOT a fix — Serial0 (UART0/CH343 bridge) boot-stage traces + a one-shot I2C bus scan (expect 0x76 BMP280 + 0x3C OLED). Comment confirms: with ARDUINO_USB_CDC_ON_BOOT=1, app Serial goes to native USB, so the developer mirrored diagnostics to UART0 where the bridge cable can see them
  implication: The previous session already suspected boot-abort + I2C device detection; prepared but never flashed this instrumentation. Also implies native-USB serial output was not visible/reliable to the user

- timestamp: 2026-08-22T11:40:00Z
  checked: balloon boot order in 9916e7c main_balloon.cpp initializeSubsystems() vs first OLED pixel
  found: Boot aborts (setup() returns, loop() parks on delay(1000) because appState.initialized==false) if ANY of: PowerMgr().begin(), Sensors().begin(), LoRaComm().begin(), PacketMgr().begin(), SysState().begin() returns false. StatusOLED().begin(BALLOON) + first showBootStage happen only AFTER PowerMgr+Sensors succeed. Camera, E32LoRa, CmdHandler, AutoCap, ImageTx failures are warnings only — boot continues
  implication: "OLED completely dark" (never even boot-stage text) + "no telemetry" unify under a boot abort at PowerMgr or Sensors — OR a hang/crash before StatusOLED().begin()

- timestamp: 2026-08-22T11:47:00Z
  checked: every pre-OLED init at 9916e7c for failure/hang paths
  found: DebugUtils::begin() unconditional `return true`. PowerManager::begin() unconditional `return true` (ADC config + cached readings only). initializeBoard()/initializeHardware() unconditional `return true`. SensorManager::begin() returns false ONLY when initBMP280() fails = Adafruit_BMP280::begin(0x76) gets no ACK on Wire(GPIO1=SDA, GPIO2=SCL); initGPS() always returns true (just Serial1.begin). No blocking loops (no while(!Serial), delays bounded) in any pre-OLED path
  implication: The ONLY code path that aborts balloon boot before the first OLED pixel is BMP280-not-acknowledged-at-0x76. Boot order makes Sensors().begin() the sole gate

- timestamp: 2026-08-22T11:50:00Z
  checked: BMP280 address handling at 9916e7c (src/sensor_manager.cpp initBMP280 + include/sensor_pins.h)
  found: Code tries ONLY 0x76 (`bmp280->begin(BMP280_ADDRESS)`), no 0x77 fallback — while the OLED path does have a 0x3C→0x3D fallback. sensor_pins.h itself documents 0x77 as a real alternative. Wire pins GPIO1/GPIO2 shared BMP280+OLED
  implication: Two candidate root causes for the ACK failure: (a) hardware/bus — sensor absent, unpowered, miswired, no pull-ups; (b) config — this breakout is at 0x77 and the code never probes it. Both produce identical firmware behavior

- timestamp: 2026-08-22T11:53:00Z
  checked: UAT history (03-UAT.md) + commit message of 9916e7c
  found: "No data from balloon" was ALREADY UAT gap G-03-1 during phase-03 UAT BEFORE 9916e7c was flashed; 9916e7c's OLED screens were built specifically as a diagnostic aid for it. After flashing 9916e7c the balloon OLED is completely dark (new observation)
  implication: The fault predates 9916e7c and aborts boot before the OLED code the commit added — consistent with the BMP280 gate. The user's "worked before with this hardware" likely refers to the old test builds (test_lora_*, test_oled_lora envs) which never initialized the BMP280 (build_src_filter = single test file) and thus never hit the gate

- timestamp: 2026-08-22T12:05:00Z
  checked: live UART0 boot capture from the balloon (COM11 identified as balloon via app-image string grep: camera_manager+E28 strings excluded from base build; instrumented build flashed)
  found: "[BOOT] I2C pre-scan: 0x3C 0x76" + "0x76 chip id: 0x58" (correct BMP280 id, both stop-separated and repeated-start read shapes) + heap 231K + "[BOOT] abort: sensor manager" + post-fail scan still 0x3C 0x76 — deterministic across 3 boots
  implication: H1's hardware half REFUTED (bus healthy, sensor present at 0x76, genuine BMP280). The abort point (SensorManager) stands; the failure is inside Adafruit_BMP280::begin despite healthy primitives

- timestamp: 2026-08-22T12:20:00Z
  checked: library-internal view via -D DEBUG_SERIAL=Serial0 (BusIO's built-in transaction tracing) + sensorID() probe in sensor_manager.cpp
  found: "Address 0x77 Not detected" immediately before "[BMP] begin failed: sensorID=0x00" — the library probes 0x77, NOT 0x76. sensorID 0x00 = heap-zeroed never-read (begin returned false at detected(), before read8). Retry identical
  implication: bmp280->begin(BMP280_ADDRESS) expands to 0x77 at the call site — the macro resolves to the LIBRARY's value, not sensor_pins.h's 0x76

- timestamp: 2026-08-22T12:25:00Z
  checked: both macro definitions + include order + warning suppression
  found: Adafruit_BMP280.h:34 `#define BMP280_ADDRESS (0x77)`; sensor_pins.h:16 `#define BMP280_ADDRESS 0x76`; sensor_manager.h includes sensor_pins.h, sensor_manager.cpp then includes <Adafruit_BMP280.h> (angle-bracket = system header = redefinition warning suppressed). Last definition wins: 0x77
  implication: ROOT CAUSE CONFIRMED. The balloon has probed 0x77 since sensor_manager.cpp was written — main balloon firmware never initialized sensors on this hardware, consistent with the UAT blocker history

- timestamp: 2026-08-22T12:50:00Z
  checked: fix verification on device (macro rename + both-address probe + graceful degrade + UART0 [BCN] trace)
  found: "[BMP] initialized at 0x76" + "[BOOT] sensors ok" + "[OLED] panel at 0x3C" + "[BCN] seq=1..8 sent ok=1" — 3 consecutive clean boots (identical traces) + 42s continuous run, zero failures
  implication: Fix verified end-to-end on the balloon side: boot completes, OLED initialized, 0x14 beacons transmit with E32 AUX-handshake success every 5s

- timestamp: 2026-08-22T12:55:00Z
  checked: guardrail signal 5 (revert-and-reconfirm) on device — revert-mutant build restoring ONLY begin(BMP280_ADDRESS)
  found: "[BMP] no sensor at 0x76 or 0x77" (init failure returned with both probes on 0x77) BUT boot continued, OLED up, beacons ok — the degrade leg contained the damage exactly as designed
  implication: Address call-site is the controlling variable (mutation killed); the graceful-degrade leg independently prevents sensor-init failures from killing telemetry+OLED. Fix reapplied and re-verified

- timestamp: 2026-08-22T13:20:00Z
  checked: user verification response — "1. OLED is working, 2. There's no telemetry displayed"
  found: Balloon OLED symptom RESOLVED (user-confirmed). No-telemetry half persists. Balloon USB moved off the PC; base station plugged in (COM10, CH340). Identified as base via flash-image strings (Leaflet/web UI present, camera_manager absent)
  implication: Balloon fix verified on the visible symptom; remaining fault is downstream of the balloon's radio (or user's browser). Continue same session for the link half

- timestamp: 2026-08-22T13:35:00Z
  checked: instrumented BASE firmware (Serial0: [E32RX] per byte, [FRAME] CRC verdicts, [BCNRX] acceptance, [E32TX] command TX, [API] per /api/state request), flashed to COM10, captured live UART0
  found: balloon 0x14 beacons arriving every 5s, "[FRAME] type=14 len=30 CRC OK" + "[BCNRX] seq=152..236 accepted" continuously; "[E32TX] cmd=20 len=16 ok=1" (base GET_STATUS poll) followed by "[FRAME] type=11 len=47 CRC OK" (balloon response, CRC valid) — BIDIRECTIONAL link fully operational. ZERO "[API] /api/state" lines across 95s of capture
  implication: Radio link, both protocol directions, CRC, and the telemetry snapshot all work. No web client polled the base during the capture — the browser is not asking (stale tab, laptop off the AP after base reboots restarted it, or wrong IP). The "no telemetry displayed" fault is in the browser/WiFi-connection interaction, not firmware

- timestamp: 2026-08-23T00:05:00Z
  checked: 240s capture during user's tablet session (WiFi toggled, http://192.168.4.1 loaded with cache breakers — user confirms page loads fully live: stops loading when WiFi off; tablet is WiFi-only)
  found: "[NET] clients=1" (tablet associated), "[API] GET / (page load)" x9, ZERO "[API] /api/state" requests, ZERO 404s — the page is served repeatedly but the data poll NEVER fires
  implication: CLIENT-SIDE SCRIPT FAULT isolated: the dashboard's inline JS either fails to parse or throws during init before the initial pollOnce() (line ~1838), so no fetch of /api/state ever happens; tiles stay at their "—" skeleton values with no stale badge (badge requires a failed poll; zero polls occur). Beaacons continue flowing server-side (seq 1099+). GPS irrelevant — no-fix beacons carry temp/alt/battery and the UI renders "No fix" for GPS only




## Resolution
<!-- OVERWRITE as understanding evolves -->

root_cause: "Three confirmed causes, sequentially uncovered. (1) PRIMARY (balloon no-data + dark OLED): BMP280_ADDRESS macro collision — sensor_pins.h defines 0x76, Adafruit_BMP280.h redefines to (0x77) after it in sensor_manager.cpp's include order (angle-bracket include silences the redefinition warning), so initBMP280 probed 0x77 while the sensor ACKs at 0x76 with chip id 0x58; begin() false; boot aborted before the beacon loop and before StatusOLED().begin. (2) AMPLIFIER (same AND-gate): SensorManager::begin treated an optional-sensor init failure as fatal — converted a wrong-address probe into total loss of telemetry AND the OLED diagnostics. (3) LATENT (uncovered after the link came up — 'no telemetry displayed' at the web UI): handleRoot built the ~80 KB dashboard page as ONE Arduino String; String::operator+= silently drops the append when realloc cannot find a contiguous block (base maxAlloc observed 172 KB while the growth path needs ~131 KB contiguous), shipping the page WITHOUT its 57 KB HTML_FOOTER script — 23,027 bytes served = header+sections exactly, byte-for-byte. Tiles rendered as static '—', zero /api/state polls, no stale badge"
fix: "(1) sensor_pins.h renames the macro to BMP280_I2C_ADDRESS with a collision-explaining note; (2) initBMP280 probes both legal strappings via explicit literals (0x76 first, 0x77 fallback) and nulls bmp280 on failure; (3) SensorManager::begin degrades instead of aborting on absent BMP (camera pattern); (4) handleRoot streams the page in 3 parts from PROGMEM (setContentLength + sendContent, house pattern from handleLeafletJs) — dynamic sections String is only ~9 KB, the page never exists as one String; (5) UART0 bring-up diagnostics retained on both boards (boot banner, abort traces, I2C pre-scan + chip id, [BMP] address, [BCN] per-beacon, [NET] 10s, [E32TX], [BCNRX], [FRAME] fail branches, [HTTP] 404, [PAGE] streamed-length + maxAlloc, [API] request lines)"
verification:
  target_test: { result: "pass (device oracle — no automated suite drives firmware boot/serving)", note: "balloon: failing 4/4 boots pre-fix, passing 4/4 post-fix. Web: 3/3 pre-fix page loads shipped 23,027 B script-less with zero polls; post-fix [PAGE] streamed 80,467 B + 32 /api/state polls in 240 s with tele=1" }
  mutation_check: { result: "pass-equivalent (Stryker not applicable to embedded C++/PlatformIO; on-device revert-mutant instead)", note: "restoring ONLY begin(BMP280_ADDRESS)=0x77 re-introduced the sensor-init failure on device; the web fix's pre-fix trace is the deterministic (byte-exact) revert state" }
  no_op_deletion: { result: "pass", note: "fixes are additive (0x77 fallback, null-out, streaming serve path); removals are (a) the RCA-justified fatal-abort and (b) investigation-only instrumentation removed after verification (documented); SSID/wifi_manager.cpp and command_handler.cpp net-zero" }
  adjacent_tests: { result: "skipped — no suite touches changed files (scripts/*.mjs cover protocol codecs)", note: "on-device adjacency: OLED paths both boards, camera boot, command round trip (GET_STATUS + 0x11 response CRC OK observed), Leaflet asset serving all exercised live" }
  revert_and_reconfirm: { result: "pass", bug_returned_on_revert: true, fixed_on_reapply: true, note: "balloon leg: dedicated revert-mutant flash reproduced the failure, reapply passed 3 boots + 42 s continuous. Web leg: revert not reflashed onto the confirmed-working user system (harm without new information) — the pre-fix trace (3 deterministic script-less loads, byte-exact 23,027) serves as the revert state; post-fix trace shows the full page + polls" }
  guardrail_verdict: accepted
oracle_type: "derived + specified: serial-observable mechanism traces ([BMP]/[BCN]/[PAGE]/[API]) + user-confirmed visible behavior (balloon OLED panel, web tiles refreshing live telemetry)"
human_verification: "user confirmed: '1. OLED is working' (balloon panel) and 'Ok thats much better. The data is there and refreshes' (web tiles on tablet over the live link)"
files_changed: [include/sensor_pins.h, src/sensor_manager.cpp, src/main_balloon.cpp, src/status_display.cpp, src/main_basestation.cpp, src/command_sender.cpp, src/image_rx_manager.cpp]

---
status: resolved
trigger: "balloon bootloops at bench — evidence at .planning/debug/bootloop-session6-evidence.md"
created: 2026-08-27
updated: 2026-08-27 late night #3 (RESOLVED — human verification returned: "yes the GPS is receiving data now" on GPIO 41; balloon12.log absent so permanent-build banner provenance recorded best-effort; session archived to resolved/)
---

# Debug: balloon TG1WDT bootloop at bench (blocks bench session #6 / plan 01-20)

**READ FIRST: `.planning/debug/bootloop-session6-evidence.md`** — full dossier from the prior
investigation: confirmed facts, ruled-out causes, discriminator runs, contradiction analysis.
This session continues from that dossier; do not re-derive what it already establishes.

## User direction (2026-08-27)

- **Goal: find_and_fix — root-cause and FIX the bench hardware fault** so stock round-#8
  firmware boots clean. (Panel-disabled unblock build was offered and NOT chosen as the
  primary path; it remains available as a discriminator experiment if useful.)
- **Hands-on: user is AT the bench with a multimeter.** Reseat/swap experiments AND
  voltage/continuity measurements are available (pull-up rail, SDA/SCL levels, supply rails).
  No scope/logic analyzer.

## Symptoms

- **Expected:** Balloon boots clean like session 5 (Aug 25, same source d8ba14e ran a full
  clean session): OLED init → camera init → LoRa init → telemetry loop, answers base commands.
- **Actual:** 40+ consecutive `rst:0x8 (TG1WDT_SYS_RST)` resets on power-up at the bench
  (balloon6.log). Hang point VARIES: 39/40 inside `Adafruit_SSD1306::begin()` between
  `[OLED] I2C scan: 0x3C 0x76` and `[OLED] panel at 0x3C`; 1/40 just after camera-manager init.
  Discriminator runs on old firmware: OLED unplugged → `ESP_ERR_INVALID_STATE` i2c spam
  (~9 ms cadence, one per SSD1306 init cmd) and `oled.begin()` never returns false;
  OLED in + camera reseated → OLED init fully succeeds, then silent wedge in the
  two-statement window `StatusOLED().showBootStage("CAMERA")` → `LoRaComm().begin()`
  (main_balloon.cpp ~:445-450), new Saved PC 0x4037627a.
- **Errors:** `TG1WDT_SYS_RST` resets; Saved PCs decode into the panic handler itself (NOT
  the hang site — int-WDT starved ~300 ms somewhere); `i2cWrite(): i2c_master_transit failed:
  [259] ESP_ERR_INVALID_STATE` (esp32-hal-i2c-ng.c:275) when OLED is unplugged.
- **Timeline:** Started bench session #6 on 2026-08-27 after boards were re-flashed/re-wired.
  Same source ran clean Aug 25 (session 5, balloon5.log). Prior boot-abort fix 1223f46
  (BMP280 address macro) was a different, already-fixed mechanism.
- **Reproduction:** Power on balloon at bench → bootloops, every boot, across: round-#8
  firmware (Aug 25 build), old-firmware rebuild (d8ba14e, Aug 27), OLED in, OLED out,
  camera reseated. Base station on the same bench is healthy (base6.log, COM10).

## Verdict (REVISED 2026-08-27 late — supersedes the inherited dossier verdict)

**"Firmware exonerated" is REVOKED.** The three failing builds (round-#8, d8ba14e rebuild,
camskip-01) all agreed because they all share commit **02142e1** (GPS_TX_PIN 45→35 + 38400
baud, include/sensor_pins.h only) — the "session-5-equivalent source" premise was FALSE:
balloon5's binary was built Aug 24 16:10:13, a day BEFORE 02142e1 (Aug 25 13:17) existed,
and 02142e1 IS an ancestor of d8ba14e. GPS-on-35 is present in 100% of failing builds,
0% of passing builds.

**New leading hypothesis (testable, not yet confirmed): H-GPIO35/PSRAM.** The board builds
with `qio_opi` / 16MB / `psram_type = opi` (N16R8-class octal PSRAM) — on R8-class modules
**GPIO 35-37 are reserved PSRAM data lines and must not be used** (Espressif datasheet).
02142e1 claims GPIO35 as UART1 RX via the GPIO matrix → breaks a PSRAM DQ line for the
product app → corruption/stalls in PSRAM-touching paths → silent int-WDT wedges. Explains
camera-independence, session-5-clean/session-6-broken (the config change and the re-wire
happened TOGETHER between sessions — the confound that manufactured the "hardware
regression" verdict), and the dead GPS RX at bench.

**Demoted to fallback (retained, not eliminated):** physical board/harness damage from the
session-6 re-wire (H-BOARD / H-HARNESS). The prepared multimeter/resistance-matrix round
stays queued as the fallback discriminator if the pin-move test below still bootloops.

**Still open under H-GPIO35 (do not paper over):** why wedges cluster on 0x3C Wire
transactions while 0x76 traffic survives seconds-to-minutes (sub-mechanism: which PSRAM
access sits in those windows — heap spill to PSRAM? cache-line stall micro-mechanism? —
not yet identified). The discriminator does not depend on resolving this; it tests the
single-variable causal claim directly.

## Eliminated
<!-- APPEND only - prevents re-investigating -->

- hypothesis: E32 / LoRaComm().begin() hang inside the old-fw wedge window
  evidence: balloon build links stubs.cpp (lora_comm.cpp excluded by build_src_filter) —
    LoRaComm().begin() is `return true;` with no I/O; real E32 begin (main_balloon.cpp:474)
    sits AFTER the ":453" print that never appears, so it is never reached. E32 begin() is
    fully timeout-bounded and prints when it runs (DEBUG_E32=true).
  timestamp: 2026-08-27 eve

- hypothesis: camera/SCCB interaction is a NECESSARY condition for the bootloop (H1)
  evidence: camskip-01 discriminator (HEAD source, Camera().begin() gated off, flashed
    2026-08-27 16:54 build): STILL bootloops — 37 TG1WDT resets in 120 s with
    esp_camera_init() never called (no SCCB install, no legacy-driver claim on I2C-0).
  timestamp: 2026-08-27 eve

- hypothesis: Wire-vs-SCCB dual-driver collision on I2C-0 as ROOT cause
  evidence: in camskip-01 no second driver ever touches I2C-0, yet wedges/storms persist.
    The collision is a real architectural hazard (and returns with camera enabled) but is
    not what breaks session-6 boots. (Known issue class: esp32-camera #741.)
  timestamp: 2026-08-27 eve

- hypothesis: firmware regression (any build) — third independent build (camskip-01 on
  HEAD) fails the same way as round-#8 and d8ba14e builds
  evidence: camskip-01 wedges identically to stock; d8ba14e (session-5-clean source)
    wedges. Session 5 ran clean on d8ba14e source. Firmware exonerated 3x.
  timestamp: 2026-08-27 eve

- hypothesis: damaged OLED module as the UNIFYING root cause (panel loads/misdrives
  the bus only when addressed with multi-byte writes — orchestrator synthesis)
  evidence: panel-OUT runs wedge at ABSENT-address probes — stock-r8-noled 17/17 die
    right after the scan prints "0x76", i.e. during probes of 0x77-0x7E with NO 0x3C
    device on the bus (status_display.cpp:44-50 probes ascending, prints per-ACK);
    those probes are byte-shape-identical to the probe that just ACK'd at 0x76.
    The floating-bus cofactor is unavailable: internal ~45k pull-ups are always
    enabled (established), and 0x76 traffic ran clean for 23 s panel-out (camskip
    cycle 20) — a floating/marginal bus would degrade it too. No physical device at
    0x3C exists in those runs to cause anything. Panel stays a CONTRIBUTOR candidate
    for panel-in runs (swap test worthwhile if a spare exists), demoted from root.
  timestamp: 2026-08-27 night

- REVOCATION (2026-08-27 late night): the entry above — "firmware regression (any build)
    ... Firmware exonerated 3x" — is INVALID and must not be relied on.
  reason: its premise "Session 5 ran clean on d8ba14e source" is factually wrong.
    balloon5.log:23 banner = "Build: Aug 24 2026 16:10:13" (compiled BEFORE 02142e1,
    Aug 25 13:17, the GPS_TX_PIN 45→35 + 38400 baud change). 02142e1 IS an ancestor of
    d8ba14e, so the "old-fw" discriminator rebuild (d8ba14e, built Aug 27 16:07) and
    camskip-01 (HEAD) BOTH carried GPS-on-35. All three "exonerating" builds share the
    one config change that the clean build lacks. The prior evidence entry "git topology
    of the two flashed firmwares ... d8ba14e (Aug 25 15:22, session-5 source)" is likewise
    corrected: the session-5 SOURCE TREE was the Aug-24-16:10-era tree (code-wise 90300b8;
    d6c2ab1/07eaea0 are docs-only follow-ups), NOT d8ba14e.
  consequence: firmware (specifically the 02142e1 GPS pin config) is BACK IN SCOPE as the
    leading candidate. See Verdict + Evidence (late-night entries) + Current Focus.

## Evidence index

- Dossier: `.planning/debug/bootloop-session6-evidence.md`
- Logs (repo root): balloon5.log (session 5, clean), balloon6.log (session 6, 40+ resets),
  balloon9.log (OLED unplugged), balloon10.log (bench round: stock r#8, panel in, 6 resets),
  base5.log, base6.log
- Discriminator logs: `/tmp/bootloop-discriminator/oldfw-boot.log`, `reseated-boot.log`,
  `noled-boot2.log` (worktree checkout of d8ba14e with its own .pio build)
- Suspect firmware window (exonerated but for reference): `git diff 4ba714a..HEAD -- src/ include/`
- Firmware.elf for addr2line: `.pio/build/esp32-s3-balloon/firmware.elf`

## Evidence (appended 2026-08-27 evening, session 2)

- timestamp: 2026-08-27 eve
  checked: platformio.ini build_src_filter for env esp32-s3-balloon
  found: balloon build EXCLUDES lora_comm.cpp and INCLUDES stubs.cpp —
    `LoRaManager::begin()` is a stub `return true;` (no SPI, no UART, no prints). Identical
    at d8ba14e. The E32 real init is `E32LoRaModule().begin()` at main_balloon.cpp:474,
    AFTER the ":453" print that never appears.
  implication: the old-fw wedge window (:443→:453) contains exactly ONE real operation —
    `StatusOLED().showBootStage("CAMERA")`, a Wire I2C burst. E32/LoRaComm fully exonerated
    for every observed wedge (never reached). Dossier's "E32 not yet exonerated" is now
    closed.
- timestamp: 2026-08-27 eve
  checked: reseated-boot.log — last two lines before each of the resets
  found: 25/25 TG1WDT boots end at exactly "[INFO] (initializeSubsystems:443): Camera
    manager initialized" → silent reset. Deterministic.
- timestamp: 2026-08-27 eve
  checked: balloon6.log (round-#8) per-boot last app line
  found: every cycle's last app line = "[OLED] I2C scan: 0x3C 0x76" → wedge inside
    Adafruit_SSD1306::begin() init burst, BEFORE camera init (one cycle died before OLED
    scan line at all). 7 full cycles captured in file.
- timestamp: 2026-08-27 eve
  checked: balloon5.log (session 5, healthy reference)
  found: ":443 Camera manager initialized" → ":453 LoRa communication initialized"
    immediate; BMP280 LIVE telemetry after "System ready" (P=101613.xx Pa varying,
    T=26.80C); only 1 rst total (power-on).
  implication: post-camera Wire transactions to 0x76 on pins 1/2 WORKED Aug 25 — the
    Wire/SCCB controller-sharing was benign on session-5 hardware.
- timestamp: 2026-08-27 eve
  checked: addr2line of saved PCs vs both ELFs
  found: round-#8 0x40375e9f = panic_enable_cache (panic_handler.c:285); old-fw
    0x4037627a = xt_highint4 (esp32s3 highint_hdl.S:49, the int-WDT level-4 handler);
    0x40379576 = esp_cpu_wait_for_intr but belongs to an rst:0xc RTC_SW_CPU_RST (upload
    reset — irrelevant).
  implication: interrupt watchdog fired; panic path produced ZERO UART output (silent) —
    consistent with ints starved ≥300 ms somewhere, PC not the hang site (confirms
    dossier).
- timestamp: 2026-08-27 eve
  checked: esp32-camera 2.0.4 driver/sccb.c + esp_camera.c camera_probe() +
    camera_manager.cpp configureCameraForBalloon()
  found: pin_sccb_sda = SIOD_GPIO_NUM = 4 (not -1) → SCCB_Init(4,5) → sccb_i2c_port =
    SCCB_I2C_PORT_DEFAULT = 0 (CONFIG_SCCB_HARDWARE_I2C_PORT1 undefined in Arduino
    builds) → LEGACY driver/i2c.h: i2c_param_config(0, {sda=4,scl=5,pullups=internal,
    100k}) + i2c_driver_install(0) — installs a second driver context/ISR on I2C_NUM_0 and
    re-routes its SDA/SCL to GPIO 4/5. Arduino Wire = NEW i2c-ng driver on I2C-0 pins 1/2
    (log: "i2cInit(): Initializing I2C Master: num=0 sda=1 scl=2 freq=100000").
  implication: two incompatible I2C driver stacks share controller I2C-0 after camera
    init — architectural hazard present in ALL sessions; electrical state decides whether
    the collision is survived.
- timestamp: 2026-08-27 eve
  checked: sensor_pins.h pin map vs camera_pins.h ESP32S3_EYE
  found: Wire bus GPIO 1/2 (BMP280 0x76 + OLED 0x3C); camera SCCB GPIO 4/5; E32 UART
    48/14 + M0/M1/AUX 19/20/21; GPS 35/47. ALSO: BATTERY_SENSE_PIN=4 collides with
    SIOD=4 (camera SCCB data) — pre-existing (both sessions, unchanged; explains ADC pegged
    at 4095; deprioritized but real).
- timestamp: 2026-08-27 eve
  checked: git topology of the two flashed firmwares
  found: d8ba14e (Aug 25 15:22, session-5 source) vs HEAD (round-#8): diff = command-path
    files only (command_sender +77, image_tx_manager +182, command_handler +49,
    auto_capture +9, headers). main_balloon.cpp / stubs.cpp / status_display.cpp /
    sensor_manager.cpp / e32_lora.cpp IDENTICAL. balloon6 build = Aug 25 23:33 (round-#8);
    old-fw rebuild = Aug 27 16:07 of d8ba14e.
  implication: firmware source exonerated (dossier verdict stands); the differing failure
    SITES between builds (oled.begin vs post-camera) reflect build/timing sensitivity of
    the same underlying fault, not different bugs.
- timestamp: 2026-08-27 eve (camskip-01 run)
  checked: discriminator build with Camera().begin() skipped, OLED UNPLUGGED at bench
    (as-left from noled-boot2), captured /tmp/bootloop-discriminator/camskip-boot.log
    (120 s, 37 TG1WDT + 1 POWERON cold start)
  found: 36/37 cycles die at "[OLED] I2C scan: 0x76" (inside oled.begin probing absent
    0x3C, mostly SILENT wedge, inv=0); cycle 6 showed 72 INVALID_STATE; cycle 20
    COMPLETED a full boot ("System ready", 21 LIVE BMP280 telemetry reads P=102171.xx
    varying, 4 E32 TX) while 336 INVALID_STATE errors streamed from OLED writes
    (1471→23408 ms, ~9 ms cadence), then died.
  implication: (1) camera NOT necessary — bus-1/2 fault confirmed independent of SCCB;
    (2) BMP280 transactions keep working while 0x3C transactions fail — the pathology is
    transaction/target-specific, not a global bus death; (3) wedge point is STOCHASTIC
    per warm reset (1/20 full recovery) — warm TG1WDT resets sometimes clear the
    controller state; (4) Adafruit_SSD1306::begin() returns TRUE with panel absent
    ("panel at 0x3C" printed in cycle 20 with only 0x76 on the bus) — presence was never
    validated by that print; (5) no slave latch-up that survives warm resets (cycle 20
    worked after 19 failures).
- timestamp: 2026-08-27 eve
  checked: esp32-hal-i2c-ng.c internal pull-up handling
  found: i2cInit sets bus_config.flags.enable_internal_pullup = 1 — GPIO1/2 always have
    weak (~45k) internal pull-ups; Wire transactions carry ms-bounded timeouts, so a
    plain timeout would PRINT (log_e) — the silent wedges are not timeouts.
  implication: silent wedge = code path where the timeout machinery itself cannot fire
    (interrupt-starved region), matching the TG1WDT/int-WDT evidence; driver-level, but
    TRIGGERED by electrical conditions on bus 1/2.
- timestamp: 2026-08-27 eve (stock-r8-noled baseline)
  checked: stock round-#8 re-flashed after reverting camskip edit (src/ tree verified
    clean vs HEAD), OLED still unplugged, captured
    /tmp/bootloop-discriminator/stock-r8-noled.log (45 s)
  found: 17/17 cycles die at "[OLED] I2C scan: 0x76" with ZERO INVALID_STATE (all silent
    wedges inside oled.begin probing the absent panel).
  implication: current bench state (OLED out) deterministically wedges on absent-device
    probe; balloon now runs stock round-#8 HEAD — canonical state for the user's physical
    discriminator round (replug OLED + cold power cycle + multimeter).
- timestamp: 2026-08-27 eve
  checked: web research on driver pathology (known issues)
  found: esp32-camera #741 (legacy/new i2c driver conflict — matches our architectural
    hazard, NOT our root cause per camskip); arduino-esp32 #11949 (i2c_master_transmit
    sync mode failures); Arduino forum threads on core-3.2 I2C error storms. No known
    issue explains wedge-on-absent-device without camera involvement.
  implication: physical bench fault remains the root cause; driver behavior is the
    symptom surface.
- timestamp: 2026-08-27 night (user bench round — balloon10.log)
  checked: balloon10.log (repo root, 381 lines, COM11 115200) — user replugged OLED
    and captured on stock round-#8 (Build: Aug 27 2026 17:01:00 — matches the
    canonical re-flash; NOT the 16:07 old-fw build)
  found: 6 resets, ALL rst:0x8 TG1WDT_SYS_RST, no other rst types, no Backtrace/Guru
    output (silent wedges). ZERO ESP_ERR_INVALID_STATE → panel present (signature
    confirmed). Every boot: BMP280 chip-id read 0x58 OK, BMP init OK, GPS init OK,
    "[OLED] I2C scan: 0x3C 0x76" (panel ACKs address probes each boot). 5/6 boots
    wedge silently inside Adafruit_SSD1306::begin() init burst right after the scan
    line (balloon6 signature). 1/6 (boot 4, lines 209-253) completes FULL
    oled.begin() + showBootStage("BOOT")/(\"SENSORS\") display bursts + camera init
    (:443, Sensor PID 0x26, PSRAM ok) then silent TG1WDT with Saved PC 0x4037627a —
    wedge in showBootStage("CAMERA") Wire burst; :453 never reached. No boot reached
    System ready. Capture begins at a fresh boot banner with no power-on rst line →
    full 10 s cold power-cycle adherence UNCONFIRMED (carry-forward caveat).
  implication: canonical-state reproduction on stock round-#8 with panel in; failure
    distribution matches balloon6. The post-camera-init first-0x3C-burst wedge is now
    25/25 across every boot that reaches it (24 reseated old-fw + 1 here) — yet
    camera remains unnecessary (camskip-01), so the SCCB interaction modulates WHERE
    the fault lands, not WHETHER: second-order interaction, not root.
- timestamp: 2026-08-27 night (synthesis evaluation)
  checked: orchestrator's unification claim ("partially-failed OLED loads/misdrives
    bus only when addressed with multi-byte writes") against the panel-OUT run class
  found: REFUTED as unifying root. Panel-out wedges sit at absent-address probes
    (stock-r8-noled 17/17 die immediately after scan prints 0x76 — code shows probes
    0x77-0x7E follow, byte-identical in shape to the successful 0x76 probe). With the
    OLED physically absent no 0x3C actor exists. Floating-bus cofactor defeated:
    internal ~45k pull-ups always enabled, and 0x76 ran clean 23 s panel-out (camskip
    cycle 20) plus every panel-out boot's chip-id read/BMP init/scan succeeded.
    Also: the synthesis's own prediction ("idle SDA/SCL reads ~3.3 V with panel in")
    holds for EVERY surviving hypothesis except stuck-low → idle DC is a weak
    discriminator, retained only because it is nearly free.
  implication: leading hypotheses re-ranked — (1) H-BOARD: balloon ESP32-S3 board
    damage localized to GPIO1/GPIO2 pads/IO-MUX or the I2C-0 peripheral (plausibly
    ESD during the session-6 re-wire handling; explains session-5-clean →
    session-6-broken on same board, base station healthy on a different board, 0x76
    surviving light load while the SSD1306 path's sustained transaction bursts trip
    the marginal hardware, warm-reset stochastic clearing); (2) H-HARNESS: damaged
    conductor / partial short from the re-wire (explains reseat-sensitivity of
    signatures); (3) H-PANEL demoted to panel-in contributor. Decisive
    discriminators re-ordered accordingly: unpowered resistance matrix first (direct
    H-HARNESS test + catches broken module-supply conductor = parasitic-power
    precondition), then substitution (spare panel / spare devkit), then
    GPIO-relocation discriminator build (bus 1/2 → free pins 3/41 with fresh wires)
    which splits board-pads from deeper-board if harness checks read clean.

## Evidence (appended 2026-08-27 late night — provenance correction round)

- timestamp: 2026-08-27 late
  checked: build-banner provenance of every balloon log vs git history of 02142e1
    (GPS_TX_PIN 45→35, GPS_RX 46→47, baud 9600→38400; include/sensor_pins.h ONLY, +6/-3)
  found: balloon5.log:23 "Build: Aug 24 2026 16:10:13" — compiled BEFORE 02142e1
    (Aug 25 13:17). balloon6.log:13 "Aug 25 23:33:02" (round-#8). balloon10.log:13
    "Aug 27 2026 17:01:00" (stock re-flash). d8ba14e rebuild built Aug 27 16:07 (session
    file record). `git merge-base --is-ancestor 02142e1 d8ba14e` → TRUE. Aug-24 build-time
    tree: last code commit 90300b8 (15:59); d6c2ab1 (16:03) and 07eaea0 (16:31) are
    docs-only. Diff 90300b8..d8ba14e --stat: sensor_pins.h (9 lines = 02142e1) plus
    command/image-path files only — NO other boot-path or pin-config change.
  implication: GPS-on-35 present in 4/4 failing builds (balloon6, d8ba14e-rebuild,
    camskip-01, balloon10), absent in 1/1 passing build (balloon5). The ONLY source change
    touching pin configuration / boot-early hardware init between the passing and failing
    firmwares is 02142e1. "Firmware exonerated" pillar collapses (see Eliminated
    REVOCATION).
- timestamp: 2026-08-27 late
  checked: PSRAM topology of the balloon build + GPIO35 legality
  found: platformio.ini env esp32-s3-balloon (and ALL other envs incl.
    esp32-s3-test-lora-balloon): board_build.arduino.memory_type = qio_opi,
    flash_size = 16MB, psram_type = opi → N16R8-class OCTAL PSRAM. On R8-class modules
    GPIO 35/36/37 are reserved PSRAM lines (Espressif ESP32-S3 datasheet; multiple pin
    references) and must not be used. GPS_TX_PIN (ESP32 RX pad) = 35 (sensor_pins.h:35).
  implication: Serial1.begin(38400, SERIAL_8N1, 35, 47) (main_balloon.cpp:615 AND
    sensor_manager.cpp:137 — both consume the macro) routes U1RXD onto a reserved PSRAM
    DQ pad via the pin matrix/IOMUX → plausibly breaks PSRAM accesses for the product app.
    CAVEAT recorded: test_lora_balloon (the "proven GPIO 35" source, src:42 "trying GPIO
    35") runs the SAME opi config — its success is only consistent if the test app never
    exercises PSRAM after boot (no camera, no big allocations). That survival assumption
    is UNVERIFIED; it is a reconciliation gap, not evidence against H-GPIO35.
- timestamp: 2026-08-27 late
  checked: GPS RX liveness per build (checkHardwareStatus — line refs 670/672 are the two
    branches of the SAME current-source if/else; no source drift in that function)
  found: balloon5 (pin 45, 9600): "GPS communication detected" EVERY boot (garbage bytes
    at wrong baud), then "GPS: No valid data" forever (checksum failures). balloon6 AND
    balloon10 (pin 35, 38400): "No GPS communication detected (may need more time)" on
    EVERY boot — zero bytes ever seen on the pin-35 product builds at bench.
  implication: secondary signal consistent with a non-functional/contended pad-35 RX in
    product builds. NOT separable from "GPS TX wire still physically on 45" (rewire state
    unverified from logs) — either way the product GPS-on-35 config has never delivered
    data at bench. The fix path must physically confirm/re-terminate the GPS TX wire.
- timestamp: 2026-08-27 late
  checked: session-6 "hardware regression" timeline vs the config change
  found: the re-wire AND the GPS pin change both landed between session 5 (clean,
    Aug-24-binary) and session 6 (bootloop, Aug-25-23:33 binary) — perfectly confounded.
    No bench run of product firmware with GPS-on-35 ever succeeded (balloon6 was the
    FIRST product build with it and bootlooped immediately).
  implication: the physical-damage hypothesis (H-BOARD/H-HARNESS) was inferred from a
    confound; it stays as fallback only. The flash-only pin-move discriminator splits the
    two cleanly with no wiring change.
- timestamp: 2026-08-27 late
  checked: open sub-mechanism under H-GPIO35 (recorded so it is not papered over)
  found: NOT yet explained why wedges cluster on 0x3C Wire bursts while 0x76 traffic
    survives 23+ s (camskip cycle 20), or what PSRAM access exists in the wedge windows
    (camskip has no camera; the exact PSRAM-touching operation in the SSD1306 path is
    unidentified). Micro-mechanism candidates: heap spill into PSRAM arenas once internal
    fragments (Arduino malloc threshold), cache-line load stall blocking interrupt
    retirement, PSRAM heap-metadata corruption expressing seconds later. All UNVERIFIED.
  implication: H-GPIO35's causal claim is testable NOW (pin-move discriminator) without
    resolving the micro-mechanism; if confirmed, the clustering question becomes a
    follow-up (and a PSRAM-exercise audit) rather than a blocker.

## Evidence (appended 2026-08-27 late night #2 — discriminator result + fix phase)

- timestamp: 2026-08-27 late #2 (checkpoint response — user bench report)
  checked: flash-only pin-move discriminator result vs the pre-registered branch map
  found: CLEAN — user reports balloon STABLE after the GPS RX pin change to GPIO 41
    ("That was the problem, it's stable again"). Branch (a) of the pre-registered
    expectation hit ⇒ H-GPIO35/PSRAM CONFIRMED as root cause; fallback H-BOARD/
    H-HARNESS never needed to explain the failing/passing split (stays recorded).
  implication: 02142e1's routing of UART1 RX onto reserved OPI-PSRAM line GPIO 35 is
    THE root cause — present in 4/4 failing builds, absent in 1/1 passing build, and
    removing only that pad claim fixes the boot. Eliminated-set stands at 6 entries
    with the false firmware-exoneration formally REVOKED.
- timestamp: 2026-08-27 late #2
  checked: balloon11.log (repo root, 30,252 lines, COM11 115200 capture)
  found: monitor attached MID-RUN — no boot banner in the capture (binary provenance
    not directly observable from the log; working tree at capture time = stock HEAD +
    the single-line scratch pin edit, and the user's stability report explicitly
    attributes the change). Contents: 11,963 live BMP280 reads (P 102,081→101,613 Pa
    and T 25.68→23.47 °C drifting over the capture — genuinely live sensor), 3,543 E32
    transmissions, [BCN] seq=2423 sent=2423 ok=1 (100 % beacon success), 5,998
    "GPS: No valid data". ZERO "rst:" lines of any kind, zero ESP-ROM/boot banners
    mid-file, zero error/guru/backtrace/WDT lines. Duration ≈1.7-3.3 h by beacon/
    telemetry cadence bounds — vastly beyond the ≥5 min "stays up" gate.
  implication: (1) hours-stable uptime on the pin-41 build; combined with balloon10
    (stock HEAD, 6/6 TG1WDT the same day) this constitutes a hardware-executed
    single-variable A/B — the embedded-firmware equivalent of revert-and-reconfirm
    (guardrail signal 5). (2) GPS: "No valid data" requires location.isValid() + ≥4
    sats + HDOP (sensor_manager.cpp:276 validateGPSData) — EXPECTED indoors regardless
    of wiring and identical to the healthy session-5 indoor signature; the wire
    position question stays open until a boot banner shows
    "GPS communication detected" (bytes seen) on the next flash.
- timestamp: 2026-08-27 late #2
  checked: git status/diff of working tree + GPS_TX_PIN consumers (grep)
  found: only code change in tree = include/sensor_pins.h single line GPS_TX_PIN
    35→41 (its trailing comment still the stale "proven GPIO 35" text). Product
    consumers of the macro: main_balloon.cpp:615 and sensor_manager.cpp:137 (both
    Serial1.begin with the macro — no hardcoded 35 anywhere in product code).
    RECURRENCE VECTOR identified: src/test_lora_balloon.cpp:42 still #defines
    GPS_TX_PIN 35 with "trying GPIO 35" — the standalone test app whose config
    02142e1 copied into sensor_pins.h; it survives on 35 only because it never
    exercises PSRAM (no camera).
  implication: permanent fix = sensor_pins.h macro (already 41) + corrected comment
    block + corrected "Sensor pins used:" validation list (still shows 35 as legal
    and claims "No conflicts detected" while BATTERY_SENSE 4 = camera SIOD collides)
    + comment-only guard at test_lora_balloon.cpp:42 so nobody re-"proves" 35 and
    copies it back.

## Bench state & tooling warnings (updated 2026-08-27 late #2 — post-discriminator)

- Balloon CURRENTLY runs the SCRATCH PIN-41 BUILD (stock HEAD source + single-line
  include/sensor_pins.h GPS_TX_PIN 35→41 edit) — STABLE per user report + balloon11.log
  (hours, zero resets). Functionally identical to the pending permanent fix
  (comment-only delta), but NOT a committed-HEAD build: re-flash after the fix commit
  for the canonical pre-session-#6 state. OLED is REPLUGGED (balloon10/11 signatures).
  Evidence logs in repo root: balloon10.log (stock bootloop), balloon11.log (pin-41
  stable run).
- Discriminator history this session: camskip-01 build flashed+captured (120 s), then
  reverted and stock re-flashed+captured (45 s baseline, 17/17 loop, OLED out).
- Uploads from this harness need `PYTHONIOENCODING=utf-8` (cp1252 crashes on esptool
  progress bars → wedge/hang). Killed uploads orphan esptool children that hold COM11 —
  kill by PID before retry. User's base monitor on COM10 is separate; balloon = COM11.
- Battery ADC pegged at 4095 in BOTH sessions 5 and 6 — not a change, deprioritized
  (likely BATTERY_SENSE_PIN=4 colliding with camera SIOD=4 — pre-existing).
- Camera still enumerates (Sensor PID 0x26, same as session 5).
- User already reseated OLED (no effect) and camera ribbon (changed signature, no fix).
- GPS TX WIRE PHYSICAL LOCATION UNVERIFIED (45 vs 35) — never established from logs;
  required knowledge for the FIX step (rewire target), not for the flash-only
  discriminator. Ask user to eyeball it while at the bench.
- Scratch discriminator pending: sensor_pins.h GPS_TX_PIN 35→41 single-line edit. After
  that flash, the balloon NO LONGER runs stock round-#8 — revert with
  `git checkout -- include/sensor_pins.h` (src/include tree is otherwise clean) before
  any stock re-flash.

## Checkpoint bookkeeping (carry-forward)

- Plan 01-20 Task 1 blocked — bench session #6 could not run. NO ledger flips; WINDOWS
  3/5/8/9 and G-01-7/G-01-9 stay OPEN.

## Current Focus

status: RESOLVED (2026-08-27 late #3) — human verification returned POSITIVE: "yes the
  GPS is receiving data now" (bytes flowing on GPIO 41; wire-on-41 confirmed in the
  checkpoint round; stability closed by balloon11.log multi-hour zero-reset run on the
  functionally identical build). balloon12.log does NOT exist in the repo root (logs end
  at balloon11.log / base6.log) → permanent-build (3f2c2c6) banner build-time provenance
  NOT closed from a captured log — recorded honestly as user-confirmed functional
  verification with best-effort canonical-binary provenance. The hardware A/B
  (balloon10: stock HEAD 6/6 TG1WDT same day vs balloon11: pin-41 hours-stable) is
  dispositive either way. KB correction: .planning/debug/knowledge-base.md DOES exist
  (prior entry balloon-no-data-oled-blank) — the earlier "not yet present" note was
  stale; no Phase-0 semantic match applied to this session's symptoms regardless.
next_action: none — archived to resolved/, KB entry appended, docs committed. Open
  field item (outside this session): outdoor GPS fix test (indoor "No valid data" is
  the expected healthy signature). Post-debug handoff: plan 01-20 Task 1 / bench
  session #6 proceeds with WINDOWS 3/5/8/9 and G-01-7/G-01-9 open, no ledger flips.
mempalace_indexing: skipped — no MemPalace MCP tools available in this environment;
  knowledge-base.md is the durable fallback.

--- (historical: state at the human-verify checkpoint follows) ---

hypothesis: CONFIRMED (pre-registered branch (a) hit) — H-GPIO35/OPI-PSRAM is the root
  cause. Flash-only pin-move discriminator (GPS_TX_PIN 35→41, the ONLY change vs stock
  HEAD) ran CLEAN at bench: user report "That was the problem, it's stable again" +
  balloon11.log (30,252 lines: 11,963 live BMP280 reads, 3,543 E32 TX with beacon
  seq=2423 ok, ZERO rst lines, ZERO errors; capture attached mid-run ≈1.7-3.3 h stable
  uptime). Stock HEAD (balloon10, Aug 27 17:01 build) bootlooped 6/6 the SAME DAY —
  a hardware-executed single-variable A/B. Fallback H-BOARD/H-HARNESS not needed to
  explain the failing/passing split (stays recorded, never invoked).
test: (fix phase) permanent GPS_TX_PIN=41 in include/sensor_pins.h with documentation
  of WHY 35-37 are forbidden on this opi-PSRAM board; corrected stale comments (GPS
  block still claims "ESP32 RX on GPIO 35"; pin-validation list shows 35 as legal and
  says "No conflicts detected" despite the battery-4/camera-SIOD collision);
  comment-only recurrence guard at src/test_lora_balloon.cpp:42 (the origin of the
  bad pin — 02142e1 copied its config into product). Build env esp32-s3-balloon to
  verify compile. Guardrail: no test suite → signals 3 (no-op/deletion) + 5
  (revert-and-reconfirm via the bench A/B) recorded in Resolution.verification.
expecting: clean build; the permanent fix is comment-only delta vs the bench-proven
  scratch build (same macros → functionally identical binary); then human-verify
  checkpoint for the permanent-build flash + GPS-path confirmation.
next_action: HUMAN-VERIFY CHECKPOINT returned (see session-manager): user flashes
  commit 3f2c2c6 (functionally identical to the bench-proven scratch build —
  comment-only delta), verifies boot banner + cold power-cycle ≥5 min zero rst:0x8,
  and settles the GPS wire question (on 41: expect "GPS communication detected" at
  boot; outdoor fix test for full closure; not on 41: re-terminate wire to GPIO41).
  On confirmation → archive_session (move to resolved/, KB entry for the
  opi-PSRAM-reserved-pin failure class, docs commit), then bench session #6 / plan
  01-20 Task 1 proceeds (WINDOWS 3/5/8/9, G-01-7/G-01-9 open — no ledger flips).
open_items_for_user (fold into checkpoint): (1) GPS TX wire physical location (41 vs
  old 35/45 pad) still unverified — balloon11 attached mid-run so the boot-time
  "GPS communication detected" line was not captured; next flash's boot output decides
  (bytes on 41 = wire moved); outdoor fix test remains for full GPS closure.
  (2) Permanent-build banner provenance + cold power-cycle ≥5 min zero-rst:0x8 gate
  (the balloon currently runs the scratch build, not a HEAD-committed build).
  (3) If wire is NOT yet on 41: re-terminate GPS TX wire to GPIO41.
known_pattern_candidate: none in KB (knowledge-base.md not yet present for this project)
reasoning_checkpoint:
  hypothesis: "commit 02142e1 set GPS_TX_PIN (UART1 RX pad) = 35, a reserved octal-PSRAM
    data line on this opi-PSRAM board; the GPIO-matrix claim breaks PSRAM for the
    product app, causing the silent int-WDT bootloop; every failing build carries this
    config, the one passing build lacks it"
  confirming_evidence:
    - "build-banner + ancestry audit: GPS-on-35 in 4/4 failing builds (balloon6 Aug 25
      23:33, d8ba14e rebuild Aug 27 16:07, camskip-01, balloon10 Aug 27 17:01), absent
      in balloon5 (built Aug 24 16:10:13, pre-02142e1); 02142e1 is an ancestor of
      d8ba14e; only pin-config/boot-hardware change in the window"
    - "board is qio_opi/16MB/opi (N16R8-class): GPIO 35-37 reserved for octal PSRAM
      (Espressif datasheet + pin references)"
    - "GPS RX has never seen a byte in any pin-35 product build at bench (balloon6 +
      balloon10: 'No GPS communication detected' every boot) while the pin-45 build saw
      bytes — the config has never worked in the product firmware, matching a broken
      pad claim"
    - "the 'hardware regression between sessions' was perfectly confounded: re-wire AND
      pin change landed together; balloon6 was the FIRST product build with GPS-on-35
      and bootlooped immediately"
  falsification_test: "scratch build with GPS_TX_PIN=41 (only change: U1RXD pad off the
    PSRAM line) STILL bootloops ⇒ H-GPIO35 eliminated — pad-35 claim exonerated, GPS
    config out of scope, physical bench round resumes; (conversely, ≥5 min clean boots
    with zero code changes beyond the pin ⇒ confirmed)"
  fix_rationale: "if confirmed: permanent GPS_TX_PIN=41 at the single definition point
    (sensor_pins.h, consumed by main_balloon.cpp:615 + sensor_manager.cpp:137) removes
    the pad conflict entirely — addresses the config root cause, not the OLED/I2C
    symptoms; wire re-termination + GPS liveness verification closes the delivery side"
  blind_spots: "exact PSRAM access inside the wedge windows unidentified (0x3C-burst
    clustering vs healthy 0x76 unexplained — open sub-mechanism, follow-up if
    confirmed); test_lora_balloon ran the SAME opi config and survived — consistent only
    if it never exercised PSRAM post-boot (unverified assumption); n=1 passing build and
    it also differs in command-path code (weak rival — wedge sites are not in command
    path); int-WDT micro-mechanism (cache/PSRAM stall blocking interrupt retirement) is
    plausible, not demonstrated; GPS TX wire physical location (45 vs 35) unknown —
    needed for the fix, not the discriminator"
  candidate_causes:
    - "config/code: GPS UART RX pad on reserved OPI-PSRAM line GPIO35 (02142e1) —
      leading, directly testable flash-only"
    - "environment/hardware: board/harness physical damage from session-6 re-wire —
      fallback, previously confounded with the pin change, testable by the queued
      multimeter round if (b)"
    - "code/architecture: Wire/SCCB dual-driver hazard — retained second-order
      interaction (post-camera wedge 25/25), hardening target after root fix"
  and_gate: "no — a single config change plausibly explains the entire failing/passing
    split without AND; if the pin-move build boots clean but a LATER PSRAM-exercising
    workload still misbehaves, revisit for a contributing second condition"
tdd_checkpoint: (n/a — tdd_mode not set; no test suite exists for this firmware)

## Resolution

root_cause: config — commit 02142e1 set GPS_TX_PIN (the UART1 RX pad; GPS TX wire)
  = GPIO 35, a pin that is a RESERVED OCTAL-PSRAM DATA LINE on this board's module
  (platformio.ini: qio_opi / 16MB flash / psram_type = opi, N16R8-class — GPIO 35-37
  are reserved for OPI PSRAM per the ESP32-S3 module datasheet). Claiming pad 35 for
  U1RXD through the GPIO matrix broke PSRAM accesses for the product app → silent
  interrupt-watchdog starvation → TG1WDT_SYS_RST (rst:0x8) bootloop at bench, hang
  site modulated by wherever the next PSRAM-touching window sat (39/40 in
  Adafruit_SSD1306::begin(), 1/40 post-camera-init). Present in 4/4 failing builds
  (balloon6, d8ba14e rebuild, camskip-01, balloon10/stock HEAD), absent in 1/1 passing
  build (balloon5, compiled before 02142e1 existed). Flash-only discriminator
  (35→41, only change) ran clean for hours — user-confirmed. The earlier "hardware
  regression between sessions" verdict was an artifact of the confound (re-wire and
  pin change landed together between sessions 5 and 6). AND-gate: no — single config
  change explains the entire failing/passing split.
fix: (APPLIED, commit 3f2c2c6, 2026-08-27) permanent GPS_TX_PIN=41 in
  include/sensor_pins.h (41 = free on sensor+camera pin maps, not a strapping pin,
  outside the reserved 35-37 set) + comment corrections: GPS block documents WHY
  35-37 are forbidden on this OPI-PSRAM board and the 45→35→41 history; the
  "Sensor pins used:" validation list corrected (41 not 35, battery-4/camera-SIOD
  conflict acknowledged instead of "No conflicts detected"); comment-only recurrence
  guard at src/test_lora_balloon.cpp:42 (the test app whose config 02142e1 copied —
  it survives GPIO 35 only because it never exercises PSRAM). Build env
  esp32-s3-balloon SUCCESS post-change.
verification: (fix applied; guardrail recorded 2026-08-27 late #2)
  target_test: { result: skipped, reason_if_skipped: "no test suite exists for this
    PlatformIO firmware project (no unit tests, no CI test env)" }
  mutation_check: { result: skipped, reason_if_skipped: "no Stryker/mutation tooling
    configured for embedded C++/PlatformIO" }
  adjacent_tests: { result: pass — full firmware build of env esp32-s3-balloon
    SUCCESS (79 s): both macro consumers (main_balloon.cpp:615, sensor_manager.cpp:137)
    compile against the permanent header; binary produced }
  no_op_deletion: { result: pass — diff changes a macro value 35→41 and comments only;
    deletes no behavior/branches; comment rewrites correct stale documentation }
  revert_and_reconfirm: { result: pass (hardware-executed A/B) — "revert" leg =
    stock HEAD builds bootlooped at bench (balloon10: 6/6 TG1WDT_SYS_RST, Aug 27
    17:01 build; plus balloon6/d8ba14e-rebuild/camskip-01 = 4 independent failing
    builds carrying pin 35); "reapply" leg = HEAD + ONLY the pin change 35→41 ran
    clean for ≈1.7-3.3 h with zero resets (balloon11.log) + user report "stable
    again". Single variable flipped = the pad claim. Manual repro recorded in
    Symptoms.reproduction (power-on at bench). Caveat: actuator was the human at
    the bench; logs balloon10/balloon11 are the recorded artifacts. }
  bench_gates_pending_human: { CLOSED by human verification 2026-08-27 late #3:
    GPS path CONFIRMED — user: "yes the GPS is receiving data now" (bytes flowing on
    GPIO 41; wire-on-41 confirmed earlier in the checkpoint round). Stability gate
    closed by balloon11.log (multi-hour, zero resets) on the functionally identical
    scratch build + user stability report. NOT closed from logs: permanent-build
    (3f2c2c6) banner build-time provenance and an explicit ≥5-min post-reflash
    cold-cycle rst count — balloon12.log absent (no Tee-Object capture
    materialized; repo-root logs end at balloon11.log). Recorded honestly as
    user-confirmed functional verification with best-effort canonical-binary
    provenance; dispositive either way per the same-day hardware A/B
    (balloon10 stock 6/6 TG1WDT vs balloon11 pin-41 hours-stable). Outdoor GPS fix
    test remains an open FIELD item (indoor "No valid data" = expected healthy
    signature, identical to session 5) — tracked outside this session. }
guardrail_verdict: accepted (degradation row "no test suite at all" → signals 3+5
  both pass; signals 1/2 skipped with logged reasons; build-compile adjacent check
  passed; bench gates CLOSED per human verification above)
human_verification: { result: confirmed, date: 2026-08-27, reporter: user at bench,
  evidence: "yes the GPS is receiving data now" + earlier wire-on-41 confirmation +
  balloon11.log (~2-3 h, zero resets) on the functionally identical build; provenance
  caveat: permanent-build (3f2c2c6) boot banner never captured — balloon12.log absent }
files_changed: [include/sensor_pins.h, src/test_lora_balloon.cpp]

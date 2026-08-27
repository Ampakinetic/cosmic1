# Evidence: balloon TG1WDT bootloop — bench session #6 (2026-08-27)

Status: UNRESOLVED — blocks plan 01-20 (bench session #6). Series A never ran.

## Confirmed facts

- `balloon6.log` (2460 lines, 2026-08-27 15:49): 40+ consecutive `rst:0x8 (TG1WDT_SYS_RST)` resets.
- Build on balloon: `Aug 25 2026 23:33:02` — the round-#8 pre-flight firmware (elf at `.pio/build/esp32-s3-balloon/firmware.elf`, same timestamp; addr2line valid against it).
- Hang point VARIES:
  - 39/40 boots: between `[OLED] I2C scan: 0x3C 0x76` (status_display.cpp:43-50 prints) and `[OLED] panel at 0x3C` (:65) — i.e., inside `Adafruit_SSD1306::begin()` (`oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)`, src/status_display.cpp:55, `periphBegin=false`).
  - 1/40 boots (log line ~790-800): got PAST OLED + camera (`Camera manager initialized`, initializeSubsystems:443) then reset with a DIFFERENT Saved PC.
- Saved PCs decode into the panic handler itself (`panic_enable_cache`, panic_handler.c:285/287 — WDT panic path), so the PC is NOT the hang site; the int-WDT (TG1) starved ~300 ms somewhere.
- Base station (base6.log, COM10) is HEALTHY on the same bench: AP up, GET_STATUS queued/transmitted, E32 TX ok. Balloon bootloops before LoRa loop starts, so commands go unanswered.
- Session 5 (`balloon5.log`, pre-round-#8 firmware, same rig): `[OLED] panel at 0x3C` → full boot → CommandHandler/AutoCapture init → commands executed. No resets.
- Per-boot `Wire.begin` warning count identical to session 5 (2/boot; 80 total = 2 × 40 boots) — no I2C call-order change.

## Not the cause (ruled out so far)

- Round #8 source diff (`git diff 4ba714a..HEAD -- src/ include/`, 407 insertions): image_tx_manager (.h/.cpp, 216), command_sender (.h/.cpp, 104), command_handler (49), auto_capture (9), image_protocol.h (30). NOTHING touches Wire/OLED/setup/I2C. Binary layout did change (image_protocol.h included broadly).
- Prior boot-abort fix 1223f46 (2026-08-23) was a different mechanism (BMP280 address macro → clean abort, fixed + hardware-verified).
- OLED stack itself worked through sessions 5 and the 01-16 bench (9916e7c, 2026-08-22).

## Open hypotheses

1. **Bench hardware marginal** (leading): boards were re-flashed/re-wired for session 6. A loose SDA/SCL/pull-up on the shared OLED+BMP280 bus explains scan-sees-device → multi-byte init stalls → int-WDT; varying hang point fits. Watch-item: `Battery monitoring active (raw reading: 4095)` — ADC pegged at rail max (verify against session 5; floating/shorted sense pin?).
2. **Round-#8 regression via layout/init-order** (not eliminated): the one boot that died right AFTER camera-manager init is adjacent to post-camera subsystem init (E32/ImageTx begin — 01-17 added image_tx_manager.h statics). Mechanism unclear; nothing in the diff blocks interrupts.

## Decisive discriminator (next step)

Re-flash the balloon with the session-5 firmware (`git stash`-clean tree not required — use a worktree/detached checkout of the pre-round commit, `pio run -e esp32-s3-balloon -t upload`):
- STILL bootloops → hardware: reseat OLED/bus cabling, check pull-ups, check bench power.
- Boots clean → round-#8 regression: `git diff 4ba714a..HEAD -- src/ include/` is the suspect window; bisect 01-17 vs 01-19 balloons-side commits (01-18 is base-only).

## Checkpoint bookkeeping

- Plan 01-20 Task 1 blocked — bench session #6 could not run. NO ledger flips; WINDOWS 3/5/8/9 and G-01-7/G-01-9 stay OPEN. Session-6 logs retained as balloon6.log / base6.log.
- Pre-flight for the flashed firmware was green (builds 2/2, wire harness 52 PASS) — this is a runtime/hardware failure the static gates could not see.

## Discriminator results (2026-08-27, RESOLVED to hardware)

1. **balloon9.log — OLED unplugged:** still bootloops, same window/PC. OLED panel exonerated; hang follows the bus/controller.
2. **Old-firmware reflash (d8ba14e = session-5-equivalent balloon source, fresh build Aug 27 16:07):** STILL bootloops — 23 TG1WDT resets in 75 s (`/tmp/bootloop-discriminator/oldfw-boot.log`). BUT different signature: 24/24 boots PASS `oled.begin` (`[OLED] panel at 0x3C` ×24), pass camera init, then the Wire driver reports `i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE` (esp32-hal-i2c-ng.c:275, ~890 occurrences / 24 boots) until the WDT resets.
3. **Session 5 (Aug 25, same balloon source) ran a full clean session** on this rig.

**Verdict: bench hardware/environment regression, NOT round-#8 code.** Two different builds of unchanged-source firmware fail at two different I2C call sites on today's bench; the same source worked Aug 25. Mechanism: the shared Wire/I2C-0 driver enters ESP_ERR_INVALID_STATE right after camera manager init — on the boundary where esp32-camera's SCCB interacts with the shared I2C port. Prime physical suspect: **camera module wiring/ribbon seating** (user reseated OLED — no effect; camera NOT yet reseated). Camera still enumerates (Sensor PID 0x26, same as session 5); battery ADC pegged 4095 in BOTH sessions (not a change).

Tooling notes: uploads from this harness need `PYTHONIOENCODING=utf-8` (cp1252 crashes on esptool progress bars, wedge/hang); killed uploads orphan esptool children that hold COM11 — kill by PID before retry (user's COM10 monitor is separate).

**Next steps:** reseat camera ribbon + check bench power/grounds → confirm clean boot on old firmware (it prints the most diagnostics) → re-flash round-#8 firmware (build at HEAD) → verify boot → run bench session #6 → 01-20 continuation.

## Follow-up captures (2026-08-27 evening) — three-way contradiction, STILL hardware

All on old firmware (d8ba14e build, "Build: Aug 27 2026 16:07:35"), balloon on COM11:

1. **Camera reseated, OLED in** (`/tmp/bootloop-discriminator/reseated-boot.log`): still loops (25 resets in 75 s). Signature CHANGED: **zero** INVALID_STATE errors; `[OLED] panel at 0x3C` on all 24 boots (full SSD1306 init burst SUCCEEDS); silent wedge in the two-statement window `StatusOLED().showBootStage("CAMERA")` → `LoRaComm().begin()` (main_balloon.cpp ~:445-450; session-5 next line was ":453 LoRa communication initialized" — never reached). Saved PC now 0x4037627a.
2. **OLED unplugged, camera reseated** (`noled-boot2.log`): still loops (24 resets). `ESP_ERR_INVALID_STATE` spam is BACK (889 ×, ~9 ms cadence — one per SSD1306 init command) AND the expected `[OLED] no panel at 0x3C/0x3D - screen disabled` line NEVER prints — meaning `oled.begin()` does not return false despite every transaction failing (Adafruit_SSD1306 ignores per-command Wire results). So with the panel out, the Wire driver reports not-initialized on a bus that init'd the BMP280 and completed a 127-address scan milliseconds earlier.

**Contradiction that defeats remote diagnosis:** I²C succeeds fully with panel in (reseated run), errors INVALID_STATE with panel out, and the round-#8 build wedges inside `oled.begin` either way. The Wire/driver state flips with physical bus topology — consistent with **bus pull-ups living on the OLED breakout** (unplug = floating/terminated bus, controller-side driver instability) but not proven. Camera reseat and OLED swap-in both changed the signature without fixing the boot. E32/LoRaComm().begin() is NOT yet exonerated (the wedge window still contains it; its AUX-timeout prints use `Serial` and never appear in the UART0 capture).

**Where this stands:** firmware exonerated (old+new builds both fail; session 5 same source clean). Hardware fault on the balloon board/bus not yet localized — candidates: bus pull-up topology, camera-module load on the shared rail, E32 module wiring, board power. Needs `/gsd-debug` with hands-on iteration (scope/multimeter if available). Pragmatic unblock option for the bench while root-causing separately: a temporary panel-disabled firmware build (skip StatusOLED entirely) to see if the rig completes session 6 — that experiment also cleanly implicates/exonerates the whole I²C/OLED path.

**Bench state:** balloon currently runs OLD firmware (d8ba14e) — MUST be re-flashed to round-#8 (HEAD) before any session-6 evidence counts. 01-20 checkpoint still open; no ledger flips; WINDOWS 3/5/8/9 + G-01-7/G-01-9 remain OPEN.

# D1 crash regression — session 7 debug round (push-start phase)

Session: 7 (bench, 2026-08-27). Firmware: round #10 (HEAD `866087b` balloon image_tx +
`3d0aaea` command_handler/main_balloon + `84f8aea` base-side). Evidence files:
`balloon.log` (operator capture, untracked), `base.log` (operator capture, untracked).
Crashes under debug: WINDOWS.md items 15 (D1) and 16 (D2 spurious busy-hold — the
companion behavioral defect fixed unconditionally by 01-25 Task 3; decoded here only
insofar as it bears on the crash windows).

Both crashes occurred during the **first post-boot capture push** (image 35 → crash 1;
image 36 → crash 2 after crash 1's reboot), with **zero project frames** in any dump.

---

## 1. DECODE

### 1.1 Deployed-ELF provenance cross-check (performed BEFORE any frame was trusted)

The panic print itself carries the build identity:

> balloon.log:375 `ELF file SHA256: 3d2b351b4`

The retained build artifact:

> `.pio/build/esp32-s3-balloon/firmware.elf` — mtime Aug 28 04:02, 11.8 MB
> `sha256sum` → `3d2b351b4…` (**MATCH**)

Additional positive cross-checks (symbols unique to this build resolve to the exact
panic-printed names):

- `0x40379576` → `esp_cpu_wait_for_intr` (matches the panic's own inline decode, balloon.log:306)
- `0x4037d7eb` → `spinlock_acquire` inlined into `xPortEnterCriticalTimeout` (matches balloon.log:358)

Round-#10 balloon hunks confirmed present in the deployed ELF by raw byte search
(`strings(1)` gives false negatives on this ELF; `grep -a -F` against the binary was
used instead — each string exactly 1 hit):

- `"second command frame dropped"` (3d0aaea hasCommand guard)
- `"re-announce held"` (866087b busy-hold gate, src/image_tx_manager.cpp:536)
- `"re-announce budget re-armed"` (866087b budget re-arm)
- `"Camera disabled due to critical power"` (3d0aaea main_balloon WR-05)

Toolchain: `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line.exe`,
flags `-pfiaC -e .pio/build/esp32-s3-balloon/firmware.elf`.

### 1.2 Crash 1 — Core 1, first panic: INT_WDT on CPU1

> balloon.log:302 `Guru Meditation Error: Core  1 panic'ed (Interrupt wdt timeout on CPU1).`

Interrupted context (the state CPU1 was in when the INT_WDT fired):

| Addr | Symbol | Role |
| --- | --- | --- |
| 0x40379576 | `esp_cpu_wait_for_intr` (cpu.c:64) | IDLE1 waiting for interrupt |
| 0x42055ea5 | `esp_vApplicationIdleHook` (freertos_hooks.c:58) | IDLE1 hook |
| 0x4037e7ef | `prvIdleTask` (tasks.c:4350) | IDLE1 task |

**Interrupted context = IDLE1.** CPU1 was doing nothing. The INT_WDT on CPU1 means
CPU1 stopped feeding its watchdog — an IDLE task in `wait_for_intr` normally feeds it
implicitly (the wdt is refreshed on interrupt entry); the watchdog firing from inside
the idle loop means **interrupts were not being taken on CPU1 anymore** — consistent
with a permanently-held kernel critical section / spinlock owner field never releasing.

Full backtrace, Core 1 (balloon.log:327 chain) — **every frame is panic machinery**
(addr2line output verbatim):

| Addr | Symbol | Role |
| --- | --- | --- |
| 0x42055fb0 | `panic_print_char_uart` (panic.c:96, inl. `panic_print_char` :132) | UART printer |
| 0x42056028 | `panic_print_str` (panic.c:145) | printer |
| 0x403760a2 | `print_str` (debug_helpers.c:54) | printer |
| 0x4037613e | `esp_backtrace_print_from_frame` (debug_helpers.c:71) | backtrace walker |
| 0x4201952f | `panic_print_backtrace` (panic_arch.c:338) | backtrace print |
| 0x420191ec | `print_state_for_core` (panic_handler.c:81) | dumper |
| 0x42019209 | `print_state` (panic_handler.c:92) | dumper |
| 0x42056303 | `esp_panic_handler` (panic.c:388) | panic entry |
| 0x420191a1 | `panic_handler` (panic_handler.c:266) | panic entry |
| 0x40375eb1 | `panicHandler` (panic_handler.c:300) | panic entry |
| 0x403762d8 | `xt_highint4` (highint_hdl.S:108) | INT_WDT vector |
| 0x00040022 | **unresolvable** (`?? ??:0`) — below any mapped region; monitor flagged `\|<-CORRUPTED` | garbage terminal frame, expected for a corrupted-context dump |

Core 0's backtrace (balloon.log:355-369) is the wedge signature tabled in §1.4 below;
its frames (`0x4037948d`→`xt_utils_compare_and_set` etc.) are all kernel/ISR code.

**Zero project frames in either backtrace.**

### 1.3 Crash 1 — Core 1, second panic: IDLE1 stack canary

> balloon.log (second panic block): `Stack canary watchpoint triggered (IDLE1)` with
> PC in `panic_print_char_uart` (panic_handler.c:95)

The canary trip is **inside the panic printer itself** — the panic handler runs on the
broken IDLE1 stack. Consequence, not cause. Not independently decoded further.

### 1.4 Crash 1 — Core 0 register dump: the actual wedge signature

| Addr | Symbol | Role |
| --- | --- | --- |
| PC 0x4037d699 region | `SysTickIsrHandler` (port_systick.c:148) → `xPortSysTickHandler` (:224) → `xTaskIncrementTick` (tasks.c:3225) → `prvTaskEnterCriticalSafeSMPOnly` → `vPortEnterCritical` → `spinlock_acquire` (spinlock.h:123) → `xt_utils_compare_and_set` (xt_utils.h:235) | **CPU0's tick ISR spinning on the FreeRTOS SMP kernel portMUX spinlock** |
| — | interrupted context: IDLE0 (`esp_cpu_wait_for_intr`) | CPU0 also idle at wdt fire |
| Saved PC 0x420190e3 | `panic_handler.c:174` | reboot moment, uninformative |

**Decisive frame:** `spinlock_acquire`'s compare-and-set loop running from the SysTick
ISR means the kernel spinlock's owner field never matched and never became free —
the classic **freed/corrupted kernel lock state** signature. A task that died holding
the portMUX, or a corrupted owner field, wedges every subsequent kernel operation on
both cores; the tick ISR then spins in the compare-and-set loop, both cores stop
feeding watchdogs, INT_WDT fires.

### 1.5 Crash 2: TG0WDT_SYS_RST, silent

> balloon.log:526-529 `rst:0x7 (TG0WDT_SYS_RST)` … `Saved PC:0x400559e3`

- `0x400559e3`: **unresolvable in the application ELF — expected.** The ESP32-S3
  `0x40055xxx` range is masked ROM (flash/cache service routines). A TG0 system
  reset with saved PC in ROM flash/cache code is the signature of a core stalled
  with the flash cache suspended (normal during NVS/spi-flash ops — fatal only if
  the stall never ends) or of a wedge severe enough that the ROM-level watchdog,
  not the INT_WDT, caught it first.
- No register dump, no backtrace — the panic path itself never ran (or never got
  far enough to print). EXCCAUSE 0x6 on both cores in crash 1 was checked against
  this class: residual register noise for INT_WDT-class panics, **not** a real
  divide-by-zero (no div/remainder instruction sits in any decoded frame).

### 1.6 Pre-crash data-plane corruption (the independent smoking gun)

The balloon's own TX log bytes were already garbage **before** either wedge — this
is corruption of live system state, not a print-time artifact:

> balloon.log:299 `E32: Transmitted 38 byte∩┐╜`  (source prints `bytes` — src/e32_lora.cpp transmit path)
> balloon.log:513 `Camera: drain∩┐╜d stale fra∩┐╜e after QQVGA downshift (160x120, 1601 B)`

U+FFFD mojibake = the terminal could not decode bytes that left the chip wrong. The
UART0 TX ring buffer (heap-allocated driver state) held corrupted bytes; crash 1's
corrupt line (:299) precedes the I2C driver failure (:301) and the panic (:302);
crash 2's (:513) precedes its silent reset (:526) by several seconds.

And the third subsystem:

> balloon.log:301 `[607000][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_STATE`

The Wire bus-0 driver (BMP280 @0x76 + OLED @0x3C, sda=1/scl=2) rejected a
transaction because its FSM was not in a state accepting a new synchronous
transaction — driver-state corruption, 5 log lines before the panic.

**Three independent subsystems (UART TX ring, I2C driver FSM, FreeRTOS kernel
spinlock) corrupted in the same phase.** No single application buffer overrun at any
single site plausibly reaches all three simultaneously; a memory-subsystem-level
fault does.

---

## 2. SUSPECT DISPOSITION

### (a) Round-#10 balloon hunks (`866087b` image_tx +106/−5, `3d0aaea` guards) — ELIMINATED as direct cause

Code-level: every touched path is check-and-return bookkeeping — no loops, no
critical sections, no writes outside member state, no OOB (framer write bounded by
`expectedTotal <= sizeof` at src/packet_framer.cpp:842 region; queue bounded 16 slots
drop-oldest-low, include/image_tx_manager.h:185,208; telemetry packing verified to
fit `sizeof(TelemetryData)`; camera buffer ownership single-owner — `currentImage`
PSRAM freed on next capture, thumbnail internal-heap freed on next
`captureThumbnail`).

Execution-level (decisive): **the logs prove none of the risk-bearing round-#10
paths ran in either crash window.** No `"second command frame dropped"`, no
`"re-announce budget re-armed"`, no eviction-reclass line, no `"re-announce held"`
in crash 1's window, and in crash 2's window the crash phase ran
`pushThumbManifest`/`pushThumbChunk` only. The single round-#10 path that did
execute pre-crash is crash 2's boot-window hold check:

> balloon.log:504 `ImageTx: re-announce held - inbound window traffic active`

— the D2 spurious fire itself: gate `millis() - lastInboundWindowRequestMs(=0) <
IMG_FULL_REANNOUNCE_BUSY_MS` true at boot (src/image_tx_manager.cpp:533), one bool
write + one `println`, ~20-30 s before the reset. A read-compare-log return cannot
corrupt memory. Round #10's balloon logic did not execute near either crash.

### (b) 01-21/01-23 framer + hasCommand guard — ELIMINATED as direct cause

Same evidence as (a): the guard/resync paths provably did not fire (no matching
lines anywhere in balloon.log), and the framer's only write is bounds-checked
member state. Crash 1's last inbound drain completed cleanly (command executed,
balloon.log:289-295); crash 2 had no inbound command between beacon start
(:525) and reset (:526).

### (c) Base-side cadence change (round #10 `84f8aea`, 01-22/01-23 base hunks) — ELIMINATED as trigger

base.log at both crash moments shows only the standing 30 s `GET_STATUS` poll — the
same cadence that ran clean through session 6. No window request, no held-command
burst, no cadence change appears before either reset. The base-side code acts on
window arms; no window was armed in either crash window.

### (d) Latent hardware/memory instability under first-post-boot capture-push load — CONFIRMED as the class (primary suspect)

See §3. What is established here: both crashes are the session's **first post-boot**
capture push — the unique first-time-of-boot concentration (first run-time
`ps_malloc` + first large PSRAM memcpy/CRC of the session, NVS flash write at ID
allocation with cache suspension, first E32 push bursts, camera reconfigure cycle) —
and the observable damage spans three independent driver/kernel structures, which
application logic at the audited sites cannot simultaneously reach. This board's
OPI-PSRAM interface is proven marginal on this exact hardware (session-6 pin-35
bootloop: GPS UART1 RX on reserved OPI-PSRAM pin 35 destabilized the whole system
until moved, fixed in `3f2c2c6`).

### Stack/heap arithmetic (eliminates plain resource exhaustion)

- loopTask stack = Arduino default 8192 B (no `ARDUINO_LOOP_STACK_SIZE` override in
  platformio.ini balloon env); deepest crash-phase chain (loop → processPacketHandling
  → ImageTx::process → pushThumbManifest/pushThumbChunk + printf) ≈ 1-1.5 KB. Plain
  overflow not supported by arithmetic. The IDLE1 canary trip is a panic-printer
  artifact (§1.3), not evidence of task-stack exhaustion.
- Internal heap at boot ≈ 206 KB free; the crash phase allocates ~8.1 KB PSRAM full
  buffer + ~1.2-1.6 KB internal thumbnail. No pressure.
- E32 transmit path (`waitForAuxHigh(1000)` → write → flush → miss-low benign →
  `waitForAuxHigh(5000)`) is delay(10)-polled — yields, does not mask interrupts;
  cannot starve the INT_WDT by itself.

---

## 3. ROOT CAUSE (single ranked cause)

**Root cause (one, ranked primary): internal-RAM corruption of RTOS/driver state during the
first-post-boot capture-push phase — memory-subsystem-level (OPI-PSRAM/cache
class on proven-marginal hardware), not an application-code regression.** One
corruption event lands during the push phase and its blast simultaneously hits the
UART TX ring (mojibake, §1.6), the I2C driver FSM (INVALID_STATE, §1.6), and the
FreeRTOS SMP kernel spinlock owner field (tick-ISR spinlock spin → INT_WDT panic,
crash 1; TG0 ROM-stall silent reset when the wedge catches the panic path itself,
crash 2 — §1.4/§1.5).

Evidence FOR:
- Zero project frames in any dump — no application code was executing at any
  sampled moment; both cores were IDLE or in kernel/ISR code.
- Three independent subsystems corrupted in the same phase (§1.6) — blast radius
  only a memory/cache-level fault explains; no audited application write reaches
  all three (§2a/§2b audits).
- Output-byte corruption precedes the driver failures and the panics by seconds
  (§1.6 timing) — corruption first, wedge second.
- Crash 2's saved PC is in ROM flash/cache space (§1.5) — consistent with the
  cache/flash-stall variant of the same wedge class.
- Proven-marginal OPI-PSRAM interface on this exact board (§2d).
- Both crashes at the identical first-post-boot concentration point.

Evidence AGAINST (recorded honestly):
- Corrupted-context crashes destroy direct proof — the corrupting store itself
  cannot be recovered from these dumps. **The mechanism cannot be uniquely
  confirmed pre-bench; this ranking is by elimination of every auditable
  application path plus blast-radius reasoning, not by direct observation.**
- Session 6 ran the same first-post-boot push path clean 12× (images 23-34,
  including 3 unspaced series-A pushes) — first-post-boot alone is not sufficient;
  a per-session/environment component exists that code cannot see.

Round-#10 regression reconciliation (why round #10 crashed where session 6 did
not): the balloon-side round-#10 code provably did not execute near either crash
(§2a), so the correlation is NOT causal at the logic level. Most parsimonious
explanations, which this debug round **cannot distinguish pre-bench**: (i) the
+106 B code/data layout shift moved a latent corruption source's blast radius onto
fatal structures, and/or (ii) bench/environment variance between sessions. Both are
stated, neither assumed.

**Named fix lever (what 01-25 Task 3 implements):** since the corrupting store is
not recoverable, the lever is the defensive + discriminating bundle —
1. **Boot-time first-use PSRAM heap warm-up** in `ImageTxManager::begin()`
   (alloc + memset-touch + free of an `IMG_MAX_IMAGE_SIZE`-class block before the
   loop starts): moves the first run-time PSRAM arena/cache touch out of the live
   capture-push phase to boot, where a fault is visible and harmless; inert if
   first-use is not the trigger.
2. **Bounded `[MEM]` discriminator instrumentation**: one diagnostic line
   (`heap / min-ever heap / PSRAM free / loopTask stack high-water`) at every
   `enqueueCapture` (the exact crash phase, bounded by capture count) and on each
   new monotone low in the 1 Hz block (naturally bounded). **Removal condition
   named: strip after the 01-27 bench closes G-01-10.**
3. The existing watchdog reset behavior stays — it is the system's recovery, not a
   defect.

NOT evidence-supported and explicitly rejected: raising `ARDUINO_LOOP_STACK_SIZE`
(stack arithmetic eliminates plain overflow, §2), any pacing/quiet-gate change
(protected surfaces), any wire-format change.

---

## 4. DETERMINISM + DISCRIMINATOR

**First-post-boot question:** yes — both session-7 crashes are the first post-boot
capture push of their respective boots (image 35 = first push after boot; image 36 =
first push after crash 1's reboot; balloon.log:289-302 and :506-526). But
first-post-boot is NOT sufficient as a cause: session 6's first-post-boot pushes
ran clean (§3 against-evidence). **The defect is not deterministic at the code
level — no code-level predicate reproduces "session 7 crashes, session 6 does
not"**, because the discriminating variable (memory-subsystem stability) is not
visible to code. Hence instrumentation, not code-fix-alone.

**01-27 bench discriminators (close/open G-01-10):**
- **D1**: run the exact crashing pattern — fresh boot, then the series-A 3×
  unspaced `CAPTURE_NOW` push-start — plus spaced captures and a repeat
  first-post-boot push (reboot between series). Exoneration = zero crash
  signatures (INT_WDT / TG0WDT_SYS_RST / canary) AND zero `[MEM]` new-low
  anomalies beyond the boot baseline across all series; each `enqueueCapture`
  `[MEM]` line present and monotone-sane. Any recurrence with the `[MEM]` line
  present localizes the corruption to before/after enqueue; recurrence with the
  line absent localizes it inside the push phase.
- **D2**: zero `re-announce held` lines in any boot window before the first
  genuine inbound window request (the receipt-ever flag makes the boot-window
  stamp inert — the :504-style spurious fire becomes impossible).

---

## 5. FIX SHAPE SELECTED (Task 2 checkpoint record)

Checkpoint `02` (fix-shape selection, gate `blocking`) auto-resolved under
`auto_advance: true` (config.json) per the 01-23 precedent. Selected: **option (a)
frame — implement the evidence-named lever — with option (b)'s bounded
instrumentation ride-along** (option (a)'s own cons name this combination when
Task 1's evidence cannot uniquely confirm the mechanism, which §3 records
explicitly). Concretely: lever items 1 + 2 of §3 (boot-time PSRAM warm-up +
bounded `[MEM]` diagnostics with named removal condition) plus the unconditional
D2 receipt-ever flag. No pacing, quiet-gate, or wire-format changes.

---

## 6. SESSION 8 RECURRENCE (01-27 bench, 2026-08-28) — the 01-25 lever DISCONFIRMED as sufficient

Session 8 crashed again. The spaced smoke SURVIVED (the session-7 crash-1 pattern
is dead at this boot); the fatal event moved deeper — mid-service of the second
image's FULL window transfer — with healthy `[MEM]` values throughout and NONE of
session 7's corruption corroboration. Evidence below; every line re-verified
against the retained consoles by the 01-27 continuation executor.

### 6.1 Provenance (cross-checked BEFORE any decoded frame was trusted)

- Firmware: round #11 (01-25 fix + 01-26 companions), balloon build banner
  `Build: Aug 28 2026 09:48:55` (balloon2.log:120, again post-reboot :1242),
  repo HEAD `56f3db6`; pre-flight on record: `pio run` 2/2 SUCCESS, harness
  exit 0.
- Deployed-ELF provenance: `.pio/build/esp32-s3-balloon/firmware.elf`
  SHA256 `c53635e1819a6ca5107b1437450dbc324e5848e685b50de73ea6a4387c8e0553`
  — **MATCHES** the round's built artifact; the decode in §6.2 is against the
  exact crashed build.
- The 01-25 fix is provably IN the deployed image: the warm-up line ran at BOTH
  boots — balloon2.log:102 and :1321 `ImageTx: PSRAM first-use warm-up done
  (G-01-10)` — and the `[MEM]` instrumentation lines are present throughout.
- Consoles: `balloon2.log` + `base2.log` (operator-retained names; the plan
  requested balloon13.log/base8.log — provenance deviation recorded in
  01-UAT.md, files never renamed, the 01-24 convention).

### 6.2 The fatal event — first chunk of a re-armed FULL window, mid-service

Context (verbatim, both consoles):

> base2.log:524-525 `CommandSender: Queued command IMAGE_WINDOW_REQUEST (seq=21)` /
> `ImageRx: window request queued for image 38 kind 1 (chunks 96..111, seq 21, pass 0)`
> base2.log:526 `command seq=21 transmit held - inbound chunk stream active` (quiet gate, then sent/ACKED :530-531)
> balloon2.log:1220 `ImageTx: FULL window armed for image 38 (chunks 96..109)`
> balloon2.log:1221 `CommandHandler: IMAGE_WINDOW_REQUEST armed` → :1224 `- SUCCESS`
> balloon2.log:1226-1227 `E32: Transmitted 217 bytes` / `ImageTx: window chunk(image 38 kind 1, 1/14, 200 B) sent`
> balloon2.log:1228-1231 `ESP-ROM:esp32s3-20210327` / `rst:0x7 (TG0WDT_SYS_RST),boot:0x2b (SPI_FAST_FLASH_BOOT)` / `Saved PC:0x40376430`

Decode (xtensa-esp32s3-elf-addr2line `-pfiaC`, ELF SHA `c53635e181…` verified
first — see §6.1):

| Addr | Symbol | Role |
| --- | --- | --- |
| 0x40376430 | `tick_hook` (esp-idf `components/esp_system/int_wdt.c:111`) | the interrupt/task-watchdog tick hook in the tick-ISR chain — where the WDT machinery lives |
| 0x403c88b8 / 0x403c8700 / 0x403cb700 | unresolvable (`?? ??:0`) | second-stage-bootloader load regions — expected, not evidence |

**Zero project frames** — again. The Saved PC sits in the same tick-ISR
neighborhood as session-7 crash 1's Core-0 wedge signature (§1.4:
`SysTickIsrHandler` → `xPortIncrementTick` → `spinlock_acquire`).

What SURVIVED before the crash (the session-7 crash-1 pattern is dead this boot):

- First post-boot CAPTURE_NOW (image 37, balloon2.log:207-213) ran the full
  push phase — enqueue, thumb manifest :219, chunks :222-253, FULL manifest :258,
  window service :261+ — with zero crash signatures; both kinds finalized
  COMPLETE at the base (thumb 8/8 base2.log:74-75, full 36/36 :238-239).
- The crash is NOT first-post-boot and NOT first-anything: it is the SECOND
  image, roughly 3.5-4 minutes in (first i2c noise at t=178075 ms :885; [BCN]
  seq=46 at :1213), during the SEVENTH FULL window of image 38 (windows 0..15
  through 80..95 served clean, :660-:1216, before the fatal 96..109 arm).

Downstream consequences (the honest record): image 38's buffers died in the
reset (post-reboot NVS restores next ID 39, balloon2.log:1318); every post-reboot
window request rejected (`window request for unknown/evicted image 38 rejected`
balloon2.log:1385/:1398 et al.); the base exhausted its passes against a dead
servicer — base2.log:651-652 `image 38 kind 1 finalized INCOMPLETE (retransmit
passes exhausted): 109/137 chunks after 3 passes` / persisted complete=false.
Zero command timeouts anywhere in base2.log (no forbidden CAPTURE_NOW
terminals this session — unlike session 7).

### 6.3 The [MEM] instrumentation verdict (01-25 discriminator, answered)

- Enqueue line PRESENT and healthy: balloon2.log:611 `[MEM] enqueue
  heap=8354328 minHeap=8352520 psram=8149436 stackHW=5744`.
- Last `[MEM]` before the reset: :1175 `heap=8332460 minHeap=8323392
  psram=8129116 stackHW=5552` — monotone-sane, no anomaly; the loopTask stack
  high-water never dropped below 5552 B of its 8192 B.
- Post-reboot baseline :1354 `heap=8553128 …` equally healthy.

Per §4's own discriminator: recurrence WITH the `[MEM]` line present and NO
anomaly means the fault is neither heap/PSRAM exhaustion nor a first-use PSRAM
touch. **The 01-25 hypothesis — first-post-boot PSRAM arena/cache touch during
the capture-push phase — is DISCONFIRMED as SUFFICIENT: the warm-up ran at both
boots (:102/:1321) and the crash recurred mid-service on healthy memory.**

### 6.4 Session-7 corruption corroboration: ABSENT

- U+FFFD mojibake: **ZERO** occurrences in balloon2.log (session 7 had
  `Transmitted 38 byte∩┐╜` :299 and `drain∩┐╜d` :513 preceding its crashes).
  The `┬░` sequence on every BMP280 line is the constant console rendering of
  the UTF-8 degree sign — present on healthy lines at both boots, not corruption.
- `i2cWrite … ESP_ERR_INVALID_STATE`: 2 occurrences (:885 at t=178075 ms,
  :1577 at t=88276 ms post-reboot) — in BOTH cases a successful `BMP280: P=…`
  read follows immediately (:886, :1578); NEITHER is adjacent to the reset (the
  fatal window is ~340 lines after :885). Recurring BMP280-path noise, recorded
  as such, distinct from the fatal TG0WDT.
- Guru Meditation / stack canary / watchpoint: ZERO. rst:0xc: ZERO. The only
  reset signatures in the whole log are the fatal rst:0x7 (:1230) and the benign
  initial POWERON rst:0x1 (:12).

Reading: §3's ranked root cause (an internal-RAM corruption blast hitting the
UART ring + I2C FSM + kernel spinlock simultaneously) has LOST its corroboration
pattern — session 8 shows the same TG0WDT reset class with NONE of the three
corrupted-subsystem signatures. The corruption seen at session 7 is now better
read as a possible co-effect of a deeper common cause (a stall/wedge), not
necessarily the mechanism itself.

### 6.5 New root-cause question (the debug round #2 brief)

The CONSTANT across session-7 crash 2 and session 8: **TG0WDT_SYS_RST during
SUSTAINED FULL-window chunk service** (the TX-heavy service loop), with the
sampled PC in interrupt/tick WDT machinery (session 8 decoded: `tick_hook`,
int_wdt.c:111 — the tick-ISR chain where the task-watchdog check lives;
session-7 crash 1's Core-0 dump was that same chain spinning on the kernel
portMUX spinlock).

Named axis for round #12: **task-watchdog starvation / loopTask (or kernel)
block during FULL window service** — what in the `serviceWindowChunk` → E32
transmit path (AUX polling, UART flush, a kernel lock held across a blocking
wait, a cache-suspended stretch) can stop feeding TG0 for the WDT period; and
whether session-7's spinlock wedge and this event are ONE mechanism (a
lock-holder stall that sometimes corrupts state and sometimes only stalls) or
two. NOT established: any memory-corruption mechanism (no visible signature
this session); any application frame (zero in the only resolvable dump address).

The `[MEM]` instrumentation STAYS IN — removal condition unchanged (strip only
after G-01-10 closes on bench evidence).

---

## 7 — SESSION-8 DEBUG ROUND #2 (round #12): WDT topology, reconstructed window, candidate arithmetic, one-or-two verdict, ranked cause

Scope: §6.5's brief, executed against the retained evidence BEFORE any code
change (the §6.1 convention — the audit's object of study is the build that
crashed). Evidence base: balloon2.log / base2.log (retained), the deployed ELF
`.pio/build/esp32-s3-balloon/firmware.elf` SHA256
`c53635e1819a6ca5107b1437450dbc324e5848e685b50de73ea6a4387c8e0553`
(re-confirmed this round; the `0x40376430 → tick_hook (int_wdt.c:111)` decode
reproduces), and the deployed framework artifacts pinned by platformio.ini:
platform espressif32 55.03.39 → arduino-esp32 3.3.9 → ESP-IDF 5.5.4 precompiled
libs. Framework-source claims were verified against the installed packages:
IDF 5.5.4 sources at `~/.platformio/packages/framework-espidf` (package version
3.50504 = v5.5.4), arduino core at `~/.platformio/packages/framework-arduinoespressif32`,
prebuilt esp32s3 config at `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig`
(hereafter "sdkconfig"). `firmware.map` links the deployed WDT objects
(`int_wdt.c.obj`, `task_wdt.c.obj`, `task_wdt_impl_timergroup.c.obj`) from
`framework-arduinoespressif32-libs/esp32s3/qio_opi/libesp_system.a`. No code
was modified before this section was written.

### 7.1 WDT topology of the deployed build

Two hardware watchdogs, two timer groups, two config axes:

| | TWDT (task watchdog) | IWDT (interrupt watchdog) |
| --- | --- | --- |
| Hardware | MWDT0 / TG0 | MWDT1 / TG1 |
| Source | task_wdt_impl_timergroup.c:28-33 (`TWDT_INSTANCE WDT_MWDT0`, `TWDT_TIMER_GROUP 0`, `SYS_TG0_WDT_INTR_SOURCE`) | int_wdt.c:32-41 (`IWDT_INSTANCE WDT_MWDT1`, `IWDT_TIMER_GROUP 1`) |
| Config | sdkconfig:2175-2179 (EN/INIT/PANIC=y, TIMEOUT_S=5, CHECK_IDLE_TASK_CPU0=y; :2180 CHECK_IDLE_TASK_CPU1 **not set**) | sdkconfig:2172-2174 (INT_WDT=y, TIMEOUT_MS=300, CHECK_CPU1=y) |
| Stage 0 | INT @5 s (impl :124) — ISR prints `Tasks currently running:` (task_wdt.c:491) then aborts (PANIC_PRINT_REBOOT sdkconfig:2139) | INT @300 ms (int_wdt.c:120) — panic print (`Interrupt wdt timeout on …`) |
| Stage 1 | RESET_SYSTEM @10 s (impl :126) — **pure hardware reset, no print path exists** | RESET_SYSTEM @600 ms (int_wdt.c:122) |

**rst:0x7 (TG0WDT_SYS_RST) maps to the TASK watchdog's stage-1 hardware
backstop.** Session 8 reset silently because stage-1 has no firmware print
path at all — the silent class and the panic class are distinguished by WHICH
watchdog trips first: session 7 crash 1's `Interrupt wdt timeout on CPU1`
panic is the IWDT class (a 300 ms tick/INT-delivery miss with the panic path
alive), while session 8's silence is the TWDT 10 s backstop.

Who can feed the TWDT in THIS build: subscribed tasks only. Subscribed:
IDLE0 (CHECK_IDLE_TASK_CPU0=y). NOT subscribed: IDLE1 (:2180 not set);
loopTask (created priority 1 pinned to ARDUINO_RUNNING_CORE=1,
cores/esp32/main.cpp:113; `loopTaskWDTEnabled = false`, main.cpp:111); and no
project code calls `esp_task_wdt_add`/`esp_task_wdt_reset` — the project
"Debug watchdog" is a disabled-by-default software latch (debug_utils.cpp:213-235,
triggerWatchdog :351 empty). **Therefore in this firmware the TWDT is fed by
exactly one thing: IDLE0 running. "TWDT unfed 10 s" ≡ "IDLE0 has not run on
CPU0 for 10 s".**

IWDT feeding (the interlock): the IWDT is fed ONLY from `tick_hook`
(int_wdt.c:104-127). CPU1's tick sets `int_wdt_cpu1_ticked` (:107-108); CPU0's
tick feeds ONLY if CPU1 ticked since the last feed (:111-125). The tick hooks
run FIRST inside each tick — `xPortSysTickHandler` calls
`esp_vApplicationTickHook()` (port_systick.c:199) BEFORE `xTaskIncrementTick()`
(:223-224, the non-SMP path this build takes, sdkconfig:2367) — on per-core
systimer interrupts (port_systick.c:72).

The tick_hook decode reconciled: `0x40376430` = int_wdt.c:111 = the CPU0-only
branch of tick_hook (`if (int_wdt_cpu1_ticked)`), reachable ONLY from CPU0's
tick ISR via the hook call above. Narrow, hard meaning: **at the reset instant
CPU0 was inside its tick-ISR hook phase.**

Hard constraints, each independently sourced — the round's core result:

| Fact | Source | Consequence |
| --- | --- | --- |
| TG0 stage-1 fired (10 s unfed) | rst:0x7, balloon2.log:1230 | IDLE0 starved ≥10 s on CPU0 |
| Stage-0 INT (5 s) never serviced | zero `Tasks currently running` prints in the whole log; :491 is the ISR's unconditional first print | the TG0_WDT interrupt's delivery (or its ISR's completion) on its CPU was dead for ≥5 s |
| IWDT never fired (300/600 ms) | zero INT_WDT prints anywhere | **both cores' tick chains executed within every 300 ms window until the reset** (a miss on either side trips the IWDT first — CPU0's feed is gated on CPU1's flag) |
| Saved PC = tick_hook:111 | addr2line (reproduced) | CPU0 was in the tick-ISR hook phase at the reset instant |
| CPU1 task level alive | log runs normally to :1227 (`E32: Transmitted 217 bytes` / chunk-sent line) | loopTask kept serving through the fatal window |
| [MEM] healthy at :611/:1175 | §6.3 | no heap/PSRAM/stack exhaustion |

The triple — ticks alive on both cores, IDLE0 scheduling dead on CPU0, stage-0
INT path dead — sits BELOW project-code visibility. That is what the candidate
arithmetic of §7.3 must respect.

### 7.2 The post-chunk execution window (reconstructed)

Ordering fact: `transmit()` logs `E32: Transmitted %zu bytes` only at its very
end (e32_lora.cpp:265-267 — AFTER waitForAuxHigh(5000) completes), and the
chunk-sent line is serviceWindowChunk's post-transmit log. balloon2.log:1226-1227
therefore PROVE the fatal transmit completed its full handshake (write → flush
→ AUX-low watch → AUX-high within 5000 ms). **The crash stretch is everything
AFTER :1227: the remainder of that loop pass plus subsequent passes, up to the
reset banner :1228-1231.**

Loop-pass cadence during sustained window service, from the Performance lines
(:915 `Count 975, Avg 223 ms` → :1189 `Count 1192, Avg 383 ms`, Max constant
1320): incremental average = (383×1192 − 223×975)/217 ≈ **1102 ms per pass**
— one full pass (sensors + one chunk transmit + gates + OLED) per ~1.1 s.

The 10 s TWDT window therefore contains ~8-9 loop passes, each executing the
main_balloon.cpp:253-330 order: updateSystemState → processSensors (BMP280
Wire read, GPS UART1 read) → processCommunications → processPowerManagement →
processPacketHandling (CmdHandler().process(), AutoCap().process(),
ImageTx().process() — chunk 2/14 onward, one transmit per pass) → legacy
telemetry/heartbeat/status gates → 1 Hz OLED refresh.

Beacon: [BCN] seq=44 :1188, seq=45 :1199, seq=46 :1213 → ~5 s cadence (30 B
telemetry E32 TXs at :1185/:1197/:1212 between). **seq=47 was DUE inside the
fatal window** (≈5 s after :1213) — its E32 transmit + AUX handshake
plausibly executed between :1227 and the reset.

What the timestamps favor: nothing project-visible. The log between :1227 and
the banner is EMPTY — not even one subsequent loop pass completed its first
loggable action (a BMP280 line normally prints within ~1.1 s; :1225 was the
last). Either the next pass never started, or it started and died before any
print. Non-adjacent, already dispositioned: [MEM] :1175 (§6.3), i2cWrite :885
(§6.4).

### 7.3 Candidate-block arithmetic

Reference periods: TWDT stage-1 = 10 s (the one that fired); IWDT = 300 ms
(the one that did not). Properties per the plan: worst-case duration vs
period; masks interrupts?; kernel portMUX across a blocking wait?; flash-cache
suspension?

| # | Candidate (site) | Worst case | Masks int? | portMUX across wait? | Cache suspend? | Disposition |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `serial->flush()` in E32LoRa::transmit (e32_lora.cpp:224 → uartFlushTxOnly, esp32-hal-uart.c:1474-1486: `while(!uart_ll_is_tx_idle(...))` — bare busy-spin, NO yield, NO timeout) | 217 B @9600 ≈ 226 ms (:1226) | no | **no** — UART_MUTEX_LOCK is `xSemaphoreTake(uart->lock, portMAX_DELAY)` (esp32-hal-uart.c:108-111), a yielding recursive mutex, not a portMUX | no | **ELIMINATED as a direct starver** (226 ms ≪ 10 s; no kernel lock; runs on CPU1, cannot starve CPU0's IDLE0). Retained as the sole non-yielding stretch — see the NB below and §7.6 |
| 2 | waitForAuxHigh/waitForAuxLow (e32_lora.cpp:534-558) | ≤5000 ms, delay(10)-polled | no | no | no | **ELIMINATED** — delay() = vTaskDelay, yields every 10 ms |
| 3 | 1 Hz OLED full-frame I2C (status_display.cpp:102-159 region; 1 KiB frame over 100 kHz Wire) | ~100 ms/refresh | no | no (IDF i2c driver, ISR-driven transaction) | no | **ELIMINATED** (bus shared with BMP280 — the :885/:1577 noise, non-adjacent, §6.4) |
| 4 | BMP280 read / GPS read (processSensors) | ms-class | no | no | no | **ELIMINATED** |
| 5 | sendTelemetryBeacon / the due seq-47 TX (same E32 transmit path) | 30 B ≈ 31 ms spin + yielding AUX waits | as (1) | as (1) | as (1) | **ELIMINATED as direct starver** (same properties as (1)) |
| 6 | PSRAM slice memcpy (createChunkPacket) | 200 B ≈ µs-class | no | no | no (ordinary cached memory ops; no cache-suspend API involved) | **ELIMINATED** |
| 7 | Serial.printf console output | ring-buffered driver write (yields on full TX ring) | no | no | no | **ELIMINATED** |
| 8 | Command response 21 B (:1222) | ~22 ms spin via (1)'s path | as (1) | as (1) | as (1) | **ELIMINATED** |

NB on candidate 1: the spin is UNBOUNDED — if `uart_ll_is_tx_idle` never
asserts, loopTask hangs forever on CPU1, which WOULD match the log's terminal
silence at :1227 (the next transmit, chunk 2/14 due ~1.1 s later, entering
flush and never returning). But IDLE1 is NOT TWDT-subscribed in this build
(sdkconfig:2180), so even that hang cannot produce a TG0 stage-1 reset — it
is a distinct latent hazard, not session 8's mechanism.

Negative result, stated honestly: **no project-code candidate can produce the
§7.1 constraint triple.** Every project task runs on CPU1, holds only yielding
locks, and none can starve IDLE0 on CPU0 or kill the TG0_WDT interrupt path.
The starver is BELOW project code (kernel / interrupt-delivery level on CPU0)
— reachable pre-bench only by elimination, not observation.

### 7.4 One mechanism or two

Session-7 crash 1's Core-0 dump (§1.4) decoded
`SysTickIsrHandler (port_systick.c:148) → xPortSysTickHandler (:224) →
xTaskIncrementTick → spinlock_acquire` CAS spin — CPU0's tick ISR observed
DIRECTLY spinning on a kernel portMUX: the INCREMENT phase of the exact chain
session-8's Saved PC samples in its HOOK phase (port_systick.c:199 — same
function, earlier call). Same chain, same trigger context (sustained TX-heavy
FULL-window service), same reset class (session-7 crash 2 was itself a silent
TG0WDT_SYS_RST, §1.5).

CORRECTION to §6.4: balloon2.log:155 `E32: Transmitted 30 ∩┐╜ytes` IS one
mojibake line of the session-7 class — the source prints `bytes`
(e32_lora.cpp:266); the corrupt bytes (e2 88 a9 e2 94 90 e2 95 9c) replaced
the `b`. §6.4's "ZERO U+FFFD" grep matched the UTF-8 replacement character,
not the raw corrupt byte sequence the console renders. Corrected count: ONE
mojibake line, ~1070 lines before the reset, non-adjacent. The corruption
class is PRESENT but weak this session; it does not reinstate §3's blast
hypothesis as the mechanism, but it does not fully exile it either.

**VERDICT: ONE mechanism family, two expressions.** The structure common to
both: CPU0's tick-chain/scheduler stalls past the TWDT period while CPU1's
task level keeps serving. Expression A (session 7, "loud"): the stall included
a ≥300 ms tick-delivery miss, so the IWDT printed its panic (with the canary
and corruption co-effects). Expression B (session 8, "silent"): ticks
sustained to the very end (IWDT fed), IDLE0 starved 10 s, the TWDT stage-0
interrupt path dead — silent stage-1 at 10 s. Discriminating evidence:
(i) both sessions' PCs sit in the same tick-ISR chain (hook phase vs increment
phase of `xPortSysTickHandler`); (ii) both resets are TG0WDT-class in the
identical trigger context; (iii) the silence is itself informative — stage-0
ALWAYS prints if its ISR runs (task_wdt.c:491 is unconditional), so session
8's silence proves interrupt-path death, a stall deeper than a mere task
starvation; (iv) session-7's corruption trio (§1.6) reads as a co-effect of
the same kernel-level wedge — honestly labeled INFERENCE: the corrupting
write itself has never been observed in either session.

Session-6 reconciliation (honest): session 6 (the 01-20 bench) ran sustained
window service clean, but its raw logs are NOT retained; the reconciliation
rests on 01-20-SUMMARY's record alone. Candidate differentiators — base
request cadence under the 01-18 quiet gate, SVGA 137-chunk tails, re-arm
count — CANNOT be checked against session-6 evidence. Recorded as a weak
point of this round, not as evidence.

### 7.5 Ranked root cause (with the honest limit)

**RANKED #1: CPU0 scheduler / interrupt-delivery stall (kernel level, below
project-code visibility) during sustained FULL-window TX service — ticks
alive, IDLE0 unrun for ≥10 s, TWDT stage-0 interrupt unserviced; the same
tick-chain class session 7 observed directly as a portMUX CAS spin.**

- Evidence FOR: the §7.1 constraint triple (each item independently sourced);
the total elimination of project-code candidates (§7.3); [MEM] health
(exhaustion ruled out, §6.3); session-7's direct observation of the wedge
class in the identical trigger context (§1.4).
- Evidence AGAINST / the honest limit (the 01-25 §3 convention, invoked
  explicitly): the stall's INNER STRUCTURE — which interrupt, which lock,
  which storm — is NOT directly observable in retained evidence: one PC
  sample, no register dump, no coredump, an empty log between :1227 and the
  banner. The blocking site itself cannot be observed pre-bench; this ranking
  is by elimination + blast radius ONLY.
- Eliminated as the cause in earlier rounds of this doc: first-use PSRAM
  (§6.3, disconfirmed), heap/stack exhaustion ([MEM] healthy), interim
  spacing (crash occurred with spaced captures, §6.2), and now every direct
  project-code starver (§7.3).

Consequence for the fix: a fix claiming to remove the kernel-level stall
outright would be UNEVIDENCED (the §6.5 prohibition). What the evidence DOES
support: removing the one hazard-shaped construct in the fatal path (the
unbounded no-yield drain) and shipping discriminators that let 01-29's bench
classify the kernel-level remainder. That is the shape §7.6 hands Task 2.

### 7.6 Named fix lever + bench discriminators (Task 2's menu)

- **Lever A (the named lever):** replace the unbounded no-yield TX-drain
  `serial->flush()` at e32_lora.cpp:224 with a YIELDING, BOUNDED drain — a
  delay(1)-polled `uart_ll_is_tx_idle(UART_LL_GET_HW(port))` with a 1000 ms
  bound. The UART port arrives as a new defaulted `begin()` parameter
  (`uartPort = 2` — both boards use UART2: main_balloon.cpp:473 `&Serial2`,
  main_basestation.cpp:62 `HardwareSerial LoRaSerial(2)`); the port number is
  required because HardwareSerial exposes no `uart_hw` accessor. Semantics
  preserved: the drain still completes before the AUX-low watch begins.
  HONEST SCOPE: §7.3 ELIMINATED the flush as session-8's direct starver —
  this lever removes the sole non-yielding, UNBOUNDED stretch in the fatal
  path (the latent loopTask-hang hazard that also matches the log's terminal
  silence); it does NOT claim to remove the kernel-level stall.
- **Discriminator B1 (boot):** a reset-cause line in ImageTxManager::begin():
  `ImageTx: [BOOT] reset-cause: <name> (G-01-10)`, via `esp_reset_reason()`
  plus a local 16-case name switch (IDF 5.5.4's prebuilt headers expose NO
  `esp_reset_reason_to_name()` — verified: esp_system.h declares only
  `esp_reset_reason()`). Gives 01-29 the reset class on every boot even when
  the ROM banner scrolls out of console capture.
- **Discriminator B2 (loop):** a latch-guarded slow-pass line at the top of
  ImageTxManager::process(): `ImageTx: [LOOP] slow pass gap %lu ms (G-01-10)`
  when the gap between consecutive process() calls exceeds
  `IMG_LOOP_SLOW_PASS_MS = 2500` (≈2.3× the 1102 ms service-pass cadence of
  §7.2; half the TWDT stage-0 period), latched once per episode, re-armed on
  a normal pass (the reannounceHoldLogged convention). Discriminates
  loop-level stalls from CPU0-side wedges: a TG0WDT reset with NO preceding
  [LOOP] line means loopTask never stalled — the session-8 CPU0-side
  signature; [LOOP] lines before a reset mean the stall caught the loop too —
  a different, loop-visible class.
- **Removal condition for B1/B2:** strip WITH the [MEM] instrumentation after
  G-01-10 closes on bench evidence (the image_tx_manager.cpp:135-136 clause,
  unchanged).
- **Untouched candidates, explicitly dispositioned:** a platformio.ini
  WDT-config lever (lengthen CONFIG_ESP_TASK_WDT_TIMEOUT_S / drop stage-1) —
  REJECTED: masks the symptom and weakens every future bench tripwire; the
  serviceWindowChunk pacing / D-05/D-07 surfaces — REJECTED: protected
  surfaces, and the crash is not pacing-correlated (six identical-cadence
  windows served clean :660-:1216).

Options this feeds Task 2: (a) Lever A only; (b) instrument-only (B1+B2);
(a)+(b) combined — the 01-25 shape. The evidence supports (a)+(b): a named
lever exists (the sole non-yielding stretch, in the fatal path) AND the
mechanism confirmation needs the discriminators (§7.5's limit).

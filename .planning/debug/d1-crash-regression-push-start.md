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

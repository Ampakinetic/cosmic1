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

### FIX SHAPE SELECTED (round #12 Task 2 checkpoint record)

Checkpoint Task 2 (fix-shape selection, gate `blocking`) auto-resolved under
`auto_advance: true` (config.json) per the 01-23/01-25/01-26 convention —
selection recorded as PENDING end-of-phase operator confirmation (joins the
three existing pending confirmations). Selected: **option (a)+(b) combined —
the 01-25 shape.** The evidence that selected it: §7.5 ranks a mechanism whose
inner structure cannot be observed pre-bench (elimination + blast radius
only), while §7.3 names exactly one hazard-shaped construct in the fatal path
(the unbounded no-yield `serial->flush()` drain at e32_lora.cpp:224 — §7.6
Lever A), and the mechanism confirmation needs the discriminators (§7.6 B1
boot reset-cause + B2 [LOOP] slow-pass latch) for 01-29 to classify any
recurrence. Concretely: Lever A (yielding bounded TX-drain, defaulted UART
port parameter) + discriminators B1/B2, each carrying its named removal
condition. NO pacing, quiet-gate, wire-format, or WDT-config changes (the
rejected candidates of §7.6 stand).

---

## 8 — SESSION 9 RECURRENCE (01-29 bench, 2026-08-28): the 01-28 lever DISCONFIRMED as sufficient — the crash moves to the INTER-WINDOW LULL, and the i2cWrite adjacency BREAKS the noise classification

Session 9 crashed AGAIN, on the round-#12 firmware (Lever A + discriminators
B1/B2 all provably in-image). The crash's THIRD expression: session 7 =
first-post-boot push; session 8 = mid-service of a re-armed window; session 9
= the inter-window LULL after a cleanly completed FIRST window. Every line
below re-verified against the retained consoles by the 01-29 continuation
executor (one correction to the incoming evidence packet discovered in
re-verification — §8.4).

### 8.1 Provenance (cross-checked BEFORE any decoded frame was trusted)

- Firmware: round #12 (01-28 fix `6ca9a36` + docs `9261b9e`), balloon build
  banner `Build: Aug 28 2026 12:06:09` (balloon3.log:23 boot 1, again :535
  post-crash boot 2); pre-flight on record from the prior executor: `pio run`
  2/2 SUCCESS, harness exit 0.
- Deployed-ELF provenance: `.pio/build/esp32-s3-balloon/firmware.elf`
  SHA256 `e09dd034a5ab7b904278902106a0348d12c800ad88b03daac5d29029ad0cb5f7`,
  mtime Aug 28 12:06 — **MATCHES** the deployed boot banner to the minute;
  the decode in §8.2 is against the exact crashed build.
- The round-#12 code is provably IN the deployed image: warm-up line at BOTH
  boots (balloon3.log:102, :614 `ImageTx: PSRAM first-use warm-up done
  (G-01-10)`), discriminator B1 at both boots (:103 `[BOOT] reset-cause:
  POWERON`, :615 `[BOOT] reset-cause: TASK_WDT`), [MEM] lines throughout.
- Consoles: `balloon3.log` (804 lines) + `base3.log` (221 lines), mtimes Aug
  28 12:31 (operator-retained names; the plan requested balloon14.log/base9.log
  — provenance deviation recorded in 01-UAT.md, files never renamed, the
  01-24 convention).

### 8.2 The fatal event — TG0WDT in the inter-window lull, after a CLEAN first window

Service map (both consoles, verbatim anchors):

> balloon3.log:366 `CommandHandler: Captured image ID 39` (first post-boot
> capture — CAPTURE_NOW ACKed base3.log:61)
> balloon3.log:375 `ImageTx: enqueued image 39 (full 7133 B, thumb 1661 B / 9 chunks, source 0)`
> thumb 9/9 chunks sent :383-:414 → base3.log:79 `image 39 kind 0 finalized COMPLETE (9/9 chunks, 1661 B)`
> base3.log:81 `manifest image 39 kind 1 (7133 B, 36 chunks, CRC 447D30AB)` → window 1 requested (chunks 0..15, seq 3) and ACKED :89
> balloon3.log:421 `ImageTx: FULL window armed for image 39 (chunks 0..15)` → **ALL 16/16 window-1 chunks transmitted** :427-:491
> balloon3.log:492 `ImageTx: re-announce held - inbound window traffic active` (genuine fire — chunks were flowing)
> inter-window lull :495-:519 — GET_STATUS serviced (:497-:501), `[BCN] seq=23` :505, BMP280 reads, [MEM] :511, Performance :514, telemetry beacon TX :518-:519
> balloon3.log:520 `[124026][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_S∩┐╜ATE`
> balloon3.log:521-522 `ESP-ROM:esp32s3-20210327` / `Build:Mar 27 2021`
> balloon3.log:523 `rst:0x7 (TG0WDT_SYS_RST),boot:0x2b (SPI_FAST_FLASH_BOOT)`
> balloon3.log:524 `Saved PC:0x4037c7fa`

Decode (xtensa-esp32s3-elf-addr2line `-pfiaC`, ELF SHA `e09dd034…` verified
first — §8.1; re-run by the continuation executor, output verbatim):

| Addr | Symbol | Role |
| --- | --- | --- |
| 0x4037c7fa | `esp_vApplicationTickHook` (esp-idf `components/esp_system/freertos_hooks.c:34`, discriminator 1) | the registered-tick-hook DISPATCH LOOP of the tick-ISR hook phase — where the per-core tick callbacks (int_wdt's `tick_hook` among them) are invoked |
| 0x403c88b8 (entry, :530) | unresolvable (`?? ??:0`) | second-stage-bootloader load region — expected, not evidence |

**Zero project frames** — the third session in a row. The three Saved-PC
samples across sessions now reconcile into ONE call chain (§8.5).

What SURVIVED before the crash (the spaced smoke's first window served
clean): capture → ACK → thumb COMPLETE → FULL window 1 armed and served 16/16
chunks on air. The session aborted at protocol step 3 (the FULL never
completed — window 2 was never requested because chunk 7 of window 1 was lost
on air and the base spent its retransmit passes on chunks 7..7, by which time
the balloon had already crashed: NACK_INVALID ×3 base3.log:162/:171/:181
against the rebooted balloon's honest rejections balloon3.log:679/:692/:713 →
base3.log:194 `image 39 kind 1 finalized INCOMPLETE (retransmit passes
exhausted): 15/36 chunks after 3 passes`). Zero `timeout after` command
terminals in base3.log (no forbidden CAPTURE_NOW terminals — unlike session 7,
like session 8).

### 8.3 The round-#12 discriminators' verdict (B1/B2 answered — they worked)

- **B2 [LOOP] slow-pass latch: ZERO fires in the whole console** (grep count
  0 against `IMG_LOOP_SLOW_PASS_MS 2500`). Corroborated by the Performance
  line in the pre-crash stretch: :514 `Performance - Loop: 0 ms, Max: 1277 ms,
  Avg: 47 ms, Count: 993` — the slowest observed pass was 1277 ms, ~half the
  2500 ms latch threshold. **loopTask never stalled.** Per §7.6's own
  discriminating read: a TG0WDT reset with NO preceding [LOOP] line is the
  session-8 CPU0-side signature — session 9 reproduces it exactly. The Lever A
  drain-replacement hypothesis (loopTask hanging in an unbounded flush) is
  therefore ALSO disconfirmed as the mechanism: the drain now yields and
  bounds, and the loop demonstrably kept cycling to the final second
  (:518-:519 telemetry TX immediately before the failing Wire transaction).
- **B1 [BOOT] reset-cause: WORKED at both boots** — :103 POWERON, and :615
  `TASK_WDT` on the crash reboot: the ROM reason register INDEPENDENTLY
  confirms the rst:0x7 = task-watchdog read of §7.1 (TG0WDT_SYS_RST ↔
  ESP_RST_TASK_WDT).
- **[MEM]: healthy throughout** — enqueue :372 `heap=8544812 minHeap=8501644
  psram=8339972 stackHW=5772`; last pre-reset sample :511 `heap=8534176
  minHeap=8499816 psram=8331100 stackHW=5744`; boot-2 baseline healthy.
  Exhaustion remains ruled out — third consecutive session.

### 8.4 The crash-adjacent i2cWrite BREAKS the non-adjacent-noise classification — and carries mojibake on its own line

Sessions 7/8 dispositioned `i2cWrite … ESP_ERR_INVALID_STATE` as recurring
non-adjacent BMP280-path noise (each occurrence followed by a successful
read, far from any reset). **Session 9's single occurrence breaks BOTH legs
of that classification:**

1. **DIRECTLY ADJACENT to the reset.** balloon3.log:520 error → :521-:522 ROM
   banner → :523 rst:0x7. No application line separates them. And unlike
   sessions 7/8, NO successful BMP280 read follows — the reset intervened.
   The failing transaction is the next Wire-0 action after the :518-:519
   beacon TX (BMP280 poll or OLED refresh — the log does not discriminate
   which device).
2. **The line itself carries the session-7 corruption-class mojibake.** The
   source string is `ESP_ERR_INVALID_STATE` (esp_err_to_name); the log bytes
   are `ESP_ERR_INVALID_S` + `e2 88 a9 e2 94 90 e2 95 9c` + `ATE` — the exact
   9-byte corrupt sequence of session 7 (§1.6: `Transmitted 38 byte∩┐╜`) and
   session 8's corrected single line (§7.4: balloon2.log:155), here replacing
   the single `T` of STATE. Hex-dump verified at :520. Count in balloon3.log:
   exactly 1; base3.log: 0. (Method note: the UTF-8-replacement-character grep
   returns 0 — the §7.4 trap AGAIN, now twice-documented; the operative grep
   is the literal rendering sequence / raw byte triple. The incoming evidence
   packet's "mojibake = 0" claim used the trapped grep; corrected here.)

Reading (honest, both directions stated): at t=124026 ms the UART0 TX bytes
were ALREADY wrong on the very line that reports the I2C driver FSM rejecting
a transaction, ≤ seconds before the TWDT stage-1 hardware reset. Two of
session-7's three corrupted-subsystem markers (UART TX ring + I2C driver FSM)
fire TOGETHER, adjacent to the third (the kernel-level wedge the TWDT reset
represents) — the session-7 §1.6 convergence pattern, this time in a tight
terminal window. Timing constraint from §7.1's topology: stage-1 fires at 10 s
unfed, so IDLE0 had been starved since ≈ t=114 s at the latest — the
outwardly-healthy lull (:495-:519, loopTask cycling normally on CPU1) was
ALREADY wedged underneath on CPU0. On this reading the i2cWrite failure +
mojibake are co-symptoms of the wedge's terminal phase, not its initiating
cause — but that is the round-#3 question, not a settled claim (§8.6).

### 8.5 Three PC samples, one chain (the one-family verdict, third data point)

| Session | Saved PC | Decoded symbol | Chain position |
| --- | --- | --- | --- |
| 7 (crash 1, Core-0 dump) | SysTick ISR region | `xPortSysTickHandler` → `xTaskIncrementTick` → `spinlock_acquire` CAS spin (§1.4) | the tick INCREMENT phase, spinning on the kernel portMUX |
| 8 | 0x40376430 | `tick_hook` (int_wdt.c:111, §6.2/§7.1) | the tick HOOK phase — the IWDT-feed hook dispatched BY esp_vApplicationTickHook |
| 9 | 0x4037c7fa | `esp_vApplicationTickHook` (freertos_hooks.c:34) | the tick HOOK phase — the dispatch loop ONE FRAME ABOVE session 8's sample |

Verified against the deployed framework sources (IDF 5.5.4,
`framework-espidf`): `esp_vApplicationTickHook` (freertos_hooks.c:29-38) is
the loop that invokes the registered per-core tick callbacks — int_wdt's
`tick_hook` (int_wdt.c:104+) is one of those callbacks. Session 8 sampled the
dispatched hook; session 9 sampled its dispatcher; session 7 observed the
same ISR's increment phase wedged on the kernel spinlock. **Three samples,
one chain, zero project frames.** §7.4's one-mechanism-family verdict stands
with its strongest cross-session confirmation yet.

### 8.6 New root-cause questions (the debug round #3 brief)

The CONSTANT across all three expressions: TG0WDT stage-1 (IDLE0 starved ≥10 s
on CPU0), PC in the tick-ISR chain, zero project frames, loopTask healthy
(session 9: Max 1277 ms, zero [LOOP] fires), [MEM] healthy. What CHANGED this
session — and must be explained, not explained away:

1. **The crash point MOVED from mid-service to the inter-window lull.**
   Session 8 died at the FIRST CHUNK of a re-armed window (TX-heavy); session
   9 died in the TX-LIGHT lull (sensors + one beacon + one GET_STATUS
   response) after a cleanly completed FIRST window, ~124 s in (t=124026 ms
   on the failing line). "Sustained TX-heavy FULL-window service" is NO
   LONGER the constant context §6.5 named — the round-#12 axis must be
   re-examined: is the IDLE0-starvation-on-CPU0 family hypothesis still the
   best fit when the wedge forms (or terminates) in a lull, or does the
   lull-vs-service distinction point at a different starver per phase?
2. **What does a Wire-0 transaction failing ESP_ERR_INVALID_STATE — on a
   line whose own bytes are corrupt — immediately before a TWDT reset
   imply?** Three candidate readings, none established: (a) driver-state
   corruption as a co-effect of the same memory/kernel fault (the §1.6
   convergence, terminal-phase edition); (b) an I2C bus wedge (stretched
   clock, stuck slave) with interrupts masked on CPU0 — the INVALID_STATE
   being the driver's honest refusal, the mojibake and the reset the wedge's
   other effects; (c) coincidence — one noise-class error that happened to
   land last. The adjacency + own-line mojibake make (c) the WEAKEST reading,
   but only round #3 evidence can rank (a) vs (b).
3. **Why is the corruption signature's return ADJACENT this time?** Session 8
   had exactly one mojibake line, ~1070 lines from its reset (§7.4); session 9
   has exactly one, ON the crash-adjacent line. Same count, different
   proximity — consistent with a wedge that degrades output/driver state in
   its final seconds, but one sample each is not a trend.

Named discriminators round #3 should consider (menu, not selection — the
§7.6 convention): a CPU0-interrupt/idle observability line (e.g. an
esp_timer-based IDLE-run counter sampled from the 1 Hz block — IDLE0's
absence is today visible only via the 10 s reset); I2C-bus health
instrumentation at the Wire-0 failure site (transaction count + FSM state at
failure, bounded); a TWDT stage-0 subscription check (the §7.1 stage-0
interrupt-path death remains inferred from silence, never observed); and an
explicit hypothesis table for lull-phase vs service-phase starvers (the §7.3
arithmetic eliminated service-phase candidates — the lull-phase set — Wire-0
transactions, GPS UART1 reads, NVS/flash ops — has NOT had the same audit).

The `[MEM]`/B1/B2 instrumentation STAYS IN — removal condition unchanged
(strip only after G-01-10 closes on bench evidence).

---

## 9 — SESSION-9 DEBUG ROUND #3 (round #13): the lull-phase audit, crash-context determinism, the I2C+mojibake convergence verdict

Scope: §8.6's three questions + the discriminator menu, executed against the
retained evidence BEFORE any code change (the §6.1/§7 convention — the audit's
object of study is the build that crashed). Evidence base: `balloon3.log` (804
lines) / `base3.log` (221 lines) (retained), the deployed ELF
`.pio/build/esp32-s3-balloon/firmware.elf` SHA256
`e09dd034a5ab7b904278902106a0348d12c800ad88b03daac5d29029ad0cb5f7`
(re-confirmed this round, §9.1), and the deployed framework artifacts pinned by
platformio.ini (platform espressif32 55.03.39 → arduino-esp32 3.3.9 → ESP-IDF
5.5.4 prebuilt libs). Framework-source claims below were re-verified against the
installed packages this round: IDF 5.5.4 at `~/.platformio/packages/framework-espidf`
(components `esp_driver_i2c/i2c_master.c`, `esp_system/freertos_hooks.c`,
`esp_system/include/esp_task_wdt.h`, `esp_system/include/esp_freertos_hooks.h`),
arduino core at `~/.platformio/packages/framework-arduinoespressif32`
(`cores/esp32/esp32-hal-i2c-ng.c`, `cores/esp32/esp32-hal-i2c.h`,
`libraries/Wire/src/Wire.cpp`, `libraries/Preferences/src/Preferences.cpp`),
prebuilt esp32s3 config at
`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig`
(hereafter "sdkconfig"; TWDT/IWDT rows re-read this round and identical to
§7.1: CONFIG_ESP_TASK_WDT_TIMEOUT_S=5 / PANIC=y / CHECK_IDLE_TASK_CPU0=y /
CPU1 not set; INT_WDT 300 ms CHECK_CPU1=y) and its FreeRTOSConfig.h
(`include/freertos/config/include/freertos/FreeRTOSConfig.h`).
Project-source claims carry `file:line` against HEAD `1cccc42` (source unchanged
since the round-#12 fix commit `6ca9a36` — docs-only commits after).

### 9.1 PROVENANCE + LULL RECONSTRUCTION

**ELF SHA check (performed FIRST, before any decode claim):**
`sha256sum .pio/build/esp32-s3-balloon/firmware.elf` →
`e09dd034a5ab7b904278902106a0348d12c800ad88b03daac5d29029ad0cb5f7` — **MATCHES**
§8.1's deployed-build SHA in full (not just the 10-hex prefix). No new addr2line
decode was required this round (§8.2's `0x4037c7fa → esp_vApplicationTickHook
freertos_hooks.c:34` decode is retained; this round re-verified the SOURCE line:
`esp-vApplicationTickHook` at `framework-espidf/components/esp_system/freertos_hooks.c:29-38`
is the per-core dispatch loop `for (n = 0; n < MAX_HOOKS; n++) { if
(tick_cb[core][n] != NULL) { tick_cb[core][n](); } }` — the Saved PC samples the
callback invocation inside that loop, exactly §8.5's reading).

**Bus-clock ground truth (new this round):** balloon3.log:58 — the deployed
driver's own boot line — `[1181][I][esp32-hal-i2c-ng.c:112] i2cInit():
Initializing I2C Master: num=0 sda=1 scl=2 freq=100000`. The balloon's Wire-0
runs at **100 kHz**. The 400 kHz upgrade exists only on the BASE board path
(src/status_display.cpp:35 sets it inside `Board::BASE`); the BALLOON path
never calls `Wire.setClock` (status_display.cpp:37 comment; the bus is begun in
src/sensor_manager.cpp:86 `Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN)` — default
100 kHz, Wire.cpp/`i2cInit` fallback esp32-hal-i2c-ng.c:98-99). All Wire-0
timing arithmetic below uses 100 kHz.

**The lull, reconstructed from balloon3.log :491-:523.** Timestamps print only
on milestones/errors, so the bracket is reconstructed from cadences (each
cadence source-cited) anchored on the two hard timestamps: [124026] on the
failing line :520 and the 993-pass Performance line :514.

| Lines | Activity | Cadence (source) |
| --- | --- | --- |
| :491 | window chunk 16/16 sent — last FULL-window transmit | — |
| :492 | `re-announce held - inbound window traffic active` (genuine fire) | bookkeeping, no radio |
| :493-:496 | BMP280 :493/:494/:496, GPS no-fix :495 | BMP280 1000 ms (balloon_config.h:34); GPS drain 2000 ms (balloon_config.h:35) |
| :497-:501 | GET_STATUS serviced: command 20 → Status sent → 47 B E32 TX → Response sent → SUCCESS | base 30 s poll (base3.log:103/:129) |
| :502-:505 | legacy telemetry packet :502, 30 B TX :504, `[BCN] seq=23` :505 | legacy telemetry 5000 ms (main_balloon.cpp:67); beacon 5000 ms (image_protocol.h:178) |
| :506-:513 | BMP280 :506/:510/:512, GPS :507/:513, heartbeat+status debug :508-:509, `[MEM] new-low` :511 | as above |
| :514 | `Performance - Loop: 0 ms, Max: 1277 ms, Avg: 47 ms, Count: 993` | 10 s gate (main_balloon.cpp:70) |
| :515-:517 | BMP280 :515/:516, GPS :517 | as above |
| :518-:519 | 30 B E32 TX + telemetry packet created (legacy gate) | as above |
| :520 | `[124026][E][esp32-hal-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed: [259] ESP_ERR_INVALID_S∩┐╜ATE` | the next Wire-0 action (BMP280 poll or OLED refresh — the log cannot discriminate which device; §9.4) |
| :521-:524 | ROM banner → rst:0x7 TG0WDT_SYS_RST → Saved PC:0x4037c7fa | the reset |

Loop bookkeeping: MAIN_LOOP_INTERVAL_MS = 100 (src/main_balloon.cpp:66) — the
loop targets 10 Hz; :514's own arithmetic gives the observed average
124026 ms / 993 passes ≈ **125 ms/pass** (compute Avg 47 ms + delay-to-100 ms on
idle passes, stretched to 0.2-1.3 s on transmit passes). **Wall-clock
brackets:** [BCN] seq=18 at :378 → seq=23 at :505 is five 5 s intervals ≈ 25 s,
so the service stretch :378-:505 spans ≈ t≈94→119 s, and the lull :505-:520 is
the final ≈5 s (t≈119→124 s). seq=24's beacon was DUE at ≈ t≈124 s — the crash
instant; the :518-:519 30 B TX is the legacy 5 s telemetry gate, and it
COMPLETED ITS FULL E32 HANDSHAKE (the `Transmitted N bytes` line prints only at
transmit end, after waitForAuxHigh(5000) — src/e32_lora.cpp:287-299 — the §7.2
ordering fact): the radio, its AUX line, and UART0 were all healthy within the
final second.

**Every activity that ran in the lull window** (the §9.2 audit's closed set):
BMP280 Wire-0 register reads (~1 Hz), the 1 Hz OLED full-frame Wire-0 refresh
(src/main_balloon.cpp:302-319 → src/status_display.cpp:159 `display()` —
invisible in the log, its tick landed somewhere in-window), GPS UART1 drains
(2 s cadence, :495/:507/:513/:517), the GET_STATUS service + 47 B response TX
(:497-:501), two 30 B legacy telemetry TXs (:504, :518-:519), the [BCN] seq=23
beacon TX (:505), heartbeat/status packet-creation debug (:508-:509 — CPU1
bookkeeping, no E32 line adjacent), Serial.printf console output for every
line, loop statistics/delay bookkeeping (main_balloon.cpp:336-353). **NOT in
the window:** any capture (last :366), any NVS write (capture-time only —
§9.2 candidate 4), any chunk/window push (window 2 never armed), any Wi-Fi
activity (the balloon runs none).

### 9.2 LULL-PHASE CANDIDATE ARITHMETIC (the §7.3 standard, applied to the set that never had it)

Reference periods (unchanged from §7.1, re-used verbatim): TWDT stage-1 = 10 s
(fired at :523); TWDT stage-0 = INT @5 s, prints unconditionally, never printed
(the stage-0 interrupt path was dead ≥5 s before the reset); IWDT = 300 ms,
never fired (both cores' tick chains ran to the end).

**The deployed-driver INVALID_STATE semantics first** (§8.6 question 2's
factual foundation — read from the deployed sources, every claim path-cited):

- The Arduino HAL wrapper `i2cWrite` (cores/esp32/esp32-hal-i2c-ng.c:237-293)
  guards each bus with `bus[].lock` = `xSemaphoreCreateMutex()` (:80) — a
  **YIELDING FreeRTOS mutex**, taken with portMAX_DELAY (:87, :249) and held
  across the transaction; **NOT a portMUX**. Every Arduino transaction is
  SYNCHRONOUS: `trans_queue_depth = 0` (:130) → `async_trans` false.
- The log's exact line `esp32-hal-i2c-ng.c:275` is the `log_e` after
  `i2c_master_transmit` returns non-OK (:273-276).
- `i2c_master_transmit` (IDF `esp_driver_i2c/i2c_master.c`, sync path): takes
  the bus `bus_lock_mux` binary semaphore (created :1092-1094; taken :1416
  region), runs `s_i2c_transaction_start` (:678-738). **A busy/stuck bus does
  NOT produce a refusal**: a prior TIMEOUT status or a bus-busy reading
  triggers `s_i2c_hw_fsm_reset(bus, true)` FIRST (:685-687) and the
  transaction proceeds (on S3 the clear-bus is the GPIO-pulse variant —
  `soc_caps.h:215` records SOC_I2C_SUPPORT_HW_FSM_RST is NOT defined for
  esp32s3 → i2c_master.c:70-88, ≤9 SCL pulses with esp_rom_delay_us,
  microsecond-class). The bus spinlock is taken only around register/FIFO
  setup (:693-715) — ISR-grade, never across a wait. Then
  `s_i2c_send_commands` (:532-613) waits on the event queue with
  `ticks_to_wait` (:588) and returns **ESP_ERR_INVALID_STATE iff the final
  status is not I2C_STATUS_DONE** (:726-728).
- The completion statuses the ISR writes (:782-809): I2C_STATUS_ACK_ERROR on
  NACK (:797-798), I2C_STATUS_TIMEOUT on SCL-timeout/arbitration (:800-801),
  I2C_STATUS_DONE on master-complete (:803-805).

**Meaning: ESP_ERR_INVALID_STATE at :275 is NOT a driver-FSM "wrong state,
refusing to start" — there is no such FSM state in the deployed stack. It is
the honest completion status of a transaction that RAN and ended non-DONE: a
NACK (ACK_ERROR) or a bus timeout (TIMEOUT).** A Wire-0 transaction therefore
cannot wedge loopTask: it is bounded at task level by the Wire timeout —
default 50 ms (`libraries/Wire/src/Wire.cpp:44` `_timeOutMillis(50)`; S3's
clear-bus adds microseconds) — plus at most a tick-bounded trailing busy-wait
after a NACK event (i2c_master.c:595-604, bounded by the same ticks_to_wait),
all on CPU1, all behind yielding locks. One audited non-reachable stretch,
recorded for completeness: the UNBOUNDED `while (i2c_ll_is_bus_busy(...)) {}`
at i2c_master.c:648 lives in `s_i2c_send_command_async` (:615) — reached only
when `async_trans == true` (:816), which this firmware never configures.

Candidate table (duration vs the 10 s stage-1 period; properties: masks
interrupts? / kernel portMUX across a blocking wait? / suspends the flash
cache? / feeds or starves IDLE0?):

| # | Candidate (site) | Worst case (lull) | Masks int? | portMUX across wait? | Cache suspend? | IDLE0 effect | Disposition |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | BMP280 Wire-0 poll (src/sensor_manager.cpp:186-187; Adafruit readPressure+readTemperature = 2 transactions of 3-4 B @100 kHz, 1 Hz) | <5 ms typical; ≤~100 ms absolute worst (50 ms timeout + tick-bounded NACK wait + µs clear-bus, §9.2 semantics) | no (ISR-driven; the I2C ISR allocates on the allocating task's core — CPU1) | no (HAL lock = yielding mutex i2c-ng.c:80; bus spinlock ISR-grade µs) | no | none — CPU1 only | **ELIMINATED** |
| 2 | 1 Hz OLED full-frame refresh (src/main_balloon.cpp:302-319 → src/status_display.cpp:159; 1024 B frame + command stream @100 kHz) | ~95 ms/refresh at 1 Hz (1024×9 µs + overhead) | no | no | no | none — CPU1 | **ELIMINATED** |
| 3 | GPS UART1 drain (src/sensor_manager.cpp:210-220; 2000 ms cadence, `while (millis() < timeout && gpsSerial->available())` bounded at 100 ms, 38400 baud sensor_pins.h:50) | 100 ms per 2 s | no (UART RX is ISR-fed ring) | no (Serial1 API = HAL yielding locks) | no | none — CPU1 | **ELIMINATED** |
| 4 | NVS image-ID persistence (src/auto_capture.cpp:318-320; `putUShort` = nvs_set_u16 + nvs_commit, Preferences.cpp:159-170 region — a REAL flash-write window WITH cache suspension) | ms-class when it fires — but it fires ONLY inside `allocateImageId()` at capture allocation (from `fire()` :290 and the CAPTURE_NOW handler); last capture :366, ≈87 s before the reset; **no capture occurred in the lull** | (would not mask) | (no) | YES at write — not in this window | none — CPU1 | **ELIMINATED as LULL-reachable** (the flash-write window belongs to the capture phase; §9.4 notes it as a non-constant across expressions) |
| 5 | [BCN] seq=23 beacon + the two legacy 30 B telemetry TXs (:504, :518-:519; src/image_tx_manager.cpp:262-330, main_balloon.cpp:964+; common path E32LoRa::transmit src/e32_lora.cpp:212-300 incl. Lever A's delay(1)-polled 1000 ms-bounded drain :244-256) | 30 B ≈ 31 ms drain; Lever-A bound 1000 ms; AUX waits ≤6000 ms worst case, delay(10)-polled (yields throughout) | no | no (UART_MUTEX_LOCK = yielding recursive mutex, esp32-hal-uart.c:108-111) | no | none — CPU1 | **ELIMINATED** (§7.3 properties unchanged post-Lever-A; the :518-:519 TX provably COMPLETED its handshake — §9.1) |
| 6 | GET_STATUS response TX 47 B (:497-:501) | 47 B ≈ 49 ms drain + same AUX path as (5) | as (5) | as (5) | as (5) | none — CPU1 | **ELIMINATED** |
| 7 | Serial.printf console output (every log line; UART0 TX ring 115200) | ring-buffered driver write; yields on a full ring | no | no | no | none — CPU1 | **ELIMINATED** |
| 8 | Loop bookkeeping / millis / delay (src/main_balloon.cpp:336-353) | delay() = vTaskDelay — yields | no | no | no | none — CPU1 | **ELIMINATED** |

**Negative result, stated honestly: no lull-phase project-code candidate can
starve IDLE0 on CPU0 for 10 s.** Every lull activity is CPU1, bounded
ms-class, and holds only yielding locks. The §7.3 verdict generalizes: the
starver is below project code in BOTH phases — the lull set simply had never
been through the arithmetic until now. And the INVALID_STATE semantics sharpen
the §8.6 question-2 foundation: the :520 line is evidence of a BUS-LEVEL
non-DONE completion (NACK or timeout), not of a corrupted driver refusing to
run — which makes the §9.4 convergence question "what does a bus-level
failure at t=124.0 s mean about the CPU0 wedge", not "what corrupted the
driver".

**The starvation window's opening (back-computation).** Stage-1 fired at
t=124.0 s ⇒ IDLE0's last successful feed was ≤ t≈114.0 s. By the §9.1
brackets, t≈114 s falls INSIDE or AT THE IMMEDIATE END of the window-service
stretch (:378-:505 ≈ t≈94-119 s) — the wedge matured during sustained service
or in its first lull second, and the lull's ≈10 s of visibly-healthy CPU1
activity (:505-:519) ran on top of an already-starving CPU0. The lull did not
form the wedge; it is where the wedge's 10 s fuse happened to burn out. (The
±few-seconds honesty bound: per-pass cost varies 0.2-1.3 s across the service
stretch, so the bracket is not finer than that.)

### 9.3 CRASH-CONTEXT DETERMINISM (§8.6 question 1)

**The three expressions against the shared constants** (§7.1's constraint
triple + the discriminator readings):

| Constant | S7 crash 1 | S7 crash 2 | S8 | S9 |
| --- | --- | --- | --- | --- |
| Reset class | INT_WDT CPU1 panic (300 ms class) + canary | rst:0x7 silent | rst:0x7 silent | rst:0x7 silent |
| PC sample | tick INCREMENT phase — `spinlock_acquire` CAS spin (§1.4) | ROM 0x40055xxx (unresolvable, §1.5) | `tick_hook` int_wdt.c:111 (hook phase) | `esp_vApplicationTickHook` freertos_hooks.c:34 (dispatch loop — one frame ABOVE S8) |
| Project frames | zero | zero | zero | zero |
| loopTask | healthy | healthy | healthy (log to :1227) | **healthy — B2 [LOOP] ZERO fires, Max 1277 ms (:514), loop cycling to :519** |
| [MEM] | n/a (pre-instrument) | n/a | healthy (:611/:1175) | healthy (:372/:511) |
| Phase at crash | first-post-boot push | first-post-boot push (post-reboot) | mid-service, chunk 1/14 of the 7th re-armed window | **inter-window lull**, ~5 s after a cleanly completed first window |
| Corruption markers | UART ring + I2C FSM + canary trio (:299/:301/:313) | none visible | 1 mojibake line ~1070 lines pre-reset (§7.4); 2 non-adjacent recovered i2cWrite | **1 mojibake ON the crash-adjacent line (:520); 1 i2cWrite adjacent, never recovered** |

**Hypothesis table** (each row: statement, predicted 01-31 discriminator
signature, verdict):

| Hypothesis | Statement | Predicted 01-31 signature | Verdict |
| --- | --- | --- | --- |
| **H-lull** — a lull-phase starver (a Wire-0/GPS/NVS lull activity starves IDLE0) | some activity that runs chiefly in TX-light phases blocks CPU0 | crashes correlate with lulls; the [I2C] instrument (§9.6) would show failure storms in lulls; [IDLE0] freezes only in lulls | **REJECTED as the family mechanism.** §9.2 eliminates every lull candidate by the same arithmetic that killed the service set (§7.3) — nothing project-code in a lull can touch CPU0's IDLE0 or interrupt path. H-lull survives only as a description of WHERE fuses burn out, not WHAT lights them |
| **H-service** — a service-phase starver (TX-heavy sustained service starves IDLE0) | some activity that runs only during window service blocks CPU0; crashes require sustained service | crashes correlate with sustained service; lull-phase sessions never reset; [IDLE0] freezes only mid-service | **WEAKENED but not zero.** S9 breaks "sustained service" as the NECESSARY context (the reset landed in a lull) — but §9.2's back-computation shows the starvation window OPENED at the service→lull transition (≤ t≈114 s), and S8 died mid-service. Service is implicated in the fault's MATURATION in 2/3 expressions, but is not the mechanism's boundary condition |
| **H-phase-independent** — an intermittent kernel/interrupt-delivery fault on CPU0 that matures (accumulates or fires stochastically) REGARDLESS of which project phase executes | the starver is below project code; once matured, ticks keep running (IWDT fed — the hook precedes the increment in the tick ISR, §7.1), IDLE0 never schedules again, the TG0 stage-0 INT path is dead, and the 10 s stage-1 reset lands wherever the maturation deadline falls — lull (S9) or service (S8) | resets at ANY phase once [IDLE0] freezes; the PC always samples the tick-ISR chain; zero project frames; [LOOP]/[MEM] healthy; terminal-phase I2C/mojibake markers appear only in SOME expressions (S7 loud trio, S8 near-clean, S9 adjacent pair) — co-effects of the terminal phase, not of the triggering phase | **BEST FIT — retained.** The only hypothesis consistent with ALL THREE expressions + §7.1's triple + §7.4's one-family verdict. The IDLE0-starvation-on-CPU0 family remains the best fit; what S9 adds is that the family's clock is FAULT MATURATION, not executing phase |

Reconciliation with prior verdicts: §7.4's ONE-mechanism-family verdict STANDS
with its strongest confirmation yet (three PC samples, one chain — §8.5, now
including the dispatcher frame); §6.5's "sustained FULL-window service" axis is
AMENDED: sustained service was the common observation of sessions 7-8, not the
mechanism. The honest residual: what matures, and what event advances it, remain
unobserved (§9.5's limit).

### 9.4 THE CONVERGENCE VERDICT (§8.6 question 2) + the proximity question (question 3)

**Question 2 — what does a Wire-0 INVALID_STATE, on a line whose own bytes are
corrupt, immediately before a TWDT reset imply?** Readings ranked:

**(b) — kernel-contention wedge, INVALID_STATE as the wedge's task-level
observable: RANKED #1.** The claim: CPU0's wedge (whatever its inner structure)
degrades the KERNEL-MEDIATED completion path of a CPU1 transaction enough that
the transaction's 50 ms event wait expires non-DONE. The concrete, source-cited
linkage: the I2C transaction's completion event is delivered by
`xQueueSendFromISR` from the CPU1 I2C ISR (i2c_master.c:807-809) into the queue
loopTask blocks on (:588) — and FreeRTOS queue operations take the kernel
(portMUX) lock shared across cores; session 7 OBSERVED CPU0's own tick ISR
spinning in `spinlock_acquire` on exactly that lock class (§1.4). A CPU0 side
that holds/spins/misbehaves on the kernel lock makes CPU1's queue operations
stretch; the transaction ends TIMEOUT-status → INVALID_STATE (:726-728); and
the same terminal degradation touches the UART TX path (the mojibake). Evidence
FOR: the direct adjacency (:520 → :523 with only the ROM banner between); the
own-line mojibake (two corrupted-subsystem markers firing in the SAME second);
no successful read after (the reset intervened); the already-open starvation
window underneath an outwardly-healthy lull (§9.2 back-computation); and the
session-7 precedent of the kernel lock being the observed wedge site. Evidence
AGAINST (recorded, not explained away): the log RAN to :520 — dozens of
kernel-mediated operations (Serial prints, delay() expiries, the completed E32
handshake at :518) succeeded in the terminal window, so the contention must be
intermittent/marginal, not an absolute lock hold; and a plain bus-level NACK
(slave-side) producing the same line cannot be excluded from the log alone —
because the driver's INVALID_STATE hides WHICH non-DONE status it was
(ACK_ERROR vs TIMEOUT — §9.2). That hidden discriminator is exactly what the
round's [I2C] instrument (§9.6) exposes.

**(a) — driver-state corruption as co-effect: RANKED #2, weakened by this
round's source read.** §8.6's phrasing assumed the driver HAS a state that can
be corrupted into refusing transactions. The deployed stack has no such FSM
state (§9.2): INVALID_STATE is a completion status, not a refusal — so reading
(a) must shrink to "the corruption landed on the bus handle/status atom or the
transaction buffers" — possible, but unfalsifiable as stated, and it predicts
no specific instrument signature beyond (b)'s. Session-7's precedent (three
subsystems corrupted at once) keeps it alive; it no longer needs the I2C error
as its flagship, because the flagship error now has a better-explained reading.

**(c) — coincidence (one noise-class NACK that happened to land last): RANKED
#3 — the weakest, as §8.6 itself anticipated.** FOR: sessions 7/8 produced
recovered i2cWrite noise far from resets (2 occurrences in S8, both
read-followed, §6.4). AGAINST: the exact adjacency + own-line mojibake +
no-successful-read-after + this being the FIRST session where the error landed
adjacent — three facts §8.6 ordered carried as discriminating evidence, and
this round carries them still. Coincidence requires the one noise event of the
session to choose the one second that contains the reset.

What each reading predicts for the round-#3 instruments: (b) — the [I2C] line
at the failure shows a TIMEOUT-class probe result and/or the [IDLE0] counter
frozen while [LOOP] stays silent (CPU0 dead, CPU1 alive, bus healthy until the
kernel path degrades); (a) — anomalous [I2C] values inconsistent with any
single bus event, or persistent INVALID_STATE across consecutive transactions;
(c) — a clean ACK-class [I2C] result with [IDLE0] healthy to the reset
(would also force a re-read of the adjacency).

**Question 3 — why the corruption signature moved adjacent (S8's single
mojibake ~1070 lines pre-reset vs S9's ON the fatal line).** The
terminal-phase-degradation reading: the wedge's final window corrupts
in-flight UART TX bytes (each session shows exactly ONE mojibake line); WHERE
that one corrupted print falls relative to the reset is print-timing luck — S8's
corrupted print was an early transmit, S9's was the terminal error print
itself. The coincidence reading cannot be excluded at one sample per session,
and the honest bound is that bound: **n=1 per session is not a trend**; the
next session's mojibake count/position (if any) is the only data that can
promote either reading. No instrument is added for this question specifically —
the [IDLE0] instrument gives the terminal phase a 1 s-resolution clock
(§9.6), which retro-sharpens exactly this timing question at the next
occurrence.

### 9.5 RANKED ROOT CAUSE (with the honest limit)

**RANKED #1 (family unchanged, boundary condition amended): CPU0
scheduler/interrupt-delivery stall — kernel level, below project-code
visibility — whose MATURATION TIME, not the executing project phase, sets the
crash time. The stall is the same family all three expressions sampled in the
tick-ISR chain; session 9 adds that it matures across (or independent of)
phase, and that its terminal phase has a task-level observable signature
(b) — kernel-contention degradation of CPU1 completion paths (§9.4 reading
(b)).**

- Evidence FOR: §9.3's table (every shared constant holds across all three
  expressions); §9.2's negative result (the lull set joins the service set in
  total elimination — the starver is below project code in BOTH phases); the
  t≈114 s back-computation (the wedge matured at the service→lull transition
  while the visible phase was healthy); §8.5's three-PC one-chain record; the
  §9.4 (b) linkage giving the family its first task-level observable.
- Evidence AGAINST / THE HONEST LIMIT (the 01-25 §3 convention, invoked
  explicitly for the third consecutive round): the stall's INNER STRUCTURE
  remains unobserved — which interrupt path dies first, whether the kernel
  lock is held/spun/corrupted, what event advances the maturation. **No
  mechanism site in project code is named. This round's honest result is
  again elimination + blast radius + determinism-shape — elimination-only.**
  Per the honesty guard (two consecutive lever fixes bench-disconfirmed: the
  01-25 warm-up at session 8, the 01-28 Lever A at session 9) and the plan's
  own prohibition on a third speculative lever, **an elimination-only audit
  routes Task 2 to instrument-only (option b)** — no lever is named by this
  evidence, and inventing one would be the exact masking failure the guard
  exists to prevent.
- Eliminated across the rounds of this doc, cumulative: first-use PSRAM (§6.3),
  heap/stack exhaustion ([MEM], three sessions), unspaced bursts (S8/S9 ran
  spaced), sustained-service as boundary condition (§9.3), every
  project-code starver in the service set (§7.3) AND the lull set (§9.2).

### 9.6 DISCRIMINATOR MENU DISPOSITION + NAMED INSTRUMENTS (Task 2's evidence record)

Menu disposition (§8.6's three candidates, each verified implementable against
the deployed framework headers this round):

1. **CPU0 idle-observability — SHIP (the round's primary instrument).**
   `esp_register_freertos_idle_hook_for_cpu(cb, 0)` (public header
   `esp_system/include/esp_freertos_hooks.h:44`; the dispatch loop verified at
   `esp_system/freertos_hooks.c:41-59` — the callback runs in IDLE0's context,
   return-true = once per tick; the hook contract forbids blocking — the
   callback is ONE counter increment on a volatile static). Sampled every 1 s
   from the loop's existing 1 Hz block (src/main_balloon.cpp:302-333 region):
   delta 0 ⇒ IDLE0 ran zero ticks in that second ⇒ one latched line
   `[IDLE0] frozen ...` (the reannounceHoldLogged latch convention: one line
   per episode, re-armed by a healthy sample). This converts §7.1's
   "IDLE0 starvation visible only via the 10 s reset" into a 1 s-resolution
   observable that fires ≈9 s BEFORE a stage-1 reset — and pins §9.3's
   hypothesis table to data (H-phase-independent predicts [IDLE0] freezing in
   ANY phase; H-service/H-lull predict phase-correlated freezing).
2. **I2C-bus health at the Wire-0 failure site — SHIP in adapted form
   (bounded).** IDF 5.5.4 exposes NO bus error-flags accessor
   (`i2c_master_bus_get_error_flags` does not exist in this
   `driver/i2c_master.h`; verified by grep) — so the "driver FSM state at
   failure" of the §8.6 menu is replaced by the honest available observable: a
   bounded `i2c_master_probe(bus, addr, timeout)` (i2c_master.h:253) against
   the HAL-exported bus handle (`i2cBusHandle(0)`, public
   `cores/esp32/esp32-hal-i2c.h:44`) on the BMP280-invalid transition, latched
   one line per episode with poll/fail counters: `[I2C] ... probe 0x76 -> ...`
   (G-01-10). The probe's verdict discriminates §9.4's readings at the next
   occurrence: ACK-class (device NACKed — bus alive) vs TIMEOUT-class (bus
   wedged) vs probe-OK (transient). Bounded: one probe transaction ≈ ≤50 ms,
   failure-path only, latch-guarded.
3. **TWDT stage-0 subscription check — SHIP (boot-time one-shot).**
   `esp_task_wdt_status(xTaskGetIdleTaskHandle())` (public
   `esp_system/include/esp_task_wdt.h:170`; `INCLUDE_xTaskGetIdleTaskHandle = 1`
   verified in the deployed prebuilt FreeRTOSConfig.h:215) beside B1 in
   ImageTxManager::begin — expected ESP_OK per sdkconfig:2175-2179
   (IDLE0 subscribed, stage-0 INT @5 s PANIC=y). This turns §7.1's
   inferred-from-silence stage-0 death into a positive boot-time fact: once
   stage-0 is proven armed, a future SILENT rst:0x7 with no
   `Tasks currently running` print is affirmative evidence the stage-0
   interrupt path died — not a config assumption.

Named log lines 01-31 greps (the [MEM]/B1/B2 naming convention, each with the
G-01-10 citation and removal condition in-source):
- `[IDLE0]` — latched freeze line (1 Hz sample; episode-latched) — G-01-10;
  removal: with [MEM]/B1/B2 after G-01-10 closes on bench evidence.
- `[I2C]` — latched failure line (probe verdict + counters) — G-01-10; removal:
  same clause.
- `[TWDT]` — boot-time one-shot subscription line — G-01-10; removal: same
  clause.
- Retained unchanged: `[MEM]`, `[BOOT]` (B1), `[LOOP]` (B2), the round-#10
  discriminator lines, the D2 receipt-ever flag.

**The lever: NONE. §9 names no mechanism site** (§9.5's elimination-only
limit) — **the round ships instrumentation only (option b).** Untouched by
explicit disposition: any pacing/quiet-gate/D-05/D-07 surface (protected; and
§9.2 shows the lull's radio activity completed cleanly), any WDT-config lever
(the §7.6 rejection stands; stage-0's death is now an instrumented observable,
not something to re-time), the wire format (unchanged; the harness stays the
tripwire).

### FIX SHAPE SELECTED (round #13 Task 2 checkpoint record)

Checkpoint Task 2 (fix-shape selection, gate `blocking`) auto-resolved under
`auto_advance: true` (config.json) per the 01-23/01-25/01-26/01-28 convention —
selection recorded as PENDING end-of-phase operator confirmation (joins the
FOUR existing pending confirmations). Selected: **option (b) — instrument-only.**
The evidence that selected it: §9.5's honest limit is elimination-only for the
THIRD consecutive round (no §9-named mechanism site exists, which option (a)
requires by this checkpoint's own acceptance criteria), and the plan's honesty
guard is explicit that two consecutive bench-disconfirmed levers make a third
speculative lever evidence-DISHONEST — while §9.6 ships three verified,
bounded, latch-guarded instruments ([IDLE0] 1 s-resolution CPU0 liveness; [I2C]
probe-verdict at the failure site; [TWDT] stage-0 boot confirmation) that make
01-31's bench session capable of CLASSIFYING the starver rather than guessing
at it. Concretely: the three instruments above, each with its named log line,
G-01-10 citation, and removal condition; NO lever, NO pacing/quiet-gate/
wire-format/WDT-config change (the rejected candidates of §7.6 and §9.6 stand);
Lever A, B1/B2, [MEM], and the round-#10 discriminator lines untouched.

## 10 — SESSION-10 RECURRENCE (01-31 bench, 2026-08-28): the FOURTH expression — an IDLE0 STACK-CANARY PANIC mid-window-service, with a full dump, and the round-#13 instruments ANSWERING: starvation NEGATIVE, [TWDT] NOT_FOUND (the §7 WDT chain under re-examination), [I2C] coverage gap

### 10.1 Provenance

- Consoles: balloon4.log (1,768 lines) / base4.log (740 lines), repo root — LOG
  NAMING DEVIATION per the 01-24 convention (plan requested balloon15.log /
  base10.log; operator names retained, never renamed).
- Firmware: round #13 (01-30 instrument package), build banner Aug 28 2026,
  balloon console `ELF file SHA256: acce78241` (balloon4.log:1478) matching the
  pre-flight recorded round-#13 build SHA — verified before interpretation.
- Both boards flashed with the same image (pre-flight both-boards rule).
- Boots: `ImageTx: [BOOT] reset-cause: POWERON (G-01-10)` :104 (boot 1) and
  `ImageTx: [BOOT] reset-cause: PANIC (G-01-10)` :1576 (boot 2) — B1's first
  PANIC reading of the campaign, independently naming the crash class.

### 10.2 Event map

1. D1 CLASS 1 (first-post-boot push) SURVIVED a third consecutive session:
   CAPTURE_NOW seq=2 (base4.log:46) ACKed (:52) → image 40 thumb COMPLETE 8/8
   (base4.log:67-68) and full COMPLETE 31/31 (:156-157). The session-7 killer
   pattern is dead at this boot on round-#13 firmware.
2. Series A ran (seq 7/8/9, base4.log:170-:192): seq=7 ACKed (:176); seq=8
   ACKed after two retries (:199/:207/:208); seq=9 held ONCE by the 01-18 quiet
   gate (:225 `command seq=9 transmit held - inbound chunk stream active`),
   retries 1/3-3/3 at :203/:213/:228 (two burned BEFORE the hold — the retry
   counter advances while the chunk stream is busy), then the FORBIDDEN
   terminal `Command seq=9 timeout after 3 retries` (:250). Series-A primary
   clause FAILED on the third capture's command (third session with this
   terminal: sessions 5, 7, 10).
3. Verdicts 6 of 8 finalize lines COMPLETE — the best series-A partial ever:
   image 40 2/2 COMPLETE; image 42 2/2 COMPLETE (base4.log:269-:270, :425-:426);
   image 41 0/2 INCOMPLETE — thumb `6/8 chunks after 3 passes` (thumbnail push
   stalled :539-540), full `0/31 chunks after 3 passes` (:484-485) after the
   balloon-side depth-3 eviction (§10.4); image 43 thumb COMPLETE 7/7
   (:588-589), full INCOMPLETE 14/119 (:712-713) CRASH-CAUSED.
4. CIF step: SET_RESOLUTION seq=26 ACKed (:558-560) → image 43 captured at
   CIF-class sizing (full 23800 B / 119 chunks vs the QVGA 6136 B / 31 chunks;
   balloon4.log:1291) with thumb COMPLETE. Zero FB-OVF lines in EITHER console.
   QVGA restore (wire 6) NOT reached.
5. CRASH: image 43's FULL window armed (balloon4.log:1296 chunks 0..15),
   base received chunks 0..1, retransmit pass re-armed chunks 2..15 (:1380) —
   14-chunk window — served 12/14 (`window chunk(image 43 kind 1, 12/14, 200 B)
   sent` :1427), then :1428 `Guru Meditation Error: Core  0 panic'ed (Unhandled
   debug exception)` → :1429 `Debug exception reason: Stack canary watchpoint
   triggered (IDLE0)` — FULL panic dump this time (register dump, EXCVADDR
   0x00000000, EXCCAUSE 0x1; faulting PC ROM `memset` writing 36 bytes at
   SP+0x1c inside IDLE0's stack region; backtrace CORRUPTED `|<-CORRUPTED`, no
   project frames decodable) → :1478 ELF SHA banner → :1483
   `rst:0xc (RTC_SW_CPU_RST)` → boot 2 clean (System ready :1606; NVS restored
   next capture ID 44), session continued (beacons accepted seq 0..11), no
   second crash, operator ended the capture.
6. Crash context: the TX-HEAVIEST stretch of the five-session campaign —
   `E32: AUX-low missed after complete write` ×81 whole-console, 217 B chunks
   back-to-back, the largest image of the phase (119 chunks) mid-window.

### 10.3 The round-#13 instruments ANSWER (session discriminator readings)

- **[IDLE0] tick counter — ARMED and NEGATIVE for starvation.**
  `[IDLE0] hook cpu0 registered=0 (G-01-10)` at :33 and :1505 is a PRINTF BUG,
  not a registration failure: `esp_register_freertos_idle_hook_for_cpu` returns
  `esp_err_t` (ESP_OK = 0); assigning it to `bool` prints 0 on SUCCESS
  (esp_system/include/esp_freertos_hooks.h:44 verified on-disk). The hook WAS
  armed at both boots and the counter ran. ZERO `[IDLE0] frozen` lines across
  the WHOLE console, both boots, through every phase including the fatal
  window: IDLE0 executed at least one tick in every 1 s sample from boot to the
  crash instant. STARVATION IS DISCONFIRMED as the session-10 crash mechanism —
  the instrument's designed purpose, delivered. The §7 'IDLE0 starved ≥10 s'
  chain CANNOT describe this crash: the canary fired while IDLE0 was being
  scheduled and run.
- **[TWDT] — ESP_ERR_NOT_FOUND, the §7 WDT chain is UNDER RE-EXAMINATION.**
  `ImageTx: [TWDT] idle0 wdt status=ESP_ERR_NOT_FOUND (G-01-10)` at BOTH boots
  (:105, :1577). The instrument calls `esp_task_wdt_status(
  xTaskGetIdleTaskHandle())` (image_tx_manager.cpp:172) from setup() context,
  which runs on CPU1 (loopTask pinned). Two readings, NOT discriminated by this
  console: (a) `xTaskGetIdleTaskHandle()` returned the CALLING core's idle
  handle (IDLE1 — genuinely unsubscribed; config subscribes CPU0 only) — an
  instrument HANDLE bug and the §7 premise merely untested; (b) it returned the
  CPU0 handle and IDLE0 is genuinely NOT subscribed to the TWDT in the deployed
  image — falsifying §7's load-bearing 'only IDLE0 feeds the TWDT' premise and
  RE-OPENING the sessions-8/9 TG0WDT_SYS_RST attribution (the 'TWDT stage-1
  backstop of IDLE0 starvation' reading would need a new reset source). The
  §7 artifact-side sdkconfig reading is not re-verifiable from the retained
  build dir (no sdkconfig artifact in .pio/build/esp32-s3-balloon/). Round #14
  MUST fix the instrument to `xTaskGetIdleTaskHandleForCPU(0)` explicitly and
  re-verify the deployed config before any further WDT-chain reasoning rides on
  it. NOTE: session 10's verdict does NOT depend on this question — the canary
  panic is direct evidence, not a silent-reset inference.
- **[I2C] — did NOT fire at its session trigger opportunity (coverage gap).**
  The console's single i2cWrite error (:1093, §10.4) produced ZERO `[I2C]`
  lines. The instrument is wired to the project-side invalid-READING transition
  (main_balloon.cpp:836-866, i2c_master_probe on the BMP280-invalid path);
  the failure occurred on the HAL WRITE path (esp32-hal-i2c-ng.c:275
  i2cWrite) whose error return evidently never surfaced as an invalid-reading
  event. Instrument-coverage gap for round #14: wire the probe to the
  write-failure return too, or verify the HAL swallows write errors.
- **B1 [BOOT] — worked** (POWERON :104 / PANIC :1576 — quoted above).
- **B2 [LOOP] — zero fires** whole console; loopTask healthy throughout
  ([MEM] `stackHW=5772` constant both boots; new-low lines throughout with
  trivial heap drift 8552964→8.55 M-class, psram constant). The breached stack
  is IDLE0's — a DIFFERENT task than any [MEM] monitors.
- **[MEM] — healthy** at every sample; exhaustion ruled out a fourth session.

### 10.4 i2cWrite/mojibake occurrence — the non-fatal class RESTORED

ONE i2cWrite ESP_ERR_INVALID_STATE whole-console: :1093
`[259068][E][esp32-∩┐╜al-i2c-ng.c:275] i2cWrite(): i2c_master_transmit failed:
[259] ESP_ERR_INVALID_STATE` — carrying the session-7/9 corruption-class
mojibake bytes ON the line (the corrected raw-byte grep
`grep -c $'\xe2\x88\xa9\xe2\x94\x90\xe2\x95\x9c'` = 1; the UTF-8-replacement-
char grep = 0 — the twice-documented trap; the corrupted span replaces the `h`
of `hal`). Classification: the KNOWN NON-FATAL class is restored — successful
BMP280 reads follow immediately (:1095-:1096) and the crash is 335 lines later.
Session 9's adjacent-to-reset pattern did NOT recur. t=259068 ms is also the
console's only t-stamp (IDF E/W log format); the crash time is bounded only by
[BCN] seq=49 (:1094, t≈259 s) and the last pre-crash beacon seq=65 (:1416).

### 10.5 What the FOURTH expression discriminates

- Expression census: session 7 crash 1 = canary on **IDLE1** (balloon.log:314)
  with full dump; session 7 crash 2 + session 8 = rst:0x7 TG0WDT silent (Saved
  PC tick_hook int_wdt.c:111 in session 8); session 9 = rst:0x7 TG0WDT silent
  (Saved PC esp_vApplicationTickHook freertos_hooks.c:34) adjacent to
  I2C+mojibake; **session 10 = canary on IDLE0 with full dump** — and the tick
  instrument proves IDLE0 was ALIVE (no ≥1 s starvation) up to the instant.
- The [IDLE0] negative splits the family: sessions 8/9's TWDT expression
  implied starvation; session 10's canary fires with IDLE0 alive and scheduled.
  The unifying candidate (LABELED HYPOTHESIS, not established): a CPU0-side
  fault family whose visible signature depends on where the damage lands —
  a stack-capacity breach of an IDLE task when IRQ frames land during heavy
  TX (session 10: canary; on Xtensa, level-1 interrupt frames push onto the
  INTERRUPTED task's stack, and CPU0 hosts the WiFi/system ISRs; the crash
  landed in the campaign's TX-heaviest stretch), versus a scheduler/tick-chain
  wedge surfacing as the watchdog (sessions 8/9) and session 7's IDLE1 canary
  as the same family touching CPU1's idle context. H-lull stays REJECTED
  (session 10 crashed in the service phase, TX-heavy). H-phase-independent
  maturation (§9.3) is WEAKENED but not dead: session 10 correlates crash time
  with LOAD (heaviest burst) rather than a fixed maturation clock.
- Round #14 routing (named, cheapest-first): (1) **[STACK] instrument** —
  `uxTaskGetStackHighWaterMark(NULL)` logged from the existing [IDLE0] idle
  hook at 1 Hz with a new-low latch (the [MEM] pattern applied to IDLE0) —
  one session CONFIRMS or REFUTES the stack-marginality hypothesis; (2) fix
  the [TWDT] handle (ForCPU(0)) and the [IDLE0] registered printf (print
  `err == ESP_OK`); (3) extend the [I2C] probe to the write-failure path;
  (4) optional A/B: a build with the idle hook unregistered to rule the
  round-#13 hook dispatch in or out (the hook body is a single increment, but
  the question is cheap to kill); (5) NO lever until [STACK] answers — three
  consecutive elimination-only rounds plus a disconfirmed-lever history make
  lever-speculation ahead of the watermark evidence dishonest by the standing
  guard.
- No bench claims ride this section beyond the quoted lines; the D1 gap stays
  OPEN with the FOURTH expression recorded (01-UAT.md G-01-10, WINDOWS 15).

## 11 — ROUND-#14 IMPLEMENTATION RECORD (01-32, 2026-08-28): the §10.5 instrument package wired — [STACK] watermark, [TWDT] ForCPU(0) fix, [IDLE0] printf fix, [I2C] write-path disposition, the A/B hook guard

### 11.1 PROVENANCE

- Code commit: `fix(01-32): D1 debug round #4 — the [STACK]/[TWDT]/[I2C]
  instrument package per debug doc §10.5` = 7d3abbc (the ledger/docs commit
  follows separately).
- Touched files (line numbers at this commit):
  - `src/main_balloon.cpp` — the [STACK] instrument: `s_idle0StackMinWords`
    latch + the throttled in-hook sample (:140-166), the fixed registration
    print + the `G01_D1_IDLE_HOOK_DISABLED` guard (:261-281), the 1 Hz
    new-low print block (:412-433); the `<freertos/task.h>` instrument
    include (:44). The round-#13 [IDLE0] counter, frozen latch, and the
    [I2C] reading-path probe are BYTE-IDENTICAL.
  - `src/image_tx_manager.cpp` — the [TWDT] handle fix (:170-172 and the
    ROUND-#14 comment block :173-185). B1, B2, [MEM], the D2 receipt-ever
    flag, and the round-#10 discriminator lines untouched.
- IMPLEMENTATION-ONLY — **NO D1 lever shipped** (§10.5 item 5's standing
  guard: three elimination-only rounds plus two bench-disconfirmed levers
  make lever-speculation ahead of the watermark evidence dishonest). The
  ONLY code changes are the four instrument items + the A/B macro. The D1
  fix decision is explicitly DEFERRED until the [STACK] watermark answers
  at 01-34.
- Builds: `pio run -e esp32-s3-balloon -e esp32-s3-basestation` 2/2 SUCCESS;
  `node scripts/verify_protocol_roundtrip.mjs` exit 0 (all retained
  surfaces byte-intact — Lever A, B1 [BOOT], B2 [LOOP], [MEM], the
  round-#10 lines, the D2 flag, the round-#13 instruments, the wire
  format). The A/B guard also compiles clean BOTH ways: the balloon env
  built SUCCESS with `-DG01_D1_IDLE_HOOK_DISABLED=1` and again without it
  (the default build is behavior-identical to round #13).
- Instrument-convention arithmetic: the REMOVAL CONDITION citation count
  across the two touched files grows 8 → 11 (+3 in main_balloon.cpp — the
  [STACK] hook-side construct, the [STACK] 1 Hz print-side construct, the
  A/B guard; image_tx_manager.cpp unchanged at 4 — the [TWDT] fix modifies
  an existing instrument whose REMOVAL CONDITION already stands in its
  comment).

### 11.2 WDT-CONFIG RE-DERIVATION (the deployed-config facts, from the pinned framework)

The §7.1 artifact-side reading is re-derived from the PINNED FRAMEWORK's
sdkconfig — the retained `.pio/build/esp32-s3-balloon/` carries no
sdkconfig artifact (the §10.3 finding), and this project's pioarduino
arduino-esp32 3.3.9 install keeps the prebuilt SDK config at:

- **Exact path:** `C:\Users\Amp\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\sdkconfig`
  (the plan-context's guessed `framework-arduinoespressif32/tools/sdk/esp32s3/`
  directory does not exist in this package layout — the
  `framework-arduinoespressif32-libs` package is the pinned SDK tree).

The [TWDT]-relevant lines (sdkconfig:2175-2180):

| Config | Value |
|---|---|
| `CONFIG_ESP_TASK_WDT_EN` | y (:2175) |
| `CONFIG_ESP_TASK_WDT_INIT` | y (:2176) — the TWDT is initialized at boot |
| `CONFIG_ESP_TASK_WDT_PANIC` | y (:2177) — stage-2 panic configured |
| `CONFIG_ESP_TASK_WDT_TIMEOUT_S` | 5 (:2178) |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` | **y** (:2179) — IDLE0 IS TWDT-subscribed by config |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` | **not set** (:2180) — IDLE1 NOT subscribed |

Reconciliation against §7.1: identical values — §7.1's table (:478) already
recorded `sdkconfig:2175-2179 (EN/INIT/PANIC=y, TIMEOUT_S=5,
CHECK_IDLE_TASK_CPU0=y; :2180 CHECK_IDLE_TASK_CPU1 not set)`; the 10 s
stage-1 period is the IMPLEMENTATION reading (RESET_SYSTEM @10 s,
task_wdt_impl_timergroup.c:124-126), not a second config value. Nothing in
the re-derivation disturbs §7.1's topology; what it adds is the decisive
prediction for §10.3's two-reading question:

**The config PREDICTS reading (a) — the wrong-handle instrument bug.** With
`CHECK_IDLE_TASK_CPU0=y` and `INIT=y`, `esp_task_wdt_status()` on the CPU0
idle handle is expected to return **ESP_OK**. Session 10's
`ESP_ERR_NOT_FOUND` is then exactly what the config predicts for the handle
the buggy call actually sampled — IDLE1, the one idle task (:2180) that is
NOT subscribed: `xTaskGetIdleTaskHandle()` from CPU1 setup() context
returned IDLE1. Reading (b) (IDLE0 genuinely unsubscribed) would contradict
the pinned config and falsify §7's 'only IDLE0 feeds the TWDT' premise,
re-opening the sessions-8/9 TG0WDT attribution. **The live boot line
remains the deciding evidence** — 01-34 reads
`ImageTx: [TWDT] idle0 wdt status=ESP_OK (G-01-10)` (expected) or the
NOT_FOUND that re-opens the attribution. Until that line is read, no
WDT-chain conclusion changes.

Handle-API finding (the plan's stop-condition check): the pinned core does
NOT declare `xTaskGetIdleTaskHandleForCPU` in task.h — it is declared in
`freertos/esp_additions/include/freertos/idf_additions.h:645` as a
deprecated static inline forwarding to `xTaskGetIdleTaskHandleForCore`
(:144; deprecation notice names ESP-IDF 6.0 removal). `idf_additions.h` is
included by FreeRTOS.h (:1533), so the call compiles and links on both
targets (verified by the builds); semantics are identical (same handle
returned). The plan's `xTaskGetIdleTaskHandleForCPU(0)` call is used
verbatim; the declaration-site finding is recorded here rather than
substituted silently.

### 11.3 THE 01-34 DISCRIMINATOR TABLE (every new/changed log line)

| Line (exact format string) | Print site | Cadence / trigger | Predicted signature | Removal condition |
|---|---|---|---|---|
| `[IDLE0] stack watermark %u words free (new low, t=%lu ms) (G-01-10)` | main_balloon.cpp:431, 1 Hz loopTask block | on a new running-minimum low; first print = the boot baseline; NEVER per-sample, NEVER from the hook (sampled in-hook every 1024 idle ticks, main_balloon.cpp:158-163) | stack-capacity hypothesis CONFIRMED by: new lows accelerating toward 0 words under heavy TX ahead of a canary panic (the baseline-to-new-low trajectory IS the deliverable; the value at the crash instant or the session minimum is the reading). Wedge hypothesis: a healthy, constant margin at the crash instant REFUTES stack capacity | strips WITH the [MEM]/B1/B2 family after G-01-10 closes on bench evidence |
| `[IDLE0] hook cpu0 registered=%d (G-01-10)` | main_balloon.cpp:278, boot one-shot | once per boot, before subsystem init | **registered=1 expected** (ESP_OK). registered=0 would now be a GENUINE registration failure (esp_freertos_hooks.h:44 return compared against ESP_OK) — a new fact, not the printf bug | same clause |
| `[IDLE0] hook cpu0 DISABLED for A/B (G-01-10)` | main_balloon.cpp:280, boot one-shot | ONLY in the `-DG01_D1_IDLE_HOOK_DISABLED=1` A/B image — identifies the arm on console | marks the A/B image; the two arms are distinguishable without console-side guesswork | same clause |
| `ImageTx: [TWDT] idle0 wdt status=%s (G-01-10)` | image_tx_manager.cpp:170-172, boot one-shot (format unchanged) | once per boot, ImageTxManager::begin | **ESP_OK expected** under the §11.2 config reading (CPU0 subscribed; the fix removes the core ambiguity). `ESP_ERR_NOT_FOUND` with the ForCPU(0) handle would falsify §7's premise and re-open the sessions-8/9 TG0WDT attribution | unchanged (in the :167 comment) |
| `[I2C] write-path …` — **NOT added** | — | — | branch (b): the write path provably swallows errors end-to-end (§11.3.1); the HAL's own `[E][esp32-hal-i2c-ng.c:275] i2cWrite()` line remains the write-path observable | — |

Retained verbatim (01-34 may also grep, all unchanged): `[MEM]` new-low
lines, `[BOOT] reset-cause` (B1), `[LOOP] slow pass gap` (B2), the
`[IDLE0] frozen` latch line, the `[I2C] BMP280 invalid … probe 0x76`
reading-path probe, the round-#10 discriminator lines (budget re-arm,
class-5 eviction labels, deadline release), the D2 receipt-ever flag.

#### 11.3.1 The [I2C] write-path disposition (§10.5 item 3, branch b — the coverage answer)

The session-10 gap (the instrument wired only to the invalid-READING path)
is closed IN WRITING as a swallowing finding, not with a speculative
poller. The full write chain was read from the pinned sources:

1. `Adafruit_SSD1306::display()` (`.pio/libdeps/esp32-s3-balloon/Adafruit
   SSD1306/Adafruit_SSD1306.cpp`) returns **void** and DISCARDS the
   `wire->endTransmission()` return at every call site (:401, :427, :435,
   :1044, :1052).
2. `TwoWire` (framework-arduinoespressif32 3.3.9,
   `libraries/Wire/src/Wire.h`) retains NO error state — the class has no
   `lastError()` accessor and no error member (full class read; the
   older-core `lastError()` API does not exist here). Its
   `endTransmission()` (`Wire.cpp:445-478`) translates the esp_err_t to
   the Arduino return codes {0, 2, 5, 4} and the return value is the ONLY
   surface — discarded by (1).
3. `i2cWrite` (`cores/esp32/esp32-hal-i2c-ng.c`, the :275 log site) logs
   `i2c_master_transmit failed: [%d] %s` at log_e and returns the
   esp_err_t to (2) — the console [E] line session 10 saw at balloon4.log:1093
   IS the HAL write-path observable.

Conclusion: nothing surfaces to project code; the honest options were the
HAL's existing [E] line (already in every console) or a speculative
write-failure poller — the plan forbids the poller, so the finding stands
as the coverage answer and the reading-path instrument stays untouched.

### 11.4 THE A/B INSTRUCTIONS (§10.5 item 4 — producing the hook-unregistered image)

- Build: add `-DG01_D1_IDLE_HOOK_DISABLED=1` to the `esp32-s3-balloon`
  env's `build_flags` in `platformio.ini` (or one-shot via
  `PLATFORMIO_BUILD_FLAGS="-DG01_D1_IDLE_HOOK_DISABLED=1" pio run -e
  esp32-s3-balloon`), rebuild, flash per the bench both-boards rule (the
  base image is unchanged — only the balloon needs the A/B arm).
- Verify the arm on console: boot 1 must print
  `[IDLE0] hook cpu0 DISABLED for A/B (G-01-10)` (and NO
  `[IDLE0] frozen`/`stack watermark` lines — the counter stops advancing).
- When to reach for it: if 01-34's [STACK] watermark shows a HEALTHY
  constant margin through a crash, the stack-capacity reading is refuted
  and the round-#13 hook-dispatch question (the hook executes once per
  tick inside IDLE0) rises in rank — the A/B image then discriminates
  hook-dispatch-in-family vs not in one session. If the watermark shows
  the accelerating-new-low signature, the stack answer has priority and
  the A/B stays on the shelf.
- Revert the build_flags line after the A/B session (the default build
  must keep the hook registered).

### 11.5 SELECTION-PROVENANCE RECORD (the sixth pending operator confirmation)

Per the 01-23/01-25/01-26/01-28/01-30 convention: the round-#14 fix-shape
selection — the §10.5 package VERBATIM (option b-instrument + fix lineage:
four instrument items + the A/B macro, no lever) — was pre-named by debug
doc §10.5 (lines 1452-1463) and STATE.md's round-#14 routing, and
auto-advanced under `workflow.auto_advance: true` (config.json). Recorded
as PENDING end-of-phase operator confirmation, the SIXTH of the campaign
(after 01-23 fix-all, 01-25 option-a+option-b, 01-26 option-a, 01-28
(a)+(b), 01-30 option-b).

No bench claims ride §11; the D1 gap stays OPEN with the FOURTH expression
pending 01-34's hardware session (01-UAT.md G-01-10, WINDOWS 15).

### 11.6 SESSION-11 READING (01-34 bench, 2026-08-29): the §11.3 instruments ANSWER — zero crashes, no fifth expression, [TWDT] ESP_OK, [STACK] refutes stack capacity, [I2C] coverage closed by absence — NO §12 (nothing crashed; this subsection IS the session's debug-doc record)

Provenance: consoles balloon5.log (1,068 lines) / base5.log (524 lines), repo
root, written 2026-08-29 ~01:05 — the operator-retained names happen to match
the plan's request this time (the #5 name-collision hazard noted in the 01-34
artifacts did not bite: the session-#5-of-2026-08-25 files were balloon5.log/
base5.log too and were OVERWRITTEN by this session's captures — the 01-24
convention records the operator's retained names either way). Firmware =
round #14 (01-32 instruments + 01-33 lever), build banner
`Build: Aug 28 2026 23:13:40` (balloon5.log:24); balloon ELF SHA256
`32d31f270cb81b52731cf77501c38980de3ac36ad398109cf1da1c0815e5043a` and base
`ce657093cb0ae6c7e6227c72ea06a30d1078ffa5a97b48d47ec244926c231718` — both
re-verified on-disk against `.pio/build/*/firmware.elf` by the 01-34 executor
(matching the pre-flight record at HEAD 70191a9). The console-side ELF-SHA
banner is ABSENT BY DESIGN — it prints only from the panic handler, and
nothing panicked; provenance rides the on-disk SHA match + the build banner.
Single boot: `[BOOT] reset-cause: POWERON` (balloon5.log:104) is the only
boot; no second boot exists to read.

THE INSTRUMENT ANSWERS (each against its §11.3 predicted signature):

- **[TWDT] = ESP_OK — the two-reading question is RESOLVED.**
  `ImageTx: [TWDT] idle0 wdt status=ESP_OK (G-01-10)` (balloon5.log:105, the
  only boot). Reading (a) confirmed exactly as §11.2's config derivation
  predicted: IDLE0 genuinely IS TWDT-subscribed (CHECK_IDLE_TASK_CPU0=y);
  §7's 'only IDLE0 feeds the TWDT' premise STANDS; the sessions-8/9 TG0WDT
  attribution survives; session 10's ESP_ERR_NOT_FOUND was the wrong-handle
  instrument bug (it sampled IDLE1). No premise re-opens.
- **[IDLE0] registered=1** (balloon5.log:33) — the printf fix proven; the
  hook is genuinely armed (ESP_OK compared, not bool-coerced).
- **[STACK] — the stack-capacity hypothesis is REFUTED per the §11.3
  pre-written rule.** The boot baseline is the ONLY watermark line in the
  whole console: `[IDLE0] stack watermark 240 words free (new low, t=2392 ms)
  (G-01-10)` (balloon5.log:138) — zero further new-lows across the entire
  session INCLUDING every multi-window FULL service. IDLE0's stack never
  dropped below 240 words (960 B) free at any 1024-idle-tick sample. The
  accelerating-toward-0 CONFIRM signature never appeared. Honest caveats,
  recorded rather than smoothed: (1) no crash occurred, so there is no
  crash-instant value to read — the refutation rides the healthy constant
  margin through the session's TX-heaviest stretch, exactly the wedge-side
  reading §11.3 wrote down in advance; (2) this session's TX-heaviest
  stretch was lighter than session 10's fatal one (AUX-low-missed ×40 vs
  ×81; largest SERVED image 41 chunks vs 119) — the refutation is as strong
  as the exercised load, and the load ceiling is named. Per §11.4's own
  instruction, the healthy-margin reading PROMOTES the round-#13
  hook-dispatch question: the A/B hook-unregistered image
  (G01_D1_IDLE_HOOK_DISABLED, the §11.4 recipe) is the next discriminator —
  the wedge family (scheduler/tick-chain, sessions 7/8/9) re-ranks to the
  top of the remaining candidate set.
- **[I2C] — the coverage answer lands by absence.** Zero `[I2C]` lines AND
  zero i2cWrite errors whole-console (both greps 0): no write-failure
  episode occurred, so the §11.3.1 branch-(b) swallowing disposition (closed
  in writing at 01-32) stands as the coverage answer — §10.3's gap is
  closed on evidence, not coverage-silence.
- **B1/B2/[MEM]**: B1 POWERON (:104); B2 `[LOOP]` zero fires (loopTask
  healthy throughout); `[MEM]` ×40 healthy (loopTask stackHW 5600-5804,
  heap floor 8,310,804 — e.g. balloon5.log:807 heap=8311180 minHeap=8310804);
  exhaustion ruled out a fifth session.

CRASH-SIGNATURE CENSUS (whole balloon5.log, executor-grepped): rst:0x7 = 0;
rst:0xc = 0; Guru Meditation = 0; stack canary = 0; i2cWrite = 0; mojibake
(corrected raw-byte method `grep -c $'\xe2\x88\xa9\xe2\x94\x90\xe2\x95\x9c'`)
= 0 (the UTF-8-replacement-char grep also 0). NO FIFTH EXPRESSION — the
first bench session of the D1 campaign with zero crash signatures
end-to-end. NO §12 is appended: §12 exists for crashes, and none occurred.

What the ladder did NOT exercise (recorded so the absence is not passed off
as a pass): D1 class 1 (first-post-boot push) PASSED a fourth consecutive
session — image 44, the only first capture, went thumb 6/6 + full 41/41 both
COMPLETE (base5.log:99/:275) across 3 distinct windows / 4 armings
(balloon5.log:397/:516+:526/:597 area) with zero crashes. Class 2's
>=7-window SVGA bar was NOT met: image 45 (SET_RESOLUTION wire 10, re-init
6 -> 11, balloon5.log:781-:782, zero FB-OVF; SVGA-class 32,850 B / 165
chunks) served ZERO full chunks — starved by the G-01-7 lever's honest-
reject path (the NEW over-rejection mode, full record in 01-UAT.md G-01-7
and WINDOWS entry 3). Class 3's >=3-minute dwell was NOT met: ~50 s TX-light
between image 44's completion and image 45's capture (balloon [BCN] seq=27
at :641 -> seq=37 at :767, the locked 5 s cadence). The instruments'
ANSWERS are the round's deliverable and they are in; the ladder's heavy
tail (sustained >=7-window SVGA service, the 3-minute dwell, series A) is
owed a re-run on the next round's firmware.

ROUTING (recorded, not speculated): §10.5's item set is ANSWERED — the
[TWDT] question settled (ESP_OK), the [STACK] question settled (REFUTED),
the [I2C] question settled (branch b by absence). D1's root cause remains
open in the WEDGE FAMILY (sessions 7/8/9's scheduler/tick-chain class);
round #15's cheapest discriminator is the §11.4 A/B hook-unregistered build,
one session, with the ladder's heavy tail re-run on the same firmware. The
G-01-7 over-rejection fix (capacity-aware supersede admission) is the
companion lever round — the two share the next bench session.

## 12 — SESSION-12 (balloon8+balloon9 bench, 2026-08-30 morning): the SD-store-era recurrence — silent TG0WDT crash-loop, the delivery livelock, ONE int-wdt PANIC ON CPU1 WITH DUMP, the Saved-PC census, and the crash-loop gate decision

### 12.1 Provenance

- `balloon8.log` (3,141 lines, 11 boots), written 2026-08-30 09:58, build
  banner `Build: Aug 29 2026 13:36:40` (balloon8.log:24) — the image-transfer
  receipt rework content, now committed as `e73d051`. `balloon9.log` (2,455
  lines, 7 boots), written 11:43, banner `Build: Aug 30 2026 10:41:32` — the
  same content plus the [STACK] loop-side sampling revision (the watermark
  line reads `new low, loop-side sample` from balloon9 on). Companion
  consoles `base8.log`/`base9.log`.
- `balloon6.log` (2026-08-29 09:02, rst:0x7 ×10) and `balloon7.log` (17:13,
  rst:0x7 ×1) are the recurrence's FIRST sessions — recorded here by census
  only (the deep record starts at balloon8). Session 11 (balloon5, §11.6,
  zero crashes) predates the 02.5 SD-store code (12d85a1..8c41585, committed
  08-29 03:09–04:47); the correlation is recorded, NOT read as causation —
  §12.5 shows the family predates the store.
- ELF provenance is banner+content only: both builds' ELFs were replaced
  on-disk by later builds before this record was written.

### 12.2 Event maps

- **balloon8**: POWERON boot + 10 reboots, each living minutes (setup
  `Uptime` ≈ 2.5 s in every banner — the deaths are NOT at fixed uptime).
  The reboots re-announce `FULL manifest(image 51, 23151 B, 104 chunks)`
  (balloon8.log:582+:1066+:1283+:1520+:1771+:1976) and die during window
  service of the card-served FULL chunks. The era's ONE non-silent death:
  `Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)`
  (balloon8.log:837) MID-WINDOW (`window chunk(image 51 kind 1, 9/16)` at
  :835), with a FULL DUMP — PC `esp_cpu_wait_for_intr` (cpu.c:64, the IDLE1
  wait loop), PS INTLEVEL nibble nonzero, `EXCCAUSE 6` read as a stale
  register of the interrupted context, the header line authoritative.
  Reset as `rst:0xc (RTC_SW_CPU_RST)` — the panic path's own restart.
- **balloon9**: boot A runs CLEAN ~5 min (6,021 loop counts, ZERO image
  activity) then dies silently (rst at balloon9.log:1516). Boots B–G are
  the DETERMINISTIC CRASH-LOOP: `Uptime: 2537 ms` in every banner (B: 2498),
  each death after the thumbnail-manifest send — B sent thumb chunks 1–7/8
  then died before the 177 B tail; C sent 7/8 + 8/8 (base received and
  finalized COMPLETE, base9.log:145-148) and died before processing the
  verdict; D–G died between `manifest(image 52 ...) sent` and the first
  chunk print.
- **The delivery livelock (base9.log)**: the base holds thumbnail 52
  finalized COMPLETE and persisted (:147-148), keeps queuing
  IMAGE_FULL_REQUEST (seq 24/25/26, :164/:189/:212), and answers every boot's
  re-manifest with `COMPLETE row — ignored, COMPLETE reply sent`
  (:171/:198/:223). Balloon-side IMAGE_ACK processing for the whole log:
  **grep = 0** — the balloon died before every COMPLETE reply landed, so the
  delivered bit was never persisted, so the next boot re-announced. Boot
  rescan (4190ef0) turned every silent crash into an immediate re-entry into
  the crash-correlated push path.

### 12.3 Crash-signature census (executor-grepped, corrected raw-byte mojibake method)

| Log | rst:0x7 | rst:0xc | Guru | canary | mojibake | i2cWrite E |
|---|---|---|---|---|---|---|
| balloon6 | 10 | 0 | — | — | — | — |
| balloon7 | 1 | 0 | — | — | — | — |
| balloon8 | 9 | 1 | 1 | 0 | 0 | 0 |
| balloon9 | 6 | 0 | 0 | 0 | 0 | 1 |

(The mojibake + i2cWrite columns are grepped 0/1 for balloon8/9 only; 6/7
were counted for rst lines only. balloon9's single i2cWrite
ESP_ERR_INVALID_STATE line is the §10.4 non-fatal class, NOT crash-adjacent.)

### 12.4 Saved-PC census (15 samples across both logs, symbolated against the balloon9 10:41 ELF — xtensa-esp-elf-addr2line)

| PC | Symbol |
|---|---|
| 0x4037c87d / 0x4037c87f | `esp_vApplicationTickHook` freertos_hooks.c:35 (session 9's was :34 — same function) |
| 0x4037d8ef | `xPortEnterCriticalTimeout` port.c:490 — **NEW family member: the cross-core spinlock-acquire spin path** |
| 0x4037dad0 | `_frxt_setup_switch` portasm.S:101 (context switch) |
| 0x4037df0e | `xPortSysTickHandler` port_systick.c:223 |
| 0x4037df91 | `SysTickIsrHandler` port_systick.c:149 |
| 0x4037e991 / 0x4037ea4f | `xTaskIncrementTick` tasks.c:3207/3375 |
| 0x4201cadf | `uart_ll_set_baudrate` ← `system_early_init` cpu_start.c:811 (boot-time — reset racing early init) |
| 0x400478a5/0x400478b7/0x400478f0/0x4004795f/0x400559cc/0x40001c38 | ROM region (unsymbolated — S3 ROM ELFs not on the bench toolchain; the CLUSTERING itself is the fact) |

Reading: **zero app-code PCs across 15 silent resets** — every sample lands
in the 1 ms-hot kernel tick/switch/lock paths. The distribution is "the CPU
was alive and servicing ticks at the reset instant", combined with the
lock-path members (`xPortEnterCriticalTimeout` spins WITH INTS MASKED).

### 12.5 What session-12 discriminates + the gate decision

- **[STACK] re-refuted a third time**: balloon9 floors 236 words (loop-side
  method), balloon8 244 — constant, no acceleration toward 0, no canary
  whole-era. The §11.3 wedge-side reading repeats.
- **The balloon8 dump NAMES the mechanism class**: `Interrupt wdt timeout on
  CPU1` = CPU1 held interrupts masked (or sat above maskable level) past the
  300 ms INT-WDT bound. The SAME class on CPU0 — where the TWDT lives —
  produces exactly the era's dominant expression: stage-0's TIMG0 interrupt
  cannot fire through a masked INTLEVEL, so no "Task watchdog got triggered"
  print, and MWDT0's stage-1 resets SILENTLY at 10 s (§7.1/§11.2 topology).
  §10.5's labeled one-family hypothesis ("visible signature depends on where
  the damage lands") now has a WITH-DUMP member: CPU1 bites → int-wdt panic
  with dump; CPU0 bites → silent TG0WDT stage-1.
- **Honest scope**: this is a mechanism CLASS, not a located bug. What
  masks INTLEVEL on either CPU remains open (driver critical sections,
  spinlock wedge — cf. the `xPortEnterCriticalTimeout`/CAS census members —
  or an ISR storm at level ≥ the WDT's). H-lull stays dead (§12.2 deaths are
  mid-service AND post-completion); H-phase-independent (§9.3) WEAKENED by
  §10 is UNWEAKENED here: balloon9 boot A died in a five-minute telemetry
  lull with zero push activity.
- **FIX SHAPE SELECTED (the crash-loop, not D1)**: reset-cause-gated
  boot-rescan — after a crash-class reset (TASK_WDT/INT_WDT/WDT/PANIC/
  CPU_LOCKUP) the 02.5-02 auto-resume is SKIPPED for that boot (no rescan
  card-walk, no admission); base pull via IMAGE_FULL_REQUEST stays served
  (handleFullRequest's card re-admit); a clean boot re-arms. Implemented as
  `e9400b9` (`feat(02.5-02): reset-cause-gated boot-rescan resume`).
  MITIGATION-labeled in source and ledger — it buys uptime and breaks the
  delivery livelock; it is NOT the D1 lever and its fate rides G-01-10's
  outcome.

## 13 — SESSION-13 (balloon10 bench, 2026-08-30 12:58): the gate VERIFIED (9/9), image data delivered through 9 silent resets, deaths NOT synchronous with SD calls, the census grows (int_wdt + usb-serial-jtag tick hooks + CAS), and the [STAMP] instrument wired

### 13.1 Provenance

`balloon10.log` (2,772 lines, 10 boots) / `base10.log` (900 lines), written
2026-08-30 12:58, banner `Build: Aug 30 2026 12:22:00` (balloon10.log:23) =
`e9400b9` content (the gate). ELF provenance by banner + content only — the
on-disk ELF was rebuilt with the [STAMP] instrument before this record.
ACTIVE bench: operator commands include CAPTURE_NOW (balloon10.log:472) and
window arming (:556); the base pulled FULLs via IMAGE_FULL_REQUEST retries.

### 13.2 The gate's bench verdict: 9/9

First boot POWERON → rescan ran normally, admitted 2 (balloon10.log:115).
All NINE subsequent boots read TASK_WDT and printed
`SdStore: boot-rescan skipped - crash-class reset TASK_WDT (…)` (:285, :926,
:1101, :1346, :1596, :1897, :2149, :2390, :2594). The balloon9 2537 ms
crash-loop is GONE; boot uptima lengthened; and the livelock is broken in
the rescan arm — though §13.4 records the base-pull arm re-exposing the push
path (by design, the no-strand trade).

### 13.3 Data delivered THROUGH the crashes

Base finalized image 53 thumbnail COMPLETE (base10.log:120) AND full 27/27
COMPLETE (:215); image 54 thumbnail COMPLETE (:274) with its full pull IN
FLIGHT at session end (95-chunk manifest, windows armed, 4/95 held,
base10.log:329/:332/:386). The receipts + keep-everything archive carried
real transfer through nine resets — the 02.5 design worked as drawn even
with the underlying D1 family still firing.

### 13.4 Death-context map (the session's central discriminate)

All nine silent rst:0x7 deaths, last firmware line before each ROM banner:

- Post-completion: boot 1 — thumb 8/8 INCLUDING the 177 B tail sent
  (balloon10.log:181, the exact chunk that killed balloon9's boot 2), death
  ~2 telemetry lines later — the completion/ACK-arrival moment again.
- Mid-window-service: resets at :1247 (13/16), :1798 (4/16), :2050 (15/16),
  :2291 (15/16 re-armed), :2495 (3/16).
- **IN-LULL**: resets at :827, :1002, :1497 — last line
  `E32: Transmitted 30 bytes` (plain beacon/telemetry TX), minutes of lull
  after the boot's last card access.

Conclusions drawn honestly: (1) deaths are NOT co-located with SD calls —
three landed in lulls after plain transmits, matching session 9's
inter-window-lull pattern (§8.2); (2) SD-NECESSITY REMAINS OPEN, not
refuted — no card-free boot existed (the operator's CAPTURE_NOW and the
base's FULL_REQUEST card re-admits touched the card on every boot), so the
weak form "SD work is sufficient-but-not-necessary trigger vs unrelated" is
undecided; (3) H-phase-independent (§9.3) is RE-STRENGTHENED toward the
session-12 reading: lull deaths in two eras.

### 13.5 Census + the census growth

Whole-log greps: rst:0x7 = 9, rst:0xc = 0, Guru = 0, canary = 0, mojibake
(raw-byte method) = 0, i2cWrite E = 0. [STACK] floors 244 ×10 / 356 ×4 —
constant, no canary, fourth consecutive refutation. Saved PCs (9 samples,
symbolated against the [STAMP] build's ELF — kernel addresses stable across
these builds):

| PC | Symbol |
|---|---|
| 0x40376478 | `tick_hook` int_wdt.c:125 — **the INT-WDT's own tick hook (session 8's :111 sibling)** |
| 0x40377801 | `usb_serial_jtag_sof_tick_hook` usb_serial_jtag_connection_monitor.c:46 — **NEW: the console's USB-SJ monitor** |
| 0x40379519 | `esp_cpu_compare_and_set` (xt_utils.h:235 inline chain) — **the spinlock CAS acquire primitive** |
| 0x4037c87d | `esp_vApplicationTickHook` freertos_hooks.c:35 (repeat) |
| 0x4037e9b1 | `xTaskIncrementTick` tasks.c:3227 (repeat) |
| 0x400478a5/0x400478db/0x40047a68/0x400559cc | ROM cluster (grows: a5, db, a68) |

The tick-hook cluster (int_wdt, usb-sj monitor, dispatcher) + the lock
primitives + the balloon8 CPU1 dump keep the §12.5 class reading standing:
interrupts masked too long on whichever CPU bites. The console's presence
(usb-sj monitor hook; UART0-bridge console under `ARDUINO_USB_CDC_ON_BOOT=0`
with `USB_MODE=1`) is RECORDED as a candidate surface, not ranked — the
session-7-era mojibake class did NOT recur here (0 hits).

### 13.6 Secondary findings (named, not actioned here)

- **Heap low-water drift**: balloon10's `[MEM] minHeap` declines monotonically
  ~143 KB across an active boot (8467364 → ~8323820) at a roughly steady
  drip through BOTH active and lull stretches; instant `heap=` bounces back
  between samples. Unproven between slow leak and fragmentation drift —
  watch on the [STAMP] session before spending a round on it.
- **NVS id-sequence quirk**: gated boot 2 printed
  `image 52 has no valid full buffer; nothing to enqueue` (balloon10.log:130)
  — an enqueue attempt carrying a stale id (card holds 53/54). Benign
  (debug-only line; nothing enqueued; no state disturbed), logged for the
  02.5 ledger's next pass.

### 13.7 ROUND-#15 ROUTING (recorded, not speculated)

The round-#15 bench package, riding one session:

1. **[STAMP] RTC starvation stamps** — wired this round in main_balloon.cpp
   (the code commit beside this doc): loopTask stamps every loop() pass
   entry, the IDLE0 hook stamps every idle tick, both into RTC_DATA_ATTR
   words that SURVIVE the stage-1 reset; one boot-time readout line.
   Predicted readings: gap (loop minus idle) < ~1 s → both tasks froze
   together (the whole-CPU masked/storm family); gap ≥ ~5 s → IDLE0 starved
   alone while loopTask passed — which, under §7.1's armed-interrupt
   premise, makes a SILENT stage-0 a contradiction worth re-opening §7 for.
   The t values read as the previous boot's age at each task's last progress.
2. The **§11.4 A/B hook-unregistered arm** (still pending, unchanged).
3. Optional console A/B: route the bench console to native USB-SJ
   (`ARDUINO_USB_CDC_ON_BOOT=1`) for one session to priced the console's
   surface (§13.5) — run only if 1+2 answer insufficiently.
4. The ladder heavy tail (≥7-window SVGA service, 3-minute dwell) re-run,
   unchanged from §11.6.

D1 stays OPEN; the mechanism class (interrupts masked too long, one CPU at
a time) is the recorded working theory; the [STAMP] gap is its next
discriminator. (01-UAT.md G-01-10, WINDOWS 15.)

## 14 — SESSION-14 (balloon11 bench, 2026-08-30 13:49): the [STAMP] field trial fails BY IMPLEMENTATION — the .rtc.data flash-load-image trap — family unchanged (4 silent), gate 4/4, image 55 delivered through all four crashes

### 14.1 Provenance

`balloon11.log` (1,312 lines, 5 boots) / `base11.log` (316 lines), written
2026-08-30 13:49, banner `Build: Aug 30 2026 13:08:45` (balloon11.log:24) =
`f93cdb6` (the [STAMP] instrument build). ELF provenance by banner + content
only — the on-disk ELF was rebuilt with the NOINIT fix before this record.

### 14.2 The [STAMP] readout: 5/5 "no prev-boot stamps" — instrument bug, NOT a discriminator answer

All five boots (including the four WARM post-crash boots) printed
`[STAMP] no prev-boot stamps (POWERON or RTC-domain reset) (G-01-10)`
(balloon11.log:20/:190/:612/:821/:1074). Root cause, verified in the build
artifacts: the instrument's words were declared `RTC_DATA_ATTR` WITH
initializers — `.rtc.data` carries a FLASH LOAD IMAGE, and the second-stage
bootloader re-copies it from flash on every non-deep-sleep boot, clobbering
the previous boot's writes. Symbols confirmed in RTC slow (`0x50000200`
-`0x50000208`, `d`/data sections) — placed, but re-loaded. The
cross-reset-persistence attribute is `RTC_NOINIT_ATTR` (`.rtc.noinit`):
never loaded, never cleared, survives every reset except true power-on
(garbage then — the existing magic-word gate covers it; the `b`-section
placement of the fixed build was verified in the ELF). Fixed and committed
beside this record. **Round-#15 item 1 is UNANSWERED — it re-runs on the
NOINIT build.** The recording honesty rule: an instrument that cannot read
is not a negative result.

### 14.3 The session: the family unchanged

rst:0x7 = 4 (one POWERON + four silent), rst:0xc = 0, Guru = 0, canary = 0,
mojibake (raw-byte method) = 0, i2cWrite E = 0. Gate 4/4
(`boot-rescan skipped - crash-class reset TASK_WDT` at balloon11.log:281/
:703/:912/:1165). Death contexts: boot 1 died at the COMPLETION MOMENT
again — thumb chunk 7/8 of image 52 sent (:178), death before the tail;
the other three: in-lull beacon TX (:600), window 3/16 (:809), window 15/16
(:1062) — the §13.4 lull/service split repeats. [MEM] heap drift this
session: min 8456596 vs start 8457484 (~900 B) — the balloon10 §13.6 drip
did NOT recur at this session's shape (active, short-lived boots);
downgraded to watch-only.

### 14.4 Delivery through the crashes — and the stuck row

Image 55: thumbnail COMPLETE (base11.log:67-68) AND full 26/26 COMPLETE
(:295-296), converged ACROSS the four crashes through resume-prefix
receipts and re-arms (`resuming (2/26 held)` :187, `(16/26 held)` :264,
then finalized) — the 02.5 delivery machinery's fourth consecutive session
working through the crash family. Image 52, by contrast, has now been stuck
undelivered since balloon9: every bench session's power-cycle re-arms the
resume (POWERON), the push re-enters, and the completion moment kills the
boot before the delivered bit persists — balloon11 boot 1 is the third
consecutive session to die inside image 52's thumbnail push. The POWERON
arm of the §12.2 livelock is its residual form (the gate by design cannot
gate a POWERON boot); a candidate 02.5-side relaxation — gate the resume on
"previous boot ALSO died mid-push of this same image" (an RTC_NOINIT
per-boot flag) — is RECORDED, not actioned; it must not silently become a
D1 lever.

### 14.5 Round-#15 status

Item 1 ([STAMP]): instrument fixed, re-run owed on the next bench session —
the gap reading remains the mechanism-class discriminator. Item 2 (the §11.4
A/B hook-unregistered arm): still pending, unchanged. D1 stays OPEN;
working theory unchanged (§13.7). (01-UAT.md G-01-10, WINDOWS 15.)

## 15 — SESSION-15 (balloon12 bench, 2026-08-30 14:00): [STAMP] ANSWERS — whole-CPU freeze confirmed — and the balloon8 dump, re-read, CAPTURES the mechanism: CPU0 spinning in a spinlock acquire with ints masked

### 15.1 Provenance

`balloon12.log` (1,032 lines, 4 boots: 1 POWERON + 3 rst:0x7) /
`base12.log` (245 lines), written 2026-08-30 14:00, banner
`Build: Aug 30 2026 13:53:35` (balloon12.log:24) = `2247f1c` content (the
NOINIT [STAMP] fix). ELF provenance by banner + content only (the on-disk
ELF was rebuilt with the §15.5 fix before this record). [STACK] floors 244
×4 / 356 ×2 — fifth consecutive refutation; [MEM] minHeap 8457484 = the
boot baseline (no drift this session).

### 15.2 The round-#15 discriminator ANSWERS: whole-CPU freeze

All three post-crash boots produced genuine [STAMP] readouts:

| Boot | loopTask last pass | IDLE0 last tick | gap (loop − idle) |
|---|---|---|---|
| 2 | t=8850 ms | t=9295 ms | **−445 ms** |
| 3 | t=83098 ms | t=83174 ms | **−76 ms** |
| 4 | t=60630 ms | t=60642 ms | **−12 ms** |

Against the §13.7 predictions: gap ≪ 1 s in EVERY reading — **both tasks
progressed to within milliseconds of the death; the whole CPU froze
together at IDLE0's last tick**. The whole-CPU masked-freeze class is
CONFIRMED; the task-starvation reading is REFUTED. Two premises settle with
it: the stage-0 silence is now EXPLAINED (the freeze masks the TIMG0
interrupt that would print — no WDT-chain contradiction, §7.1's
armed-interrupt premise STANDS: IDLE0 was alive and feeding until the
freeze instant); and §13.7's boot-age readings (9.3 s / 83.2 s / 60.6 s at
freeze) kill any fixed-maturation clock. One death's last console line is
corrupted mid-print (`GPS: N∩┐╜ valid data`) — the freeze caught the
console mid-byte, exactly the class's signature.

### 15.3 The balloon8 dump, re-read: the mechanism CAPTURED (retrospectively completes §12.5)

balloon8.log's CPU1 panic (:837) printed BOTH cores, and the §12.5 record
mined only the CPU1 half. The full dump:

- **CPU1** (the panicking core): backtrace `xt_utils_wait_for_intr` ←
  `esp_vApplicationIdleHook` ← `prvIdleTask` (:850-854) — **IDLE1 is
  innocent**: it sits in its normal wait loop. Its SysTick stopped being
  served (tick increment takes a scheduler spinlock on SMP), the INT-WDT
  feed stopped with it, and the int-wdt panicked CPU1.
- **CPU0**: `PC: 0x4037d8ec — xPortEnterCriticalTimeout port.c:490,
  PS 0x00060034` (:859-860) — **CPU0 was SPINNING IN A SPINLOCK ACQUIRE
  with its interrupts masked** at the instant of the panic.

This is the silent-reset mechanism photographed. Re-reading the censuses
(§12.4/§13.5): `xPortEnterCriticalTimeout`, `esp_cpu_compare_and_set`,
`_frxt_setup_switch`, and the tick-path PCs are all samples of ONE hang —
CPU0 wedged acquiring a spinlock that is never released (dead/corrupted
owner, or an ISR-over-task self-deadlock of a non-recursive lock), CPU0's
ints masked so the TWDT stage-0 cannot print, stage-1 resetting silently
at +10 s; CPU1's tick starvation produces the int-wdt expression when it
lands first. LOCALIZED CLASS: **a spinlock acquire on CPU0 that never
completes.** The dump does not name the lock's owner — that is the
remaining question. Candidate lock families (from the death contexts and
the drivers in the hot path): heap_caps region locks (any allocation),
UART driver locks (the two-console write path), I2C bus locks (the
i2cWrite ESP_ERR_INVALID_STATE class), SDMMC locks. Not ranked — recorded.

### 15.4 The session's events

Gate 3/3 (skip lines at balloon12.log:281/:637/:941). Deaths: boot 1 at
image 52's thumb 7/8 completion moment (:178 — the FOURTH consecutive
session to die inside image 52's push); boot 2 at image 56's thumb 3/7
mid-push (:534); boot 3 lull-adjacent with the corrupted line above.
POWERON boot 1 also completed the boot rescan honestly: 5 already-
delivered records skipped, torn 47/48 still skipped (:111-113).

### 15.5 The thumbnail-heal gap and its fix (committed beside this record)

The gate's no-strand promise had a hole the session exposed: base heal
windows for image 56's missing thumb chunks (2..6) were NACK_INVALID four
times — `window request for unknown/evicted image 56 rejected`
(balloon12.log:689/:753/:821/:977) — because the crash killed the RAM
entry and the gate skips re-admission; the base's D-24 stall detector then
FINALIZED the row INCOMPLETE at 2/7 (`thumbnail push stalled; passes
exhausted`, base12.log:238-239). The rescue asymmetry: FULL pulls are
rescued end-to-end (handleFullRequest card re-admit + the base's
window-NACK re-arm), but the thumbnail push — balloon-driven, base-healed —
had no rescue and NO wire command by which the base could request a
re-announce. FIX (balloon side, one site): on a THUMBNAIL-kind window
request whose target is unknown, re-admit the record from the card through
the SAME validated boot-rescan fill — the entry lands at
PUSH_THUMB_MANIFEST, the next process() pass re-announces, and the base's
restart-on-new-manifest behavior re-drives the row. Guards in-source:
thumb-kind only, record still owes the thumbnail, id not already queued,
free slot required; FULL-kind asks deliberately NOT re-admitted here (that
class is already rescued, and a thumbDelivered record would park where no
window can arm). Wire format unchanged; builds 2/2, harness exit 0.

### 15.6 Round-#15 verdict + routing

Item 1 ([STAMP]): **ANSWERED** — whole-CPU freeze; the instrument and the
NOINIT lesson both earn their keep. Item 2 (the §11.4 A/B hook-unregistered
arm): **DEMOTED** — [STAMP] proves IDLE0 liveness to the freeze instant,
which is the very question the A/B was built to ask; the hook dispatch is
exonerated. D1's remaining question is NARROWER than ever: NAME THE LOCK
whose acquire never completes on CPU0. Routing (recorded, not speculated):
(1) mine every future dual-core dump — each carries both CPUs' PCs, and a
CPU1-expression with the owner's frame visible would name it; (2) the ROM
cluster PCs (§12.4) can be symbolated against Espressif's published
esp-rom-elfs (a network fetch — the operator's call); (3) the §14.4
52-row POWERON livelock candidate stays recorded-not-actioned. (01-UAT.md
G-01-10, WINDOWS 15.)

## 16 — SESSION-16 (balloon13 bench, 2026-08-30): the family unchanged — [STAMP] confirms the freeze three more times, image 57 fully delivered through all three crashes, the heal fix unexercised, image 52's completion-moment death (fifth consecutive)

### 16.1 Provenance

`balloon13.log` (996 lines, 4 boots: 1 POWERON + 3 rst:0x7) / `base13.log`
(257 lines), 2026-08-30 = `515d38c` content (the thumbnail-heal fix; banner
not re-cited — ELF provenance by content only, the on-disk ELF moves with
every rebuild). Census: rst:0x7 = 3, Guru = 0, canary = 0, mojibake
(raw-byte AND replacement-char greps) = 0, i2cWrite E = 0. [STACK] floor
244 words ×4 — the refutation repeats. Gate 3/3 (balloon13.log:282/:516/
and the third).

### 16.2 [STAMP]: three more freeze confirmations, no drift

gap (loop − idle) = **−439 ms** (freeze at boot-age 9.3 s), **−39 ms**
(45.5 s), **−445 ms** (53.6 s) — the §15.2 whole-CPU freeze class repeats
exactly: IDLE0's last tick within milliseconds of loopTask's last pass,
every time. No new timing structure; the instrument is stable across
sessions.

### 16.3 The session's events

- Boot 1: rescan admitted 3 undelivered (balloon13.log:117); image 52's
  thumb pushed 1–7 then the boot died at the **completion moment for the
  FIFTH consecutive session** (:179 → rst at :182). The base is silent
  about image 52 all session (base13.log has zero 52 lines): its row has
  been COMPLETE-finalized since base9 (:147) — the balloon's card bit can
  never set because the verdict is rate-limited/dropped on re-manifests,
  so every POWERON re-push burns the completion moment again. §14.4's
  relaxation candidate stays the right 02.5-side fix shape.
- Boot 2: CAPTURE_NOW → image 57 (balloon13.log:573); thumb 6/6 COMPLETE
  (base13.log:91-92); the base's UI full pull then hit the crash mid-window
  and the EXISTING rescue fired end-to-end: `window request rejected for
  requested image 57 - re-arming full` (base13.log:211) → re-manifest →
  `resuming (23/27 held)` → **27/27 COMPLETE** (:239-240). Second
  consecutive session the delivery machinery converged through the crash
  family.
- Boot 3 death: mid-window-service (image 57 full, 8/11, balloon13.log —
  the usual class). Boot 2's death was lull (last line BMP280).
- **The thumbnail-heal fix (§15.5) was unexercised** — no thumbnail ended
  the session incomplete, so no `card re-admitted` line exists. The fix's
  first field trial is still owed; nothing in this session disturbs it.

### 16.4 Round-#15 verdict unchanged

[STAMP] answered (§15.2/§16.2); the open question is unchanged and narrow:
NAME THE LOCK whose acquire never completes on CPU0 (§15.3). No new dump,
no new census members, no new candidates this session. Routing unchanged
(§15.6). (01-UAT.md G-01-10, WINDOWS 15.)

## 17 — THE 52-ROW LATCH IMPLEMENTED (post-session-16, 2026-08-30): the §14.4 candidate actioned — with one honest re-homing: the latch lives in the CARD META, not RTC

### 17.1 The re-homing decision (recorded deviation from §14.4's wording)

§14.4 recorded the candidate as "an RTC_NOINIT per-boot flag". That
mechanism cannot carry this state: the livelock's driver is the
POWER-CYCLE (each bench session opens with a true power-on), and RTC
domain state is garbage after a true power-on — RTC_NOINIT only crosses
WARM resets, which the existing reset-cause gate (e9400b9) already
covers. The durable home is the record the rescan already reads on every
boot: a third bit in BalloonCaptureRecord::flags.

### 17.2 The mechanism (as built)

- `SD_ST_RESUME_LATCH` (flags bit 2, 0x04) — NO record-size change, torn-
  record validation untouched, every pre-existing card record compatible
  (bit unset = today's behavior).
- SET once per record at boot-rescan ADMISSION (`admitRescanned` →
  `markResumeLatched`, a boot-time one-byte in-place flags write, OFF the
  push hot path; failure logged WITHOUT status.writeFailed — a failed
  latch degrades to today's unprotected resume, never gates a capture).
- CLEARED by any provable delivery (`markDelivered` now clears the bit
  with either delivery bit) — a delivered row retires its suppression
  naturally, whatever path delivered it.
- WITHHELD from the resume set: `bootRescan` skips latched + undelivered
  records with a named per-id line and a counted summary line
  (`rescan withheld %u resume-latched record(s) ...`). The balloon's
  first boot of a session stays ALIVE instead of burning the same
  completion moment again.
- BYPASSED by explicit asks: `handleFullRequest` and the thumbnail-heal
  re-admit use `loadResumedRecord` and never consult the latch (documented
  at both sites) — the operator's pull re-arms a latched row, and that
  pull's delivery clears the latch. The keep-everything archive gains no
  deletion surface and loses no recovery path.

### 17.3 Expected bench signature (the prediction, written before the session)

Next session on the current card: boot 1 (POWERON) admits 52, LATCHES it,
pushes, and dies at the completion moment — the SIXTH and (by design)
final auto-resume death for 52. Every POWERON boot after that prints
`image 52 auto-resume withheld (resume-latched by a previous boot's
failed attempt) - explicit pull still served` and runs on. 52's row
remains fully recoverable: a base UI full-pull (the FULL_REQUEST rescue
chain, proven on 57 in §16.3) delivers it and retires the latch. The
deaths the family still produces will then be spread across fresh
captures and window service — the D1 lock hunt (§15.6) continues
unchanged.

The latch is 02.5-side resume POLICY (labeled MITIGATION in source, per
the §14.4 guard): it buys session uptime and breaks the 52 loop; it makes
no D1 claim.

## 18 — SESSION-17 (balloon14 bench, 2026-08-30): the latch HOLDS (best session of the campaign: three images fully delivered, boots to 139 s) and a SECOND dual-core dump — CPU0 healthy in the tick-hook dispatch while CPU1's tick service is dead

### 18.1 Provenance + session stats

`balloon14.log` (2,826 lines, 8 boots: 1 POWERON + 6 silent rst:0x7 + 1
rst:0xc panic) / `base14.log` (1,015 lines), 2026-08-30, = `57c3d5e`
content (the resume latch). **The latch held exactly as §17.3 predicted**:
boot 1 (POWERON) printed four per-id withhold lines — images 49/52/56/58
(balloon14.log:113-116) — and `rescan withheld 4 resume-latched record(s)
...` + `rescan found 0 undelivered image(s), admitted 0` (:118-119); 52
NEVER pushed again (no completion-moment death this session; the withhold
lines print only at rescan time, i.e. boot 1 — crash-class boots skip the
rescan via the gate). [STAMP] ×7, gaps −158/−27/−655/−489/−507/−521/−468 ms
— the whole-CPU-freeze reading repeats; boot ages 23–139 s. One death's
ROM banner carries console corruption (`∩┐╜∩┐╜∩┐╜ESP-ROM`, :1607) — third
corruption-at-death observation. **Delivery: the campaign's best — images
59 (thumb + full 28/28), 60 (thumb + full 105/105 chunks, 23,239 B — the
largest yet), 61 (thumb + full 28/28) ALL finalized COMPLETE
(base14.log:70/:276/:329/:773/:817/:991); zero INCOMPLETE rows.**

### 18.2 The second dual-core dump (balloon14.log:1557-1597) — and a decoder exoneration

`Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)`
mid-window (image 60 kind 1, 9/16, :1556). Both cores:

- **CPU1**: PC `esp_cpu_wait_for_intr`, backtrace idle-hook ← prvIdleTask,
  **PS 0x00060b34 → INTLEVEL 4** — parked in the idle wait with its
  level-1..4 interrupts (SysTick among them) MASKED. Its tick service had
  stopped; the INT-WDT (which preempts at higher level) panicked it.
- **CPU0**: PC `usb_serial_jtag_sof_tick_hook` (connection_monitor.c:38),
  backtrace `xPortSysTickHandler port_systick.c:199` ← SysTickIsrHandler ←
  `_xt_lowint1`, interrupted context IDLE0 — **CPU0 was ALIVE and
  servicing a SysTick, inside the tick-hook dispatch**.

The backtrace decoder's frame #0 named `esp_psram_check_ptr_addr` —
EXONERATED by the symbol table: that function spans 0x403777c4–~0x403777e9
and `usb_serial_jtag_sof_tick_hook` starts at EXACTLY the register-dump PC
0x403777ec (nm on the deployed ELF); the checker is a pure pointer-range
compare (esp_psram.c:525-549, no PSRAM access) and the backtrace's
0x403777e9 is a 3-byte-earlier misattribution across the adjacent IRAM
boundary. The call site is verified in the pinned IDF 5.5.4 source:
`xPortSysTickHandler` calls `esp_vApplicationTickHook()` at that line —
the dispatcher that walks the registered tick hooks (int_wdt's, the USB-SJ
monitor's, the project's idle-hook sibling family — the entire Saved-PC
census).

### 18.3 What the two dumps say TOGETHER (the working theory, refined)

Balloon8's dump: CPU0 stuck in `xPortEnterCriticalTimeout` (spinlock
acquire, ints masked) while CPU1 starved. Balloon14's dump: CPU0 healthy
in the pre-lock phase of its tick (the hook dispatch runs BEFORE the tick
handler's critical section — verified in the local IDF source) while CPU1
is ALREADY parked with ticks masked. The pinned kernel's tick path takes
the KERNEL SPINLOCK on BOTH cores every tick
(`taskENTER_CRITICAL_FROM_ISR()` in xPortSysTickHandler; on the SMP
kernel only core 0 increments). Unified mechanism (labeled hypothesis,
strongest on record): **a kernel/driver spinlock becomes permanently
held; whichever core's tick or switch next needs it wedges inside its own
ISR/switch path — ints masked at that level, so the TWDT stage-0 cannot
print (the silent TG0WDT family, Saved PCs = tick/switch/lock code), the
MWDT stage-1 resets silently; the other core continues briefly and
expresses the int-wdt panic with a dump (balloon8/14) or starves next.**
§15.3's "CPU0 spinning" reading is corrected to "WHICHEVER core takes the
dead lock" — the census's mixed-core PCs now read as samples of both
roles. The lock's OWNER remains unnamed — the one question left.

### 18.4 Routing

(1) **[TICKSTAMP] candidate (recorded, not actioned)**: register a project
tick hook stamping a per-core last-tick RTC_NOINIT word (the [STAMP]
pattern, tick side) — the next silent reset would read out WHICH core's
ticks died first and how long the survivor kept ticking, hardening the
ordering data the two dumps only sample. (2) Mine every future dual-core
dump. (3) The kernel-lock provenance question is now a pinned-IDF-source
reading task (which locks the balloon's hot path takes: UART console
writes, SDMMC, heap) — recorded, not speculated. (01-UAT.md G-01-10,
WINDOWS 15.)

## 19 — [TICKSTAMP] WIRED (post-session-17, 2026-08-30): the §18.4 item-1 instrument implemented — per-core tick stamps that cross the silent reset

A project tick hook (`tickStampHook`, IRAM, lock-free — one millis() RTC
store per tick per core) registered via `esp_register_freertos_tick_hook`
(both cores) in main_balloon.cpp beside the [STAMP] block; two
RTC_NOINIT words + magic, read out once at the next boot as
`[TICKSTAMP] prev boot: core0 last tick t=%lu ms, core1 last tick t=%lu
ms, gap %ld ms (core0 minus core1) (G-01-10)` with a registration check
line, re-armed after the readout. Pre-written reading rules (§18.4's
prediction, made concrete): gap ≈ 0 → both cores ticked to the end
(the balloon14 CPU1-only reading would be wrong); gap positive → core1's
tick service died first (the balloon14 shape); gap negative → core0's
died first (the balloon8 shape, CPU0 wedged mid-spinlock-acquire).
Combined with the [STAMP] task words every death now leaves a four-point
picture: loopTask, IDLE0, tick-core0, tick-core1. Documented subtlety:
the stamp freezes at the last COMPLETED hook dispatch — a core wedging
at the tick handler's post-hook kernel-lock take still leaves a fresh
stamp, so the ordering data survives. First genuine readouts owed at the
next bench session's first crash. (01-UAT.md G-01-10, WINDOWS 15.)

## 20 — SESSION-18 (balloon15 bench, 2026-08-30): THE MECHANISM IS NAMED — all seven dumps die in the SYSTIMER read's unbounded valid-bit spin, and the spin-on-halt configuration is ARMED BY DEFAULT

### 20.1 Provenance + the session's own finding

`balloon15.log` (4,144 lines, 8 boots: 1 POWERON + 1 silent rst:0x7 + SEVEN
rst:0xc int-wdt panics with dual-core dumps) / `base15.log` (1,262 lines),
2026-08-30, build `3ad7be9` (the [TICKSTAMP] v1). Recorded honestly FIRST:
v1 had a registration gap — `esp_register_freertos_tick_hook` registers on
the CALLING core only (esp_freertos_hooks.h:77, the comment the design
read and misapplied), setup() runs on CPU1, so core0's stamp read t=0 in
every boot (balloon15.log:944/:1228/:1688/:2191/:2625/:2946). The gap
itself produced the session's discovery: the hook (registered on CPU1)
calls millis() ON EVERY CPU1 TICK, and millis() reads the systimer —
**the probe converted the silent TG0WDT family into SEVEN int-wdt panics
with dumps** (the one silent rst:0x7 = the wedge caught on a CPU0-side
read path the hook could not see). Session totals: boot lifetimes 25–262 s
(boot 1: 4.4 min); [STAMP] gaps −2/−80/−1443/−509/−60/−663 ms — the
whole-CPU freeze reading continues.

### 20.2 THE DECODE: every dump dies in the same three frames

All seven CPU1 last-tick chains are frame-for-frame identical (addr2line
against the deployed ELF):

```
STUCK: systimer_ll_counter_snapshot / systimer_ll_is_counter_value_valid
       (systimer_hal.h:90/:95 inlined into systimer_hal.c:50-51)
   ← esp_timer_impl_get_time (esp_timer_impl_systimer.c:72)
   ← millis (esp32-hal-misc.c:209)
   ← tickStampHook (main_balloon.cpp:216)
   ← esp_vApplicationTickHook (freertos_hooks.c:36)
   ← xPortSysTickHandler (port_systick.c:199) ← SysTickIsrHandler
```

The read is `systimer_ll_counter_snapshot()` (set the unit's UPDATE bit)
followed by an **UNBOUNDED** `while (!timer_unit_value_valid);`
(systimer_hal.c:51, verified in the pinned IDF 5.5.4 source) — if the
systimer unit stops acknowledging the snapshot, the read spins forever AT
THE CALLER'S INTERRUPT LEVEL. THE WEDGE IS THE SYSTIMER READ ITSELF.

### 20.3 Why the counter would stop acknowledging: the stall-on-halt configuration

`esp_timer_impl_early_init` (esp_timer_impl_systimer.c:176-179) arms, BY
DEFAULT, on every boot:

```c
bool can_stall = (cpuid < portNUM_PROCESSORS);   // TRUE for BOTH CPUs
systimer_hal_counter_can_stall_by_cpu(..., cpuid, can_stall);
```

the SYSTIMER counter **stalls whenever EITHER CPU is debug-halted**. A
spurious halt assertion therefore stops the counter, the UPDATE
acknowledgment never comes, and the first time-read after that spins
unbounded. Candidate trigger #1 (recorded, not confirmed): the USB-SJ
debug module on the bench USB — the classic spurious-halt source, and
consistent with the console corruption observed at three deaths
(§13.5/§16.1/§17.1). Alternatives: counter/clock hardware stall; an S3
systimer erratum. Ruled OUT this session: WiFi modem sleep (zero [NET]
lines whole-log — the balloon never brings WiFi up).

### 20.4 The unification (the campaign's expressions, one mechanism)

- **Silent TG0WDT deaths**: the wedge caught by a task/idle/tick time-read
  OUTSIDE ISR-panic reach — IDLE0 dies INSIDE its own millis() (the idle
  hook reads millis()!), the TWDT is never fed, stage-0's print itself
  cannot run through the spinning context, stage-1 resets silently. The
  Saved-PC censuses (§12.4/§13.5) are exactly the system's hot time-read
  paths.
- **Int-wdt panics with dumps** (balloon8/14/15): the wedge caught at
  tick-ISR level on the probed core.
- **[STAMP] gaps ≈ 0** (sessions 15-18): every core's NEXT time-read
  wedges within milliseconds of the stall — a simultaneous-looking freeze
  from individual call sites.
- HONEST LIMIT: boot 4's micro-timeline (loopTask frozen at 92.6 s, IDLE0
  at 93.1 s, yet CPU1's ticks ran to 108.1 s — a 15 s window with the
  tasks dead and the counter alive) shows the per-boot ordering VARIES —
  transient stall-and-recover, or stacked events. The dumps' common fact
  stands on seven identical chains; the micro-ordering is not settled.

### 20.5 [TICKSTAMP] v2 (committed beside this record)

(1) Registration FIXED: `esp_register_freertos_tick_hook_for_cpu` on BOTH
cores — every stall-side wedge should now produce a DUMP instead of a
silent reset. (2) Per-core CCOUNT stamps added (CPU cycle counter — an
independent clock, no systimer involvement): at the next boot, a core
whose tick stamp froze while its ccount stamp ADVANCED was **alive and
running with the systimer dead** — the stall named even for silent
deaths. Readout: `[TICKSTAMP] prev boot: core0 tick t=%lu ms (ccount
%lu ms), core1 tick t=%lu ms (ccount %lu ms)`.

### 20.6 Routing

(1) v2 readouts + (expected) dumps at the next session — the first
dual-instrument reading of a stall. (2) The stall-trigger A/B: run a bench
session with the USB-SJ port physically DISCONNECTED (console stays on
the UART bridge) — if the deaths stop, the spurious-halt theory is
confirmed and the mitigation is electrical (cable/ferrite/descent
configuration), not software. (3) S3 systimer erratum search (network —
the operator's call). D1 is no longer "open" in the old sense: the wedge
site is dump-proven; what remains is the TRIGGER. (01-UAT.md G-01-10,
WINDOWS 15.)

## 21 — SESSION-19 (balloon16 bench, 2026-08-30): v2 verified on both cores — the dumps repeat frame-identical, BOTH cores' ticks now die at the SAME millisecond, and the two-stage death structure is SYSTEMATIC (tasks first, ticks seconds later)

### 21.1 Provenance + v2 verdicts

`balloon16.log` (1,717 lines, 5 boots: 1 POWERON + 1 silent rst:0x7 + 3
int-wdt panics) / `base16.log` (379 lines), 2026-08-30, build `648ccad`
(the [TICKSTAMP] v2). v2 verdicts: `hooks registered cpu0=1 cpu1=1` every
boot ✓; the three dumps still die frame-identical in the systimer
valid-bit spin (same chain as §20.2, addresses +0x1c from the rebuild),
every one expressed on CPU1, interrupted context IDLE1. Delivery through
the deaths continues: image 67 thumb + full 37/37 COMPLETE
(base16.log:70/:362); the latch held (8 rows withheld at boot 1's rescan,
`rescan found 0 undelivered`, balloon16.log:124).

### 21.2 The session's structural finding: a TWO-STAGE death, 5/5 systematic

The v2 tick stamps expose what the v1 single-core data could not:

- **Both cores' tick stamps die at the IDENTICAL millisecond in every
  death** (123934/123934, 30109/30109, 44564/44564, 25907/25907 ms) — the
  systimer snapshot handshake breaks for BOTH cores simultaneously, as it
  must for a shared peripheral; neither core's tick service dies first.
- **The task stamps freeze 0.4–15.5 s BEFORE the final tick** in every
  death (e.g. loopTask 108430 / IDLE0 108935 / ticks 123934) — the task
  contexts stopped while the tick layer kept running.

With balloon15's boot 4 (the §20.4 honest limit), this is FIVE instances
of the same shape: **STAGE 1 — both cores' task contexts stop (loopTask
and IDLE0's stamps freeze, possibly tens of seconds of gap between them)
while ISRs and the systimer counter keep running; STAGE 2 — the systimer
snapshot handshake breaks simultaneously for both cores, the tick ISRs
wedge in the valid-bit spin, and the int-wdt panics (or, once, the
silent stage-1 reset wins the race).** The dumps always photograph stage
2; the [STAMP]/[TICKSTAMP] task stamps are stage 1's only recorder.

### 21.3 What stage 1 is NOT (the structured eliminations)

- **Not the kernel-lock wedge of §15.3/§18.3 as the WHOLE story**: the SMP
  tick path takes the kernel lock in its post-hook critical section — if
  the kernel lock died in stage 1, the ticks would wedge within one tick,
  not survive 0.4–15.5 s. Balloon8's CPU0-in-`xPortEnterCriticalTimeout`
  snapshot remains real, but it cannot be stage 1's mechanism.
- **Not a spurious debug-halt at stage 1**: a halted core stops its ISRs
  too — the ticks kept running through stage 1 on both cores.
- Stage 1 candidates that survive: a lock/resource the tasks need that
  the tick path does not (a driver lock wedged by a dead owner, with
  loopTask AND whatever IDLE0 was postponed behind both blocked — the
  exact mechanism still unnamed); or a scheduler-state wedge that stops
  context switches without holding the tick path's lock.
- The stage 1 → stage 2 CAUSAL question is open: whether the systimer
  handshake break is an independent second fault or a consequence of
  stage 1 (a stalled/interlocked clock or bus state after the task freeze)
  is exactly what the USB-SJ disconnect A/B (§20.6 item 2) and the errata
  search must decide. The one silent rst:0x7 per session (stage 2 winning
  as a silent stage-1-reset before CPU1's int-wdt matures) stays the
  minority expression, 1 per session across sessions 18-19.

### 21.4 Instrument fix (committed beside this record)

The ccount stamps read as ~17 s "ages" because CCOUNT is 32-bit and wraps
every 2^32/240 MHz ≈ 17.9 s — session-19's ccount values are wrap PHASES,
not ages (recorded as the instrument bug it is). The fix is the readout
comment + the wrap note in the format string; the alive-vs-frozen
comparison (tick stamp vs ccount stamp of the SAME core, within a wrap
window) remains valid and is the v2 payoff still owed its first clean
reading. (01-UAT.md G-01-10, WINDOWS 15.)

## 22 — OPERATOR FACT + PIN FINDING (post-session-19, 2026-08-30): nothing is connected to the USB-SJ port — and the SD card drives GPIO39 (MTCK) and GPIO40 (MTDI)

### 22.1 The operator fact and its first consequence

The operator confirmed NOTHING is plugged into the board's USB-SJ port
(the console runs on the UART-bridge port per the 01-10 bench deviation).
The §20.6 "USB-SJ disconnect A/B" is therefore already satisfied as an
elimination in its cable form: a HOST-driven debug halt cannot occur —
no host is on the USB-SJ path. The spurious-halt trigger candidate
loses its host-attachment arm.

### 22.2 The pin finding

The SD store's transport pins (sd_store_balloon.h:54-56) are:

- `SD_STORE_CLK_PIN = 39` — **GPIO39 = MTCK, the JTAG clock**
- `SD_STORE_CMD_PIN = 38`
- `SD_STORE_DATA_PIN = 40` — **GPIO40 = MTDI, JTAG data-in**

The card's clock line toggles at SDMMC_FREQ_DEFAULT (20 MHz) on the
JTAG-clock pin during every card access. On the ESP32-S3 the JTAG source
defaults to the USB-SJ peripheral; the external pins serve JTAG only when
the strapping pin is LOW at reset (or via eFuse). EVERY boot in the
campaign reads `boot:0x2b` — the standard strapping value, GPIO0 high —
so the external pins are (per the docs) plain GPIOs and the
JTAG-TAP-latch reading of the SD-clock-on-MTCK fact DOWNGRADES to
"possible but unverified" (residual arms: the live-but-hostless USB-SJ
peripheral's documented interaction quirks with MTCK activity; board-level
deviations from a stock DevKitC are unknown). What the fact DOES
establish regardless of mechanism: **the SD-line class is the only
board-level novelty that correlates with the crash era** — sessions 7-11
(pre-SD-store) ended with the FIRST zero-crash session; the SD store
landed; balloon6 onwards produced 10/1/9/6/9/4/9/3/6/9 silent-or-panic
deaths. §12.1's "recorded, NOT read as causation" correlation now has a
concrete board-level candidate underneath it.

### 22.3 The decisive A/B (hardware-only, no code)

Run one bench session with the SD CARD REMOVED (the firmware already owns
this regime: mount failure → `SD card store unavailable - captures take
the volatile fallback` → no SD IO at all; captures still flow via the
volatile fallback and the delivery machinery). Deaths stop → the SD-line
class is CONFIRMED as the trigger family (mechanism then narrowed by the
JTAG/electrical split). Deaths continue → the SD lines are exonerated and
the hunt moves to supply/XTAL integrity and the S3 systimer errata.
Either branch is a campaign-level answer for one card removal.

### 22.4 Board questions ANSWERED (2026-08-30, with the card-removal A/B launched)

The operator answered all three gating questions:

1. **Power**: bench power is PC USB through the bridge port — AND AN
   EXTERNAL SUPPLY WAS ALREADY TRIED, WITH THE FAILURES CONTINUING. The
   PC-rail-quality candidate is ELIMINATED as the primary trigger: the
   deaths travel with the board+card across two independent supplies.
   (Board-local electrical paths — e.g. the card's own draw on shared
   local rails — remain technically possible but are now second-order.)
2. **Board**: stock DevKitC — no custom wiring; GPIO39-42 carry ONLY the
   SD card (CLK/CMD/DATA per §22.2). No other JTAG-domain users.
3. **GPIO0**: button only — strapping normal, consistent with every
   boot:0x2b; the external-JTAG mux stays unselected per the docs.

The trigger candidate ladder after the eliminations: (1) the SD-line
class (board-local: JTAG-domain residual, SDMMC interaction, or local
electrical) — PRIME, the card-removal A/B is RUNNING as this is written;
(2) an S3 systimer erratum independent of SD — predicts deaths continue
card-less; (3) stage 1 (the task-context freeze that precedes the
systimer break by 0.4–15.5 s) — untouched by all of the above and still
the campaign's one genuinely open mechanism question. Interpretation
guide for the card-less session: `SD card store unavailable - captures
take the volatile fallback` at boot, NO SdStore rescan/persist/chunk
lines, captures served from PSRAM buffers; the instruments ([STAMP],
[TICKSTAMP] v2, gate, latch) all function unchanged. (01-UAT.md G-01-10,
WINDOWS 15.)

## 23 — SESSION-20 (balloon17 bench, 2026-08-30): THE CARD-REMOVAL A/B VERDICT — ZERO CRASHES — the SD-line class is CONFIRMED as the trigger family; the card-less session also exposed (and this record's fix retires) a capture-gate bug that stranded the volatile fallback

### 23.1 The verdict

`balloon17.log` (507 lines, ONE boot) / `base17.log` (252 lines),
2026-08-30. Census: rst:0x7 = 0, rst:0xc = 0, Guru = 0 — **the first
card-less session of the campaign ran end-to-end with ZERO deaths**,
against an era in which every card-in session lost boots within 9–140 s.
The mount failed honestly (`sdmmc_card_init failed (0x107)`,
balloon17.log:107-109 — card absent), the volatile-fallback regime
engaged (`SD card store unavailable - captures take the volatile
fallback`), NO SD IO occurred all session — and nothing froze. THE
SD-LINE CLASS IS CONFIRMED: the trigger is board-local to the card and
its pins (GPIO39/38/40 = MTCK/—/MTDI — §22.2), travelling across two
independent supplies (§22.4) and independent of load phase (the earlier
lull deaths had card IO in their boots; this session had none and none
died). The S3-systimer-erratum-independent-of-SD candidate drops below
the line; stage 1 (the task-freeze that precedes the systimer break)
now reads as part of the SD-correlated event chain — its mechanism is
the remaining question INSIDE the confirmed class.

### 23.2 The gate bug the A/B exposed (fixed beside this record)

No CAPTURE_NOW succeeded card-less: `capture refused - card full
(keep-everything archive...)` three times (balloon17.log:398/:461/:490)
— a FALSE message (the card was ABSENT, not full). Root cause:
`hasHeadroomFor` returned FALSE when the store was unavailable — an
"honest card-full-adjacent refusal" written for the keep-everything
gates that predates the D-03 volatile fallback and STRANDED it: without
a card, no capture could ever pass the pre-gates, so the
PSRAM-buffer regime built in 02.5-01 was unreachable through the command
path. FIX: `hasHeadroomFor` now returns TRUE when the store is
unavailable — the capture proceeds, `persistCapture`'s !available branch
names the module-side skip, and the caller takes the fallback regime.
All three consumers (CommandHandler CAPTURE_NOW, AutoCapture::fire,
persistCapture's Step-1 backstop) share the one implementation, so the
single fix covers the path; persistCapture's Step-1 comment's mounted-
invariant still holds (its !available branch returns first). Committed
beside this record; the balloon17 card-less session is also the proof
the fallback REGIME is stable — zero crashes under it.

### 23.3 The fix ladder for the confirmed trigger (recorded, not actioned)

With the class confirmed, the engineering options in cost order:

1. **Rewire the card off the JTAG pins** (CLK 39→ a non-JTAG GPIO; DATA
   40 → likewise — free S3 GPIOs exist; GPIO35-37 are PSRAM-bound on
   this 8 MB module, so pick from 33/34/47/... per the DevKitC header).
   Removes the trigger at the board level; the keep-everything archive
   comes back. THE real fix for flight.
2. **Disarm stall-on-halt** (`systimer_ll_counter_can_stall_by_cpu(...,
   false)` for both CPUs at boot): software-only, removes the stage-2
   systimer death even if the JTAG/halt event still occurs — but a
   spurious halt itself would still freeze the cores (a halt is only
   clearable by the debugger), so this mitigates without curing.
3. **Keep-the-card-out** (volatile fallback): works TODAY post-fix
   (captures now pass), at the cost of the persistent archive and the
   boot-resume machinery — the flight decision if the rewire waits.

The JTAG-vs-electrical split inside the confirmed class (does the card's
CLK on MTCK latch the TAP, or is it local rail/ground interaction?) is
decidable later by option 1 alone: a rewire that eliminates the deaths
closes the class. (01-UAT.md G-01-10, WINDOWS 15.)

## 24 — SESSION-21 (balloon18 bench, 2026-08-30): the gate fix works card-less — and a NEW death signature appears: 12 silent `rst:0x8 TG1WDT_SYS_RST`, every one AT THE CAMERA CAPTURE MOMENT — the SDMMC-claim vs camera discrimination wired as the next A/B

### 24.1 Provenance + the session's two findings

`balloon18.log` (2,773 lines, 13 boots: 1 POWERON + 12 `rst:0x8
TG1WDT_SYS_RST`) / `base18.log` (478 lines), 2026-08-30, build `69fad61`
(the capture-gate fix), card still out (mount fails 0x107 as designed).
FINDING 1 — the gate fix VERIFIED card-less: `Captured image ID 71` →
`enqueued image 71 (full 7738 B, thumb 1113 B / 5 chunks)` → thumbnail
**base-confirmed COMPLETE** and the base finalized thumb (5/5) AND full
(35/35) (base18.log:98/:315) — the capture→push→ACK→deliver chain ran
end-to-end with no card, for the first time. FINDING 2 — a NEW death
signature: **`rst:0x8 TG1WDT_SYS_RST` ×12, never before seen in the
campaign** — the INT-WDT's hardware stage-2 reset WITHOUT its panic ever
printing (the panic interrupt could not run: the wedge context sat at or
above the int-wdt's level — cf. balloon14's CPU1 parked at INTLEVEL 4,
§18.2, whose full expression this may be).

### 24.2 The deaths track the CAMERA CAPTURE MOMENT

Four of four sampled TG1WDT deaths show the same last lines:
`CommandHandler: CAPTURE_NOW` → `Camera: Image captured, size: 6xxx
bytes, duration: 0 ms` → **instant reset**, nothing between — the wedge
hits in the post-capture processing before the next Serial line. [STAMP]
gaps −3/−4/−6 ms with both cores' tick stamps within 1–3 ms — the whole
system, all the way down, in the same instant. Boot lifetimes mixed
(2.9 s/3.0 s short deaths at captures; 102 s/138 s long boots too —
image 71's boot survived its capture and delivered).

**This re-frames balloon17's zero-death verdict (§23.1), recorded as the
correction it is**: balloon17's captures were REFUSED by the gate bug —
the camera never ran — so "card-less = crash-free" was confounded with
"card-less AND camera-idle". The confirmed statement is now narrower and
sharper: **the deaths require the SDMMC subsystem to have CLAIMED the
JTAG-domain pins (39/38/40 muxed, controller clocked — even a failed
begin does that) AND camera captures to run.** Card-in sessions
(balloon6-16): SDMMC claimed + bus traffic + camera → the TG0WDT/int-wdt
families. Card-less with camera idle (balloon17): alive. Card-less with
captures (balloon18): the new TG1WDT family.

### 24.3 The discrimination wired (the code commit beside this record)

`G01_SDMMC_BEGIN_DISABLED` (build flag, balloon env): skips
`BalloonSdStoreTx().begin()` ENTIRELY — the SDMMC controller stays
unclaimed, pins 39/38/40 never muxed, MTCK silent — while everything
else runs IDENTICALLY (volatile-fallback captures ON via the gate fix,
all instruments armed; the arm prints `[SDMMC] begin DISABLED for A/B`
on the console; both arms compile-verified). The reading rules,
pre-written:

- **Card-less + flag + captures → deaths STOP**: the SDMMC
  claiming/clocking of the JTAG-domain pins is REQUIRED for the wedge —
  the JTAG/electrical family stands, the camera capture is the tripwire,
  and the fix ladder (§23.3 rewire / disarm) targets the right thing.
- **Card-less + flag + captures → deaths CONTINUE**: the camera alone is
  the trigger — a plain camera-driver bug (an entirely different, much
  simpler world: the esp32-camera DMA/interrupt path, XCLK GPIO15, PCLK
  GPIO13 — no JTAG-domain involvement), and the SD-era correlation was
  SDMMC-claim + camera coincidence.

Either branch is another campaign-level answer for one build flag.

### 24.4 Operator question ANSWERED + the A/B image shipped

The operator confirmed the card was FULLY ABSENT during balloon18 (empty
socket — the failed begin's 0x107 timeout is the empty-socket floating
CMD/D0 case, per the driver's own pull-up warning at balloon18.log:108).
So the session-21 deaths are the cleanest possible configuration: no
card, no bus traffic, pins muxed+claimed by the failed begin only — and
still the camera capture moment kills. The discrimination A/B image
(`G01_SDMMC_BEGIN_DISABLED` in the balloon env's build_flags, with a
removal comment in platformio.ini; the arm's console marker
`[SDMMC] begin DISABLED for A/B - controller unclaimed (G-01-10)`
verified present in the built ELF) is ready to flash: card-less, captures
ON, run to the deaths-or-quiet verdict per §24.3's rules. (01-UAT.md
G-01-10, WINDOWS 15.)

## 25 — SESSION-22 (balloon19 bench, 2026-08-30): BRANCH B CONFIRMED — the camera alone is the trigger; the SD card, the JTAG pins, and the SDMMC claim are ALL exonerated — and the fix lever is the framebuffer count (the standing LCD_CAM DMA)

### 25.1 The verdict

`balloon19.log` (1,233 lines, 7 boots: 1 POWERON + 6 `rst:0x8 TG1WDT`)
/ `base19.log` (185 lines), 2026-08-30, build `95b5aaf` (the A/B arm).
`[SDMMC] begin DISABLED for A/B` on every boot (:106/:341/:479 — the
controller NEVER claimed, pins 39/38/40 untouched all session) — and SIX
deaths, every one at the camera capture moment (`CAPTURE_NOW` →
`Camera: Image captured, size: N bytes` → instant reset, five sampled
identical), [STAMP] gaps −3/−3/−3/−4 ms, both cores' ticks within 1 ms —
the same instant whole-system wedge as balloon18, now with the SDMMC
completely out of the circuit. **BRANCH B: the esp32-camera capture is
the trigger. Necessary and sufficient. The SD card, the JTAG-domain pins,
and the SDMMC claim are exonerated** — the entire §22-24 SD-line class
was a confound (balloon17's camera-idle via the gate bug; §24.2's
correction anticipated this).

### 25.2 The mechanism reading (labeled hypothesis, now the working theory)

The S3's esp32-camera drives the **LCD_CAM peripheral** (not the ESP32's
I2S — the architectural difference that makes the S3 path the less
mature one). The balloon's config: XCLK 20 MHz, JPEG, **fb_count 2**,
CAMERA_GRAB_LATEST — which keeps the LCD_CAM DMA **continuously
re-armed in the background between captures**. A standing, always-armed
DMA transfer explains the campaign's shape better than anything before
it: deaths at ANY phase (captures, pushes, lulls) with variable latency
(§21's two-stage structure = the wedge propagating through shared
bus/clock resources after the DMA misfires); the dump-proven death site
(the systimer snapshot-valid spin — the systimer's update handshake dies
when the bus/clock domain wedges — the systimer was the WITNESS, not the
culprit); the silent vs panicking expressions depending on which context
held the spin. The SD-era correlation reduces to: the capture-heavy
bench campaigns co-varied with the SD store's presence, and balloon17's
"card-less = crash-free" was camera-idle (the gate bug). THE SYSTIMER
ERRATUM ARM CLOSES; the "name the lock" question of §15.6 dissolves —
there was no lock; there was a bus.

### 25.3 The fix lever (committed beside this record)

`fb_count = 1` (camera_manager.cpp): the DMA arms ONLY during
esp_camera_fb_get() — fetch-paced, no background re-fill. The no-PSRAM
configuration path (the driver's own single-buffer mode) — a tested
driver state. Costs: no double-buffering (the CR-03 stale-frame drain
loses its second-buffer premise — harmless at the balloon's interval
cadence). Expected reading, pre-written: deaths STOP (or collapse to
rarity) at the balloon's interval cadence → the standing-DMA theory
CONFIRMED and the balloon is flight-viable with the camera; deaths
continue at the same rate → fetch-paced DMA still wedges → next levers:
XCLK 20→10 MHz, then the esp32-camera driver version audit for the
pioarduino 3.3.9 S3 LCD_CAM path. The remaining honest thread: whether
EVERY SD-era death (including the no-CAPTURE_NOW resume-push deaths of
balloon11/13) reduces to the camera is not yet proven — the
capture-command/auto-interval coverage of those boots needs the base-log
cross-check; the fb1 world decides it empirically (card back in + fb1 +
camera: if quiet, the whole campaign was one bug). (01-UAT.md G-01-10,
WINDOWS 15.)

## 26 — SESSION-23 (balloon20 bench, 2026-08-30): fb1 REFUTED — byte-identical deaths — the wedge sits in the capture path itself; XCLK 10 MHz + DRAM frame buffer wired as the signal-domain reduction

### 26.1 The refutation

`balloon20.log` (1,139 lines, 7 boots: 1 POWERON + 6 `rst:0x8 TG1WDT`) /
`base20.log` (109 lines), 2026-08-30, build = `d3cf356` (fb_count 1, A/B
flag still armed, card out). The death signature is BYTE-IDENTICAL to
balloon18/19: six deaths, all `rst:0x8`, all at
`Camera: Image captured, size: N bytes` (sizes 6520-10793 B), instant
wedge. **fb_count 1 — fetch-paced DMA, no background re-fill — dies
exactly like fb_count 2. The standing-DMA theory is REFUTED.** What
survives: the trigger is the capture path itself, post-fb_get (the
driver RETURNS the frame — the print follows the return — and the wedge
lands in the buffer handling after it), independent of buffering mode,
card state, and SDMMC claim.

### 26.2 The signal-domain reduction (the code commit beside this record)

Two config levers wired together (one theme: remove the S3 capture
pipeline's highest-risk specifics; if it quiets, bisection can follow —
for the flight decision both stay):

1. **XCLK 20 → 10 MHz**: halves the sensor/LCD_CAM signal-domain rate —
   the standard first reduction for S3 LCD_CAM capture instability.
2. **fb_location PSRAM → DRAM**: the S3's LCD_CAM DMA writes frame
   buffers into PSRAM through the **EDMA/cache path** — the
   highest-risk specific of the S3 capture pipeline, and the wedge lands
   post-fb_get where the EDMA/PSRAM-cache interaction is live. A DRAM fb
   (SVGA JPEG at the balloon's sizes, 6-23 KB, fits the internal heap at
   one buffer) takes the EDMA/PSRAM path entirely out of the capture.

Pre-written readings: deaths STOP → the S3 EDMA/PSRAM-cache and/or
signal-domain interaction under LCD_CAM is named, and the balloon is
flight-viable with both reductions kept; deaths PERSIST with both → the
esp32-camera driver's S3 LCD_CAM path itself is the suspect — the
driver-version audit for the pioarduino 3.3.9 embedded esp32-camera is
the next move (network). The XCLK/fb_location changes are flight-viable
configurations in their own right: XCLK 10 MHz costs capture speed the
balloon never needed; a DRAM fb costs PSRAM headroom it has. (01-UAT.md
G-01-10, WINDOWS 15.)

## 27 — SESSION-24 (balloon21 bench, 2026-08-30): the signal-domain reduction REFUTED too — XCLK 10 MHz + DRAM fb still dies at captures — and the audit finally names the variable every test left constant: CAMERA_GRAB_LATEST is a CONTINUOUS-capture mode

### 27.1 The second refutation

`balloon21.log` (634 lines, 4 boots: 1 POWERON + 3 `rst:0x8 TG1WDT`) /
`base21.log` (28 lines), 2026-08-30, build `0bbf150`. The banner confirms
`XCLK freq: 10000000 Hz` (the DRAM-fb DEBUG line is release-silent) —
and three deaths, all `rst:0x8`, all at
`Camera: Image captured, size: N bytes` (6770/3766/3766 B — the DRAM-fb
captures themselves work; the wedge still lands post-return). **The
signal-domain reduction is refuted: XCLK 10 MHz + DRAM framebuffer dies
exactly like 20 MHz + PSRAM.** Four capture-pipeline configurations have
now produced byte-identical deaths (fb2/PSRAM/20MHz, fb1/PSRAM/20MHz,
fb1/PSRAM/10MHz, fb1/DRAM/10MHz — across SDMMC claimed and unclaimed,
card in and out).

### 27.2 The audit names the constant: CAMERA_GRAB_LATEST never stopped capturing

Every prior lever varied configuration AROUND a continuously-running
DMA. `CAMERA_GRAB_LATEST` (esp32-camera semantics) means the driver
CAPTURES CONTINUOUSLY, replacing the buffer in a loop — **with
fb_count 1 included**, which is why the session-22 fb1 lever was a
no-op on the DMA duty cycle and "refuted" nothing: the exposure never
changed. The XCLK and fb_location levers varied the clock and
destination OF a transfer that never stopped. THE ONE VARIABLE NONE OF
THE TESTS TOUCHED IS THE DMA ITSELF.

### 27.3 The lever (committed beside this record)

`grab_mode = CAMERA_GRAB_WHEN_EMPTY`: the driver captures ONLY when
esp_camera_fb_get() is pending — the LCD_CAM DMA idles between
captures. The balloon's interval cadence never wanted a standing DMA;
all four prior reductions (fb1, DRAM fb, XCLK 10 MHz — kept, they are
flight-viable configurations) stay in place around it. Pre-written
readings: deaths STOP → the continuous LCD_CAM DMA is confirmed as the
trigger and the campaign's every death since balloon6 unifies under it
(including the no-CAPTURE_NOW resume-push deaths — the continuous DMA
runs from boot regardless of capture commands, which is how the 52-row
pushes died with the camera "idle"); deaths CONTINUE with the DMA
truly fetch-paced → the continuous-DMA family closes too and the
suspect becomes the esp32-camera driver's S3 LCD_CAM path itself
(version audit, then a pin-level/driver-patch investigation).
(01-UAT.md G-01-10, WINDOWS 15.)

## 28 — SESSION-25 (balloon22 bench, 2026-08-31): WHEN_EMPTY also dies at captures — the continuous-DMA family CLOSES — and reading our OWN death window names the site at last: the QQVGA downswitch (two mid-stream SCCB re-programs) + drain fetch + thumbnail fetch, in OUR capture sequence — the switch is retired

### 28.1 The third refutation, and where it pointed

`balloon22.log` (966 lines, 6 boots: 1 POWERON + 5 `rst:0x8 TG1WDT`),
2026-08-31, build `0b15776` (GRAB_WHEN_EMPTY + all prior reductions).
Five deaths, all at `Camera: Image captured` — byte-identical. The
continuous-DMA family CLOSES: with the DMA truly fetch-paced, the fetch
itself still wedges. But THIS refutation finally forced the reading of
the death window on the balloon side — the wedge sits AFTER the
"Image captured" print (which `captureImage()` emits after
`esp_camera_fb_get()` RETURNS) and BEFORE `Captured image ID N` (the
CommandHandler's post-capture line, never printed in a death boot).
Between them runs OUR code: `captureThumbnail()` → `createThumbnail()`'s
**QQVGA downswitch** — `setFrameSize(FRAMESIZE_QQVGA)` + `setQuality(20)`
(two SCCB re-programs to a streaming sensor) — then the CR-03 drain
fetch, then the thumbnail fetch. A live sensor reconfiguration sandwiched
between two fetches, on EVERY capture, in EVERY one of the 24 prior
configurations — which is why every hardware lever "refuted" nothing:
the sequence ran unchanged through all of them.

### 28.2 The fix (committed beside this record)

`createThumbnail` no longer captures anything: the thumbnail IS the
full frame's bytes, copied, when they fit the IMG-02 thumbnail budget
(`THUMB_MAX_BYTES` = 8192 — the balloon's QVGA-class fulls run
3.7–10.8 KB, inside the budget most captures); over-budget frames take
the honest no-thumbnail path the enqueue already handles (thumbLength 0
→ the ANNOUNCE_FULL path; the base pulls the full). No sensor ops, no
second fetch, no switch — the sensor holds one size/quality boot to
boot. The CR-04 dangling-member discipline is preserved; the old
CR-03 stale-impostor class becomes impossible by construction (there is
no second frame to be stale).

### 28.3 Pre-written readings + the honest remainder

- Deaths STOP → the mid-stream sensor reconfiguration under LCD_CAM DMA
  is NAMED as the wedge, and the balloon is flight-viable as built:
  thumbnails carry full-frame bytes within budget, the base pulls fulls
  beyond it. All prior reductions (fb1, DRAM fb, XCLK 10 MHz,
  WHEN_EMPTY) stay — each is flight-viable and each removes a risk
  class.
- Deaths PERSIST → the remaining window shrinks to
  getCurrentImage/post-capture bookkeeping (the PSRAM copy, the NVS
  id-commit) — the interim-print bisector (a named line between each
  step) becomes the tool, and the suspect list is down to project code.
- The honest remainder, unchanged: the SD-era resume-push deaths
  (balloon11/13's boots with no CAPTURE_NOW printed) still need their
  capture-coverage cross-checked against the base logs' command timing —
  the quiet world that §28.3's first reading opens would make the check
  academic (the whole family fixed), and it runs anyway on the next
  card-in session. (01-UAT.md G-01-10, WINDOWS 15.)

## 29 — SESSION-26 (balloon23 bench, 2026-08-31): the QQVGA retire did NOT stop it — §28.3's persist branch taken — and the FIRST Saved-PC decode of the TG1WDT era names a new expression: the fault machinery itself (double exception, panic spin, one unmapped PC); the death window is bisected to three statements and the [CAPWIN] instrument + NVS-off A/B are wired

### 29.1 The verdict and the window, after the retire

`balloon23.log` (1,101 lines, 7 boots: 1 POWERON + 6 `rst:0x8 TG1WDT_SYS_RST`),
2026-08-31 08:24, card out, SDMMC arm still on
(`[SDMMC] begin DISABLED for A/B` every boot). **Six deaths, all at
`Camera: Image captured, size: 6128-6391 B, duration: 0 ms` → instant reset,
byte-identical to balloon18-22. The session-25 QQVGA retire (cfb1ca5) is
provably IN the deployed image** (the `exceeds thumbnail budget` string is in
the flashed firmware.bin) and **unexercised by the deaths**: the manual
CAPTURE_NOW path (command_handler.cpp handleCaptureNow) never calls
captureThumbnail at all — the retire removed a sequence the manual path did
not run. §28.3's persist branch: the remaining window shrinks to project code.

Provenance resolution (recorded because the banner misleads): every boot
prints `Build: Aug 30 2026 22:25:37`, yet the binary contains cfb1ca5 code —
the banner is `__DATE__ " " __TIME__` (main_balloon.cpp:73) and
main_balloon.o was not recompiled after 22:25:37 (only camera_manager.cpp /
auto_capture-side files changed since), so pio's incremental link reused the
stale banner. The on-disk ELF (mtime Aug 31 00:10, SHA256 `b2b11394…`) IS the
deployed build; the decode below is against it.

### 29.2 THE DECODE: the TG1WDT era dies in the fault machinery — a new expression

First Saved-PC decode of the whole TG1WDT era (38 samples, balloon18-23,
addr2line `-pfiaC` against the deployed ELF; the era's logs were never decoded
before this round):

| PC (count) | Symbol |
|---|---|
| 0x403743c0/c3/c5/c8 (20) | `_DoubleExceptionVector` (xtensa_vectors.S:564-568) |
| 0x40378218/1a/20 (7) | `_xt_panic` (panic_handler_asm.S:28-30 — the panic spin loop) |
| 0x403782a6/ac (6) | `rtc_cntl_ll_disable_tagmem_retention` (inlined `rtc_cntl_hal.c:144` — the cold-restart/panic HAL path) |
| 0x40377ab1 (1) | `_xt_handle_exc` (xtensa_vectors.S:725) |
| 0x403ea61c (4 — balloon19 ×1, balloon23 ×3) | **UNRESOLVABLE — beyond all linked IRAM** (`.iram0` ends 0x40386300; the address sits in the unmapped cache-window region) |

**Zero project frames, zero kernel tick-chain PCs** — the exact OPPOSITE of
the SD-era censuses (§12.4/§13.5: tick_hook, xPortEnterCriticalTimeout,
xTaskIncrementTick, SysTickIsrHandler — a CPU ALIVE in the tick/lock paths).
The TG1WDT-era CPU is photographed ALREADY INSIDE THE FAULT PATH: exception
#1 fired, the handler context faulted AGAIN (double exception — the classic
cause: a fetch from flash while the flash cache is suspended, or a wild
jump from corrupted control flow), the panic machinery spun at a level the
INT-WDT could not preempt, and MWDT1's stage-2 reset silently at ~600 ms —
which is why nothing ever prints and the reset lands 300-600 ms class after
the freeze. The `0x403ea61c` samples add the wild-PC reading: at least
sometimes the CPU JUMPED TO AN UNMAPPED ADDRESS — control-flow corruption.

### 29.3 The window, bisected from code

With the QQVGA switch gone, the manual path between the last console line
and the never-printed `CommandHandler: Captured image ID N` is exactly three
statements (command_handler.cpp handleCaptureNow + auto_capture.cpp):

1. `AutoCap().allocateImageId()` → `imageIdPrefs.putUShort(kImageIdKey, …)` —
   **Preferences.putUShort = nvs_set_u16 + nvs_commit: a REAL flash write
   (cache suspension + other-core stall), the only heavyweight in the
   window**;
2. `AutoCap().markCaptureBaseline()` → `millis()` (trivial);
3. the `Serial.printf("CommandHandler: Captured image ID %d\n", …)` itself.

Corroborating context, recorded as hypothesis not fact: the NVS partition is
the minimal 20 KB (partitions.csv `nvs,0x9000,0x5000`); ~24 bench sessions of
per-capture u16 writes (each changed value = new entry + old garbage) put the
partition deep into garbage-collection territory, where a write can carry a
4 KB sector erase (a tens-of-ms cache-suspended window) — consistent with
both the era boundary (balloon4/5-era captures ran clean at the same NVS
call) and "crashing every time NOW". The wild-PC/corruption reading (§29.2)
stays the competing hypothesis: an LCD_CAM DMA scribble consuming a return
address would land its death at the next call boundary — the same window —
and the bisector cannot distinguish location-of-death from
cause-of-corruption when the death is between two flushed lines. Both
readings predict DIFFERENT bench signatures (below).

### 29.4 The round's package (committed beside this record)

1. **[CAPWIN] bisector** — latch-free flushed lines bracketing the window:
   `[CAPWIN] pre-id id=%u` / `[CAPWIN] post-id` inside allocateImageId (both
   capture paths covered), `[CAPWIN] post-baseline` after markCaptureBaseline
   in handleCaptureNow. Flush guarantee: a printed line is ON THE WIRE before
   execution proceeds, so presence proves reach; silence names the step.
   Removal: with the G-01-10 instrument family.
2. **`G01_CAPWIN_NVS_ID_DISABLED` A/B arm** (platformio.ini, balloon env,
   removal comment in-source): begin() forces the EXISTING fail-open path
   (`imageIdPrefsOpen = false`) — RAM-only IDs, zero flash writes anywhere in
   the capture window, capture flow otherwise byte-identical. Console marker:
   `[CAPWIN] NVS id-commit DISABLED for A/B - RAM-only IDs this session`.

Pre-written readings (one session answers):

- **Deaths STOP with the A/B armed** → the NVS flash write in the capture
  window is NAMED as the death interaction (§29.3 hypothesis 1). Fix shape:
  move the id-commit OUT of the capture window (queued commit from the 1 Hz
  loop block, RAM-authoritative ID handed out immediately) — behavior-preserving,
  the overwrite bug stays fixed (RAM advances instantly; a crash loses at
  most the last ID's persistence).
- **Deaths CONTINUE with the A/B armed** → the flash write is exonerated in
  this window; the [CAPWIN] lines then say WHERE it dies: between
  `Image captured` and `pre-id` → the millis/printf/handler-entry path —
  pointing at async/corruption origin (§29.3 hypothesis 2, the DMA-scribble
  class); between `pre-id` and `post-id` (NVS skipped — near-empty interval) →
  an async event landing in a µs-scale window = corruption, not code;
  between `post-id`/`post-baseline` and the ID print → the printf path.
- **The bisector prints never appear at all** → the death moves BEFORE the
  id allocation — i.e. inside captureImage()'s tail — contradicting the
  "Image captured" print's presence; record and re-read (would itself be a
  new fact).
- Companion one-shot (operator's call, hardware-only): `esptool erase_flash`
  then re-flash — if deaths drop from every-capture to rare WITH NVS writes
  re-enabled, the §29.3 GC-frequency correlation is independently confirmed.

The SD-era honest remainder (capture-coverage cross-check of balloon11/13)
is unchanged; the SDMMC arm stays on until G-01-10 closes. (01-UAT.md
G-01-10, WINDOWS 15.)

## 30 — SESSION-27 (balloon24 bench, 2026-08-31): THE [CAPWIN] A/B VERDICT — deaths STOP — the NVS flash write in the capture window is NAMED — and the fix lands: the id-commit is DEFERRED out of the capture window (session-27, code committed beside this record)

### 30.1 The verdict

`balloon24.log` (860 lines, ONE boot) / `base24.log` (240 lines), 2026-08-31
10:14, the round-#16 image (`9e063f5`: [CAPWIN] bisector + the
G01_CAPWIN_NVS_ID_DISABLED arm), card out, SDMMC arm still on. Census:
**rst:0x8 = 0, rst:0x7 = 0, rst:0xc = 0, Guru = 0 — ZERO crash signatures
for the first capture-active session since balloon17** (which ran zero
captures; this one ran one to completion). The arm marker printed at boot
(balloon24.log:103 `[CAPWIN] NVS id-commit DISABLED for A/B - RAM-only IDs
this session`). The single capture survived its ENTIRE death window — the
first since the TG1WDT era began (balloon18-23: 41 deaths / 42 captures,
the lone survivor balloon18's image-71 boot):

> balloon24.log:499-503 `Camera: Image captured, size: 5780 bytes, duration:
> 0 ms` → `[CAPWIN] pre-id id=1` → `[CAPWIN] post-id` → `[CAPWIN]
> post-baseline` → `CommandHandler: Captured image ID 1`

and delivered end-to-end: enqueue (volatile fallback — card out) :507-:511,
thumbnail window 10/10 (:693-:728), the base finalized the full image
COMPLETE (base24.log:153-154 `image 1 kind 0 finalized COMPLETE (26/26
chunks, 5780 B)` → persisted to the base card, complete=true). Session ran
minutes past the capture (2100+ loop passes, Max 1361 ms, beacons to
seq=48), ended by the operator — no crash at any phase.

Per §29.4's first pre-written reading: **the NVS flash write in the capture
window is NAMED as the death interaction** — the A/B removed ONLY the
putUShort/commit (the fail-open RAM-only path, byte-identical flow
otherwise) and the death rate went from ~every capture to zero. The
mechanism reading (consistent with §29.2's double-exception decode): a
cache-suspending SPI1 write landing on a JUST-REARMED LCD_CAM capture
(fb_return under WHEN_EMPTY re-arms the DMA within the same milliseconds;
the deferred world simply never overlaps them). Honest scale: n=1 capture
this session against the era's 42 — the verdict rides the A/B's
single-variable construction plus the §29.2/§29.3 evidence chain, and the
next session is the confirmation (below).

### 30.2 The fix (the §29.4 fix shape, implemented)

`allocateImageId()` now hands out the RAM ID immediately (the 01-11
overwrite bug stays fixed — the counter advances and is answered to the
base instantly) and records a PENDING persist; `AutoCapture::process()`
writes it to NVS `kImageIdPersistDelayMs` (1500 ms) after the allocation —
deliberately from the TOP of process(), BEFORE the enabled/camera master
gate, because manual CAPTURE_NOW allocations must persist while the balloon
runs auto-capture disabled. The 1500 ms delay puts the flash write
comfortably past the capture window (the WHEN_EMPTY refill completes
~100-200 ms after fb_return at XCLK 10 MHz). Contract preservation: one
write attempt per allocation; a NEWER allocation refreshes the request
while pending stays set (one NVS value — committing lastImageId covers
every earlier allocation); a failed write degrades to RAM-only for the
boot with the result logged. The G01_CAPWIN_NVS_ID_DISABLED flag and its
begin() block are REMOVED (verdict delivered); the [CAPWIN] bisector lines
STAY (their removal condition is unchanged — they now bracket a µs-scale
RAM-only interval and the deferred write carries its own line).

### 30.3 Pre-written readings for the next bench session

- **`[CAPWIN] deferred id-commit id=N written=2` appears ~1.5 s after each
  capture, and deaths stay ZERO across multiple captures** (the bar balloon24
  could not set: more than one capture per boot, including an unspaced
  series-A push) → the fix is CONFIRMED; G-01-10's camera-capture family is
  closable pending the SD-era resume-push cross-check (§28.3 remainder) on a
  card-in session.
- **A death lands AT the deferred commit** (last lines = the allocation
  bracket, death ~1.5 s post-capture, no `deferred id-commit` line) → the
  flash write is fatal ANYWHERE, not just in the capture window — the
  systimer/flash-cache class of §20 re-opens with a new trigger site, and
  the NVS write moves to a further-reduced window (e.g., boot-time-only
  persistence).
- **Deaths return AT the capture moment despite the deferral** → the A/B's
  n=1 was insufficient — re-arm the NVS-off flag for a higher-n repeat.
- **A crash during a normal (non-capture) boot phase** → unrelated family;
  record per the census discipline.

The SDMMC arm stays on; the [CAPWIN]/[MEM]/[STAMP]/[TICKSTAMP] instruments
stay with their standing removal conditions. (01-UAT.md G-01-10, WINDOWS 15.)

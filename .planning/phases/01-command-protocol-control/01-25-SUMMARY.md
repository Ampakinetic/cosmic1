---
phase: "01"
plan: "25"
subsystem: command-protocol-control
tags: [debug, crash-decode, watchdog, psram, instrumentation, windows-ledger]
requires:
  - "01-24 session-7 crash evidence (balloon.log/base.log, retained untracked operator captures)"
  - "retained deployed ELF .pio/build/esp32-s3-balloon/firmware.elf (SHA256 3d2b351b4, the build that crashed)"
provides:
  - ".planning/debug/d1-crash-regression-push-start.md — retained debug-session doc: full decode, suspect dispositions, ranked root cause, 01-27 bench discriminators (G-01-10's 'root cause NOT established' replaced)"
  - "ImageTxManager boot-time PSRAM first-use warm-up (begin()) — D1 lever 1"
  - "ImageTxManager bounded [MEM] diagnostics (per-enqueue line + 1 s new-low watch) — D1 lever 2, removal condition named (01-27 closes G-01-10)"
  - "ImageTxManager.inboundWindowRequestSeen receipt-ever flag — D2 fix (G-01-11 / WINDOWS 16): boot-epoch zero stamp inert by construction"
affects:
  - "01-27 bench session (D1/D2 discriminators defined in the debug doc §4)"
  - "WINDOWS 15/16 reason fields (root cause recorded; both stay open)"
  - "G-01-7 discriminator semantics (the 're-announce held' line now means genuine inbound traffic)"
tech-stack:
  added: []
  patterns:
    - "receipt-ever flag gating a wrap-safe stamp check (zero-value inertness without muting the log line)"
    - "boot-time first-use heap warm-up (alloc + memset-touch + free before the loop starts)"
    - "monotone new-low latch + 1 s throttle for bounded diagnostic printing"
key-files:
  created:
    - .planning/debug/d1-crash-regression-push-start.md
  modified:
    - src/image_tx_manager.cpp
    - include/image_tx_manager.h
    - .planning/WINDOWS.md
decisions:
  - "Task 2 fix-shape auto-selected under auto_advance per the plan's own 01-23 convention text: option-a lever + option-b bounded instrumentation ride-along (the combination option-a's cons bless when evidence cannot uniquely confirm); recorded in debug doc §5"
  - "Root cause ranked primary: internal-RAM corruption of RTOS/driver state at the first-post-boot capture push (memory-subsystem class on proven-marginal OPI-PSRAM hardware) — with the honest limit stated: the corrupted-context crash class destroys direct proof, mechanism cannot be uniquely confirmed pre-bench, instrumentation is the discriminator"
  - "ARDUINO_LOOP_STACK_SIZE raise REJECTED — stack arithmetic eliminates plain loopTask overflow (8192 B default vs ~1-1.5 KB deepest crash-phase chain); platformio.ini untouched"
  - "No round-#10 revert: every balloon-side round-#10 path provably never executed near either crash (log-proven) — reverting would destroy G-01-7 lever work without removing the mechanism"
metrics:
  duration: "1170s (~20 min agent time; decode/evidence gathering spanned the session's full context)"
  completed: 2026-08-28
status: complete
requirements-completed: [CTRL-01, CTRL-06, PRI-02, IMG-01, IMG-03, IMG-04, PRI-03]
estimate:
  tokens: 22000
actuals:
  tokens: 10800
  tasks: 3
  commits: 2
---

# Phase 01 Plan 25: D1 Crash-Regression Debug Round + D2 Stamp Fix Summary

**One-liner:** Session-7's two balloon crash dumps decoded against the proven deployed ELF (kernel-spinlock wedge + silent TG0 ROM-stall, zero project frames, corruption in three independent subsystems) → every round-#10 suspect eliminated by execution evidence → root cause ranked as internal-RAM corruption at the first-post-boot push → boot-time PSRAM warm-up + bounded [MEM] instrumentation landed, D2 receipt-ever flag makes the boot-epoch busy-hold impossible, builds 2/2 + harness exit 0, WINDOWS 15/16 carry the root cause and stay open for 01-27's hardware verdict.

## What Was Built

### Task 1 — the debug round (commit ed1ec47)

`.planning/debug/d1-crash-regression-push-start.md`, four numbered sections per the house convention (verbatim quotes with file:line):

1. **DECODE** — deployed-ELF provenance cross-check FIRST (panic-printed `ELF file SHA256: 3d2b351b4` matches `sha256sum` of the retained firmware.elf; the two inline-symbol cross-checks 0x40379576→`esp_cpu_wait_for_intr` and 0x4037d7eb→`spinlock_acquire` reproduce balloon.log:306/:358). Every backtrace address then resolved via `xtensa-esp32s3-elf-addr2line -pfiaC -e`: crash 1 Core 1 = INT_WDT with interrupted context IDLE1 and an all-panic-machinery backtrace (0x42055fb0 `panic_print_char_uart` … 0x403762d8 `xt_highint4`), terminal 0x00040022 recorded unresolvable-with-reason (`?? ??:0`, monitor-flagged CORRUPTED); the second panic = IDLE1 canary inside the panic printer (consequence, not cause); crash 1 Core 0 = the decisive wedge signature — the SysTick ISR spinning in `spinlock_acquire`→`xt_utils_compare_and_set` inside `xTaskIncrementTick` (kernel portMUX never released); crash 2 = TG0WDT_SYS_RST with saved PC 0x400559e3 in the ROM flash/cache range (unresolvable in the app ELF, expected for the class). Zero project frames anywhere. The independent smoking gun recorded: pre-crash mojibake in the balloon's own TX bytes (balloon.log:299/:513) + `i2cWrite … ESP_ERR_INVALID_STATE` (balloon.log:301) — corruption in UART ring, I2C driver FSM, and kernel lock, three subsystems no single application overwrite reaches.
2. **SUSPECT DISPOSITION** — (a) 01-21 balloon hunks ELIMINATED (check-and-return bookkeeping; execution-proven absent: no hold/re-arm/eviction/second-drop lines in either crash window — the only round-#10 path that ran is crash 2's boot-window hold check at balloon.log:504, a read-compare-log return ~20-30 s before the reset); (b) 01-23 framer/guard ELIMINATED (provably never fired; the only write is bounds-checked member state); (c) base-side cadence ELIMINATED as trigger (only the standing 30 s GET_STATUS poll at both crash moments, same as session 6); (d) latent instability under first-post-boot push CONFIRMED as class. Stack/heap audit: loopTask 8192 B default vs ~1-1.5 KB deepest chain (plain overflow eliminated), ~206 KB internal heap free at boot (no pressure), E32 AUX polling delay-yields (cannot starve INT_WDT).
3. **ROOT CAUSE (one, ranked)** — internal-RAM corruption of RTOS/driver state during the first-post-boot capture-push phase, memory-subsystem class (OPI-PSRAM/cache on hardware whose PSRAM interface is proven marginal — the session-6 pin-35 saga). Evidence for/against both recorded, including the honest limit: corrupted-context crashes destroy direct proof; the mechanism cannot be uniquely confirmed pre-bench. Round-#10 regression reconciled without causation: layout shift of a latent corruption source and/or session environment variance — indistinguishable pre-bench, both stated. Named lever: (1) boot-time first-use PSRAM warm-up in `ImageTxManager::begin()`, (2) bounded [MEM] diagnostics with removal condition, (3) watchdog reset behavior stays (it is the recovery).
4. **DETERMINISM + DISCRIMINATOR** — both crashes were first-post-boot pushes, but session 6 ran the same path clean 12×: not deterministic at code level, no code-level predicate separates the sessions; hence instrumentation. 01-27 discriminators: D1 = series-A pattern (fresh boot, 3× unspaced CAPTURE_NOW) + spaced + repeat-first-push with zero crash signatures AND zero [MEM] new-low anomalies; D2 = zero busy-hold lines in any boot window before the first genuine inbound window request.

### Task 2 — fix-shape checkpoint (gate honored per plan convention)

`checkpoint:decision gate="blocking"` with `auto_advance: true`: the plan's own context text names the 01-23 convention — the recommended option auto-selects and is recorded for end-of-phase operator confirmation. Selected and recorded in debug doc §5: **option-a lever + option-b bounded instrumentation ride-along** — option-a's cons explicitly bless this combination when Task 1's evidence cannot uniquely confirm the mechanism (which §3 states). No unevidenced lever proceeded; protected surfaces named untouched.

### Task 3 — the fixes (commit 8539c4c)

- **D1 lever 1 — boot-time PSRAM first-use warm-up** (`ImageTxManager::begin()`): `ps_malloc(IMG_MAX_IMAGE_SIZE)` + `memset(warm, 0xA5, …)` + `free`, before the loop starts, with a one-line result log. Moves the session's first run-time PSRAM arena/cache touch out of the live capture-push phase to boot (visible, harmless); inert if first-use is not the trigger.
- **D1 lever 2 — bounded [MEM] diagnostics**: `logMemDiagnostic(phase)` prints `heap / minHeap / psram / stackHW` once per `enqueueCapture` (the exact crash phase, bounded by capture count) and from `process()` on a 1 s throttle only on a NEW monotone low of min-ever-heap or loopTask stack high-water (naturally bounded). Removal condition named in-source and in the doc: strip after 01-27 closes G-01-10.
- **D2 — receipt-ever flag**: `bool inboundWindowRequestSeen` declared beside `lastInboundWindowRequestMs` (include/image_tx_manager.h), initialized false in the constructor AND `begin()`, set true at the stamp site immediately after the `millis()` stamp (same before-kind-validation trust-boundary position), ANDed into the busy-hold gate. The zero stamp is now inert; the hold line itself is NOT muted — balloon.log:504-style phantom episodes are impossible and the line regains its G-01-7 evidentiary meaning.
- **Ledger**: WINDOWS 15/16 reasons appended in BOTH copies (markdown table + JSON) with the established root cause, the landed fix, builds/harness status, and the 01-27 bench discriminator; both stay open; front-matter counts unchanged (3 open / 13 fixed / 16 total — verified by JSON re-parse).

## Verification

- Task 1 automated verify: PASS (grep chain: "Root cause", 0x40379576, 0x400559e3, 866087b, 3d0aaea all present).
- Task 3 automated verify: PASS — `pio run -e esp32-s3-balloon -e esp32-s3-basestation` exit 0 with SUCCESS for both envs (image_tx_manager.cpp recompiled; the one warning, `CAMERA_MODEL_ESP32S3_EYE redefined` in board_config.h, is pre-existing and out of scope), `node scripts/verify_protocol_roundtrip.mjs` exit 0 (all checks PASS), header contains `inboundWindowRequestSeen`, cpp has 6 occurrences (≥2 required), `channelQuietForTx` intact in command_sender.cpp, `'"id": 15'` present in WINDOWS.md (16 too), 4 "01-25 UPDATE" markers (2 entries × 2 copies).
- Round-#10 discriminator log lines verified verbatim post-change: busy-hold episode log (image_tx_manager.cpp:620), budget re-arm log (:1027), second-command-frame drop guard (command_handler.cpp:892). Wire format, 01-18 quiet gate, D-05/D-07 pacing untouched (harness green is the tripwire).
- Protected-surface diff check: `git show 8539c4c` touches exactly src/image_tx_manager.cpp, include/image_tx_manager.h, .planning/WINDOWS.md — command_handler.cpp and platformio.ini needed no change (their conditional levers were dispositioned out by the evidence).

## Deviations from Plan

- **[Rule 3 - blocking-issue avoidance] plan files list vs actual**: `src/command_handler.cpp` and `platformio.ini` are listed in the plan's files_modified but the selected lever names neither site (the framer/guard and stack-raise levers were dispositioned ELIMINATED/REJECTED by the debug evidence) — untouched, exactly as the plan's conditional artifact entries anticipate.
- **[Plan-convention] begin() comment updated**: the 01-21 comment claiming the zero stamp gives a boot-epoch quiet floor was made stale by the D2 fix and was rewritten to state the new semantics (zero stamp inert; boot spacing comes from the IDLE_MS cadence alone).
- No Rule 1/2 code fixes beyond the planned work; no auth gates; no out-of-scope fixes (pre-existing board_config warning left alone).

## Task 2 Selection Record (operator confirmation point)

Auto-selected under `auto_advance: true` per the plan's own 01-23 convention text. The selection for end-of-phase operator review: **option-a (implement the ranked root cause's named lever) with option-b's bounded instrumentation ride-along**. The ranked root cause and both levers are in `.planning/debug/d1-crash-regression-push-start.md` §3; the selection rationale in §5. An operator who disagrees with the ranking can route back to the checkpoint — the levers are independently strippable (warm-up and [MEM] lines are self-contained; the D2 flag is orthogonal).

## Known Stubs

None. The [MEM] instrumentation is deliberate bounded diagnostics with a named removal condition (not a stub of missing functionality); the D1 truth itself honestly awaits 01-27's bench evidence via WINDOWS 15 staying open.

## TDD Gate Compliance

N/A — plan type is `execute` (no tdd tasks; no test-first commits required). The wire harness remains the regression tripwire and exits 0.

## Self-Check: PASSED

- `.planning/debug/d1-crash-regression-push-start.md` — FOUND
- `.planning/phases/01-command-protocol-control/01-25-SUMMARY.md` — FOUND
- Task 1 commit `ed1ec47` — FOUND in git log
- Task 3 commit `8539c4c` — FOUND in git log
- No file deletions in either commit (diff-filter=D empty for both)


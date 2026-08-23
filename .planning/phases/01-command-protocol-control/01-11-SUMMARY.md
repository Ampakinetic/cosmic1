---
phase: 01-command-protocol-control
plan: "11"
subsystem: hardware
tags: [uat, gap-closure, bench-verification, e32-aux-handshake, phantom-tx-failure, g-01-5, g-01-6, image-id-nvs-persistence, g-01-7-concurrent-starvation, vga-fbovf, status-led-gpio39]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: the 01-10 discriminator evidence — G-01-5 mechanism named (E32 AUX phantom TX-failure, e32_lora.cpp:223/:227), G-01-6 branch confirmed (image-ID reboot reset overwrites IMG_00001_*), routing inputs + ride-along list
provides:
  - "G-01-5 RESOLVED on the bench: phantom transmit-failure accounting fixed (f265556) — 0 chunk FAILED across all three post-fix sessions vs ~41/capture baseline; transfers complete reliably with capture spacing (4/4 kinds COMPLETE in the discriminator session)"
  - "G-01-6 RESOLVED on the bench: image-ID counter persisted to NVS (678d4f1) — IDs 1..5 sequential across three reboots; gallery index grows 2->3->4->5 with IMG_00002_*..IMG_00005_* appended, no overwrite"
  - "Ride-alongs R1/R2 verified: STATUS_LED off SDMMC-owned GPIO 39 with edge-gated writes (91bee03) — IO-39 HAL error flood ZERO lines (was 3.5k-9.6k/session); thumbnail QQVGA guard (c67e1a5) — correct sizing every session, the v3 7138-B quirk never recurred"
  - "Two NEW open gaps routed with named levers: G-01-7 concurrent-transfer starvation (serialize transfers / never evict a pending heal; interim mitigation: space captures) and G-01-8 SET_RESOLUTION VGA+ buffer-realloc defect (NACK above allocated buffer or re-init camera)"
affects: [Phase 1 close-out (security gate + G-01-7/G-01-8 future round), IMG-02, IMG-03, IMG-06, /gsd-ship (WINDOWS entries 3/4 open)]

# Actuals (#2632) — pairs with the plan's estimate (6000 tokens) on the same chars/4 scale.
actuals:
  tokens: 2834     # chars/4 over the realized code diffs (11336 chars, 5 files, +130/-24 across f265556/678d4f1/91bee03/c67e1a5); docs/evidence excluded per 01-09/01-10 convention
  tasks: 3
  commits: 6       # 4 code (f265556/678d4f1/91bee03/c67e1a5) + task-2 tracking docs (734baab) + this close-out docs commit

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Benign-handshake reclassification: a complete UART write followed by a missed AUX-low logs 'treated as sent' instead of booking FAILED — TX-failure accounting must distinguish bytes-left-the-radio from bytes-never-written"
    - "NVS-persisted monotonic counters (image-ID) with fail-open fallback to the RAM counter — persistence defects (overwrite-not-append) are fixed at the counter, not at the storage index"
    - "Before/after bench tally discipline: same-series comparison against the 01-10 baseline tallies with verbatim finalize lines, never code-level-only closure (T-01-11-03)"

key-files:
  created: []
  modified:
    - src/e32_lora.cpp               # f265556 (branch c, G-01-5): missed AUX-low after a complete write reclassified benign; short-write and pre-write readiness stay true failures
    - src/auto_capture.cpp          # 678d4f1 (branch e, G-01-6): image-ID counter persisted to NVS across reboot, fail-open; e-site deviation — trace named auto_capture.cpp, not the plan-anticipated sd_storage.cpp
    - include/base_station_config.h # 91bee03 (R1): STATUS_LED remapped 39 -> 41 (off the SDMMC-owned SD_CLK pin)
    - src/main_basestation.cpp      # 91bee03 (R1): edge-gated updateLED writes — kills the per-pass HAL error flood
    - src/camera_manager.cpp        # c67e1a5 (R2): thumbnail frame verified QQVGA-sized before enqueue (guard, not a workaround)
    - .planning/phases/01-command-protocol-control/01-UAT.md  # G-01-5/G-01-6 resolved with verbatim evidence; new G-01-7/G-01-8 entries; Test 3 note updated
    - .planning/WINDOWS.md          # entry 2 fixed; entries 3 (G-01-7) / 4 (G-01-8) appended open

key-decisions:
  - "Task 1 routing (operator-confirmed): branch c + branch e + ride-alongs R1/R2 — the 01-10 evidence forced both defect fixes (AUX accounting for G-01-5, image-ID persistence for G-01-6); no pacing levers pulled, single-lever discipline held (no image_protocol.h constants touched)"
  - "G-01-5 closure evidence standard: mechanism-specific (phantom FAILED -> 0 across all post-fix sessions) PLUS the truth itself (thumbnail + full COMPLETE at bench range with spacing) — session 3's spaced captures are the discriminator that separates the fixed mechanism from the newly-named concurrency residual"
  - "G-01-6 closure evidence standard: persistence proven across THREE reboots (IDs 2/3/4/5 on boots 2/3), gallery index observed growing 2->3->4->5 — the exact inverse of the pre-fix 'locked at 6' symptom"
  - "Concurrent-transfer starvation (G-01-7) recorded as a NEW open gap rather than a G-01-5 residual: session 2 (unspaced) 2 INCOMPLETE verdicts vs session 3 (spaced) 4/4 COMPLETE isolates concurrency as the mechanism; the honest operating note is that unspaced captures still risk starvation until the serialization lever lands"
  - "SET_RESOLUTION VGA defect (G-01-8) recorded as a NEW open gap: latent since 01-04 (sensor-only framesize change, no buffer realloc), exposed by this round's settings series — NOT a regression from the 01-11 commits"
  - "IMG-02/IMG-03 flipped complete: the 01-10 blocking condition ('gap-blocked until 01-11's remediation is bench-verified') is discharged; the narrower residual truths live on as G-01-7/G-01-8 with WINDOWS entries 3/4 keeping /gsd-ship honestly blocked"
  - "Raw bench logs (base*.log / balloon*.log, now holding the three post-fix sessions) stay untracked — verbatim excerpts in 01-UAT.md are the record of note"

patterns-established:
  - "Spacing-as-discriminator: when a transfer defect only reproduces with overlapping captures, run a spaced series (wait for both finalize lines between triggers) to separate concurrency starvation from link loss before assigning mechanism"

requirements-completed: [IMG-02, IMG-03, IMG-06, PRI-03]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "Branch c (G-01-5): E32 AUX phantom transmit-failure accounting fix — a missed AUX-low after a COMPLETE write no longer books FAILED (benign log line instead); short-write and pre-write readiness remain true failures"
    requirement: "IMG-02"
    verification:
      - kind: integration
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation (2 succeeded) + node scripts/verify_protocol_roundtrip.mjs (exit 0, 49/49) — recorded at 734baab"
        status: pass
      - kind: manual_procedural
        ref: "Operator bench, three post-fix sessions 2026-08-24: phantom FAILED 0/0/0 (vs 41/capture baseline); 31/0/90 benign 'E32: AUX-low missed after complete write - treated as sent' lines; spaced series 4/4 kinds COMPLETE — verbatim quotes in 01-UAT.md G-01-5 verified_by"
        status: pass
    human_judgment: false
  - id: D2
    description: "Branch e (G-01-6): image-ID counter persisted to NVS across reboot (fail-open) so fresh-boot captures append IMG_0000N_* instead of overwriting IMG_00001_*"
    requirement: "IMG-06"
    verification:
      - kind: manual_procedural
        ref: "Operator bench: 'CommandHandler: Captured image ID 2' on second boot (balloon2.log:106), IDs 3/4/5 on later boots; 'SdStorage: gallery index built' 2 -> 3 -> 4 -> 5 across sessions with IMG_00002_*..IMG_00005_* persisted — quotes in 01-UAT.md G-01-6 verified_by"
        status: pass
    human_judgment: false
  - id: D3
    description: "Ride-along R1: STATUS_LED remapped off SDMMC-owned GPIO 39 with edge-gated writes (zero __digitalWrite IO-39 HAL error lines, was 3,513-9,563/session) and ride-along R2: thumbnail QQVGA frame-dimension guard (correct sizing 1362-1703 B / 7-9 chunks every session; the v3 7138-B quirk never recurred)"
    requirement: ""
    verification:
      - kind: manual_procedural
        ref: "grep of post-fix base logs: 0x '__digitalWrite' / 'IO 39 is not set' (base.log session 1); finalize lines show thumbnails 7/7 1362 B, 8/8 1465 B, 8/8 1468 B, 9/9 1703 B"
        status: pass
    human_judgment: false
  - id: D4
    description: "Task 1 routing decision recorded with the forcing 01-10 console lines (branch c + branch e + R1/R2; no pacing levers) and Task 3 bench re-verification completed with before/after tallies"
    requirement: "PRI-03"
    verification:
      - kind: other
        ref: "STATE.md decision entry + 734baab (Task 2 tracking) + before/after table in this SUMMARY"
        status: pass
    human_judgment: false
  - id: D5
    description: "Residual findings honestly routed, not silently closed: G-01-7 concurrent-transfer starvation and G-01-8 SET_RESOLUTION VGA+ buffer-realloc defect recorded as open gaps with named levers and missing lists (01-UAT.md), mirrored in WINDOWS entries 3/4"
    requirement: ""
    verification:
      - kind: other
        ref: "01-UAT.md G-01-7/G-01-8 entries; WINDOWS.md entries 3 (open) / 4 (open)"
        status: pass
    human_judgment: false

# Metrics
duration: ~1 day wall-clock across executor sessions + operator bench (Tasks 1-2 on 2026-08-23; bench re-verification sessions 2026-08-24 ~09:41-10:01 local; close-out 2026-08-24)
completed: 2026-08-24
status: complete
---

# Phase 01 Plan 11: G-01-5/G-01-6 Remediation + Bench Re-Verification Summary

**Both 01-10-named mechanisms fixed and operator-verified on the bench — phantom transmit-failure accounting (f265556) collapsed FAILED to 0/0/0 across three post-fix sessions and NVS image-ID persistence (678d4f1) made the gallery grow for the first time (2->3->4->5 across reboots) — while the re-verification series honestly surfaced two NEW open gaps: concurrent-transfer starvation under unspaced captures (G-01-7) and a SET_RESOLUTION VGA+ frame-buffer overflow that fails all captures until reboot (G-01-8)**

## Performance

- **Duration:** ~1 day wall-clock (Tasks 1-2 executor sessions 2026-08-23; operator bench sessions 2026-08-24 ~09:41/~09:53/~10:01 local; close-out 2026-08-24)
- **Tasks:** 3/3 (Task 1 decision checkpoint; Task 2 four atomic fixes; Task 3 bench re-verification checkpoint — completed via this continuation)
- **Code files modified:** 5 (commits f265556, 678d4f1, 91bee03, c67e1a5)
- **Commits:** 4 code + 734baab (Task 2 tracking) + this docs commit

## Routing Decision (Task 1)

**Selected: branch-c + branch-e + ride-alongs R1/R2** (operator-confirmed). Forcing lines from the 01-10 evidence:

- Branch c (G-01-5): `E32: Transmit timeout - AUX didn't go low` followed by `ImageTx: chunk(...) FAILED` while the base finalized 4/4 kinds COMPLETE — the defect is the write-then-wait accounting at e32_lora.cpp:223/:227, exactly where the trace points
- Branch e (G-01-6): `CommandHandler: Captured image ID 1` on every fresh boot (balloon3.log:127) + `SdStorage: gallery index built — 6 image(s)` re-printed after each persist — overwrite-not-append, the ID counter is the defect site
- Ride-alongs: GPIO39 `__digitalWrite` HAL flood (R1) and the `thumb 7138 B == full` sizing quirk (R2), both routed from 01-10
- Single-lever discipline held: no pacing constants in image_protocol.h were touched (the discriminator proved the loss signal was fictional — pacing was the wrong lever)

## Before/After Tally — 01-10 Baseline vs 01-11 Post-Fix Sessions

| Metric | 01-10 baseline (pre-fix) | Session 1 (pre-VGA-incident) | Session 2 (unspaced) | Session 3 (spaced, discriminator) |
|---|---|---|---|---|
| Balloon-booked chunk FAILED | ~41/capture (v3: 41 total) | **0** | 0 | **0** |
| Benign "AUX-low missed after complete write" | n/a (defect booked FAILED instead) | 31 | 0 | 90 |
| Spurious cmd=30 window requests | 4-5/capture | ~5 (driven by real END MARKER MISS only) | (tail-window churn, see G-01-7) | 1-2/pass, legitimate |
| END MARKER MISS (real air loss) | 6/1/2 per session, all self-healed | 0 | 6 | 5, all recovered by pass budget |
| Thumbnail verdict | COMPLETE but phantom-driven churn | 7/7 COMPLETE, 1362 B | image 2: INCOMPLETE 6/8 (G-01-7); image 3: 8/8 COMPLETE | image 4: 8/8 COMPLETE 1465 B; image 5: 9/9 COMPLETE 1703 B |
| Full verdict | COMPLETE | 35/35 COMPLETE, 6931 B | image 2: 51/51 COMPLETE; image 3: INCOMPLETE 15/52 (G-01-7) | image 4: 52/52 COMPLETE 10225 B; image 5: 53/53 COMPLETE 10431 B (tail 49..52 pass 2) |
| Gallery index across reboots | LOCKED at 6 (ID 1 re-allocated every boot) | 1 | **2 -> 3** (grows) | **4 -> 5** (grows) |
| Image IDs across reboots | 1, 1, 1 (reset every boot) | 1 | **2, 3** | **4, 5** |
| IO-39 HAL error flood | 9,563 / 3,513 / 6,122 lines per session | **0** | 0 | 0 |
| CAPTURE_NOW verdicts | SUCCESS | SUCCESS x1, then FAILED x4 after the VGA defect (G-01-8) | SUCCESS x2 | SUCCESS x2 |

## Task Commits

Each task was committed atomically:

1. **Task 1: Routing decision** — operator-confirmed branch-c + branch-e + R1/R2; recorded in STATE.md and 734baab (no code)
2. **Task 2: Implement the selected branches** - `f265556` (branch c: e32_lora.cpp phantom-failure accounting), `678d4f1` (branch e: auto_capture.cpp NVS image-ID persistence), `91bee03` (R1: STATUS_LED 39->41 + edge-gated writes), `c67e1a5` (R2: thumbnail QQVGA guard) — builds 2/2 + harness 49/49 green, tracked by `734baab`
3. **Task 3: Bench re-verification** — operator-executed hardware sessions 2026-08-24; statuses flipped on evidence in 01-UAT.md + WINDOWS.md (this commit)

**Plan metadata:** this docs commit (SUMMARY + 01-UAT.md + WINDOWS.md) + tracking commit (STATE.md/ROADMAP.md/REQUIREMENTS.md).

## Bench Evidence — Three Post-Fix Sessions (2026-08-24, consolidated UART0 logging)

### Session 1 (balloon.log / base.log) — before/after table PASSED until the settings series

- Pre-resolution-incident capture passed everything: base.log:68 `ImageRx: image 1 kind 0 finalized COMPLETE (7/7 chunks, 1362 B)` and :186 `ImageRx: image 1 kind 1 finalized COMPLETE (35/35 chunks, 6931 B)`; window requests 0..15 / 16..31 / 27..28 (pass 1+2) / 32..34 — legitimate ARQ, no phantom churn
- Balloon: 46 ImageTx chunk/window sends, **0 FAILED**, 31x benign `E32: AUX-low missed after complete write - treated as sent (bytes left the radio)` (balloon.log:268 et al.)
- R1 verified: **zero** `__digitalWrite`/`IO 39` lines in base.log (baseline: thousands per session)
- **New defect discovered (G-01-8):** balloon.log:582 `CommandHandler: Command SET_RESOLUTION - SUCCESS` (value 9 = VGA, command_protocol.h:72) followed by 4,379x `cam_hal: FB-OVF` (from :588) and 4x `CommandHandler: Command CAPTURE_NOW - FAILED` — all captures dead until reboot. Root cause: setFrameSize (camera_manager.cpp:398-412) changes the sensor framesize only; the PSRAM frame buffers (fb_count=2) stay sized for the boot framesize. Latent since 01-04, NOT a 01-11 regression. Recovery by reboot confirmed (session 2 works)

### Session 2 (balloon2.log / base2.log) — G-01-6 proven; unspaced captures expose G-01-7

- NVS persistence proven on the second boot: balloon2.log:106 `CommandHandler: Captured image ID 2` (pre-fix: ID 1 every boot), :614 `Captured image ID 3`
- Gallery GROWS for the first time: base2.log:243 `SdStorage: gallery index built — 2 image(s)` -> :370 `— 3 image(s)`; persists now append — base2.log:232 `SdStorage: finalized /images/IMG_00002.JPG (51/51 chunks, 10172 B persisted, complete=true)`
- Two honest failures under unspaced (back-to-back) captures: (a) base2.log:296 `ImageRx: image 2 kind 0 finalized INCOMPLETE (thumbnail push stalled; passes exhausted): 6/8 chunks after 3 passes` — the heal starved behind image 2's own full pull (231: kind 1 `COMPLETE (51/51 chunks, 10172 B)`) and was then evicted: balloon2.log:656 `ImageTx: window request for image 3 supersedes older entry image 2; evicted`; (b) base2.log:498 `image 3 kind 1 finalized INCOMPLETE (retransmit passes exhausted): 15/52 chunks after 3 passes` — the base re-requested tail windows 13..14 (seq 55/56) and 14..14 (seq 57/58/59) five times, the balloon armed and sent each time, chunks never arrived (6 END MARKER MISS) — half-duplex immediate-retransmit turnaround friction, bounded by the pass budget

### Session 3 (base3.log / balloon3.log) — the Task 3 discriminator: SPACED captures, everything completes

- Operator waited for both finalize lines between triggers; images 4 and 5 both fully complete: base3.log:75 `image 4 kind 0 finalized COMPLETE (8/8 chunks, 1465 B)`, :237 `kind 1 finalized COMPLETE (52/52 chunks, 10225 B)`, :290 `image 5 kind 0 finalized COMPLETE (9/9 chunks, 1703 B)`, :524 `image 5 kind 1 finalized COMPLETE (53/53 chunks, 10431 B)`
- Tail recovery by pass budget: base3.log:491/:512 `window request queued for image 5 kind 1 (chunks 49..52, seq 87/88, pass 1/2)` -> :524 COMPLETE
- Balloon: 157 chunk sends / **0 FAILED** / 90 benign AUX-missed; base: 5 END MARKER MISS, all recovered
- Gallery index 4 -> 5 (base3.log:84 -> :302/:528)
- **Verdict:** with capture spacing, everything completes; the residual mechanism is concurrent-transfer starvation (new capture supersedes/evicts the previous image's pending transfer) with bounded tail-chunk turnaround friction

## Status Flips (evidence-forced, per the Task 3 contract)

- **G-01-5 -> resolved** (resolved_by: 01-11 f265556 + R2 c67e1a5): named mechanism fixed and verified — 0 phantom FAILED across all three post-fix sessions vs ~41/capture baseline; transfers complete reliably with capture spacing (4/4 kinds COMPLETE session 3 + 2/2 session 1 pre-VGA-incident). Full evidence in 01-UAT.md G-01-5 verified_by
- **G-01-6 -> resolved** (resolved_by: 01-11 678d4f1): persistence proven across reboots (IDs 2,3,4,5 sequential); gallery index grows 2->3->4->5 with fresh-boot captures appending IMG_00002_*..IMG_00005_*. Full evidence in 01-UAT.md G-01-6 verified_by
- **WINDOWS entry 2 -> fixed** with the closure evidence; entries 3 (G-01-7) and 4 (G-01-8) appended open

## New Findings Routed (NOT resolved)

1. **G-01-7 — concurrent-transfer starvation** (severity major, open): truth "a capture triggered while the previous image's transfer/heal is pending does not prevent its completion". Discriminated by session 2 (unspaced: 2 INCOMPLETE) vs session 3 (spaced: 4/4 COMPLETE). Named lever (operator-proposed): serialize transfers — thumbnail completes fully (push + heal) before the full pull starts, and/or never evict a pending heal entry for a new capture; secondary: inter-window RX-settle gap for the 13..14/14..14/49..52 tail-chunk friction (pass-bounded today). Mirrored in WINDOWS entry 3
2. **G-01-8 — SET_RESOLUTION buffer-realloc defect** (severity major, open): VGA (value 9) floods FB-OVF and fails all captures until reboot; setFrameSize never reallocates the boot-sized PSRAM buffers. Fix class: NACK framesizes above the allocated buffer or re-init the camera on size change. Settings visible-effect + CIF 400x296 spot-checks (Test 3 clauses) stay open under this gap. Mirrored in WINDOWS entry 4
3. **R2 outcome recorded as verified:** thumbnail QQVGA guard shipped (c67e1a5); correct sizing observed in every session (1362-1703 B / 7-9 chunks); the v3 7138-B quirk did not recur

**Honest operating note:** unspaced captures still risk starvation (INCOMPLETE thumbnails/fulls) until the G-01-7 serialization lever lands. **Interim mitigation, documented as operating discipline: space captures — wait for both finalize lines (thumbnail + full) before triggering the next capture.**

## Files Created/Modified

- `src/e32_lora.cpp` - f265556: complete-write + missed AUX-low reclassified benign (the G-01-5 fix at the trace-named site)
- `src/auto_capture.cpp` - 678d4f1: NVS-persisted image-ID counter, fail-open (the G-01-6 fix at the trace-named e-site)
- `include/base_station_config.h` + `src/main_basestation.cpp` - 91bee03: STATUS_LED 39->41 + edge-gated writes (R1)
- `src/camera_manager.cpp` - c67e1a5: thumbnail QQVGA frame-dimension guard (R2)
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-5/G-01-6 resolved with verbatim evidence; G-01-7/G-01-8 opened; Test 3 note updated
- `.planning/WINDOWS.md` - entry 2 fixed; entries 3/4 open
- `.planning/phases/01-command-protocol-control/01-11-SUMMARY.md` - this file
- Raw logs (untracked, evidence source only): `balloon.log`, `base.log`, `balloon2.log`, `base2.log`, `balloon3.log`, `base3.log` — now holding the three POST-FIX sessions (the pre-fix 01-10 logs they replaced are quoted verbatim in 01-UAT.md/01-10-SUMMARY.md)

## Decisions Made

See key-decisions in the frontmatter. The load-bearing ones: closure evidence is mechanism-specific AND truth-specific (phantom FAILED -> 0 AND completions observed); the two new residuals are new gaps, not forced closures; IMG-02/IMG-03 flip complete because the 01-10 blocking condition (bench-verified remediation) is discharged — the narrower residual truths live in G-01-7/G-01-8 with the ledger keeping /gsd-ship blocked.

## Deviations from Plan

None this continuation — Task 3 executed as specified (statuses flipped only on operator-observed evidence; raw logs uncommitted). Prior-session deviations, already recorded at 734baab and STATE.md, stand:

1. **Branch c e-site:** the fix landed in `src/e32_lora.cpp` (where the 01-10 trace named the defect, :223/:227), not the plan-anticipated image_tx/rx manager files — the plan's own trace-named-site clause ("fix exactly what the trace names") governed
2. **Branch e e-site:** the fix landed in `src/auto_capture.cpp` (the ID-counter owner), not the plan-anticipated `src/sd_storage.cpp` — same trace-named-site clause
3. **R2 implemented as a guard, not a workaround:** c67e1a5 verifies the thumbnail frame came back QQVGA-sized and bails honestly on full-size frames (the plan anticipated a "quirk look"; the guard is the minimal honest fix)

---

**Total deviations:** 3 (all e-site/scope-shape corrections inside the plan's own trace-named-site and ride-along clauses; none architectural).
**Impact on plan:** No scope creep — every commit is at a trace-named site; single-lever discipline held (zero pacing-constant changes, verified by the unchanged image_protocol.h).

## Issues Encountered

- Session 1's settings series bricked captures until reboot (G-01-8) — the operator recovered by rebooting and confirmed the next session works; the incident is fully captured as an open gap rather than blocking the G-01-5/G-01-6 closures (the transfers before the incident passed the entire before/after table)
- Operator packet tallies vs measured log counts, reconciled this session: "48 chunks sent" (session 1) = 46 ImageTx chunk/window send lines + 2 manifests; "six re-requests" (session 2 tail windows) = 5 tail-window re-requests (seq 55-59) + 1 initial window (seq 53); "161 chunks" (session 3) = 157 chunk lines + manifests/beacons. The SUMMARY and UAT quote the measured counts

## Known Stubs

None — all four code commits are real implementations; no source-code changes were made by this continuation.

## Threat Model Disposition (T-01-11-01..04)

- **T-01-11-01 (DoS/pacing vs beacon priority):** accepted/mitigated — no pacing levers pulled; PRI-01 order-gates green in the Task 2 harness run (49/49); telemetry cadence unimpaired in all bench sessions
- **T-01-11-02 (OPTION EEPROM desync, branch b):** not applicable — branch b was not selected
- **T-01-11-03 (Repudiation/closure without hardware proof):** mitigated — both flips cite operator-observed post-fix console lines quoted verbatim with log attribution; residuals routed as open gaps, never forced closed
- **T-01-11-04 (branch d refetch loop):** not applicable — branch d was not selected

## Threat Flags

None — no new network/auth/file-access surface; all changes are accounting/persistence/GPIO-guard fixes at existing sites.

## User Setup Required

None — bench sessions already executed by the operator.

## Next Phase Readiness

- **Phase 1: all 11 plans executed.** Remaining before phase complete: security gate (`/gsd-secure-phase 1`) and the G-01-7/G-01-8 remediation round (future plan) — WINDOWS ledger has 2 open entries (3/4), so /gsd-ship stays honestly blocked
- G-01-7 named lever ready for planning: serialize transfers (thumbnail push+heal completion before full pull; no heal-entry eviction on new capture) + optional inter-window RX-settle gap
- G-01-8 fix class ready for planning: NACK above the allocated buffer or camera re-init on size change; re-run the settings visible-effect + CIF 400x296 spot-checks after it
- Interim operating discipline (until G-01-7 lands): space captures — wait for both finalize lines between triggers

## Self-Check: PASSED

- Commits f265556 / 678d4f1 / 91bee03 / c67e1a5 / 734baab present in git log (verified at continuation start)
- 01-11-SUMMARY.md (this file) written; 01-UAT.md G-01-5/G-01-6 resolved + G-01-7/G-01-8 open; WINDOWS.md entry 2 fixed + entries 3/4 open (table and JSON copies consistent)
- Every quoted console line re-verified against the raw logs this session (counts: session 1 — 46 sends/0 FAILED/31 AUX-missed/4379 FB-OVF/0 IO-39; session 2 — END MARKER MISS 6, eviction line balloon2.log:656, 5 tail re-requests seq 55-59; session 3 — 4/4 COMPLETE verdicts, gallery 4->5, 157 sends/0 FAILED/90 AUX-missed/5 END MARKER MISS; IDs 1..5 sequential)
- No source-code changes this continuation; raw logs left untracked

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-24*

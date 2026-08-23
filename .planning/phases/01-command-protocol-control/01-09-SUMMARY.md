---
phase: 01-command-protocol-control
plan: "09"
subsystem: hardware
tags: [uat, gap-closure, sd-mmc, e32, air-rate, bench-verification, g-01-3, g-01-4, g-01-5]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: in-page AJAX submit for all section#capture forms (01-07, 754f834)
  - phase: 01-command-protocol-control
    provides: real E32 register protocol + boot-time 9.6 kbps air-rate enforcement on both boards (01-08, e16845a/ce24cd8)
provides:
  - "G-01-3 hardware closure evidence: operator-observed Storage mount (SD_MMC) + operator-observed end-to-end transfer (CRC-verified full image rendered from SD) — the two hardware truths the plan's prohibition demanded"
  - "G-01-4 runtime truth confirmation (browser stays on the admin page across capture triggers)"
  - "G-01-5 named residual: thumbnail push-burst loss (B2/B4-class) with the exact serial discriminator lines that separate heal-lost-on-air from heal-not-serviced — the input to the next gap round"
affects: [Phase 1 close-out (G-01-5 round + security gate), IMG-02, Phase 3 gallery UX]

# Actuals (#2632) — pairs with the plan's estimate (8000 tokens) on the same chars/4 scale.
actuals:
  tokens: 2278     # chars/4 over the realized code diff (794df00)
  tasks: 2
  commits: 1       # code commit 794df00; plan was checkpoint-driven (5 operator bench iterations)

# Tech tracking
tech-stack:
  added:
    - "SD_MMC 1-bit transport (arduino-esp32 SD_MMC class) on the base's built-in slot"
  patterns:
    - "Operator datasheet ground truth beats plan-embedded pin assumptions — the 02-03 'editable constants' contract resolved by replacing the transport, not the pin numbers"
    - "Paired-invariant verification by negative event: the base-only flash severing the link PROVED the 01-08 rate enforcement is load-bearing"

key-files:
  created: []
  modified:
    - include/base_station_config.h   # 794df00: SD_*_PIN constants renamed to the SDMMC slot (CLK=39/CMD=38/DATA=40)
    - include/sd_storage.h            # 794df00
    - src/sd_storage.cpp              # 794df00: SD_MMC 1-bit begin/mount, /images prep

key-decisions:
  - "G-01-3 root cause A was a TRANSPORT mismatch, not wiring: the base's built-in SD slot is SDMMC (CLK=39/CMD=38/DATA=40, no CS) — the 02-03 SPI-breakout assumption never matched this hardware; the authorized scope expansion switched sd_storage to SD_MMC 1-bit (794df00) instead of editing SPI pins"
  - "The air-rate mismatch event (base-only re-flash severed the pair: telemetry gone, window requests TIMEOUT; balloon re-flash at 9.6k restored it) is recorded as POSITIVE evidence — 01-08's boot-time enforcement is real and both-boards-same-rate is a hard link invariant"
  - "Plan closed WITH a documented residual rather than held open: the plan's own success criteria define the residual path ('a named residual mechanism... recorded as the input to the next gap round — not silently dropped'), and the prohibition's two required hardware observations (boot line + completed transfer) are both operator-observed"
  - "Thumbnail residual routed as NEW gap G-01-5 (01-UAT.md) + WINDOWS ledger entry, not inline-fixed — iteration was routing/analysis only; the heal machinery reads correct end-to-end (kind-split arming, thumbBuffer retention, 15-min TTL) so the loss classifies as B2/B4 push-burst transport, pending the serial discriminator"
  - "Full-resolution viewer wish routed to Phase 3 backlog as a todo (.planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md) — presentation-only; the /img/{id} data path is proven"

patterns-established:
  - "Bench evidence tables in summaries: every plan truth judged against the operator's verbatim observation, with unsatisfied clauses named as residuals rather than stretched"

requirements-completed: [IMG-05]

# Metrics
duration: 118min (5 operator bench-checkpoint iterations)
completed: 2026-08-23
status: complete
---

# Phase 01 Plan 09: G-01-3 Hardware Bench Closure (Storage + Transfer) Summary

**Operator-verified hardware closure of UAT blocker G-01-3: SD storage mounts via an operator-authorized SD_MMC transport switch (the built-in slot was never an SPI breakout), and at the bench-enforced 9.6 kbps air rate a triggered capture completed end-to-end with the full image CRC-verified and rendered from SD — the thumbnail push-burst loss that remains is routed as named gap G-01-5, not silently dropped**

## Performance

- **Duration:** ~118 min of executor sessions spanning 5 operator bench-checkpoint iterations (2026-08-22T23:52Z -> 2026-08-23T01:50Z UTC, operator bench time between)
- **Tasks:** 2/2
- **Code files modified:** 3 (one commit, 794df00)
- **Commits:** 794df00 (code) + this docs commit

## Evidence Table — Plan Truths vs Operator Bench Observations

| # | Plan truth | Verdict | Evidence (operator-observed, 01-09 bench chain) |
|---|------------|---------|--------------------------------------------------|
| 1 | Storage truth (root cause A): card mounts, boot line `SdStorage: card mounted, /images ready`, Storage tile OK | **SATISFIED (with authorized deviation)** | Wiring audit escalated to the operator's datasheet: the base has a BUILT-IN SD slot on SDMMC pins CLK=39/CMD=38/DATA=40 (no CS) — the SPI-breakout assumption (SCK=12/MISO=13/MOSI=11/CS=10) never matched this hardware. Authorized scope expansion -> SD_MMC 1-bit switch (794df00). Operator then observed the success verdict AND the dashboard Storage tile OK (green). Note: fulls stream from SD only (main_basestation.cpp:3441), so the later intact full render re-confirms storage on every view |
| 2 | Transfer truth (root cause B): capture completes end-to-end — terminal states without TIMEOUT, progress COMPLETE, thumbnail renders, four files in /images | **CORE SATISFIED; thumbnail clause RESIDUAL (routed as G-01-5)** | Capture: command ACKed, image transferred, full image renders and looks good (operator verbatim). The full is servable ONLY when its sidecar verified complete (D-47) and fulls are COMPLETE only after the esp_rom_crc32_le SD read-back (D-23) — so "full image looks good" is operator-visible proof of a CRC-verified end-to-end transfer at 9.6 kbps. Zero timeout reports. Thumbnail: renders CORRUPT with the honest "Incomplete" badge — NOT the working render the truth intended; see G-01-5 below |
| 3 | Rate change did not sever the pair: round-trip ACKs < 2 s after flashing both boards | **SATISFIED (and stress-proved)** | Telemetry restored + capture command ACKed after BOTH boards ran the 9.6k firmware. The interim base-only flash had severed the pair (balloon still at factory 2.4 kbps — no telemetry, window requests TIMEOUT) — the exact scenario 01-08's tripwire comment warned about, resolved exactly as designed; the negative event proves the enforcement is load-bearing |
| 4 | Discriminator evidence captured so any residual is NAMED, not guessed | **PARTIAL** | Top-level matrix discriminated BY OUTCOME: B1 eliminated (216-B chunk frames transmit and complete — the full's sustained multi-RF-packet stream verified end-to-end), B3 unlikely (the full's longer transmissions completed; PA sag would hit them hardest). Residual narrowed to B2/B4-class on the thumbnail's push burst specifically. Serial trace lines were NOT captured/pasted (operator reported UI-level outcomes) — the heal-phase trace capture is item 1 of G-01-5's missing list |
| 5 | Constants-edit contract (02-03) if wiring differs | **SATISFIED (expanded form)** | 794df00 renamed/edited the constants to the audited hardware (SD_CLK_PIN/SD_CMD_PIN/SD_DATA_PIN per the datasheet) — the contract's purpose (constants follow physical truth, executor rebuilds) exercised via the authorized transport switch |

**Prohibition check (T-01-09-03):** "MUST NOT mark G-01-3 resolved on code-level evidence alone — closure requires the operator-observed boot line AND an observed completed transfer on hardware." Both hardware observations exist: Storage tile OK observed by the operator (Task 1 chain) and the completed capture with an intact, SD-served full image observed by the operator (Task 2 chain). The prohibition is satisfied; G-01-3 is marked resolved in 01-UAT.md.

## The Two Checkpoint Chains (full bench narrative)

### Chain A — Storage root cause (Task 1, closed at iteration 3)

1. Checkpoint 1 asked for the four-wire audit against the SPI constants. The operator's audit produced a DATASHEET finding instead of a wiring mismatch: the base station board has a built-in microSD slot wired to the ESP32-S3 SDMMC peripheral (CLK=GPIO39, CMD=GPIO38, DATA0=GPIO40) — there is no CS line and there never was an SPI breakout on those pins. The 02-03 plan's SPI wiring was an assumption deferred to "hardware UAT" — and this session was that UAT.
2. Deviation Rule 4 checkpoint returned (new transport = structural change); the operator authorized the scope expansion at the checkpoint.
3. Executor switched `sd_storage` to SD_MMC 1-bit mode (commit 794df00): constants renamed SD_CLK_PIN/SD_CMD_PIN/SD_DATA_PIN, begin/mount path rewritten, GPIO39 STATUS_LED coexistence verified safe (its pinMode at main_basestation.cpp:2060 precedes SDStorage().begin() at :2117).
4. Operator re-flashed and observed the mount verdict + Storage tile OK (green). Card identity (original vs the known-good second) was never explicitly answered — moot: a card mounted, persisted, and later served the verified full image off the card.

### Chain B — Link + transfer root cause (Task 2, closed at iteration 5)

1. After 794df00 the base was re-flashed ALONE — and the link went dead: no telemetry, window requests TIMEOUT. Diagnosis (iteration 4): air-rate mismatch. The base now enforces 9.6 kbps at boot (01-08, ce24cd8) while the balloon still ran pre-01-08 firmware at the factory 2.4 kbps — one-board-only severance, exactly the tripwire scenario 01-08's "modules may mismatch" design anticipated. This event is positive evidence the register writes are real (the module behavior changed persistently).
2. Checkpoint 4 sent the operator to flash the balloon. Iteration 5 report: telemetry restored, trigger ACKed, one capture executed.
3. Capture outcome: the FULL image transferred, verified, and renders intact ("The full image looks good") — G-01-3's transfer symptom (ALL transmissions timing out) is broken on hardware. The THUMBNAIL renders corrupted with the "Incomplete" badge over it — a new, narrower residual, routed below.

## The Residual — G-01-5 Thumbnail Push-Burst Loss (classified, routed, NOT inline-fixed)

**Observation:** thumbnail corrupt + "Incomplete" badge; full intact. **Classification (read-only code analysis, this iteration): (c) inherent residual transport loss (B2/B4-class) inside the designed bounded-degradation path — no code defect indicated by the read; no manual operator repair affordance exists** (the automatic D-22 kind-addressed heal is the only repair path; once terminal INCOMPLETE the id's thumbnail is never re-requested — a fresh capture gets a fresh chance).

The structural asymmetry that localizes it: the FULL transfers as a windowed ARQ pull (per-window verification mid-flight; `passCount` resets on every accepted chunk — image_rx_manager.cpp:605), so its losses heal DURING transfer. The THUMBNAIL transfers as a blind push burst immediately after its manifest; holes are discovered only post-hoc, and the heal is gated until the full pull finishes (gate 1, image_rx_manager.cpp:190) and then bounded to 3 passes (:198-199 "thumbnail push stalled; passes exhausted"). The partial `IMG_{id}_T.JPG` streams to SD during reception (acceptChunk writes every chunk, :595), so the corrupt render + D-48 badge is the designed honest degradation working as built. Balloon-side servicing verified present (kind-split window arming slices `thumbBuffer`, image_tx_manager.cpp:634-732; buffers retained post-SERVED for a 15-min TTL) — the heal was servable, which points at heal-window loss on air (B2/B4) or a timing/margin edge at the new air rate, discriminated by the serial lines in G-01-5's missing list. Whether the heal had exhausted its passes versus still being mid-recovery at observation time is also resolved by those traces.

**Routing:** new gap `G-01-5` in 01-UAT.md (severity major, test 3) + WINDOWS ledger entry 2 (open, kind unmet-truth) — picked up by the next `/gsd-verify-work 1` round. **Deferred to that round:** serial discriminator capture, second-capture re-test, the 01-08 deferred pacing levers if B4 confirms, and the ride-along Test 3 spot-checks (settings visibility, CIF 400x296) that only now became possible.

## Accomplishments

- G-01-3 (UAT blocker, Test 3) RESOLVED on hardware: both independent root causes closed — (A) storage mounts after the SD_MMC transport correction, (B) picture transfers complete without timing out at the bench-enforced 9.6 kbps air rate
- G-01-4 runtime truth confirmed in the same session: the operator triggered captures from the dashboard with no raw-JSON navigation — WINDOWS ledger entry 1 closed fixed
- The air-rate enforcement's real-world effect proven by its own negative event (base-only flash severance) and recovery (balloon re-flash) — the strongest possible confirmation short of an oscilloscope
- Two bench findings routed per GSD conventions: G-01-5 (thumbnail residual, this phase's next gap round) and the full-resolution gallery viewer (Phase 3 backlog todo)
- IMG-05 (base stores received images on SD) marked complete — the intact full was served from the card

## Task Commits

Each task was committed atomically:

1. **Task 1: SD transport switch to SD_MMC 1-bit on the built-in slot (authorized scope expansion)** - `794df00` (fix)
2. **Task 2: Bench discriminator + end-to-end verification** - operator-executed hardware session (no code); evidence recorded here and in 01-UAT.md

**Plan metadata:** docs commit (this file + UAT/STATE/ROADMAP/REQUIREMENTS/WINDOWS/todo routing).

## Files Created/Modified

- `include/base_station_config.h`, `include/sd_storage.h`, `src/sd_storage.cpp` - 794df00 (SD_MMC 1-bit transport)
- `.planning/phases/01-command-protocol-control/01-09-SUMMARY.md` - this file
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-3/G-01-4 resolved, G-01-5 added, Test 3 residual updated, Test 4 pass
- `.planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md` - Phase 3 backlog capture
- `.planning/WINDOWS.md` - entry 1 fixed (G-01-4), entry 2 open (G-01-5)
- `.planning/STATE.md`, `.planning/ROADMAP.md`, `.planning/REQUIREMENTS.md` - tracking updates

## Decisions Made

See key-decisions in the frontmatter — the load-bearing one for future phases: **G-01-3 closed WITH a documented residual under the plan's own residual path** (success criteria: "a named residual mechanism... recorded as the input to the next gap round — not silently dropped"), because the prohibition's two hardware truths are operator-observed and the plan never demanded a flawless thumbnail — it demanded an honest record of anything that still fails.

## Deviations from Plan

### Authorized Scope Expansion (checkpoint-approved, iteration 2-3)

**1. SD transport switched SPI -> SD_MMC 1-bit (Task 1)**
- **Found during:** Task 1 wiring audit checkpoint
- **Issue:** The plan assumed an SPI breakout at SCK=12/MISO=13/MOSI=11/CS=10 (02-03's unverified assumption). The operator's datasheet shows the base's built-in slot is SDMMC (CLK=39/CMD=38/DATA=40, no CS) — a constants edit could not fix a wrong bus peripheral.
- **Fix:** Rule 4 checkpoint returned; operator authorized. sd_storage moved to SD_MMC 1-bit (794df00); constants renamed to the audited pins; GPIO39 LED coexistence verified.
- **Files modified:** include/base_station_config.h, include/sd_storage.h, src/sd_storage.cpp
- **Committed in:** 794df00

### Inherited Context (documented in 01-08-SUMMARY.md, not re-executed here)

**2. SPED manual-conformance correction** — the E32 register layout followed the official manual (SPED [7:6] parity / [5:3] baud / [2:0] air rate) rather than the plan-expected layout; inherited from 01-08 where it was plan-directed ("the manual wins"). The 01-09 bench validated it: the enforcement demonstrably changed module behavior (the mismatch event).

### Judgment Calls (this iteration, routing-only as instructed)

**3. Plan closed with a documented residual rather than held open.** Truth 2's thumbnail clause ("the thumbnail renders in the UI") is NOT satisfied as intended — the thumbnail renders corrupt with the honest Incomplete badge. Holding the plan open would demand a 6th operator iteration for serial traces that the follow-up gap round needs anyway as its baseline; the plan's own success criteria define the residual-recording path as a complete outcome, and its top-level bar ("Storage section reflects the mounted card; picture transmissions complete without timing out") is met on operator evidence.

**Total deviations:** 1 authorized scope expansion + 1 inherited context + 1 documented judgment call. None silently dropped.

## Issues Encountered

- The interim link severance (base-only flash) consumed one bench iteration — but converted 01-08's theoretical one-board-severs-the-pair warning into observed, resolved evidence.
- Two plan questions went unanswered by the operator (card identity; explicit four-file listing on the SD). Neither is load-bearing: a card mounted and served the verified full (storage truth), and the corrupt-thumbnail render + badge + detail-view metadata imply all four files exist (the partial _T.JPG and its sidecar are prerequisites of the observed UI behavior). Recorded as noted omissions, per the truths' own bar.

## Known Stubs

None — no code was authored this iteration beyond 794df00 (a real transport implementation, operator-verified).

## Threat Model Disposition (T-01-09-01..03)

- **T-01-09-01 (Tampering/pin-constant edits):** mitigated — the constants change was the authorized transport switch; the operator-observed mount verdict on the next power cycle is the re-confirmation the mitigation named.
- **T-01-09-02 (DoS/persisted non-factory air rate):** accepted, and now doubly evidenced — both boards enforce 9.6 kbps from begin() every boot, and the mismatch event proved what divergence costs (severance) and that re-flashing one board restores convergence via the same code path.
- **T-01-09-03 (Repudiation/closure without hardware evidence):** mitigated — this SUMMARY records the operator-observed boot/mount verdict and the observed completed transfer; the gap flip to resolved cites both.

## Threat Flags

None — no new network/auth/file-access surface introduced. 794df00 changed a storage bus driver only.

## User Setup Required

None beyond the already-completed bench session. The G-01-5 round will request one capture with both serial consoles logging (lines enumerated in 01-UAT.md G-01-5 missing list).

## Next Phase Readiness

- Phase 1 close-out remains: G-01-5 discriminator round (verify-work) + `/gsd-secure-phase 1`
- IMG-02 stays "Gaps Found" until G-01-5 resolves the thumbnail reliability residual
- Phase 3 picked up the full-resolution viewer todo; the gallery's data paths (index, sidecars, /img routes) are all hardware-proven by this session
- 01-08's deferred follow-up condition is now armed: if G-01-5's discriminator proves B4 saturation at 9.6 kbps, the pacing levers (image_tx/rx margins, heal pass bound, window size) get their own plan

## Self-Check: PASSED

- 794df00 present in git log; touches exactly the three listed files
- 01-09-SUMMARY.md (this file), 01-UAT.md G-01-5 block, todo file, WINDOWS.md entries 1(fixed)/2(open) verified on disk post-write
- G-01-3/G-01-4 gap flips verified in 01-UAT.md (status lines re-read)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-23*

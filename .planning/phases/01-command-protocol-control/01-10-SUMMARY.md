---
phase: 01-command-protocol-control
plan: "10"
subsystem: hardware
tags: [uat, gap-closure, bench-verification, e32-aux-handshake, phantom-tx-failure, g-01-5, g-01-6, wr-01, image-id-reset]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: working 9.6 kbps link + SD_MMC storage + end-to-end transfer proven at the 01-09 bench (01-09, 794df00/ce24cd8)
  - phase: 01-command-protocol-control
    provides: the G-01-5/G-01-6 gap entries with their discriminator line lists (01-09 escalation report)
provides:
  - "G-01-5 mechanism NAMED from dual-console bench traces: E32 AUX handshake phantom transmit-failure (balloon TX accounting defect, e32_lora.cpp:223 write-then-:227-wait) — NOT air loss; real air loss ~1-2 chunks/16, self-healed; B5 close-range RX-overload NOT reproduced (downgraded)"
  - "G-01-6 branch CONFIRMED: persistence overwrite — balloon image-ID counter resets on reboot, re-allocates ID 1, overwrites existing card files; gallery count cannot grow (proven twice with 4/4 COMPLETE transfers)"
  - "WR-01 closed at code level (c9770b1): zero dummy PowerData, validity-gated safety branches, PowerMgr-fed telemetry; runtime regression watch GREEN across all bench sessions"
  - "Routing inputs for 01-11 complete: AUX accounting fix + image-ID persistence are the remediation branches; GPIO39 LED error-flood cleanup + thumbnail-sizing quirk ride along"
affects: [Phase 1 close-out (01-11 remediation + bench re-verification + security gate), IMG-02, IMG-03]

# Actuals (#2632) — pairs with the plan's estimate (5200 tokens) on the same chars/4 scale.
actuals:
  tokens: 1811     # chars/4 over the realized code diffs (c9770b1 6271 chars + 3fc35a7 974 chars); docs/evidence excluded per 01-09 convention
  tasks: 3
  commits: 2        # code commits c9770b1 + 3fc35a7; evidence recorded via this docs commit

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Dual-console per-chunk discriminator: balloon TX accounting vs base finalize verdicts compared line-by-line exposes phantom failures (booked FAILED, data arrived) that single-side logs cannot"
    - "Verbatim-quote evidence discipline: load-bearing console lines quoted into 01-UAT.md with file:line refs; multi-MB raw logs stay uncommitted as the source only"

key-files:
  created: []
  modified:
    - src/main_balloon.cpp            # c9770b1: three dummy PowerData sites -> validity-gated PowerMgr reads + batteryReadingValid() helper (WR-01)
    - platformio.ini                  # 3fc35a7: CDC_ON_BOOT=0 in both production envs (bench deviation — all logs onto monitored UART0)
    - .planning/phases/01-command-protocol-control/01-UAT.md   # G-01-5/G-01-6 discriminator_evidence blocks, Test 3 note
    - .planning/WINDOWS.md            # entry 2 reason updated (mechanism named, B5 downgraded, series deferred)

key-decisions:
  - "G-01-5 mechanism class is a FOURTH class outside the plan's three-way taxonomy (heal-lost-on-air / heal-not-serviced / heal-not-fired): an E32 AUX handshake PHANTOM transmit-failure — e32_lora.cpp transmit() writes bytes at :223 then waits for AUX-low at :227 and books FAILED on timeout, but the data has already left the radio. Evidence: v3 balloon tallied 37 sent / 41 FAILED while the base finalized both kinds COMPLETE 36/36 and received every chunk"
  - "B5 (close-range 30 dBm RX-overload/desense) downgraded: NOT reproduced — every close-range capture on fresh-boot firmware completed with ~zero real loss, leaving B5 no unexplained loss to account for; the escalated 'fulls failing at close range' symptom is attributed to the G-01-6 overwrite interaction plus the v1 phantom-failure session"
  - "G-01-6 remediation direction set: persist the balloon image-ID counter across reboot (or derive next ID from card/index state) so captures append instead of overwriting IMG_00001_* — the overwrite was proven twice with perfect transfers, so it is NOT a G-01-5 symptom"
  - "01-08's deferred pacing levers stay unpulled — single-lever discipline held: the discriminator proved the 'loss' driving spurious window requests (4-5 cmd=30 per capture) is fictional, so pacing margins are the wrong lever; the AUX accounting fix is the right one"
  - "GPIO39 STATUS_LED error-flood (esp32-hal-gpio __digitalWrite warning, 9,563/3,513/6,122 lines per session, ~7-10 ms synchronous blocking each) routed to 01-11 as a ride-along cleanup — NOT the loss mechanism (everything arrived despite it), but operator-visible console noise that impairs bench work"
  - "No requirement IDs flipped complete by this round: IMG-02/IMG-03 stay gap-blocked until 01-11's remediation is bench-verified; CTRL-02/IMG-06/PRI-03 were already Complete from prior phases — the plan's declared requirements list touched them as anchors, not as newly-satisfied truths"
  - "Raw bench logs (base*.log, balloon*.log, multi-MB) stay untracked in the repo root — evidence excerpts quoted verbatim into 01-UAT.md are the record of note"

patterns-established:
  - "Phantom-failure discriminator: when TX-side failure counts disagree with RX-side completion counts, audit the handshake ordering (write-then-wait vs wait-then-write) before blaming the medium"

requirements-completed: []

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "WR-01 code closure — three dummy PowerData sites in src/main_balloon.cpp replaced with validity-gated PowerMgr reads (batteryReadingValid() gate: [1.8,8.0] V AND nonzero raw ADC) so a floating sense line can never trigger emergency or camera-disable, and telemetry stops reporting a fabricated 85%"
    requirement: ""
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation (2 succeeded) + node scripts/verify_protocol_roundtrip.mjs (exit 0, 49/49) + grep gates (0x '0.1f, 85', 0x 'batteryPercentage = 85', >=3x batteryReadingValid)"
        status: pass
    human_judgment: false
  - id: D2
    description: "G-01-5 serial discriminator evidence captured on both consoles with the mechanism class named (AUX phantom TX-failure) and real air loss quantified (END MARKER MISS 6/1/2 per session, CRC FAIL 0, all self-healed)"
    requirement: "IMG-02"
    verification:
      - kind: manual_procedural
        ref: "balloon3.log / base2.log / base3.log dual-console traces quoted verbatim in 01-UAT.md G-01-5 discriminator_evidence"
        status: pass
    human_judgment: true
    rationale: "Operator bench session is the evidence source of record (T-01-10-03); the gap itself stays open pending 01-11 remediation + bench re-verification"
  - id: D3
    description: "G-01-6 zero-tooling discriminator run with the branch named (persistence overwrite via image-ID reset on reboot, NOT refresh-stale) and the deciding console evidence recorded"
    requirement: "IMG-06"
    verification:
      - kind: manual_procedural
        ref: "operator browser check (/gallery?page=1 JSON total == grid == 6) + base2.log/base3.log gallery-index/persist lines quoted in 01-UAT.md G-01-6 discriminator_evidence"
        status: pass
    human_judgment: true
    rationale: "Operator-observed browser comparison is the branch decider; gap stays open pending 01-11 remediation"
  - id: D4
    description: "WR-01 runtime regression watch across all bench sessions — no false 'Critical battery' trigger, no Emergency entry, no camera auto-disable; beacons carried batt=valid"
    requirement: ""
    verification:
      - kind: manual_procedural
        ref: "0x 'Critical battery' across balloon*.log; base3.log 'ImageRx: beacon seq=3 ... batt=valid' (quotes in 01-UAT.md)"
        status: pass
    human_judgment: true
    rationale: "Runtime safety behavior on hardware is operator/bench-observed, not automatable from code"

# Metrics
duration: ~5.5h wall-clock across three executor sessions + operator bench sessions (2026-08-23T03:46Z -> 09:19Z)
completed: 2026-08-23
status: complete
---

# Phase 01 Plan 10: G-01-5/G-01-6 Bench Discriminator Round + WR-01 Summary

**Bench discriminator round named both open mechanisms from dual-console traces: G-01-5's "loss" is an E32 AUX handshake phantom transmit-failure (balloon books FAILED at e32_lora.cpp:227 after the bytes already left at :223 — the base received 72/72 chunks and finalized 4/4 kinds COMPLETE), G-01-6's locked gallery is an image-ID reboot reset that overwrites IMG_00001_* instead of appending, and B5 close-range RX-overload was NOT reproduced — routing both fixes, plus a GPIO39 LED error-flood cleanup, into 01-11**

## Performance

- **Duration:** ~5.5 h wall-clock spanning three executor sessions plus operator bench sessions (2026-08-23T03:46Z -> 2026-08-23T09:19Z UTC; operator bench time between)
- **Tasks:** 3/3 (Task 1 code; Tasks 2-3 evidence-capture checkpoints — recorded via this continuation)
- **Code files modified:** 2 (commits c9770b1, 3fc35a7)
- **Commits:** c9770b1 (WR-01) + 3fc35a7 (log consolidation deviation) + this docs commit

## Evidence Table — Plan Truths vs Bench Observations

| # | Plan truth | Verdict | Evidence (verbatim console lines, log of origin) |
|---|------------|---------|--------------------------------------------------|
| 1 | G-01-6 discriminator: /gallery?page=1 JSON total vs grid count, branch named | **SATISFIED — branch: persistence OVERWRITE (not refresh-stale)** | Operator-observed: JSON total == dashboard grid == 6. Base console: `SdStorage: gallery index built — 6 image(s)` printed AFTER persisting (base2.log:940 post-thumb, :2912 post-full; base3.log:3492/:5893) — index never grows. `CommandHandler: Captured image ID 1` (balloon3.log:127): fresh-boot balloon re-allocates ID 1 and `IMG_00001_*` overwrites the card's existing image-1 files. Overwrite-not-append proven twice with 4/4 COMPLETE transfers |
| 2 | G-01-5 serial discriminator on both consoles, mechanism class NAMED | **SATISFIED — named class is a fourth class outside the plan's taxonomy: AUX phantom TX-failure** | Balloon per-chunk: `E32: Transmit timeout - AUX didn't go low` then `ImageTx: chunk(image 1, 1/36, 200 B) FAILED` (balloon3.log:135-136 et al.); v3 tally 37 sent / 41 FAILED, 43 AUX timeouts. Base received everything: `ImageRx: image 1 kind 0 finalized COMPLETE (36/36 chunks, 7138 B)` (base3.log:3320), `kind 1 finalized COMPLETE (36/36 chunks, 7138 B)` (base3.log:5758); v2: kind 0 `COMPLETE (8/8 chunks, 1465 B)` first-try no-heal (base2.log:866) + kind 1 `COMPLETE (36/36 chunks, 7158 B)` with ONE re-request pass `window request queued for image 1 kind 1 (chunks 14..15, seq 5, pass 1)` (base2.log:1857). Code: e32_lora.cpp:223 `serial->write()` + flush BEFORE :227 `waitForAuxLow(1000)` → FAILED booked after the bytes left. Beacons hit it too: `[BCN] seq=40 sent=39 ok=0` (balloon.log:20) while base accepted every beacon |
| 3 | Reliability tally close vs separated (B5 discriminator) | **DEVIATION — series replaced by three instrumented sessions; B5 NOT reproduced, downgraded** | Every close-range capture on fresh-boot firmware completed with ~zero real loss: real air loss = `END MARKER MISS` 6x (base.log v1), 1x (base2.log), 2x (base3.log); `CRC FAIL` 0 in every session; all recovered in one ARQ pass. Separation series not run (operator-approved); rate quantification rides 01-11 Task 3's before/after series |
| 4 | Second-capture re-test (per-burst vs systematic) | **SATISFIED — per-burst disproven** | v2 AND v3 were each fresh captures on fresh-boot firmware; both thumbnails arrived intact first-try (8/8 and 36/36 COMPLETE). The standing corrupt-thumbnail symptom from 01-09 is mechanism-based (phantom-failure-driven spurious window churn + G-01-6 overwrite), not per-burst RNG |
| 5 | SC-3 ride-alongs: settings visible-effect + CIF 400x296 + WR-01 watch | **PARTIAL — WR-01 watch GREEN; settings/CIF DEFERRED to 01-11 Task 3 (operator-approved)** | Watch: 0x `Critical battery` in all balloon logs, no Emergency entry, no camera auto-disable; `ImageRx: beacon seq=3 alt=0.0m temp=24.0C gps=no-fix batt=valid` (base3.log). Settings visible-effect + CIF spot-checks re-run with 01-11's remediated-firmware bench series |
| 6 | WR-01 closed at code level (no dummy PowerData, validity-gated branches, builds green) | **SATISFIED** | c9770b1: grep gates 0x `0.1f, 85` / 0x `batteryPercentage = 85` / >=3x `batteryReadingValid`; both targets SUCCESS; harness 49/49 exit 0. Documented semantic deviation: thresholds are VOLTS (3.3/3.0 V, balloon_config.h) — comparisons ride measured voltage, not percentage |
| 7 | UI-SPEC loading/populated anchors hold through the ride-along series | **PARTIAL — exercised incidentally, not as the planned series** | Commands submitted in-page across sessions and advanced to terminal states: v2 `[E32TX] cmd=01/20/30` = 1/4/4 rows all ok=1; v3 = 1/6/5; balloon executed each with SUCCESS (`CommandHandler: Command CAPTURE_NOW - SUCCESS`, balloon3.log:130). The formal series rides 01-11 Task 3 with the deferred spot-checks |
| 8 | Terminal Timeout/Failed copy (backstop truth) | **NOT EXERCISED (stands as backstop)** | No genuine command timeout occurred on the instrumented firmware (v1's failures predate instrumentation). The CommandState-to-string mapping remains code-verifiable; hardware degraded-link test stays with the UAT degraded-link round |

**Prohibition checks:** (1) *No fabricated success* — every tally above cites a finalize/console line of record; the two operator-only observations (browser JSON check; live-console boot lines not retained in saved logs) are attributed as operator-reported. (2) *No code-level-only gap closure* — G-01-5 and G-01-6 remain **open** in 01-UAT.md; this round records routing evidence only, closure happens in 01-11 after remediation + bench re-verification. (3) *No invalid-reading safety trigger* — watch green (D4).

## Accomplishments

- G-01-5 mechanism NAMED from dual-console per-chunk evidence: E32 AUX handshake phantom transmit-failure — a balloon-side TX accounting defect (write-then-wait ordering), not air loss; the spurious window requests (4-5 cmd=30/capture) are the false loss signal driving otherwise-correct heal machinery
- G-01-6 branch CONFIRMED with overwrite-not-append proven twice: image-ID counter resets on reboot → IMG_00001_* overwrites existing card files → gallery count cannot grow, even with perfect transfers
- B5 close-range RX-overload hypothesis downgraded (not reproduced); real air loss measured tiny (1-2 END MARKER MISS chunks/16, zero CRC failures, all self-healed)
- WR-01 closed at code level (c9770b1) with a GREEN runtime regression watch across all sessions
- Secondary findings routed: GPIO39 STATUS_LED hot-loop error flood (exact counts 9,563/3,513/6,122 per session) and a thumbnail-sizing quirk (`full 7138 B, thumb 7138 B / 36 chunks`, balloon3.log:132 vs v2's correct 1465 B / 8 chunks) — both queued for 01-11

## Task Commits

Each task was committed atomically:

1. **Task 1: WR-01 closure — validity-gated PowerMgr reads (3 dummy PowerData sites)** - `c9770b1` (fix, +76/-26 src/main_balloon.cpp)
2. **Task 2: Bench discriminator round (G-01-6 zero-tooling, G-01-5 serial discriminator, second capture)** - operator-executed hardware sessions; evidence recorded in 01-UAT.md + this file (no code commit)
3. **Task 3: SC-3 ride-alongs** - WR-01 regression watch green across all sessions; settings/CIF spot-checks deferred to 01-11 Task 3 (operator-approved); evidence in 01-UAT.md Test 3 note

**Plan metadata:** this docs commit (SUMMARY + 01-UAT.md + WINDOWS.md) + tracking commit (STATE.md/ROADMAP.md).

Mid-round code deviation: `3fc35a7` (log consolidation, see Deviations #1).

## Files Created/Modified

- `src/main_balloon.cpp` - c9770b1: batteryReadingValid() helper + three PowerMgr-wired sites, validity-gated safety branches
- `platformio.ini` - 3fc35a7: CDC_ON_BOOT=0 in both production envs (both boards re-flashed after it)
- `.planning/phases/01-command-protocol-control/01-UAT.md` - G-01-5/G-01-6 root_cause updates + discriminator_evidence blocks (verbatim quotes) + missing-list disposition; Test 3 note
- `.planning/WINDOWS.md` - entry 2 reason filled (mechanism named, B5 downgraded, series deferred)
- `.planning/phases/01-command-protocol-control/01-10-SUMMARY.md` - this file
- Raw logs (untracked, evidence source only): `base.log`, `base2.log`, `base3.log`, `balloon.log`, `balloon2.log`, `balloon3.log`

## Decisions Made

See key-decisions in the frontmatter. The load-bearing one for 01-11: **the remediation lever is the AUX handshake accounting, not pacing** — 01-08's deferred pacing levers stay unpulled because the discriminator proved the loss signal itself is fictional.

## Deviations from Plan

### Mid-Round Code Changes (Rules 1/3 — blocking observability)

**1. [Rule 3 - Blocking] Log consolidation to UART0 (CDC_ON_BOOT=0), commit 3fc35a7**
- **Found during:** Task 2 prep (first discriminator attempt)
- **Issue:** discriminator lines (ImageTx/CommandHandler/ImageRx) print via plain `Serial`, which ARDUINO_USB_CDC_ON_BOOT=1 routed to USB-CDC — invisible on the monitored bridge UART0 port; balloon.log/balloon2.log captures show control-plane lines only
- **Fix:** CDC_ON_BOOT=0 in the two production envs (platformio.ini), observability-only; both boards re-flashed before the v3 session. ALL logs on UART0 from then on
- **Files modified:** platformio.ini
- **Verification:** balloon3.log carries the full ImageTx/CommandHandler discriminator set (v1/v2 balloon logs predate it)
- **Committed in:** 3fc35a7

**2. [Rule 1 - Bug semantics] Task 1 thresholds are VOLTS, not percentage**
- **Found during:** Task 1 implementation
- **Issue:** plan text spoke of percentage thresholds; balloon_config.h defines BATTERY_LOW_THRESHOLD 3.3 / critical 3.0 in VOLTS
- **Fix:** comparisons ride measured voltage; percentage flows to telemetry/appState only
- **Files modified:** src/main_balloon.cpp
- **Verification:** builds 2/2 + harness 49/49 + grep gates
- **Committed in:** c9770b1 (documented there)

### Bench-Protocol Deviations (operator-approved)

**3. Step 4 reliability series (8-10 captures close + separated) replaced by three instrumented sessions**
- **Found during:** Task 2 bench execution
- **Issue/Rationale:** the mechanism was caught red-handed with per-chunk dual-side evidence in single-capture runs (v2/v3); close-range completions left B5 nothing to explain, so the separation arm had no residual to discriminate. v1 additionally provided a flood-session observation (the historically-failing profile: zero finalizes, 6 END MARKER MISS)
- **Consequence:** real-loss RATE not formally quantified at range — deferred quantification rides 01-11 Task 3's before/after series on fixed firmware (recorded in 01-UAT.md G-01-6/G-01-5 missing lists and WINDOWS entry 2)

**4. Task 3 settings visible-effect + CIF 400x296 spot-checks deferred to 01-11 Task 3**
- **Found during:** Task 3 bench execution
- **Rationale:** that task re-runs the capture series on remediated firmware; judging visible-effect on firmware about to change wastes an operator session. WR-01 watch portion DID run and is green
- **Consequence:** Test 3 stays `issue` until 01-11's re-verification (01-UAT.md note updated)

---

**Total deviations:** 4 (2 mid-round code fixes under Rules 1/3, 2 operator-approved bench-protocol deferrals). None silently dropped; all four recorded in 01-UAT.md / WINDOWS.md where the next round consumes them.
**Impact on plan:** No scope creep — both code changes were prerequisites for capturing the plan's own evidence; both deferrals move work to the plan that fixes what this round diagnosed.

## Issues Encountered

- The CDC routing issue (Deviation 1) cost one bench generation (v1/v2 balloon consoles partially blind) — converted into the fix that made v3 the fully-instrumented session
- Three evidence-packet claims could not be quoted from the retained logs and are recorded as operator-reported instead: the live-console `CommandHandler: Response failed` ACK false-failures (consistent with the 2 non-chunk AUX timeouts retained in balloon3.log), the boot banner `Emergency Shutdown: Enabled` (power_manager.cpp:706 config echo), and `Battery monitoring active (raw reading: 4095)` (main_balloon.cpp:688) — the saved balloon logs don't include the boot window. One packet numeric was corrected against the log of record: v3's enqueue is `full 7138 B, thumb 7138 B` (balloon3.log:132); 7158 B was v2's full (base2.log:2865)
- ADC full-scale note for flight config: the bench unit's floating sense line reads 4095 raw, which the [1.8,8.0] V gate cannot distinguish from a real pack — expected at bench, flagged in 01-UAT.md

## Known Stubs

None — no code was authored this continuation; c9770b1/3fc35a7 are real implementations.

## Threat Model Disposition (T-01-10-01..03)

- **T-01-10-01 (Tampering/bench frames):** accepted as planned — no new surface; CRC16 + fixed-length receivers unchanged; observed CRC FAIL count 0 across all sessions
- **T-01-10-02 (DoS/invalid battery triggering safety):** mitigated and bench-watched GREEN — the validity gate preceded every safety branch across all sessions with zero false triggers (D4)
- **T-01-10-03 (Repudiation/closure without hardware evidence):** mitigated — every verdict in this SUMMARY cites a console line of record; both gaps deliberately left OPEN because closure requires the 01-11 remediation to be operator-verified

## Threat Flags

None — no new network/auth/file-access surface. 3fc35a7 changes a build flag (log routing) only.

## User Setup Required

None — the bench session already ran. 01-11 will request one more bench re-verification series after remediation.

## Next Phase Readiness

- **01-11 routing inputs complete:** branch (a) G-01-5 = AUX handshake accounting fix at e32_lora.cpp:207-233 (stop booking FAILED after a successful write; or wait-then-write); branch (b) G-01-6 = image-ID persistence across reboot; ride-alongs: GPIO39 STATUS_LED remap + updateLED throttle (main_basestation.cpp:40, :3672/:3676), thumbnail-sizing quirk (image 1 thumb 7138 B == full, balloon3.log:132)
- Gaps G-01-5/G-01-6 stay OPEN in 01-UAT.md + WINDOWS entry 2 until 01-11's bench re-verification — /gsd-ship remains blocked by design
- IMG-02/IMG-03 requirements deliberately not marked complete this round (see key-decisions)
- After 01-11: security gate (`/gsd-secure-phase 1`) before phase complete

## Self-Check: PASSED

- c9770b1 and 3fc35a7 present in git log; c9770b1 touches exactly src/main_balloon.cpp, 3fc35a7 exactly platformio.ini
- 01-10-SUMMARY.md (this file), 01-UAT.md G-01-5/G-01-6 discriminator_evidence blocks, WINDOWS.md entry 2 reason verified written before commit
- Every quoted console line re-verified against the raw logs this session (counts: AUX 43, FAILED 41, sent 37, END MARKER MISS 6/1/2, CRC FAIL 0/0/0, cmd=30 18/4/5, flood 9,563/3,513/6,122, finalize verdicts as quoted)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-23*

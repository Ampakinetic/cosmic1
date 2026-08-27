---
phase: "01"
plan: "24"
subsystem: bench-verification
tags: [bench, uat, evidence, crash-regression, ledger-discipline]
requires:
  - "01-21/01-22/01-23 code rounds (round #10 behavior deployed at ELF SHA256 3d2b351b4)"
provides:
  - "Honest session-7 record in 01-UAT.md (failed bench, nothing closed)"
  - "D1 crash-regression blocker routed (G-01-10 / WINDOWS 15) with suspects and debug path"
  - "D2 spurious boot-time re-announce hold routed (G-01-11 / WINDOWS 16)"
affects:
  - ".planning/phases/01-command-protocol-control/01-UAT.md"
  - ".planning/WINDOWS.md"
  - ".planning/STATE.md"
tech-stack:
  added: []
  patterns:
    - "citation-convention bench recording (every verdict carries a log line number)"
key-files:
  created: []
  modified:
    - ".planning/phases/01-command-protocol-control/01-UAT.md"
    - ".planning/WINDOWS.md"
    - ".planning/STATE.md"
decisions:
  - "Record session 7 as FAILED with zero ledger flips rather than salvage partial verdicts - the crashes invalidate every series-A discriminator and both images' buffers died in the resets"
  - "Route the crash regression as a named blocker (D1/G-01-10/WINDOWS 15) with a debug round ahead of any bench re-run instead of guessing at a code fix inside this plan"
  - "Keep G-01-7 unjudgeable-not-disproven: zero round-#10 discriminators engaged before the crashes, so the levers stay named and untested"
metrics:
  duration: "15303s (~4h 15m, includes operator bench session wait)"
  completed: 2026-08-28
status: complete
requirements-completed: []
estimate:
  tokens: 48000
actuals:
  tokens: 25666
  tasks: 2
  commits: 2
---

# Phase 01 Plan 24: Operator Bench Re-Verification Session #7 Summary

**One-liner:** Bench session #7 FAILED - the balloon hard-crashed twice during series A (I2C INVALID_STATE + INT_WDT CPU1, then TG0WDT at post-capture radio push start), so this plan's real deliverable became the honest record: D1 crash regression routed as a blocker with a debug path, D2 spurious boot-time re-announce hold routed as minor, and ZERO ledger flips on evidence that proves nothing.

## What Was Built

Nothing was built - and that is the finding. The plan's Task 1 checkpoint handed the operator a pre-flight-verified bench (builds 2/2 SUCCESS at ELF SHA256 3d2b351b4, protocol harness exit 0, zero source drift since 01-23), and the operator ran series A. The balloon HARD-CRASHED twice:

- **Crash 1 (image 35, balloon.log:289-311):** CAPTURE_NOW ACKed, full 8100 B, thumbnail enqueued (1188 B / 6 chunks), manifest sent, then `i2cWrite(): i2c_master_transit failed: [259] ESP_ERR_INVALID_STATE` (balloon.log:301) immediately followed by `Guru Meditation Error: Core 1 panic'ed (Interrupt wdt timeout on CPU1)` (balloon.log:302); second panic with IDLE1 stack canary (balloon.log:313-314); reboot rst:0xc (balloon.log:377-380).
- **Crash 2 (image 36, balloon.log:506-528):** post-reboot CAPTURE_NOW ACKed, full 7810 B, manifest sent, chunk 1/9 emitted, beacon stream started, then rst:0x7 (TG0WDT_SYS_RST) with NO panic output (balloon.log:526-528).
- **Consequences:** session aborted; series-A verdict 0/6 (2 captures executed, 0 transferred - both images' buffers died in the resets); both crash-window CAPTURE_NOWs hit the FORBIDDEN terminal `timeout after 3 retries` (base.log:141, base.log:143); every window request for image 36 rejected post-reset (balloon.log:670/:673/:728/:785/:845) while the base retried into NACK_INVALIDs (base.log:167/:199/:236/:273).

## Task 1: Operator Bench Session (checkpoint:human-verify - gate honored)

Pre-flight (executor, before checkpoint): `pio run -e esp32-s3-balloon -e esp32-s3-basestation` 2/2 SUCCESS; `node scripts/verify_protocol_roundtrip.mjs` exit 0 all PASS; `git diff 3d0aaea..HEAD` touched only `.planning/` files (zero source drift - the deployed firmware is exactly round #10's code).

Operator verdict returned at the checkpoint: **session FAILED - nothing may flip.** No auto-approval was applied (plan frontmatter `autonomous: false` plus its no-fabrication prohibitions override `auto_advance: true`).

## Task 2: Honest Ledger Recording (all edits, nothing flipped)

**01-UAT.md** (commit bb5932e):
- Test 3 note appended with the full session-7 record: both crash timelines with exact log-line citations, 0/6 series-A verdict, forbidden CAPTURE_NOW terminals, D1/D2 as named new defects, explicit NOT RUN clauses (SC-3 pairs / CIF cycle / QVGA restore - verifier-grepped zero SET_* lines in either log), WR-01 battery watch GREEN (55 `batt=valid`, zero `Critical battery`), dashboard glance baseline-only, and the log-naming deviation (balloon.log/base.log vs the plan's balloon12/base7 convention - provenance recorded rather than renaming the operator's retained files).
- Test 4 note appended: WR-03 cadence discriminator NOT RUN (session aborted before any settings command), SC-5 advisory not discharged, no regression.
- G-01-7 root_cause extended: UNJUDGEABLE this session - zero round-#10 discriminators engaged (no `re-announce budget re-armed`, no receipt-evidenced class-5 evictions, no base `full-pull activation deadline reached`) because the crashes precede any real burst; the levers stay unverified-not-disproven; D1 debug precedes any G-01-7 re-run.
- New gap **G-01-10** (D1 crash regression, blocker, test 3) and **G-01-11** (D2 spurious boot-time `re-announce held` hold, minor, test 3) appended with full root_cause/artifacts/missing fields. Test 3 stays `issue`. Test 4 stays PASS (untouched verdict; the not-run discriminator is recorded inside its note).

**WINDOWS.md** (commit bb5932e):
- Entry **15** (D1) and entry **16** (D2) added as OPEN in BOTH the markdown table and the JSON copy (recorded_at 2026-08-28T08:20:00.000Z).
- Entry 3 (G-01-7) reason appended in both copies: session-7 NO EVIDENCE note naming D1 - the entry stays at its current status.
- Front matter reconciled: `open_count: 3 / fixed_count: 13 / total_count: 16`, `last_updated: 2026-08-28T08:20:00.000Z`. Validated post-edit: JSON parses, 16 entries, open ids exactly {3, 15, 16}, table rows 16, all counts agree across front matter / table / JSON.

**STATE.md** (commit bb5932e):
- Current Status paragraph: 01-24 executed, bench #7 FAILED with crash citations, D1/D2 routed, NOTHING FLIPPED, interim discipline (avoid unspaced capture bursts until D1 fixed), remaining gates named (`/gsd-secure-phase 1` explicitly).
- Progress bullets added for 01-21/01-22/01-23 (round #10 code rounds, no bench claims) and 01-24 (this record).
- Current Phase status line: rounds complete through 01-24, ledger 3 open entries (3/15/16), D1 debug round required before any bench re-run.
- Next Steps item 1 repointed to the D1 debug round (addr2line on the crash-1 backtrace with the deployed ELF, suspects named in order: 01-21 pushPending busy-hold/budget re-arm, 01-23 framer resync/command-in-flight guard; session 6 ran the same unspaced pattern clean, so this is a round-#10 regression); item 4 ledger note updated to 3 open entries.
- Footer updated.

## What Was NOT Done (the honesty list)

- **No ledger flips.** G-01-7 not judged, WINDOWS 3/10-14 untouched in status, no requirement marked complete (`requirements.mark-complete` deliberately skipped; `requirements-completed: []`).
- **SC-3 visible-effect pairs, WR-03 cadence discriminator, CIF cycle, QVGA restore: NOT RUN** - sixth round riding (session aborted before any settings command; zero SET_* and zero AUTO_CAPTURE lines in both logs, verifier-counted).
- **No D1 code fix attempted inside this plan** - a crash regression with an un-decoded backtrace needs its own debug round (Rule 4 territory: the fix choice among three suspect changes is an engineering decision with the ELF evidence in hand, not an inline patch).
- **Round #10's positive discriminators produced no verdicts either way** - the crashes precede every discriminating event, so 01-21/01-22/01-23 remain bench-unvalidated (builds + harness green only).

## D1 Debug Round Handoff (next executor starts here)

1. Firmware evidence: `.pio/build` ELF, SHA256 3d2b351b4. Decode balloon.log:313-314/:351 backtrace via addr2line (toolchain in PlatformIO packages).
2. Suspects in order: (a) 01-21 `pushPending` busy-hold on dequeue + budget re-arm before radio drain; (b) 01-23 image framer resync tolerance + command-in-flight guard on image start; (c) latent instability the new TX cadence exercises (I2C cam bus + INT_WDT interplay).
3. Discriminator for the fix: session 6 (balloon11.log/base6.log) ran the identical unspaced series-A pattern clean on pre-round-#10 firmware - 3/3 ACKs, zero resets across 12 images.
4. D2 rides the same round: the `re-announce held - inbound window traffic active` line fires spuriously at every boot (balloon.log:135/:504/:652) because the zero-initialized receipt stamp satisfies the wrap-safe `(millis() - stamp) < IMG_FULL_REANNOUNCE_BUSY_MS` check for the first 15 s of uptime; latch-guarded and harmless, but it pollutes discriminator evidence - gate on `stamp != 0`.
5. After D1 closes: re-run the bench moment (SC-3 pairs + WR-03 discriminator + CIF/QVGA-restore clauses + G-01-7 series A + D2 zero-stamp check).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Session log naming differs from the plan's convention**
- **Found during:** Task 1 checkpoint return (operator verdict)
- **Issue:** The plan specified retaining consoles as balloon12.log/base7.log; the operator's retained files are balloon.log/base.log (1,125 / 429 lines, untracked in repo root per prior-session convention).
- **Fix:** Recorded the provenance deviation explicitly in 01-UAT.md's Test 3 note and adapted all citations to the actual filenames rather than renaming the operator's files post-hoc.
- **Files modified:** .planning/phases/01-command-protocol-control/01-UAT.md
- **Commit:** bb5932e

**2. [Rule 3 - Blocking] Plan verify greps and commit message adapted to a failed session**
- **Found during:** Task 2 verify
- **Issue:** The plan's verify greps assumed the planned log names, and its commit message template assumed successful ledger flips.
- **Fix:** Verify greps run against the actual log names (16 citation lines in 01-UAT.md, 7 in WINDOWS.md, 7 `01-24` + 4 `secure-phase` occurrences in STATE.md - all pass); commit message rewritten to state the truth (FAILED - nothing closed, D1/D2 routed).
- **Files modified:** .planning/WINDOWS.md, .planning/STATE.md
- **Commit:** bb5932e

**3. [Rule 4 boundary respected - D1 routed, not fixed]**
- **Found during:** Task 2
- **Issue:** The crash regression is a blocker whose fix requires choosing among three round-#10 changes with backtrace evidence not yet decoded.
- **Action:** Routed as G-01-10/WINDOWS 15 with suspects, debug path, and discriminator named; no code change made in this plan.
- **Commit:** bb5932e

## Auth Gates

None.

## Known Stubs

None - no code was written; every ledger claim carries a log-line citation.

## Windows Ledger

Entries 15 (D1) and 16 (D2) recorded OPEN this plan (via direct ledger edit; `gsd-tools windows append` equivalent). Entry 3 annotated with the session-7 no-evidence note, status unchanged. Ledger stands at 3 open / 13 fixed / 16 total and blocks `/gsd-ship` until a clean bench session closes them - which is exactly what an honest ledger should do after a failed bench.

## Self-Check: PASSED

- File check: `.planning/phases/01-command-protocol-control/01-24-SUMMARY.md` FOUND
- Commit check: bb5932e (ledger evidence commit) FOUND; 967373d (plan-completion commit) FOUND
- Task 2 verify greps re-run after edits: 01-UAT.md cites balloon.log/base.log (16 lines); WINDOWS.md cites them (7 lines); STATE.md contains "01-24" (7x) and "secure-phase" (4x)
- WINDOWS.md integrity: JSON copy parses; 16 entries; open ids exactly {3, 15, 16}; front matter 3/13/16 reconciles with table (16 rows) and JSON
- Honesty audit: no G-01-7/G-01-9 closure claims, no WINDOWS status flips on entries 3/10-14, no requirements marked complete (requirements.mark-complete deliberately skipped; requirements-completed: [])
- Untracked residue: balloon.log/base.log (session evidence, prior-session convention - left untracked), plus pre-existing .gsd/, .planning/milestone.lock, .planning/research/ (not this plan's output, left untouched)
- Bench verdict: FAILED (recorded as such everywhere) - record-keeping tasks complete, gaps honestly open

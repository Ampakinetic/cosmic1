---
phase: 01-command-protocol-control
plan: 23
subsystem: protocol
tags: [lora-framer, windows-ledger, json-escape, power-management, command-robustness]

requires:
  - phase: 01-command-protocol-control (plan 20)
    provides: bench session #6 evidence base and the WINDOWS ledger convention (entries 8/9 evidence-class precedent)
  - phase: 01-command-protocol-control (review round #9, 7342b46)
    provides: the five confirmed Warnings WR-01..WR-05 with concrete fix shapes
provides:
  - WINDOWS entries 10-14 routed AND fixed (code + builds 2/2 + harness evidence class, unexercised-at-bench triggers recorded by name)
  - storedToSd ownership guard in SdStorage::finalizeImage (WR-01)
  - hasCommand-guarded command-frame acceptance with named drop log (WR-02)
  - CMD_FRAME_INTERBYTE_MS 200 inter-byte resync in both length-driven framers (review-WR-03)
  - RFC-8259-strict jsonEscape for any 1-32 byte SSID (WR-04)
  - CRITICAL-battery camera disable with distinct SYS_INFO discriminator (WR-05)
affects: [01-24 bench reflash, phase-close security gate, gsd-ship]

actuals:
  tokens: 8800   # chars/4 over the realized diff (28,829 diff chars code+ledger, plus this SUMMARY)
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Framer inter-byte resync: wrap-safe millis-subtraction gap check at the top of every length-driven processIncomingByte, keyed on a per-byte lastFrameByteMs stamp — independent of frame-completion-time latches"
    - "Evidence-class ledger flip for unstageable scenarios: code fix + builds 2/2 + harness exit 0 with the unexercised trigger recorded BY NAME, never a fabricated bench moment (entries 8/9 convention)"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/01-23-SUMMARY.md
  modified:
    - .planning/WINDOWS.md
    - src/sd_storage.cpp
    - src/command_handler.cpp
    - include/command_handler.h
    - src/command_sender.cpp
    - include/command_sender.h
    - include/command_protocol.h
    - src/main_basestation.cpp
    - src/main_balloon.cpp

key-decisions:
  - "Disposition = fix all five (option-a): auto-selected at the Task 2 checkpoint under the project's workflow.auto_advance=true YOLO config (gate=blocking default); recorded here so the end-of-phase UAT can surface it for operator confirmation"
  - "WR-02 shape is refuse-don't-NACK: the dropped frame rides the base's existing D-05/D-07 retry as a fresh command — no new protocol surface; the 01-15 BUSY-deferral stays scoped to IMAGE_WINDOW_REQUEST"
  - "review-WR-03 resync composes with, never touches, the 01-18 quiet gate — the gate keys on completed frame types at frame-completion time; the framer reset keys on per-byte timing"
  - "jsonEscape contract: strictly RFC-8259-valid output for any 1-32 byte SSID — control bytes as \\u00XX, bytes >= 0x80 dropped rather than emitted as invalid UTF-8; password never appears in any response (T-03-12 unchanged)"
  - "WR-05 fix lives inside the existing !isEmergencyActive() guard mirroring the LOW branch exactly, with a distinct SYS_INFO line for bench discrimination; the dead onSystemEvent dispatcher block stays untouched (inert, recorded in entry 14)"

patterns-established:
  - "Ledger routing BEFORE disposition: confirmed review findings enter WINDOWS as open entries first (routing half closes unconditionally), then flip per decided disposition — no finding rides an existing entry's status (WR-05/entry-8 separation enforced)"

requirements-completed: [CTRL-06, PRI-02, IMG-05, IMG-06, WEB-05]

coverage:
  - id: D1
    description: "Every confirmed round-#9 finding routed: WINDOWS entries 10-14 in table AND JSON copy with reconciled front-matter counts (5 more open than before the task)"
    requirement: "CTRL-06"
    verification:
      - kind: other
        ref: "grep '\"id\": 10..14' both copies; node JSON.parse copy -> 14 entries; counts 6 open / 14 total pre-fix, 1 open / 13 fixed post-fix"
        status: pass
    human_judgment: false
  - id: D2
    description: "All five fixes source-visible and green: ownership guard, hasCommand guard + drop log, CMD_FRAME_INTERBYTE_MS in both framers, \\u00XX jsonEscape, CRITICAL-branch camera disable"
    requirement: "PRI-02"
    verification:
      - kind: integration
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation -> 2 succeeded"
        status: pass
      - kind: unit
        ref: "node scripts/verify_protocol_roundtrip.mjs -> all wire-format regression checks passed, exit 0"
        status: pass
      - kind: other
        ref: "plan Task 3 compound verify: per-entry pattern greps + ledger-status agreement + channelQuietForTx intact -> ALL TASK-3 VERIFY CHECKS PASS"
        status: pass
    human_judgment: false
  - id: D3
    description: "Operator disposition decision for the five findings (01-VERIFICATION.md Human Verification #5)"
    verification: []
    human_judgment: true
    rationale: "Acceptance of robustness-edge-case risk is a maintainer judgment; this run captured it via auto-mode option-a selection under workflow.auto_advance=true (gate=blocking default auto-selects the recommended option) — the end-of-phase verifier should surface the auto-selection for operator confirmation rather than treat it as an interactive approval"

duration: 12min
completed: 2026-08-28
status: complete
---

# Phase 1 Plan 23: Review Round #9 Routing + Companion Fixes Summary

**WINDOWS entries 10-14 routed and fixed: storedToSd ownership guard, command-drain drop log, 200 ms framer inter-byte resync, RFC-8259-strict jsonEscape, and CRITICAL-battery camera disable — builds 2/2, harness 52/52, 01-VERIFICATION gap 4 closed**

## Performance

- **Duration:** ~12 min
- **Started:** 2026-08-27T15:57:02Z
- **Completed:** 2026-08-27T16:09:00Z
- **Tasks:** 3 (2 auto, 1 checkpoint:decision auto-resolved)
- **Files modified:** 9 (8 source/header + WINDOWS.md)

## Accomplishments

- 01-VERIFICATION.md gap 4 CLOSED: all five confirmed round-#9 warnings (WR-01..WR-05, 7342b46) live in the WINDOWS ledger as entries 10-14 — table AND JSON copy, counts reconciled — with decided dispositions (all fixed; zero waivers, so the no-unreasoned-waive prohibition holds trivially)
- Five review-shaped fixes landed green on both targets: WR-01 storedToSd computed only from the transfer's OWN kind handle; WR-02 second-command-in-one-drain refused with a named drop log (no NACK, no counter/timing change); review-WR-03 CMD_FRAME_INTERBYTE_MS 200 resync in both length-driven framers (wrap-safe, quiet-gate untouched); WR-04 jsonEscape emits strictly valid JSON for any SSID; WR-05 CRITICAL battery disables the camera exactly as LOW does
- Scope guards diff-proven intact: the 01-18 quiet gate (channelQuietForTx + latch block unchanged), the 01-15 BUSY-deferral scoping, and the 01-19 dispatch gate have zero hunks in this round's diff
- Ledger back to 1 open entry (G-01-7 burst full-delivery, entry 3) / 13 fixed / 14 total; the command-path companions' discriminator lines (drop log, 'Camera disabled due to critical power') ride the 01-24 reflash

## Task Commits

Each task was committed atomically:

1. **Task 1: Route WR-01..WR-05 as WINDOWS entries 10-14** - `eae8bb6` (docs)
2. **Task 2: Disposition decision** - checkpoint:decision gate="blocking", auto-mode active (`workflow.auto_advance: true`): auto-selected **option-a (fix all five, the plan's recommendation)** — no commit (decision recorded in ledger reasons + here)
3. **Task 3: Implement the approved fixes + builds + harness + entry updates** - `3d0aaea` (fix)

**Plan metadata:** committed with STATE/ROADMAP after this SUMMARY (see below)

## Files Created/Modified

- `.planning/WINDOWS.md` - entries 10-14 routed (Task 1) then flipped fixed with evidence-class reasons naming each unstageable trigger; front matter 1 open / 13 fixed / 14 total
- `src/sd_storage.cpp` - WR-01: `m.storedToSd = (*handleId == meta.imageId) && (*persistedBytes > 0);` — the ownership guard ANDs the byte check; the :296 handle-close block unchanged
- `src/command_handler.cpp` - WR-02: `if (!hasCommand)` guards the pendingCommand assignment, else the named drop log 'CommandHandler: second command frame dropped - handler busy (sender will retry)'; review-WR-03 resync at the top of processIncomingByte; ctor inits lastFrameByteMs
- `include/command_handler.h` - `uint32_t lastFrameByteMs;` member (command reception block)
- `src/command_sender.cpp` - review-WR-03 resync twin at the top of processIncomingByte; ctor init; quiet-gate latch untouched
- `include/command_sender.h` - `uint32_t lastFrameByteMs;` member (response reception block)
- `include/command_protocol.h` - `static constexpr uint32_t CMD_FRAME_INTERBYTE_MS = 200;` beside CMD_MAX_PACKET_SIZE with the 9600-baud rationale comment
- `src/main_basestation.cpp` - WR-04: jsonEscape escapes quote/backslash, control bytes <0x20 as `\u00XX` via snprintf, passes <0x80 through, drops >=0x80; header comment updated
- `src/main_balloon.cpp` - WR-05: CRITICAL branch disables the camera inside the existing `!isEmergencyActive()` guard with SYS_INFO "Camera disabled due to critical power"; the dead onSystemEvent dispatcher block left untouched

## Decisions Made

- Disposition = option-a (fix all five): the Task 2 checkpoint carried `gate="blocking"` (the default) and the project config has `workflow.auto_advance: true`, so per the auto-mode checkpoint protocol the recommended first option was auto-selected and logged. Recorded prominently here and in coverage D3 (human_judgment: true) so the end-of-phase UAT surfaces the auto-selection for operator confirmation — 01-VERIFICATION.md HV #5 frames disposition as a maintainer judgment.
- WR-02 refuse-don't-NACK (plan prohibitions honored): no protocol addition; the dropped frame re-enters via the base's existing D-05 timeout + D-07 backoff retry, and the 01-15 BUSY-deferral stays scoped to IMAGE_WINDOW_REQUEST.
- Framer resync independence (review-WR-03): the 200 ms gap check sits at byte-arrival time; the 01-18 quiet-gate latch sits at frame-completion time on completed frame types — different keying, no interaction, and the diff proves the latch block untouched.
- WR-05 entry separation: entry 14 records explicitly that it is NOT entry 8's story (entry 8 made camera-down safe to serve through; this finding was about reaching camera-down at all).

## Deviations from Plan

None - plan executed exactly as written.

(One transparency note, not a deviation: the Task 2 operator decision was captured via the auto-mode protocol rather than interactively — documented under Decisions Made and coverage D3.)

## Issues Encountered

None. Builds succeeded first pass on both targets; the harness passed all 52 checks on the first run; the ledger JSON edits needed one retry after a heredoc backslash-escaping quirk in the update script (no repo impact).

## User Setup Required

None - no external service configuration required.

## Known Stubs

None - no stubbed or placeholder code was introduced; all five fixes are live code paths (the two dead-code paths this round TOUCHES were already dead and are deliberately left untouched: the onSystemEvent dispatcher block, recorded as dead in entry 14).

## Threat Flags

None beyond the plan's own threat model — all five STRIDE register entries (T-01-23-01..05, all `mitigate`) were implemented as their own mitigation; no new security surface was introduced.

## Next Phase Readiness

- Ready for the 01-24 bench reflash: this round's firmware diff rides it (wire-change-free — the inter-byte resync is receive-side only — but both boards reflash together per the 01-13 discipline since both sides changed)
- Bench watch lines: 'CommandHandler: second command frame dropped - handler busy (sender will retry)' and 'Camera disabled due to critical power'; the framer resync and jsonEscape fixes are silent-by-design (their triggers are unstageable — recorded by name in entries 12/13)
- Ledger state: 1 open entry (3 = G-01-7 burst full-delivery) blocks /gsd-ship until the residual round closes it; entries 10-14 are fixed on the code+builds+harness class
- Remaining phase-close items unchanged: the bench moment (SC-3 pairs, WR-03 cadence discriminator, CIF/QVGA clauses) + security gate (/gsd-secure-phase 1)
- Spec-less probe: recorded visible skip — phase_req_ids null this run, no probe predicates generated (standard goal-backward verification, the established determination)

## Self-Check: PASSED

- All 10 key files exist on disk (FOUND via `[ -f ]`)
- Both task commits present in git log (eae8bb6 routing, 3d0aaea fixes)
- Task 3 compound verify re-run end-to-end: builds 2/2 SUCCESS, harness exit 0, per-entry pattern greps + ledger-status agreement + channelQuietForTx intact — ALL TASK-3 VERIFY CHECKS PASS
- Ledger table/JSON agreement: 13 fixed / 1 open / 14 total in both copies; JSON parses valid

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-28*

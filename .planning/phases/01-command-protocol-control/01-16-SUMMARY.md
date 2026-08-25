---
phase: 01-command-protocol-control
plan: 16
subsystem: bench-verification
tags: [uat, bench-session, gap-ledger, g-01-7, g-01-9, evidence]
requires: ["01-13", "01-14", "01-15"]
provides: ["bench-verified thumb sizing (G-01-9 A)", "bench-verified CRC integrity (G-01-9 B)", "bench-verified retry bound (WINDOWS 6)", "named residual levers for 01-17"]
affects: [".planning/phases/01-command-protocol-control/01-UAT.md", ".planning/WINDOWS.md", ".planning/STATE.md"]
tech-stack:
  added: []
  patterns: ["evidence-ledger flips on verbatim log lines with line numbers (house convention)", "base-side wire evidence accepted for balloon-console-absent clauses only with the gap recorded"]
key-files:
  created:
    - ".planning/phases/01-command-protocol-control/01-16-SUMMARY.md"
  modified:
    - ".planning/phases/01-command-protocol-control/01-UAT.md"
    - ".planning/WINDOWS.md"
    - ".planning/STATE.md"
    - ".planning/ROADMAP.md"
decisions:
  - "G-01-9 stays open with defects A+B closed on bench quotes and defect C narrowed to the air-loss class (TX-success + no receipt consumes the one-shot announce) — no forced close"
  - "G-01-7 stays open rescoped: session-4 failure modes eliminated; residuals are command survivability under unspaced load and manifest air loss"
  - "WINDOWS entries 6+7 flipped fixed on bench evidence; entries 3+5 carry the named residual levers; counts reconciled 2 open / 5 fixed"
  - "Reboot clause judged on base-side wire evidence (beacon 451->0 reset amid continuous post-reset chunk flow + image 21 NVS-continued/QVGA-class) with the detached balloon console recorded as an evidence gap"
metrics:
  duration: "~2h executor + operator bench session #5 (2026-08-25)"
  completed: 2026-08-25
  tasks_completed: 2
  commits: 2
status: complete
actuals:
  tokens: 20800   # chars/4 over the realized diff (ledger updates + SUMMARY); plan estimated 9000 — evidence-dense ledger lines dominated
  tasks: 2
  commits: 2
---

# Phase 01 Plan 16: Bench Re-verification of the G-01-9/G-01-7 Remediation Summary

Operator bench session #5 (balloon5.log/base5.log) proved the 01-13/01-14/01-15 remediation eliminated every session-4 transfer defect class (zero INCOMPLETE verdicts, zero stored-bytes CRC mismatches, all thumbs genuine QQVGA, retry bound held) — and honestly left two narrow residuals open: image 15's full silently lost to manifest AIR LOSS (TX-success consumed the one-shot announce) and the third unspaced capture command lost 4/4 transmissions under the chunk storm.

## What Was Done

**Task 1 (operator checkpoint, pre-flight in the prior session):** both boards flashed together with the 07eaea0 build (pre-flight: builds 2/2, harness 52 PASS / 0 FAIL — recorded at 2b197da). Provenance note: the orchestrator's GPS UART fix 02142e1 landed between pre-flight and the flash (sensor_pins.h only, no image-path code) — the bench ran on image-identical firmware; evidence validity unaffected. The operator ran series A (3 unspaced CAPTURE_NOW), the settings series (CIF, SVGA, QVGA restore, reboot, intended SC-3 pairs), and the dashboard glance, retaining both consoles as balloon5.log (6,791 lines) / base5.log (3,658 lines) at the repo root (untracked, prior-session convention).

**Task 2 (this session, commit 8adc0bc):** every ledger flip made strictly from verbatim log lines — 01-UAT.md (G-01-7/G-01-9 gap blocks + Test 3 note), WINDOWS.md (entries 3/5/6/7, table + JSON + counts), STATE.md (round outcome, next levers, session), ROADMAP.md plan progress.

## Evidence: Session 4 vs Session 5

| Metric | Session 4 (balloon4/base4) | Session 5 (balloon5/base5) | Verdict |
|---|---|---|---|
| INCOMPLETE verdicts | thumb 35/36 (base4:634) + 2 CRC-mismatch fulls (:855/:3268) | ZERO in either log | FIXED |
| stored-bytes CRC mismatches | 2 (:854/:3267) | ZERO — incl. CIF 31-chunk (base5:776) and SVGA 80/82-chunk (:1243/:2770) fulls, all complete=true | FIXED (G-01-9 B) |
| Full-sized thumbs | 4 of 6 (7157-28808 B / 36-145 chunks) | 0 of 8 — all 1176-1340 B / 6-7 chunks | FIXED (G-01-9 A) |
| Drained-stale-frame discriminator | did not exist | 7 lines, one per logged capture (balloon5:196/:240/:697/:1027/:1523/:5012/:6397) | discriminator live |
| BUSY deferrals burning passes | 2 (balloon4:1154/:1157) | ZERO (contention never arose) | FIXED |
| Retry-ordinal overruns | 13x 'attempt 4/3' | ZERO — all 52 lines '(retry K/3)' K<=3; honest 'timeout after 3 retries' (base5:126) | FIXED (WINDOWS 6) |
| Post-terminal chunk ignores | 14-16 cluster, overrun-driven | 25 total, worst 14 (image 17 k1, :779-807) — bounded stragglers from in-bound retries, guard verified, cancel-on-finalize ends them (:778) | benign by design |
| Beacon delivery | 238/253 accepted | 484 accepted, worst gap 2 consecutive seqs | cadence held |
| END MARKER MISS / CRC FAIL | 61 / 0 | 30 / 0 | halved, framer honest |
| cam_hal FB-OVF | 4 (thumb path) | ZERO | camera healthy |
| Silently lost fulls | 1 (image 8) | 1 (image 15) | RESIDUAL OPEN |

## Series Verdicts

- **Series A (G-01-7 truth): FAILED, honestly kept open.** 3 unspaced triggers: seq=4 ACKED (:35, image 14), seq=5 retry-2/3 ACKED (:70, image 15), seq=6 lost all 4 transmissions under the image-14/15 chunk storm and timed out honestly (base5:126). Images that ran: 5/6 verdicts COMPLETE (img14 k0 :108 / k1 :263, img15 k0 :310, img16 k0 :401 / k1 :504 — image 16 from the operator's spaced re-trigger seq=14); image 15's full SILENTLY LOST (balloon5:274 'FULL manifest(image 15 ...) sent' / zero receipt at base / no drop log). Zero INCOMPLETE verdicts — the session-4 failure mode is gone; the acceptance now fails on command survivability + manifest air loss.
- **3a CIF: PASS.** re-init 6->8 (balloon5:1000-1004 SUCCESS); image 17 both kinds COMPLETE (base5:577/:776), thumb 1332 B / 7 chunks.
- **3b SVGA: PASS.** re-init 8->11 (balloon5:1502-1506); images 18 (80/80, base5:1243) and 19 (82/82, :2770) both kinds COMPLETE; thumbs 1283/1176 B — contrast with session 4's 145-chunk SVGA thumb impostor and 144/144 CRC-mismatch full.
- **3c QVGA restore: PASS by wire evidence.** Two SET_RESOLUTION-to-6 ACKed (balloon5:5048/:6384); post-restore image 20 is QVGA-class (4090 B / 21 chunks = image-14 boot-QVGA's 4094 B / 21; not SVGA-class 16354/82), both kinds COMPLETE (base5:3148/:3258). The operator's "resolution still looks high" impression is contradicted by the wire — likely viewing pre-restore image 19.
- **3d Reboot: EXERCISED, verified base-side.** Beacon seq reset 451->0 amid CONTINUOUS post-reset traffic (base5:3341-3343: image-21 chunk 1 -> beacon seq=0 -> chunk 2; a uint16 counter cannot wrap at 451, and a rebooting balloon could not keep serving — the reset + continuity together prove a real reboot with a fresh ImageTxManager). First post-reboot capture: image 21, NVS ID sequence continued 20->21, QVGA-class 4094 B / 21 chunks, both kinds COMPLETE (:3378/:3518). The balloon console detached BEFORE the reboot (balloon5.log ends at idle telemetry, last [BCN] seq=452 at :6777) — balloon-side boot lines absent, gap recorded, clause judged on base-side wire evidence only.
- **3e SC-3 pairs: NOT RUN.** Zero settings commands all session beyond SET_RESOLUTION (base5 command queue: 9x CAPTURE_NOW, 4x SET_RESOLUTION, rest GET_STATUS/IMAGE_WINDOW_REQUEST). Clause stays open under Test 3 (third round riding).
- **4 Dashboard glance (UI-SPEC): unverified-by-log (operator-did-not-flag).** Browser observations leave no console evidence; the operator flagged nothing; no pass recorded. No UI file was touched this round, so no regression surface exists.

## Deviations from Plan

### Auto-handled

**1. [Rule 3 - Blocking] Reboot evidence landed base-side only**
- The balloon serial console detached before the series-3d reboot, so the plan-expected balloon boot lines ('Frame size: 6' post-reboot, first-capture drain lines) are absent from balloon5.log. Resolved by verifying the clause on base-side wire evidence (beacon reset fingerprint + chunk continuity + ID continuation + QVGA-class sizing) and recording the evidence gap explicitly in Test 3's note — no balloon-side claim fabricated.
**2. [Rule 1 - Bug] Misleading log lines investigated before routing**
- 48 '[HTTP] GET /img/... -> 404' lines looked like a gallery-serving failure; code read proved the line is the handleNotFound DISPATCH banner (main_basestation.cpp:3641) printed BEFORE handleImage serves — not a response code. Similarly the healed-thumb 'B persisted' undercount (1231->800, 1340->740) is documented conservative accounting (sd_storage.cpp:234 'accounting restarts at each flip') while complete=true reflects the read-back CRC. Both documented as log-reading caveats in Test 3's note and STATE decisions — no phantom defects routed, no code touched (plan prohibition respected).
**3. [Housekeeping] WINDOWS entry 7 updated beyond the plan's named 3/5/6**
- The 01-15 deviation entry's entire purpose was discrimination at this bench; its log fired as designed (base5:283). Flipped fixed with evidence; recorded here as a deliberate extension of the plan's ledger scope.

None of these changed any gap status beyond what the logs support.

## Auth Gates

None.

## Known Stubs

None — documentation-only plan; no code stubs, no skipped tests, no unrun `<verify>` (Task 2's automated verify ran 4/4 green before commit 8adc0bc).

## Threat Flags

None — no new security-relevant surface (documentation-only; the T-01-16-01 evidence-integrity mitigation was applied: every flip above cites verbatim lines with line numbers from the retained logs).

## Remaining Open (routed, next round)

- G-01-9 defect C / WINDOWS 5: receipt-driven FULL-manifest recovery (base-side nudge or periodic bounded re-announce independent of TX verdict) — session-5 proof: image 15.
- G-01-7 / WINDOWS 3: command air-priority or pacing during window service — session-5 proof: seq=6 lost 4/4.
- SC-3 visible-effect pairs (third consecutive round riding).
- Phase-close gate after the narrow round: `/gsd-secure-phase 1`.

## Self-Check: PASSED

- 01-16-SUMMARY.md exists and is committed (8b7313b); Task-2 ledger commit exists (8adc0bc)
- Plan `<verify>` automated checks 4/4 green (G-01-7 in 01-UAT.md; WINDOWS JSON ids 3+6; secure-phase in STATE.md)
- Post-commit deletion check: no tracked files deleted in either commit
- Untracked session logs retained at repo root per prior-session convention; unrelated working-tree modifications untouched

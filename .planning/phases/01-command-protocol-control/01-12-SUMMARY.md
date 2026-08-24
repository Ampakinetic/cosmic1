---
phase: 01-command-protocol-control
plan: "12"
subsystem: hardware
tags: [uat, gap-closure, bench-verification, g-01-7-concurrent-starvation, g-01-8-framesize-reinit, g-01-9-thumb-corruption, vga-fbovf, svga, cif, command-retry-overrun]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: the 01-11 discriminator evidence — G-01-7 mechanism named (spaced-vs-unspaced sessions isolate concurrency), G-01-8 mechanism named (sensor-only setFrameSize against QVGA-sized PSRAM buffers), named levers at trace-named sites
provides:
  - "G-01-8 RESOLVED on the bench: framesize re-init with recovery (f51bad6) — CIF/VGA/SVGA each executed via 'Camera: framesize growth requires re-init' with ZERO FB-OVF at every settings step (baseline: 4,379), all 6 captures SUCCESS, camera never died; the false-SUCCESS-then-dead defect is closed"
  - "G-01-7 levers IN and functioning (1064480) but series-A acceptance FAILED — hold fired 3x, zero mid-service evictions, heals serialize ahead of fulls; residual rescoped: defer-aware D-24 pass accounting + full-manifest re-announce + the G-01-9 thumb fix; gap stays OPEN"
  - "NEW open gap G-01-9 (thumb/corruption complex) with three named defects: full-sized thumbnails passing the QQVGA metadata-only guard (4 of 6 captures), all-chunks-received fulls failing stored-bytes CRC (balloon-source-side), and a lost FULL manifest silently dropping a full"
  - "NEW WINDOWS entry 6: CommandSender retry-bound overrun (13x 'attempt 4/3' in base4.log) — pre-existing, out of 01-12 scope, deferred-items.md"
affects: [Phase 1 close-out (security gate + G-01-9/G-01-7 round), IMG-02, IMG-03, PRI-03, CTRL-02, /gsd-ship (WINDOWS entries 3/5/6 open)]

# Actuals (#2632) — pairs with the plan's estimate (14000 tokens, confidence low) on the same chars/4 scale.
actuals:
  tokens: 5400     # chars/4 over the realized code diffs (21636 chars, 7 files, +225/-23 across 1064480/f51bad6); docs/evidence excluded per 01-09..01-11 convention — over-estimate by ~2.6x, the estimate priced a third code mechanism that turned out to be two
  tasks: 3
  commits: 4       # 2 code (1064480, f51bad6) + task-tracking docs (6d749c8) + this close-out docs commit

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "allocatedFrameSize bound: a derived capacity field written ONLY inside initCamera (never a second framesize identity) — growth above it takes a bounded re-init with mandatory recovery-to-previous-size before any false return, so the verdict is truthful and the camera is never left dead"
    - "Serialization hold as priority ordering: pendingHealThumbnail gates both activation sites without adding a fourth base-speaks trigger — D-21 untouched; the D-24 3-pass bound keeps the hold self-releasing"
    - "Payload-vs-metadata discriminator (G-01-9 defect A): a dimension-only guard (fb->width/height) passes full-sized payloads — correctness checks on transferred media must validate the payload (size/SOF), not the capture metadata"

key-files:
  created:
    - .planning/phases/01-command-protocol-control/deferred-items.md   # CommandSender retry-bound overrun (WINDOWS entry 6), out-of-scope discovery
    - .planning/todos/pending/2026-08-24-gallery-thumb-number-overlay.md  # operator bench friction, routed to Phase 03 gallery UX
    - .planning/todos/pending/2026-08-24-incomplete-badge-on-renderable-images.md  # operator-perceived false positive; badge verified CORRECT, root cause G-01-9
  modified:
    - include/image_protocol.h      # 1064480: IMG_WINDOW_RX_SETTLE_MS = 500 with the settle < preempt < stall invariant comment
    - include/image_rx_manager.h    # 1064480: pendingHealThumbnail declaration + healHoldLoggedId member
    - src/image_rx_manager.cpp      # 1064480: serialization hold at both activation sites + settle gate on the window-complete cadence
    - include/image_tx_manager.h    # 1064480: ImageTxEntry windowArmedAtMs
    - src/image_tx_manager.cpp      # 1064480: evictEntriesOlderThan mid-service guard + balloon-side settle gate in serviceWindowChunk
    - src/camera_manager.h          # f51bad6: allocatedFrameSize member
    - src/camera_manager.cpp        # f51bad6: re-init-with-recovery setFrameSize, allocatedFrameSize refresh in initCamera, cached-settings initCamera
    - .planning/phases/01-command-protocol-control/01-UAT.md  # G-01-7 UPDATE + rescoped missing; G-01-8 resolved; G-01-9 opened; Test 3 note extended
    - .planning/WINDOWS.md          # entry 3 rescoped open; entry 4 fixed; entries 5 (G-01-9) / 6 (retry overrun) appended open

key-decisions:
  - "G-01-8 closed on bench evidence, G-01-7 honestly left open: the levers provably engaged (hold 3x, zero mid-service evictions) but the acceptance truth is 'unspaced captures finalize every kind COMPLETE' and series A produced 3 INCOMPLETE verdicts — no forced close (carried prohibition)"
  - "The series-A failures decompose into three DISTINCT mechanisms, not one: heal-window NACK-deferral under the pre-existing one-at-a-time BUSY guard (D-24 passes exhausted with 35/36), balloon-source-side stored-bytes corruption (36/36 received, CRC mismatch), and a lost FULL manifest with no re-announce path — each gets its own lever in the rescoped missing list"
  - "G-01-9 opened as a new gap: the full-sized-thumbnail complex was MASKED pre-01-11 (phantom FAILED churn dominated) and is not a regression from 1064480/f51bad6 — 4 of 6 captures produced thumbs at full size passing the c67e1a5 dimension guard, proving the guard checks metadata, not payload"
  - "SVGA ran as the plan's own optional stretch (step c) — either outcome a pass; it executed cleanly (the session's only SVGA loss is the image-11 full CRC mismatch, G-01-9 defect B, not a buffer fault)"
  - "Series-B steps d/e/f (QVGA restore, reboot boot-resolution check, SC-3 visible-effect pairs) were NOT executed this session — recorded as unexercised clauses in G-01-8's missing list, riding the next bench round under Test 3; G-01-8 still resolved because its named defect (false SUCCESS + dead captures + FB-OVF flood) is closed at every exercised size"
  - "Operator's CIF/VGA/SVGA labels were CORRECT: the compiled firmware uses the FRAMEWORK sensor.h enum (QVGA=6, CIF=8, VGA=10, SVGA=11 — boot dump 'Frame size: 6', balloon4.log:73) while the handler logs WIRE codes; the 01-06 by-name translation worked as designed"
  - "The '600 B/1360 B persisted' gallery oddities are benign accounting (sd_storage.cpp:234 resets persistedBytes per handle flip), not data loss — no defect booked"
  - "Raw bench logs (base4.log / balloon4.log) stay untracked — verbatim excerpts in 01-UAT.md are the record of note"

patterns-established:
  - "Enum-layer audit before verdict assignment: when operator labels, handler log codes, and internal enums disagree, compile-resolve the actual header the firmware uses before calling any of them wrong"

requirements-completed: [IMG-02, IMG-03, PRI-03, CTRL-02]

# Metrics
duration: Tasks 1-2 executor session + operator bench session #4 2026-08-24 ~13:16 local (balloon4.log 5897 lines / base4.log 3338 lines) + close-out same day
completed: 2026-08-24
status: complete
---

# Phase 01 Plan 12: G-01-7/G-01-8 Remediation + Bench Re-Verification Summary

**The G-01-8 framesize re-init-with-recovery fix (f51bad6) is bench-PROVEN — CIF/VGA/SVGA all executed via honest re-init with zero FB-OVF (baseline 4,379) and all 6 captures SUCCESS — while the G-01-7 levers (1064480) engaged exactly as designed (hold 3x, zero mid-service evictions) but the unspaced series-A acceptance honestly FAILED on three distinct residual mechanisms (heal NACK-deferral under the pre-existing BUSY guard, balloon-source-side stored-bytes CRC corruption, and a silently lost FULL manifest), leaving G-01-7 rescoped-open and opening the new G-01-9 thumb/corruption complex plus a CommandSender retry-bound overrun (WINDOWS entry 6)**

## Performance

- **Duration:** Tasks 1-2 executor session 2026-08-23/24; operator bench session #4 2026-08-24 ~13:16 local; close-out 2026-08-24
- **Tasks:** 3/3 (Task 1 G-01-7 levers; Task 2 G-01-8 re-init bound; Task 3 bench checkpoint — recorded via this continuation)
- **Code files modified:** 7 (commits 1064480, f51bad6)
- **Commits:** 2 code + 6d749c8 (Task 1-2 tracking) + this docs commit

## Before/After Tally — Session-2/3 Baselines vs 01-12 Session 4

| Metric | Session 2 (unspaced, pre-fix baseline) | Session 3 (spaced, baseline) | Session 4 series A (unspaced, post-fix) | Session 4 series B (settings) |
|---|---|---|---|---|
| Thumbnail verdict | image 2 INCOMPLETE 6/8; image 3 COMPLETE 8/8 | 8/8 + 9/9 COMPLETE | image 6 COMPLETE 8/8 1411 B (base4.log:69); image 7 INCOMPLETE 35/36 (:634); image 8 COMPLETE 36/36 (:938) | image 9 full-sized INCOMPLETE 56/57 (:1297); image 10 CORRECT 7/7 1343 B (:1581); image 11 full-sized COMPLETE 145/145 (:2678) |
| Full verdict | image 2 COMPLETE 51/51; image 3 INCOMPLETE 15/52 | 52/52 + 53/53 COMPLETE | image 6 COMPLETE 36/36 (:519); image 7 INCOMPLETE 36/36-recv CRC mismatch (:854-855); image 8 SILENTLY NEVER RECEIVED (manifest lost) | CIF 57/57 COMPLETE 11223 B (:1464); VGA 97/97 COMPLETE (:1938); SVGA INCOMPLETE 144/144-recv CRC mismatch (:3267-3268) |
| Mid-service eviction | image 2 heal EVICTED (balloon2.log:656) | n/a (spaced) | ZERO — all 5 supersede-evictions post-terminal (balloon4.log:1235/:2323-2324/:2849/:4835) | n/a (serialized settings) |
| Full-pull hold | n/a (did not exist) | n/a | fired 3x — base4.log:521/:1127/:2336 'full-pull activation held - thumbnail heal pending' | fired where applicable |
| cam_hal FB-OVF at settings | 4,379 lines (VGA incident) | n/a | ZERO (session total 4, all series-A thumbnail-path: balloon4.log:353-354/:396-397) | ZERO at CIF/VGA/SVGA steps |
| CAPTURE_NOW verdicts | SUCCESS x2 | SUCCESS x2 | SUCCESS x3 (IDs 6/7/8) | SUCCESS x3 (IDs 9/10/11) — camera never died |
| Beacon cadence (PRI-01) | held | held | TX 253 / RX 238 (15 lost, no Critical stalls; WR-01 green) | held |
| Balloon chunk FAILED | 0 | 0 | 0 (1007 sends; 569 benign AUX-missed) | 0 |
| END MARKER MISS | 6 | 5 | 61 (elevated — air loss real, all pass-bounded) | (included in session total) |

## Task Commits

Each task was committed atomically:

1. **Task 1: G-01-7 levers** — `1064480` (5 files, +137/-6): pendingHealThumbnail hold at both activation sites, evictEntriesOlderThan mid-service guard, IMG_WINDOW_RX_SETTLE_MS = 500 + windowArmedAtMs settle on both sides. Builds 2/2, harness 49/49
2. **Task 2: G-01-8 re-init bound** — `f51bad6` (2 files, +88/-17): allocatedFrameSize tracked in initCamera, setFrameSize re-init path with mandatory recovery, cached-settings re-init. Builds 2/2, harness 49/49. Both tracked by `6d749c8`
3. **Task 3: Bench re-verification** — operator-executed hardware session #4 2026-08-24 ~13:16 (balloon4.log 5897 lines / base4.log 3338 lines); statuses flipped on evidence in 01-UAT.md + WINDOWS.md (this commit)

## Bench Evidence — Session 4 (2026-08-24, balloon4.log / base4.log)

### Series A (unspaced back-to-back captures, QVGA) — levers engaged, acceptance FAILED

- Image 6 both kinds COMPLETE: base4.log:69 `ImageRx: image 6 kind 0 finalized COMPLETE (8/8 chunks, 1411 B)`; :519 kind 1 COMPLETE 36/36 after 12 window requests (seq 5-19, tail chunks 8..13 recovering to pass 3) — the ARQ path healthy under unspaced load
- The hold engaged exactly as designed: base4.log:521/:1127/:2336 `full-pull activation held - thumbnail heal pending`; ZERO mid-service evictions (all 5 supersede-evictions balloon4.log:1235/:2323-2324/:2849/:4835 name post-terminal entries; the new defer form never needed to fire)
- Image 7 thumb INCOMPLETE 35/36 (base4.log:634): its heal window requests were NACK-deferred by the PRE-EXISTING one-at-a-time BUSY guard (image_tx_manager.cpp:700-712, not Task 1 code — confirmed via git show 1064480) while image 8's window was mid-service: balloon4.log:1154/:1161 `window request for image 7 deferred - image 8 window mid-service` -> :1157/:1164 `FAILED` (the session's only 2 failed commands); the D-24 3-pass budget exhausted with exactly one chunk missing
- Image 7 full INCOMPLETE on stored-bytes CRC: base4.log:854 `stored-bytes CRC mismatch (got FCAFC250, manifest 9DE9EFA0)` -> :855 INCOMPLETE — 36/36 chunks received, so this is balloon-source-side corruption (G-01-9 defect B), not air loss (0 CRC FAIL at the frame level)
- Image 8 full silently lost: the balloon sent the FULL manifest (balloon4.log:722) but the base never received it (10/11 manifests) — no re-announce path exists (G-01-9 defect C); its thumb (full-sized, 36/36) completed (:938)
- **Verdict:** the G-01-7 truth ("unspaced captures finalize every kind COMPLETE") is NOT yet met — gap stays open with the residual rescoped (01-UAT.md G-01-7 UPDATE)

### Series B (settings series) — G-01-8 PROVEN

- CIF: balloon4.log:1791 `Camera: framesize growth requires re-init (6 -> 8)` + :1795 SUCCESS; capture 9 full COMPLETE 57/57, 11223 B (base4.log:1464) — 1.57x the QVGA baseline size (7157 B/36 chunks), confirming the 400x296 upsize; its thumb was full-sized 11223 B INCOMPLETE 56/57 (base4.log:1297) — G-01-9 defect A, not a resolution fault
- VGA (the G-01-8 named incident size): balloon4.log:2784 `(8 -> 10)` + :2788 SUCCESS; image 10 full COMPLETE 97/97 (base4.log:1938) and thumb CORRECTLY sized 7/7 1343 B (base4.log:1581); ZERO FB-OVF at the step — the pre-fix baseline at this size was 4,379 FB-OVF lines and 4x CAPTURE_NOW FAILED until reboot
- SVGA (optional stretch, step c): balloon4.log:3588 `(10 -> 11)` + :3592 SUCCESS; thumb full-sized 28808 B COMPLETE 145/145 (base4.log:2678); full received 144/144 but failed stored-bytes CRC (base4.log:3267 `mismatch (got CC9DADC2, manifest D043406D)`) -> INCOMPLETE :3268 — G-01-9 defect B again; the buffer/re-init machinery itself held (camera kept capturing)
- All 6 session captures SUCCESS (IDs 6-11); the camera never died; the session's only 4 FB-OVF lines are series-A thumbnail-path events (balloon4.log:353-354/:396-397), zero at any settings step
- Series-B steps d/e/f NOT executed: no QVGA restore, no reboot boot-resolution check, no SC-3 visible-effect pairs — recorded as unexercised clauses in G-01-8's missing list, riding the next bench round under Test 3

### Session-wide tallies

- Beacons: TX 253 consecutive / RX 238 (15 lost to air, no Critical battery event; boot `Emergency Shutdown: Enabled` balloon4.log:56 is the known benign config echo — WR-01 green)
- 61 END MARKER MISS (elevated vs prior sessions' 0-6 — real air loss, all bounded by pass budgets); 0 frame-level CRC FAIL; 1007 balloon chunk sends / 0 FAILED; 569 benign AUX-missed lines
- 16x post-terminal `chunk for finalized image 11 kind 0 ignored` (base4.log:3286-3310) — late kind-0 re-arms from the retry-overrun (WINDOWS entry 6), correctly ignored
- Enum audit: boot dump `Frame size: 6` (balloon4.log:73) against the FRAMEWORK sensor.h the firmware compiles (QVGA=6, CIF=8, VGA=10, SVGA=11) — the handler's "Set resolution to 8/9/10" lines are WIRE codes; the operator's CIF/VGA/SVGA labels were correct all along (01-06 by-name translation working as designed)

## Status Flips (evidence-forced, per the Task 3 contract)

- **G-01-8 -> resolved** (resolved_by: 01-12 f51bad6): the named defect (false SUCCESS, FB-OVF flood, captures dead until reboot) is closed at every exercised size — CIF/VGA/SVGA honest re-init, zero FB-OVF, captures keep succeeding, camera never died. Unexercised clauses (QVGA restore d, reboot boot-resolution e, SC-3 pairs f) recorded in missing, riding Test 3. Full quotes in 01-UAT.md G-01-8 verified_by
- **G-01-7 -> stays OPEN, rescoped** (UPDATE appended): levers in and functioning; residual = defer-aware D-24 pass accounting (the BUSY-guard NACK deferrals consume passes without transfer opportunity) + full-manifest re-announce (G-01-9 defect C) + the G-01-9 thumb fix (a full-sized thumb at 36 chunks starves the heal budget a QQVGA thumb would never touch). Interim mitigation unchanged: space captures
- **G-01-9 -> NEW open gap**: defect A full-sized thumbnails (4 of 6 captures; `Camera: Thumbnail created, size: 7157 bytes` balloon4.log:355; the c67e1a5 guard at camera_manager.cpp:361 checks fb->width/height only — metadata, not payload; candidate mechanism: stale/in-flight pre-downshift frame under fb_count 2 + CAMERA_GRAB_LATEST; levers: payload-size/JPEG-SOF check or drain-and-recapture); defect B all-chunks CRC mismatch (balloon-source-side: buffer mutation post-announce or wrong-buffer slicing under k0/k1 re-arm races; discriminator lever: kind-tag the balloon window-chunk log); defect C lost FULL manifest (bounded re-announce). Mirrored in WINDOWS entry 5
- **WINDOWS entry 6 -> NEW open**: CommandSender retry-bound overrun, 13x `Retrying command seq=N (attempt 4/3)` (base4.log:115/:290/:372/:2330) — pre-existing, out of 01-12 scope (deferred-items.md)

## Plan-Checker Advisories (restated for the record)

1. The recovery path itself can fail: `Camera: CRITICAL - recovery re-init FAILED...` exists at camera_manager.cpp:478 — in that terminal case the camera is left down and the operator verdict is honest (false return), which is the G-01-8 contract; it did NOT fire in any bench session
2. optimizeForQuality() has zero callers, and QQVGA thumbnail pairs always take the sensor-only path (QQVGA <= allocatedFrameSize at every bench size) — no re-init storm per capture, as the key_link promised
3. The nine UI-SPEC explicit truths rode prior rounds' bench evidence (not re-proven this session — no UI changes shipped in 01-12)

## Files Created/Modified

- Code (1064480): `include/image_protocol.h`, `include/image_rx_manager.h`, `src/image_rx_manager.cpp`, `include/image_tx_manager.h`, `src/image_tx_manager.cpp`
- Code (f51bad6): `src/camera_manager.h`, `src/camera_manager.cpp`
- Docs: `01-UAT.md` (G-01-7 UPDATE + G-01-8 resolved + G-01-9 opened + Test 3 note), `WINDOWS.md` (entries 3/4/5/6, counts open 3 / fixed 3 / total 6), `deferred-items.md` (retry overrun), two todos in `.planning/todos/pending/`, this SUMMARY
- Raw logs (untracked, evidence source only): `balloon4.log`, `base4.log`

## Decisions Made

See key-decisions in the frontmatter. The load-bearing ones: verdicts follow evidence only (G-01-8 closed on exercised truth, G-01-7 kept open despite functioning levers); the series-A failure decomposed into three named mechanisms rather than monolithic "still broken"; G-01-9 is newly-exposed (masked pre-01-11), not a regression.

## Deviations from Plan

None in code — both tasks landed exactly at their plan-named sites with their verify gates green. Documentation-level honesty notes (not deviations): SVGA was the plan's own optional stretch step c (either outcome a pass — it executed); series-B steps d/e/f unexecuted and recorded as unexercised clauses; the G-01-9/entry-6 discoveries are bench findings routed per the plan's own "record the residual honestly" contract.

---

**Total deviations:** 0
**Impact on plan:** none — all residuals routed with named mechanisms.

## Issues Encountered

- Series A failed its acceptance (3 INCOMPLETE verdicts) — honestly recorded, gap rescoped, no forced close
- 61 END MARKER MISS this session (vs 0-6 prior) — elevated air loss this bench day, all pass-bounded; worth watching in the next session but not itself a defect verdict
- Operator tally reconciliations: "thumb 145 chunks vs full 144" (SVGA) is real — the thumb was full-sized and one chunk larger than the full's manifest count (G-01-9 defect A evidence); "600 B/1360 B persisted" gallery values are benign per-handle accounting (sd_storage.cpp:234), not truncation

## Known Stubs

None — both code commits are real implementations; no source-code changes were made by this continuation.

## Threat Model Disposition (T-01-12-01..06)

- **T-01-12-01 (re-init leaves sensor dead):** mitigated and bench-proven — recovery path exists and the failure branch never fired across CIF/VGA/SVGA; the CRITICAL terminal log (camera_manager.cpp:478) is the honest last resort
- **T-01-12-02 (guard wedges all 3 TX slots):** mitigated — zero wedge observed; guard scoped to the supersede path, and the defer form never even fired (all supersede targets were terminal)
- **T-01-12-03 (hold starves fulls forever):** mitigated by construction — D-24 3-pass bound finalized the image-7 thumb INCOMPLETE and released the hold (observed); the RESCOPED residual is that passes are consumed while NACK-deferred, routed in G-01-7's missing list
- **T-01-12-04 (settle delays telemetry):** mitigated — beacon cadence held throughout (TX 253/RX 238); PRI-01 order gates green at commit-time harness (49/49)
- **T-01-12-05 (closure without bench proof):** mitigated — G-01-8 closure cites operator-observed quotes; G-01-7 kept OPEN despite functioning levers (the exact case this threat guards)
- **T-01-12-06 (untrusted RF reaching new paths):** mitigated — no new wire surface; harness green; post-terminal re-arm traffic correctly ignored

## Threat Flags

None — no new network/auth/file-access surface; scheduling/camera-lifecycle changes at existing sites only.

## User Setup Required

None — bench session already executed by the operator.

## Next Phase Readiness

- **Phase 1: all 12 plans executed.** Remaining before phase complete: security gate (`/gsd-secure-phase 1`) and the G-01-7-residual/G-01-9 remediation round (future plan) — WINDOWS ledger has 3 open entries (3/5/6), so /gsd-ship stays honestly blocked
- G-01-9 named levers ready for planning: payload check (JPEG SOF/size) in the thumbnail guard or drain-and-recapture; kind-tagged balloon window-chunk logging to discriminate defect B; bounded FULL-manifest re-announce (fixes defect C and serves G-01-7)
- G-01-7 rescoped levers: defer-aware pass accounting (a NACK-deferred heal window must not consume a D-24 pass); fold in the G-01-9 thumb fix (full-sized thumbs inflate heal budgets)
- WINDOWS entry 6 micro-fix candidate: off-by-one in the CommandSender retry counter (or an ACK race re-arm) — deferred-items.md
- Unexercised series-B clauses (QVGA restore, reboot boot-resolution, SC-3 visible-effect pairs) ride the next bench round under Test 3
- Interim operating discipline (until G-01-7 residual lands): space captures — wait for both finalize lines between triggers

## Self-Check: PASSED

- Commits 1064480 / f51bad6 / 6d749c8 present in git log (verified at continuation start; file sets match the per-task lists exactly)
- 01-12-SUMMARY.md (this file) written; 01-UAT.md G-01-7 UPDATE + G-01-8 resolved + G-01-9 open + Test 3 note extended; WINDOWS.md entries 3/4/5/6 updated with counts open 3 / fixed 3 / total 6 (table and JSON copies consistent); deferred-items.md + two todos created
- Every quoted console line re-verified against balloon4.log / base4.log this session (counts: hold 3x at base4.log:521/:1127/:2336; evictions 5, all post-terminal; FB-OVF session total 4; captures 6/6 SUCCESS; beacons TX 253/RX 238; END MARKER MISS 61; retry overrun 13x; post-terminal ignored chunks 16x)
- No source-code changes this continuation; raw logs left untracked

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-24*

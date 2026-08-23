---
phase: 01-command-protocol-control
verified: 2026-08-23T02:25:45Z
status: gaps_found
score: 4/5 must-haves verified
behavior_unverified: 1 # SC-3: capture-path execution hardware-proven end-to-end; the settings-adjustment visible-effect + CIF clauses were never explicitly exercised (ride the G-01-5 bench round per 01-UAT.md test 3 note)
overrides_applied: 0
re_verification:
  previous_status: human_needed
  previous_score: 1/5
  gaps_closed:
    - "Hardware UAT item 1 (SC-2): command round trip over real radios — UAT test 1 PASS on hardware (re-tested after the 1223f46 balloon boot fix; G-01-1 resolved), re-confirmed at the 01-09 bench (trigger ACKed after both boards converged at 9.6 kbps)"
    - "Hardware UAT item 2 (SC-4): retry/TIMEOUT on degraded link incl. the duplicate-ACK edge — UAT test 2 PASS on hardware; CR-03 terminal-state guard re-confirmed intact at this HEAD (command_sender.cpp:410, before all counter/statistics mutations)"
    - "Hardware UAT item 4 (SC-5): auto-capture cadence + disable — UAT test 4 PASS on hardware (G-01-4 resolved by 01-07/754f834: delegated in-page AJAX submit on section#capture, verifier-read at main_basestation.cpp:1511-1578; runtime truth operator-confirmed at the 01-09 bench, WINDOWS ledger entry 1 closed fixed)"
    - "Hardware UAT item 3, G-01-3 half (storage + transfer): RESOLVED on hardware at the 01-09 bench — SD_MMC 1-bit transport switch (794df00, verifier-verified: sd_storage.cpp:55-91 SD_MMC.setPins/begin 1-bit + 'card mounted' verdict; base_station_config.h:41-43 CLK=39/CMD=38/DATA=40), E32 register protocol + boot-time 9.6 kbps enforcement (01-08 e16845a/ce24cd8, verifier-verified: readConfig 0xC1-triple + 6-byte frame + head check e32_lora.cpp:313-369, writeConfigRegisters 0xC0 echo-verified :376-420, ensureLinkConfig fail-open at end of begin() :137/:473-505), operator observed Storage tile OK + a capture completing end-to-end with the full image CRC-verified and rendered from SD; the interim base-only-flash severance event stress-proved the rate enforcement is real and load-bearing"
    - "Prohibition review item 5 (01-06 plan, flagged judgment-tier): UAT test 5 PASS — human-confirmed on hardware that no fabricated protocol/camera state reaches the wire or UI; the prior round's non-authoritative flag is resolved with human authority"
  gaps_remaining:
    - "G-01-5 (new residual from the 01-09 bench, structured below): thumbnail push-burst loss — corrupt preview + honest Incomplete badge while the full completes; UAT test 3 stays 'issue'; WINDOWS ledger entry 2 open (blocks /gsd-ship)"
  regressions: [] # harness 49/49 exit 0 (verifier-run, grown from 15 to 49 clauses incl. WR-12 type-dispatch teeth); both pio targets SUCCESS (verifier-run); CR-01/CR-02/CR-05 prior closures re-checked at this HEAD; CR-04 Phase-2 deferral honored (thumbnail.buffer nulled on failure paths, camera_manager.cpp:333/:348); raw literal integrity intact (2 openers / 2 closers)
gaps:
  - truth: "A triggered capture's thumbnail arrives intact and renders uncorrupted in the gallery (or its D-22 kind-addressed heal recovers it to COMPLETE) while the full image completes — residual clause of 01-09 plan truth 2; recorded as gap G-01-5 in 01-UAT.md"
    status: failed
    reason: "Operator-observed on the 01-09 bench (iteration 5): 'the thumbnail image looks corrupted, and there is an Incomplete message over it. The full image looks good.' The Incomplete badge is the designed honest degradation of a chunk-deficient thumbnail (finalized INCOMPLETE), not sensor corruption. Classified B2/B4-class push-burst transport loss: the full's windowed ARQ recovers losses mid-flight, the thumbnail's blind push burst + post-hoc 3-pass heal did not. The serial discriminator was NOT captured, so B2 (air loss) vs B4 (saturation/margin at the new air rate) vs a heal-servicing defect is not yet named — the outcome-level matrix only eliminated B1 and made B3 unlikely."
    artifacts:
      - path: "src/image_rx_manager.cpp"
        issue: "thumbnail heal loop: gated until the full pull finishes (:190-196), bounded to 3 passes with finalizeIncomplete (:198-199) — pass bound and stall margins are the code levers"
      - path: "src/image_tx_manager.cpp"
        issue: "kind-split window arming (:634-732) + thumbBuffer retention — servicing verified present by code read; bench-trace confirmation pending"
      - path: "include/image_protocol.h"
        issue: "IMG_WINDOW_STALL_MS / IMG_THUMB_HEAL_IDLE_MS / IMG_RETRANSMIT_MAX_PASSES — pacing levers (01-08's deferred follow-up condition, armed only if B4 confirms)"
    missing:
      - "Serial discriminator on the thumbnail heal phase (base console: 'image N kind 0 finalized INCOMPLETE (thumbnail push stalled; passes exhausted)' + '[E32TX] cmd=30' heal-request rows + '[FRAME] type=13 CRC FAIL' counts; balloon console: 'THUMBNAIL window armed' + 'window chunk sent|FAILED') — full enumeration in 01-UAT.md G-01-5 missing list"
      - "Second-capture re-test: whether a fresh capture's thumbnail arrives intact (per-burst vs systematic loss)"
      - "If B4 push-burst saturation persists at 9.6 kbps: pacing round per 01-08's deferred follow-up condition (image_tx/rx margins, heal pass bound, window size)"
      - "Ride-along closure of UAT test 3's untested clauses: settings-visibility + CIF 400x296 spot-checks — also closes SC-3's behavior-unverified half below"
deferred:
  - truth: "Full-resolution gallery image viewer (operator bench wish: the full renders barely larger than its thumbnail)"
    addressed_in: "Phase 3 backlog"
    evidence: ".planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md (verifier-verified on disk; presentation-only — the /img/{id} SD-streaming data path is hardware-proven)"
  - truth: "D-13 separate camera-controls page / D-15 accordion settings groups"
    addressed_in: "Phase 3"
    evidence: "Carried from prior verifications; Phase 3 delivered the single-page dashboard layout (03-01, WEB-04) — informational, superseded in practice"
  - truth: "CR-04 + WR-11 createThumbnail dangling-buffer double-free (prior deferral to Phase 2)"
    addressed_in: "Phase 2 (CLOSED)"
    evidence: "Phase 2 plan 02-01 tracer claims the fix; this verifier confirmed in code: thumbnail.buffer = nullptr on every failure path (camera_manager.cpp:333, :348) and the WR-08 downgrade-verify bail (d19b082). Deferral honored — no longer open"
behavior_unverified_items:
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "Send SET_RESOLUTION / SET_SATURATION / SET_EXPOSURE / SET_WB from the UI (in-page via 01-07), capture, and inspect; exercise the CIF 400x296 option"
    expected: "Visible changes per setting in captured images; the CIF option (real FRAMESIZE_CIF post-01-06 relabel) executes successfully"
    why_human: "Capture-command execution is hardware-proven end-to-end (01-09 bench), but the settings-adjustment visible-effect clause and the CIF option were never explicitly exercised — 01-UAT.md test 3 note routes them to the G-01-5 re-test round ('images only now flow'). Sensor acceptance and visual assessment need the physical camera"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #4, after UAT gap closure 01-07/01-08/01-09)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-23T02:25:45Z
**Status:** gaps_found
**Re-verification:** Yes — #4, after UAT gap closure (plans 01-07/754f834, 01-08/e16845a+ce24cd8, 01-09/794df00; docs 625581b, review 5366a93 — all six commits verifier-confirmed in git log with exactly the claimed file sets)

## Goal Achievement

The previous round's status was `human_needed` with 5 hardware items. All 5 ran: UAT tests 1, 2, 4, 5 PASS; test 3 split into G-01-3 (RESOLVED on hardware) and a new routed residual G-01-5 (thumbnail push-burst loss). The four behavior-unverified truths of round #3 are now hardware-resolved except SC-3's settings clauses. What keeps the phase open is one observed, unresolved hardware failure — G-01-5 — plus the SC-3 spot-checks that ride its bench round.

This verifier independently proved every code claim of the three gap plans in source (details below), ran the wire-format harness (49/49, exit 0) and both firmware builds (SUCCESS) at this HEAD, and re-checked all prior-round closures for regression. The 01-09-SUMMARY evidence table was cross-read against 01-UAT.md and the WINDOWS ledger; the three artifacts agree (G-01-3/G-01-4 resolved with operator evidence; G-01-5 open).

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as all prior rounds).

Note on Phase 2 interlock: 02-VERIFICATION.md (2026-08-19) still reads `gaps_found` over CR-01/CR-02 SD-persistence defects — but the fixes landed the same day (81cba6e lazy-open/reopen 12:32, bc34eab flush-before-verify 12:34, both verifier-confirmed in code: image_rx_manager.cpp:516-542 conditional open, :631 flushTransfer before the stored-CRC read-back). The 01-09 bench (2026-08-22/23) therefore flashed fixed firmware — the operator-observed CRC-verified COMPLETE full is consistent, and D-47 (full servable only when its sidecar verified complete) makes the intact render operator-visible proof. Phase 2's own re-verification round remains that phase's business.

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | section#capture at main_basestation.cpp:2266-2512 contains all 13 forms (11 control + the two guarded Phase 3 forms); 21 routes registered; queue panel (:2515) and pollOnce (:692) present; the G-01-4 fix makes every control form submit in-page (delegated listener :1511-1578, verifier-read) |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ✓ VERIFIED | Hardware UAT test 1 PASS (re-test after 1223f46); 01-09 bench: trigger ACKed with both boards at 9.6 kbps; wire format code-proven by harness clauses (f1)-(f4) (byte 2 == 0x11 on ACK/NACK paths, defective variant provably 0x00); response factories still assign the shared constant (command_protocol.cpp:243/:260/:279/:635) |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Capture-command execution hardware-proven end-to-end (01-09 bench: command ACKed, image transferred, CRC-verified full rendered); 7 sensor setters + name-mapped SET path intact at code level (frameSizeFromEsp now at command_handler.cpp:892, used at :653). The settings visible-effect + CIF 400x296 clauses were never explicitly exercised — 01-UAT.md test 3 note routes them to the G-01-5 re-test round. See behavior_unverified_items |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ✓ VERIFIED | Hardware UAT test 2 PASS on degraded link incl. the duplicate-ACK edge (the prior verifier's precondition was met); CR-03 terminal-state guard re-confirmed at this HEAD: command_sender.cpp:410 returns for ACKED/FAILED/TIMEOUT before response storage, decrements, and statistics |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ✓ VERIFIED | Hardware UAT test 4 PASS (exact cadence incl. a value above 30 s, zero captures after ACKed disable, one image-ID sequence, manual trigger throughout); CR-05 regression gates re-run clean at this HEAD: isTimeToCapture 0 project-wide, live captureImage callers only auto_capture.cpp:247 + command_handler.cpp:212, allocateImageId only at those two authorities, the lone CAMERA_CAPTURE_INTERVAL_MS define in balloon_config.h:36 has zero users (dead constant, informational) |

**Score:** 4/5 truths verified (1 present, behavior-unverified — routed to the G-01-5 bench round; no truth carries a code gap)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | Full-resolution gallery image viewer (bench wish; presentation-only) | Phase 3 backlog | Todo file on disk: .planning/todos/pending/2026-08-23-full-resolution-gallery-image-viewer.md |
| 2 | D-13/D-15 page-layout preferences | Phase 3 | Carried; superseded by the delivered single-page dashboard (WEB-04) |
| 3 | CR-04 + WR-11 createThumbnail dangling buffer (prior round's must-fix-before-Phase-2-caller) | Phase 2 — CLOSED | Fix verified in code by this verifier: thumbnail.buffer nulled on every failure path (camera_manager.cpp:333/:348); WR-08 downgrade-verify bail (d19b082) |

### Required Artifacts (this round's gap plans 01-07/01-08/01-09)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/main_basestation.cpp` (01-07) | Delegated submit listener on section#capture: defaultPrevented + id guards, generic urlencoded serialization, per-form message div, in-flight disabling, fetch(form action), pollOnce after settle | ✓ VERIFIED | :1522 getElementById('capture') delegated listener; :1523 defaultPrevented guard; :1525 alerts-form/wifi-form skip; :1526 preventDefault; :1531-1537 generic serialization over form.elements; :1542-1550 lazily-created js-capture-msg div; :1555 in-flight disable; :1557-1560 fetch to form.getAttribute('action'); :1569 server message only via setText; :1570/:1576 pollOnce on settle AND catch. Insertion strictly inside the HTML_FOOTER raw literal (opener :638; exactly 2 openers / 2 closers in the file); commit 754f834 is 69 insertions, 0 deletions, this file only |
| `src/e32_lora.cpp` (01-08) | readConfig/writeConfig register frames with bounded reads, echo verification, decoded boot logging; ensureLinkConfig fail-open enforcement in begin() | ✓ VERIFIED | readConfig :313-369 (0xC1 0xC1 0xC1 at :324, 6-byte bounded frame, isConfigHeadByte accepts {0xC0,0xC1,0xC2} at :23-25, decoded cfg boot line :358-365); writeConfigRegisters :376-420 (0xC0 + five registers, echo compared byte-for-byte :406-408 — success ONLY on match); ensureLinkConfig :473-505 called as begin()'s last step :137 before `return true`; fail-open strings :477/:502 present; setParameters/setAddress/setChannel rebuilt as read-modify-write |
| `include/e32_lora.h` (01-08) | Register-mirrored E32Config; SPED masks per manual; E32_TARGET_AIR_DATA_RATE = 9.6k with retune rationale; corrected 2.4k factory-default comment | ✓ VERIFIED | E32Config = five registers :57-63; masks PARITY 0xC0 [7:6] / BAUD 0x38 [5:3] / AIR 0x07 [2:0] :39-41; RATE_2_4kbps = 0x02 "FACTORY DEFAULT" :91 with the corrected-comment block :85-89; E32_TARGET_AIR_DATA_RATE = RATE_9_6kbps (0x04) :107-108 with the B1/B2/B4 rationale |
| `include/base_station_config.h` + `include/sd_storage.h` + `src/sd_storage.cpp` (01-09) | SD_MMC 1-bit transport on the built-in slot pins CLK=39/CMD=38/DATA=40 | ✓ VERIFIED | SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_DATA_PIN) sd_storage.cpp:55; SD_MMC.begin("/sdcard", true /*1-bit*/, ...) :62; failure line names the pins :65-67; success verdict 'card mounted, /images ready' :91; constants :41-43; all storage operations (open/write/sidecar/serve/enumerate) on SD_MMC. GPIO39 STATUS_LED coexistence: pinMode at main_basestation.cpp:2060 precedes SDStorage begin |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| section#capture submit events (11 control forms) | fetch POST to each form's own action route | one delegated listener; form.getAttribute('action') supplies the route | ✓ WIRED | All 11 control forms verifier-confirmed inside the section (:2266-2512); no route list hardcoded; empty-body forms (/capture, /auto-capture-stop) serialize correctly |
| fetch response JSON message field | per-form message div + pollOnce() | r.json() → setClass/setText → pollOnce | ✓ WIRED | :1562-1570; sendResponse emits {status,message} (main_basestation.cpp:3651+) — the handler reads j.message, matching the actual shape |
| E32LoRa::begin (base initLoRa + balloon begin) | ensureLinkConfig → readConfig → conditional writeConfigRegisters → verified echo | ensure step as begin()'s last step | ✓ WIRED | :137; zero main-file changes for the ensure step (verifier: both mains call plain begin() — main_balloon.cpp:474, main_basestation initLoRa); hardware proof: base-only re-flash severed the pair (rate divergence), balloon re-flash restored it — enforcement demonstrably reads and writes real module state |
| SPED air-rate bits [2:0] | E32_TARGET_AIR_DATA_RATE comparison + masked patch | read-modify-write preserving all other bits/registers | ✓ WIRED | :481-492; mask 0x07, all other SPED bits and ADDH/ADDL/CHAN/OPTION pass through from the fresh read |
| Manifest/threshold payloads, window ARQ, SD persistence (cross-phase links the bench exercised) | image_rx/tx managers + SD_MMC storage | full-image announce → windowed pull → CRC read-back → sidecar → /img route | ✓ WIRED | Operator-observed end-to-end on hardware at the 01-09 bench; CR-01/CR-02 fixes verified in code (see Goal Achievement note) |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| Per-form message divs | res.msg | sendResponse {status,message} from each POST route | Yes | ✓ FLOWING — server-returned truth only; ACK/terminal wording still comes exclusively from the polled queue |
| E32 decoded boot line | ADDH/ADDL/SPED/CHAN/OPTION | parsed 6-byte module return frame | Yes | ✓ FLOWING — measured, never assumed; the severance event proves the values are real |
| Storage tile / gallery | mount state + /images files | SD_MMC.begin verdict + stored sidecars | Yes | ✓ FLOWING — operator-observed OK + CRC-verified full served from the card |
| Thumbnail render | IMG_{id}_T.JPG | thumbnail push burst + 3-pass heal | Partial | ✗ DISCONNECTED AT BENCH — the burst loses chunks that the heal did not recover (G-01-5); the honest Incomplete badge + corrupt preview IS the real data state, truthfully displayed |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite (49 clauses incl. (f1)-(f4) type-byte teeth, img-g type-dispatch incl. WR-12, img-h status layout) | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run) | All PASS, exit 0 | ✓ PASS |
| Both firmware targets compile | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (verifier-run) | 2 succeeded (26.5 s / 26.4 s) | ✓ PASS |
| 01-07 structure gates | defaultPrevented / getElementById('capture') / form.getAttribute('action') / rawliteral openers | present / present / present / exactly 2 (+2 closers); 3 submit listeners total (alerts :1412, wifi :1477, delegated :1522) | ✓ PASS |
| 01-08 structure gates | 0xC0 frame / 0xC1 0xC1 0xC1 / ensureLinkConfig ≥2 / 'config read failed' / 'modules may mismatch' | all present | ✓ PASS |
| CR-05 gates (regression, main_balloon changed since round #3 by 1223f46/9916e7c) | isTimeToCapture count; live captureImage/allocateImageId callers | 0; only auto_capture.cpp:247/:248 + command_handler.cpp:212/:218 | ✓ PASS |
| CR-03 terminal-state guard (regression) | handleResponse early return | command_sender.cpp:410, before storage/decrements/statistics | ✓ PASS |
| Thumbnail end-to-end at bench range | operator bench observation (01-09 iteration 5) | corrupt + Incomplete badge while full completes | ✗ FAIL (gap G-01-5) |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists in the repo. The declared verification commands (harness + both builds) were executed by this verifier above. SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-02, 01-03, 01-05, 01-06, 01-07 | Trigger camera capture from base station web UI | ✓ SATISFIED | Hardware UAT test 1 + 01-09 bench (trigger ACKed, capture executed); in-page submit removes the UX blocker (G-01-4 closed, WINDOWS entry 1 fixed) |
| CTRL-02 | 01-03, 01-04, 01-06, 01-07 | Adjust all camera settings remotely | ✓ SATISFIED (code; visible-effect + CIF spot-checks ride G-01-5 round) | 7/7 forms/routes/handlers; SET path name-mapped; GET_STATUS truthful; forms now exercisable in-page — the explicit per-setting bench spot-check is the G-01-5 ride-along |
| CTRL-03 | 01-03, 01-04, 01-05, 01-07 | Manual + automatic capture modes | ✓ SATISFIED | Hardware UAT test 4 PASS |
| CTRL-04 | 01-03, 01-04, 01-05, 01-07 | Automatic capture fixed interval timing | ✓ SATISFIED | Hardware UAT test 4 PASS (exact cadence incl. >30 s value — legacy interleave gone, CR-05 gates re-verified) |
| CTRL-06 | 01-02, 01-03, 01-06 | Failed commands retried with timeout | ✓ SATISFIED | Hardware UAT test 2 PASS incl. duplicate-ACK edge; CR-03 guard intact |
| PRI-02 | 01-02, 01-06 | Retry mechanism with timeout for failed transmissions | ✓ SATISFIED | Same evidence as CTRL-06 |

Orphaned requirements: none. The union of `requirements` fields across 01-02..01-09 covers all six Phase-1-mapped IDs (CTRL-01..04, CTRL-06, PRI-02 — REQUIREMENTS.md marks all six Complete, accurate). The additional IDs claimed by the gap plans (IMG-01/02/04/05, PRI-03 in 01-08/01-09) are Phase-2-mapped requirements whose hardware truths surfaced in Phase 1's UAT — IMG-02 honestly remains "Gaps Found" pending G-01-5; no conflict, no orphan.

### Plan Prohibition Verdicts (judgment-tier — autonomous, NON-AUTHORITATIVE)

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-06 (prior round, flagged) | MUST NOT report fabricated protocol or camera state | **PASS — human-confirmed** | UAT test 5 PASS on hardware; the prior round's non-authoritative flag is resolved with human authority |
| 01-07 | MUST NOT render fabricated command state (message shows only the server-returned text; terminal wording stays with the polled queue) | PASS (code level, non-authoritative) — flagged, human review recommended | Verifier-read: setText(msg, res.msg \|\| fallback) :1569; handler never authors a state word; bench-corroborated (operator saw server verdicts in-page). Rides the next bench round |
| 01-08 | MUST NOT fabricate configuration success (echo-verified writes; boot log states measured rates only) | PASS (code level + hardware-corroborated, non-authoritative) — flagged, human review recommended | Echo comparison :406-408 is wired enforcement in the write path; boot logs name measured rates (:358-365, :483, :495). The severance/recovery event on hardware corroborates that reported config state is real. Operator sees the E32 cfg line at every boot — confirm at the next bench session |
| 01-09 | MUST NOT mark G-01-3 resolved on code-level evidence alone (operator-observed boot line AND completed transfer required) | SATISFIED | Both hardware truths operator-observed and recorded in 01-09-SUMMARY's evidence table (Storage tile OK; capture end-to-end with CRC-verified full rendered from SD) |

### Anti-Patterns Found

No TBD/FIXME/XXX in any file modified by 01-07/01-08/01-09 (verifier grep clean). Items below are from the fresh 01-REVIEW.md (5366a93) — each material claim was re-derived by this verifier where load-bearing (WR-05 re-read in source; WR-12 cross-checked against harness clauses; the rest accepted at Warning level with the review's file:line evidence). None blocks the phase goal.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/image_rx_manager.cpp | 190-199 | G-01-5: thumbnail heal 3-pass bound + full-pull gate — the push-burst loss path (structured gap above) | 🛑 Gap (routed) | The one open item; discriminator round decides defect vs transport loss vs pacing |
| src/main_balloon.cpp | 726-727, 804-805, 915-918 | WR-01 (new): dummy PowerData {3.7f, 0.1f, 85, ...} — emergency/low-power branches permanently unreachable; telemetry carries hardcoded 85% | ⚠️ Warning | Inert safety logic + fabricated telemetry value; real voltage exists via PowerMgr and is simply not wired. Recommend routing to the next gap round or Phase 2 close-out — highest-value warning in the set |
| include/sensor_pins.h | 52 | WR-03 (new): GPIO4 double-assigned (BATTERY_SENSE_PIN vs camera SCCB SDA on ESP32S3_EYE) — 1 Hz analogRead races camera I2C | ⚠️ Warning | Undocumented hardware collision; not the G-01-5 mechanism (the Incomplete badge indicates chunk loss, not sensor corruption — the discriminator will settle it authoritatively), but fix before flight |
| src/main_balloon.cpp | 574-577 | WR-02 (carried as WR-13): inverted camera health check — warns when camera is healthy | ⚠️ Warning | False diagnostic only |
| platformio.ini | 259-314 | WR-04 (new): basestation env missing board_build.partitions — 1.2 MB default app partition vs growing web surface | ⚠️ Warning | Builds today; will fail as assets grow |
| src/e32_lora.cpp | 591-609 | WR-05 (new, 01-08 code, verifier-confirmed): exitConfigMode ignores saved previousMode (dead local) — correct today only because every caller enters from NORMAL | ⚠️ Warning | Comment/code disagreement; breaks silently if a WOR-mode caller ever enters config |
| src/main_basestation.cpp | 978-979 | WR-06 (new): canvas-fallback centering offsets identically zero (dead geometry math) | ⚠️ Warning | Cosmetic |
| Carried Info items | — | IN-01..IN-10 from 01-REVIEW (dead code, stale 17-byte comments, seq-wrap edges, control-char JSON escaping, asymmetric RX buffers, at-least-once re-execution, blocking AUX waits, /api/state side effect, weak AP default) | ℹ️ Info | None in a Phase 1 must-have domain; PRI/IN-10 overlap the pending /gsd-secure-phase 1 gate |

Closed since the prior verification: WR-12 (receive-side type validation) — now wired and harness-proven (img-g clauses: balloon discards CRC-valid 0x11 frames, base discards 0x10, both discard unknown 0x77); WR-07 (E32 config API contradicted datasheet / setChannel silent no-op) — eliminated by 01-08's register rewrite; CR-04/WR-11 — fixed in Phase 2 (verifier-confirmed).

### SUMMARY vs Reality

1. 01-07/01-08/01-09 SUMMARY claims reproduce on this verifier's own reads, greps, harness run, and builds: all six commits exist with exactly the claimed file sets (754f834: 69 insertions in main_basestation.cpp only; e16845a/ce24cd8: e32_lora.{h,cpp} only; 794df00: the three SD files only); the delegated listener, register protocol, enforcement, and SD_MMC transport are all substantive and as described.
2. The 01-08 manual-conformance deviation (SPED [7:6]/[5:3]/[2:0], HEAD echo 0xC0/0xC2) is visible in the code with the manual citations, and the bench severance event validates that the layout and writes are real.
3. The 01-09 evidence table honestly marks what was NOT satisfied (thumbnail clause residual; discriminator traces partial; two operator questions unanswered and correctly deemed non-load-bearing) — no claim stretching found.
4. 01-REVIEW.md (5366a93) was treated as claims: WR-05 re-derived in source (confirmed), WR-12 cross-checked against the harness (confirmed wired), the G-01-5-adjacent WR-03 examined against the Incomplete-badge mechanism (review itself does not claim it as the cause). Its "verified clean" list matches this verifier's independent findings on the probed surfaces.
5. REQUIREMENTS.md marks CTRL-01..04/CTRL-06/PRI-02 Complete — accurate; IMG-02 stays Gaps Found, correctly tied to G-01-5.

### Gaps Summary

One gap: **G-01-5 — thumbnail push-burst loss** (structured in frontmatter for `/gsd-plan-phase --gaps`). It is an observed hardware failure (corrupt preview + honest Incomplete badge while the full completes CRC-verified), classified B2/B4-class transport loss with the discriminating serial traces not yet captured, so the precise mechanism (air loss vs saturation/margin vs servicing defect) is unnamed. Its closure round is already enumerated in 01-UAT.md: bench discriminator capture, second-capture re-test, conditional pacing levers (01-08's armed follow-up), and the ride-along SC-3 spot-checks (settings visible-effect + CIF 400x296) which also clear this report's one behavior-unverified truth. WINDOWS ledger entry 2 (open) blocks /gsd-ship until it closes — correctly.

Everything else is green: 4/5 SCs verified (three of them now on hardware evidence, not just code), all requirements satisfied, all artifacts substantive and wired, all key links connected, harness 49/49, both builds SUCCESS, no debt markers, prior closures regression-intact, and the prior round's flagged prohibition human-confirmed. The phase goal — bidirectional LoRa command/control with ACK, retry, manual + auto capture — is achieved and hardware-proven; what remains is a Phase-2-domain image-reliability residual that surfaced in this phase's UAT, honestly routed, plus its ride-along spot-checks.

Recommended next step: `/gsd-plan-phase 1 --gaps` (G-01-5 discriminator + pacing round, operator bench session), then `/gsd-secure-phase 1` (WR-08/IN-10 scope). Consider folding WR-01 (inert battery safety) into the same round.

---

_Verified: 2026-08-23T02:25:45Z_
_Verifier: Claude (gsd-verifier)_

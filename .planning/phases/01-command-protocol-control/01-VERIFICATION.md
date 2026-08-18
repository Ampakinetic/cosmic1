---
phase: 01-command-protocol-control
verified: 2026-08-18T11:12:04Z
status: human_needed
score: 1/5 must-haves verified
behavior_unverified: 4 # SC-2/SC-3/SC-4/SC-5 present + wired, all three response-path gaps CLOSED; runtime behavior needs hardware UAT
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 1/5
  gaps_closed:
    - "Gap 1 / CR-01 + WR-05: response packet-type conformance — createResponsePacket assigns packet.type = PACKET_TYPE_RESPONSE as first field (src/command_protocol.cpp:374), createCommandPacket assigns PACKET_TYPE_COMMAND (:355), createACK/createNACK/createStatus derive from the one shared constant (include/command_protocol.h:15), serializer emits resp.type verbatim (:159), CommandHandler::process routes ALL successful responses through the factory (:98-103); harness clause (f) with factory mirror + defective-variant teeth — verifier-run 15/15 exit 0"
    - "Gap 2 / CR-02 + WR-01: truthful GET_STATUS resolution — handleGetStatus uses frameSizeFromEsp(camera->getFrameSize()) (src/command_handler.cpp:579), by-name reverse mapping over nine real framesize_t constants with documented QVGA default (:741-766, const-qualified), FrameSize value 8 relabeled FRAMESIZE_CIF with truthful differs-by-design header comment (include/command_protocol.h:58-72), forward map CIF->real CIF (:714-716), UI option 'CIF 400x296' (src/main_basestation.cpp:616); negative gates: FRAMESIZE_QXGA remnants 0, false matching-comment 0"
    - "Gap 3 / CR-03: terminal-state guard — handleResponse returns for ACKED/FAILED/TIMEOUT slots (src/command_sender.cpp:336-338) BEFORE response storage (:341), pendingCommandCount-- (:349/:358), and statistics; retryCommand/findTrackedCommand/findFreeSlot mechanics unchanged and intact"
  gaps_remaining: []
  regressions: [] # SC-1 routes (12 incl. 7 settings + auto-capture pair), CR-05 gates (legacy symbols 0 in main_balloon, isTimeToCapture 0, sole capture/ID authorities intact), retry mechanics (ackTimeoutFor :24/:243, backoff :205), protocol constants, /status flow — all re-checked on this verifier's own runs
deferred:
  - truth: "D-13: separate camera-controls page from telemetry display"
    addressed_in: "Phase 3"
    evidence: "Plan 01-03 deferral truth; Phase 3 goal 'Full base station control panel with telemetry, maps, and gallery'; accepted in commit 0b52b9e"
  - truth: "D-15: accordion panels grouping camera settings"
    addressed_in: "Phase 3"
    evidence: "WEB-04 top-down layout restructure; accepted in commit 0b52b9e"
  - truth: "CR-04 + WR-11: createThumbnail failure paths leave currentThumbnail.buffer dangling (double-free on next freeCurrentThumbnail) and the 4000-byte allocation estimate makes the failure path the common case — dormant, no Phase 1 caller"
    addressed_in: "Phase 2"
    evidence: "Phase 2 deliverables list 'Thumbnail generation on balloon' and IMG-02 'Thumbnail preview displays immediately on base station' — Phase 2 wires the first createThumbnail caller. Dormancy proven by this verifier: createThumbnail called only from captureThumbnail (camera_manager.cpp:214), captureThumbnail only from captureBoth (:229), captureBoth has no callers; currentThumbnail initializes null (:12) and freeCurrentThumbnail is null-guarded (:588), so the dangling state is unreachable in Phase 1 code paths. MUST be fixed (null the member on both failure paths; enlarge/realloc the estimate) before or as the first task of the Phase 2 thumbnail work — crash-class defect, not a Phase 1 goal blocker"
behavior_unverified_items:
  - truth: "LoRa command packets transmitted from base station to balloon and acknowledged (SC-2)"
    test: "Power both ESP32-S3 boards with E32 modules linked; issue a capture command from the web UI"
    expected: "Balloon executes; ACK arrives within the D-05 window (2 s for CAPTURE_NOW); UI queue row shows ACK Received; degraded link yields 3 paced retries then Timeout"
    why_human: "Requires physical radios; E32 AUX timing (WR-04) can only be evaluated on hardware. Wire-format half is now code-proven: byte 2 == 0x11 asserted by harness clause (f) on ACK and NACK paths"
  - truth: "Balloon receives camera commands and adjusts camera settings accordingly (SC-3)"
    test: "Send SET_RESOLUTION / SET_SATURATION / SET_EXPOSURE / SET_WB from the UI, then capture and inspect the image"
    expected: "Sensor accepts the values; visible change in captured images; NACK_BUSY if a sensor call fails"
    why_human: "Sensor acceptance of saturation/ae_level/wb_mode values and visual image assessment need the physical camera module"
  - truth: "Failed commands are retried with timeout and user is notified (SC-4)"
    test: "Power the balloon off; issue a capture command; watch the Command Queue panel"
    expected: "Row shows Sent, then Failed/Timeout after 3 paced attempts (D-05 window + D-07 backoff); LED turns red 'No link'; counters advance; pending returns to 0 — including when a duplicate/late ACK arrives after a retry"
    why_human: "Retry state transitions at runtime need two radios; no host test exercises the CommandSender state machine. The prior verifier's precondition is now met: gap 3 (terminal-state guard) is in place, so the duplicate-ACK edge can be safely exercised on a slow link"
  - truth: "Both manual trigger and interval-based auto-capture work end-to-end (SC-5)"
    test: "Enable auto-capture at 10 s and at 60 s; then disable; issue a manual CAPTURE_NOW between them"
    expected: "Captures at exactly the commanded cadence (including above 30 s, no interleave); zero automatic captures after the ACKed disable; manual trigger still works; image IDs form one sequence"
    why_human: "Timing behavior across a real RF link with physical radios and camera cannot be exercised by host builds or the wire-format harness"
human_verification:
  - test: "End-to-end command round trip over real radios (UAT 1)"
    expected: "ACK within 2 s for CAPTURE_NOW; queue row 'ACK Received'; counters advance; degraded link gives 3 paced retries then Timeout"
    why_human: "RF delivery, AUX timing, mode switching (WR-04) — hardware only"
  - test: "Retry/TIMEOUT on degraded link, now including the duplicate-ACK edge (UAT 2)"
    expected: "3 paced attempts then Timeout; LED red 'No link'; pending count returns to 0 and stays 0 even when a late ACK follows a retry"
    why_human: "Runtime state-machine transitions need two radios; the CR-03 guard is code-proven but the race itself is a runtime phenomenon"
  - test: "Camera settings on the physical sensor (UAT 3)"
    expected: "Visible changes per setting; NACK_BUSY on sensor failure; resolution options incl. CIF 400x296 execute successfully"
    why_human: "Physical camera required"
  - test: "Auto-capture interval and disable behavior (UAT 4)"
    expected: "Exact commanded cadence; zero captures after ACKed disable; one continuous image-ID sequence; manual trigger works throughout"
    why_human: "Timing across the radio link"
  - test: "Prohibition review (01-06 plan, judgment-tier, flagged): MUST NOT report fabricated protocol or camera state"
    expected: "Human confirms at UAT that responses carry their documented type on the wire and GET_STATUS fields reflect actual module state (boot QVGA reports 320x240)"
    why_human: "unverified-prohibition — human review recommended. This verifier's NON-AUTHORITATIVE code-level judgment is PASS (all four response factories assign the shared constant; GET_STATUS derives fields via real getters and the name-based mapping; no numeric reinterpretation remains), but the prohibition is judgment-tier with no wired enforcement, so it cannot be marked green by an autonomous run"
---

# Phase 1: Command Protocol & Control Verification Report (Re-verification #3, after response-path gap closure)

**Phase Goal:** Establish bidirectional LoRa communication for camera control
**Verified:** 2026-08-18T11:12:04Z
**Status:** human_needed
**Re-verification:** Yes — #3, after response-path gap closure (plan 01-06, commits 75b8514 / 36674ff / 56704e2)

## Goal Achievement

All three response-path criticals from re-verification #2 are genuinely closed. This verifier proved each closure line-by-line in the current code, independently of both the 01-06 SUMMARY and the fresh 01-REVIEW.md (commit fa921a7) that anticipated the closures:

1. **CR-01/WR-05 (response type byte):** `createResponsePacket` assigns `packet.type = PACKET_TYPE_RESPONSE` as the first field set (src/command_protocol.cpp:374); `createCommandPacket` assigns `PACKET_TYPE_COMMAND` (:355) for symmetry; `createACK`/`createNACK`/`createStatus` all derive from the one shared constant (include/command_protocol.h:15) with zero inline 0x11 casts remaining; `serializeResponse` emits `resp.type` verbatim (:159); and `CommandHandler::process` routes every successful response — ACK and STATUS alike — through the factory (:98-103). The harness now models the real construction path: `createResponsePacketMirror` transcribes the zero-initialized C++ factory with a `defective` option, the serializer mirror emits the packet's own type field, and clause (f1)-(f4) asserts byte 2 == 0x11 on both ACK and NACK paths while the defective variant provably yields 0x00. Verifier-run: 15/15 clauses, exit 0. The harness can no longer certify a byte the firmware does not emit.
2. **CR-02/WR-01 (GET_STATUS cross-enum cast):** `handleGetStatus` now derives `currentResolution` via `frameSizeFromEsp(camera->getFrameSize())` (src/command_handler.cpp:579) — a by-name reverse mapping over the nine real `framesize_t` constants with a documented QVGA fallback for sizes no protocol code can set (:741-766, const-qualified, matching the header declaration after the auto-fixed compile error). The FrameSize block is rewritten as protocol-internal wire codes with a truthful "numbering DELIBERATELY DIFFERS … translated BY NAME" comment (include/command_protocol.h:58-72); value 8 is renamed `FRAMESIZE_CIF` and maps to real FRAMESIZE_CIF in both directions (:714-716); the UI option reads "CIF 400x296" (src/main_basestation.cpp:616). Negative gates: `FRAMESIZE_QXGA` remnants in src/+include = 0; false "matching framesize_t" comments = 0. Boot QVGA now reports as 320x240.
3. **CR-03 (duplicate-response accounting):** `handleResponse` returns early for any slot in ACKED/FAILED/TIMEOUT (src/command_sender.cpp:336-338), placed before response storage (:341), both `pendingCommandCount--` sites (:349, :358), and the statistics updates — the retry-edge duplicate ACK can no longer underflow the uint8 counter or latch `hasPendingCommands()` true. Sibling mechanics are deliberately untouched: `findTrackedCommand` still matches terminal slots (legitimate for `cancelCommand`'s own-guarded decrement and UI queries), `retryCommand` still resets the window on both outcomes, eviction still clears terminal slots.

Independent corroboration: the fresh adversarial re-review (01-REVIEW.md, fa921a7) — which executed the 15-clause harness itself — reaches the same closure verdict on all three. Nothing in this round contradicts it, and this verifier did not rely on it.

**No FAILED truths, no missing/stub artifacts, no unwired links, no blocker anti-patterns remain.** What keeps the phase open is exactly what kept it open before the gap cycle began, now with a clean code base: SC-2/SC-3/SC-4/SC-5 assert runtime behavior over physical radios and a physical camera that no host harness can exercise. The four hardware UAT items are carried, and UAT 2's prior precondition ("run after the terminal-state guard lands") is now satisfied.

Note on mode: ROADMAP.md marks Phase 1 `Mode: mvp`, but the goal is not in user-story format, so standard goal-backward verification applies (same determination as both prior verifications).

Note on CR-04: the review's one remaining Critical (createThumbnail failure-path dangling buffer → double-free) is real but **not part of this phase's delivered surface** — this verifier re-proved dormancy by grep and lifecycle analysis (see Deferred Items): no Phase 1 code path can reach the failure branches, `currentThumbnail` starts null, and `freeCurrentThumbnail` is null-guarded. Phase 2's explicit deliverable "Thumbnail generation on balloon" wires the first caller, so the defect is deferred there with a must-fix-before-caller note. WR-12 (receive-side type validation) and WR-13 (false health-check diagnostic) are new Warnings, verified present by this verifier, neither inside a Phase 1 must-have domain.

### Observable Truths

Must-haves are the 5 ROADMAP success criteria (roadmap contract governs; plan must_haves add detail, never subtract).

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Base station web interface has camera control section with trigger button and settings forms (SC-1) | ✓ VERIFIED | Regression intact: 12 routes registered (main_basestation.cpp:461-473) incl. all 7 settings + auto-capture pair; 10 sendCommand call sites; queue panel and /status flow unchanged. WR-01 resolved by the CIF relabel — the previously always-NACKing resolution option now names and executes real FRAMESIZE_CIF |
| 2 | LoRa command packets transmitted from base station to balloon and acknowledged (SC-2) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Code gap CLOSED (CR-01/WR-05 — factory assigns 0x11, serializer emits verbatim, process routes all success responses through the factory, harness clause (f) with teeth, verifier-run 15/15). Wire-format half is code-proven; the RF round trip itself (incl. WR-04 AUX race) is hardware UAT item 1 |
| 3 | Balloon receives camera commands and adjusts camera settings accordingly (SC-3) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Code gap CLOSED (CR-02 — name-based reverse mapping at :579/:741-766, both directions by name, negative gates zero). Adjust path regression intact (7 sensor setters, ACK only on true return). Sensor acceptance of values is hardware UAT item 3 |
| 4 | Failed commands are retried with timeout and user is notified (SC-4) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | Code gap CLOSED (CR-03 — terminal-state guard :336-338 before any counter/statistics change; retry/backoff mechanics regression-intact: ackTimeoutFor :24 used :243, clamped backoff shift :205). Runtime retry transitions are hardware UAT item 2 — now safe to run on a slow link per the prior verifier's precondition |
| 5 | Both manual trigger and interval-based auto-capture work end-to-end (SC-5) | ⚠️ PRESENT_BEHAVIOR_UNVERIFIED | CR-05 closure regression-verified on this run: legacy symbols 0 in main_balloon.cpp, isTimeToCapture 0 project-wide, live captureImage calls only in AutoCapture::process (auto_capture.cpp:98) and the CAPTURE_NOW handler (command_handler.cpp:201), allocateImageId only at command_handler.cpp:207 + auto_capture.cpp:99, wiring gates :387/:725/:728 intact. Runtime cadence/disable is hardware UAT item 4 |

**Score:** 1/5 truths verified (4 present, behavior-unverified — routed to human UAT; none carries a code gap anymore)

### Deferred Items

| # | Item | Addressed In | Evidence |
|---|------|-------------|----------|
| 1 | D-13: separate camera-controls page (controls stay cards on the single page) | Phase 3 | WEB-04 top-down layout restructure; commit 0b52b9e |
| 2 | D-15: accordion settings groups (stacked single-field forms retained) | Phase 3 | WEB-04 top-down layout restructure; commit 0b52b9e |
| 3 | CR-04 + WR-11: createThumbnail dangling-buffer double-free on failure paths; undersized 4000-byte estimate makes failure the common case | Phase 2 | Phase 2 deliverables: "Thumbnail generation on balloon", IMG-02 thumbnail preview. Dormant in Phase 1 (verifier-proven: no caller of captureThumbnail/captureBoth outside camera_manager; buffer starts null; free is null-guarded). Must be fixed before/at the first Phase 2 caller |

### Required Artifacts (01-06 plan must_haves)

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/command_protocol.cpp` | Factories assign packet.type; shared constant in all response constructors | ✓ VERIFIED | createResponsePacket :374, createCommandPacket :355, createACK/createNACK/createStatus :239/:256/:275 — all `PACKET_TYPE_RESPONSE`, zero inline casts |
| `include/command_protocol.h` | PACKET_TYPE_RESPONSE constant; FrameSize value 8 renamed CIF with truthful comment | ✓ VERIFIED | Constant :15; FRAMESIZE_CIF :66; "translated BY NAME" block comment :58-61 |
| `src/command_handler.cpp` | frameSizeFromEsp reverse mapping used by handleGetStatus; CIF forward case | ✓ VERIFIED | Call site :579; mapping :741-766 (const); framesizeFromInt CIF :714-716 |
| `include/command_handler.h` | frameSizeFromEsp declaration | ✓ VERIFIED | Declaration present next to framesizeFromInt; const mismatch auto-fixed in 36674ff — both targets compile |
| `src/command_sender.cpp` | Terminal-state guard at top of handleResponse | ✓ VERIFIED | :336-338, before storage (:341), decrements (:349/:358), statistics |
| `src/main_basestation.cpp` | Value-8 option labeled with the true mode name | ✓ VERIFIED | "CIF 400x296" :616 |
| `scripts/verify_protocol_roundtrip.mjs` | Factory mirror + type-emitting serializer mirror + clause (f) with teeth | ✓ VERIFIED | createResponsePacketMirror :132-155; serializer emits packet.type :175; (f1)-(f4) :374-396; clauses (c)/(e) routed through the factory; header clause list updated |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| CommandHandler::process success responses | createResponsePacket → serializeResponse → wire byte 2 | Every ACK/STATUS traverses the chain | ✓ WIRED | process :98-103 builds via the factory; factory assigns type :374; serializer emits verbatim :159 — byte 2 is 0x11 on the real construction path |
| camera->getFrameSize (real framesize_t) | handleGetStatus → ResponseStatusData.currentResolution | frameSizeFromEsp name-based reverse mapping | ✓ WIRED | :579 → :741-766; raw numeric reinterpretation eliminated |
| deserializeResponse output | handleResponse terminal-state guard | duplicate/late responses return before counter/statistics change | ✓ WIRED | processIncomingByte :311-313 → handleResponse :336-338 guard precedes all mutations |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| /status | sent/acked/failed/pending | CmdSender counters + hasPendingCommands | Yes | ✓ FLOWING — duplicate-response corruption path closed by the :336-338 guard |
| /status | lastCmd/lastSeq/lastState/lastRetry | appState + getCommandState/RetryCount | Yes | ✓ FLOWING |
| /status | connected/linkText | lastAckTime + terminal outcomes | Yes | ⚠️ FLOWING — WR-06 uint16 ACK-edge truncation remains a Warning |
| /status | queue[] | getCommandQueue over 5-slot table | Yes | ✓ FLOWING |
| GET_STATUS response | imageId/enabled/interval | AutoCap() module | Yes | ✓ FLOWING |
| GET_STATUS response | currentResolution | frameSizeFromEsp(camera->getFrameSize()) | Yes | ✓ FLOWING — previously CORRUPTED (cross-enum cast), now name-translated |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Wire-format regression suite incl. clause (f) | `node scripts/verify_protocol_roundtrip.mjs` (verifier-run) | 15/15 PASS, exit 0 — (f1) ACK byte 2 == 0x11, (f2) NACK byte 2 == 0x11, (f4) defective variant == 0x00 | ✓ PASS |
| Both firmware targets compile | `pio run -e esp32-s3-balloon -e esp32-s3-basestation` (verifier-run) | 2 succeeded (29.3 s / 20.6 s) | ✓ PASS |
| CR-05 removal gates (regression) | Negative greps (legacy symbols in main_balloon, isTimeToCapture) + positive wiring gates :387/:725/:728 | 0 / 0 / all present | ✓ PASS |
| Gap-2 negative gates | FRAMESIZE_QXGA remnants; false matching-comments | 0 / 0 | ✓ PASS |
| CR-04 dormancy gate | grep captureThumbnail/captureBoth/createThumbnail callers outside camera_manager | 0 external callers | ✓ PASS (dormant; deferred to Phase 2) |
| Retry state machine exercised by a host test | none exists — no host harness for CommandSender | n/a | ? SKIP (hardware UAT item 2; guard is code-proven, race is runtime) |

### Probe Execution

No probes declared in any PLAN/SUMMARY; no `scripts/*/tests/probe-*.sh` exists. The declared verification commands (harness + both builds) were executed by this verifier above. SKIPPED otherwise.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| CTRL-01 | 01-02, 01-03, 01-05, 01-06 | Trigger camera capture from base station web UI | ✓ SATISFIED (code; UAT 1 outstanding) | Full path intact; response type now conforms on the entire success path (CR-01 closed) |
| CTRL-02 | 01-03, 01-04, 01-06 | Adjust ALL camera settings remotely | ✓ SATISFIED (code; UAT 3 outstanding) | 7/7 forms/routes/handlers; SET path name-mapped; WR-01 resolved — every resolution option now targets a real OV2640 mode; GET_STATUS reports truthfully (CR-02 closed) |
| CTRL-03 | 01-03, 01-04, 01-05 | Manual + automatic capture modes | ✓ SATISFIED (code; UAT 4 outstanding) | CR-05 closure regression-verified this run |
| CTRL-04 | 01-03, 01-04, 01-05 | Automatic capture fixed interval timing | ✓ SATISFIED (code; UAT 4 outstanding) | Exact commanded cadence 1000..3600000 ms; one ID sequence |
| CTRL-06 | 01-02, 01-03, 01-06 | Failed commands retried with timeout | ✓ SATISFIED (code; UAT 2 outstanding) | Retry mechanics intact AND accounting integrity restored (CR-03 closed) — the requirement's defect from re-verification #2 is gone |
| PRI-02 | 01-02, 01-06 | Retry with timeout for failed transmissions | ✓ SATISFIED (code; UAT 2 outstanding) | Same evidence as CTRL-06 |

Orphaned requirements: none — the union of `requirements` fields across 01-02..01-06 equals exactly the six phase IDs; REQUIREMENTS.md traceability maps the same six to Phase 1 (CTRL-05 correctly routes to Phase 2).

### Plan Prohibition Verdicts (judgment-tier — autonomous, NON-AUTHORITATIVE)

**unverified-prohibition — human review recommended** (01-06 plan, flagged at planning time; no wired enforcement exists for a judgment-tier item, so it cannot be marked green autonomously).

| Plan | Prohibition | Verdict | Notes |
|------|-------------|---------|-------|
| 01-06 | MUST NOT report fabricated protocol or camera state: every response-construction path carries its documented packet type (0x11) onto the wire; GET_STATUS fields reflect actual module state through real conversions, never a raw numeric reinterpretation across differently-numbered enums | PASS (code level, non-authoritative) | All four response factories assign the shared constant; GET_STATUS derives every field from real getters plus the name-based mapping; numeric reinterpretation eliminated (negative gates zero). Residual truthfulness items at Warning level, none fabricating protocol state: WR-13 false health-check diagnostic (log-only; `performSystemChecks` returns true unconditionally), documented QVGA fallback for unrepresentable sizes, IN-08 GET_STATUS endianness (no consumer yet). Human confirmation rides the UAT items above |

### Anti-Patterns Found

Confirmed present by this verifier unless marked "review-reported". None is a Phase 1 must-have blocker; CR-04 is deferred to Phase 2 with a must-fix note.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/camera_manager.cpp | 294, 311-317, 328-334 | CR-04: createThumbnail failure paths free `thumbnail.buffer` without nulling — dangling member; next freeCurrentThumbnail double-frees. Feeds off WR-11 (4000-byte estimate routinely undersized, so failure is the common case once called) | 🛑 Critical defect, DORMANT in Phase 1 → deferred to Phase 2 | No Phase 1 caller (verifier grep-proven); `enableCamera(false)` sites (main_balloon:705/:937) only free a null buffer today. Fix (null member on both failure paths + allocation headroom/realloc) must land before Phase 2 wires the thumbnail path |
| src/command_handler.cpp / src/command_sender.cpp | 673-680 / 309-313 | WR-12 (new): receive paths never validate the packet-type byte — a CRC-valid RESPONSE framed on the balloon's channel would be executed as a command (and symmetrically on the base station). Missing receive-side counterpart of the CR-01 fix | ⚠️ Warning | Latent with a single disciplined pair; live risk when Phase 2 image traffic shares the channel. One-comparison fix + harness clause recommended in Phase 2 protocol work |
| src/main_balloon.cpp | 465-469 | WR-13 (new): health check condition degenerates to `appState.cameraActive` (real check commented out) — every boot with a working camera logs "Camera system health check failed" | ⚠️ Warning | False diagnostic only — `performSystemChecks` returns true unconditionally (:483); masks genuine check failures in the same report |
| src/command_handler.cpp | 676-679 | WR-02 (carried): single pending-command slot; second framed command in same read burst overwrites the first | ⚠️ Warning | Recovered via base-station retry after the D-05 window |
| src/command_handler.cpp | 147-226 | WR-03 (carried): no duplicate-sequence suppression — retried CAPTURE_NOW re-executes, burns image IDs | ⚠️ Warning | Gaps in Phase 2 image-ID sequence; interacts with WR-09 |
| src/e32_lora.cpp | 158-202, 104-131 | WR-04 (carried): blocking transmit up to ~7 s; setMode delay(50) | ⚠️ Warning | Deferred to hardware bring-up |
| src/main_basestation.cpp | 503-507 | WR-06 (carried): ACK-edge detection truncates 32-bit counter to 16 bits — LED edge case at 65536 boundaries | ⚠️ Warning | Link-LED truthfulness edge |
| src/e32_lora.cpp | 264-344 | WR-07 (carried): E32 config API contradicts datasheet; setChannel silently no-ops | ⚠️ Warning | Uncalled; fix at radio bring-up |
| src/main_basestation.cpp | 38-41 | WR-08 (carried): hardcoded AP credentials; unauthenticated command endpoints | ⚠️ Warning | Bench-acceptable; `/gsd-secure-phase` scope |
| include/command_protocol.h | 185 | WR-09 (carried): 2 s CAPTURE_NOW window can be shorter than worst-case capture | ⚠️ Warning | Interacts with WR-03 at UAT 2 |
| src/main_basestation.cpp | 1058-1069 | WR-10 (carried): auto-capture chip latches latest issued interval, not the ACKed one; seq-wrap freezes the chip | ⚠️ Warning | UI truthfulness of the auto-capture chip |
| src/camera_manager.cpp | 290-334 | WR-11 (carried): thumbnail size estimate too small | ⚠️ Warning | Folded into the CR-04 Phase 2 deferral |
| src/command_handler.cpp / command_sender.cpp | 633-640 / 270-277 | IN-06 (carried): 0xAA 0xAA 0x55 loses frame sync; duplicated validatePacket | ℹ️ Info | Noise-robustness gap |
| include/command_protocol.h | 110-173 | IN-08 (carried): dead payload/response structs misdocument the wire; GET_STATUS memcpy little-endian vs big-endian convention | ℹ️ Info | First real GET_STATUS consumer must define the wire encoding |
| src/command_sender.cpp | 383-396, 406-415 | IN-01 (carried): dead findOldestCommand; eviction comment/code mismatch | ℹ️ Info | Cleanup |
| src/main_basestation.cpp | 1110-1123 | IN-02 (carried): dead sendHTML/updateLED; LED blinks unconditionally | ℹ️ Info | Cleanup |
| src/main_balloon.cpp | 375 | IN-03 (carried): hardcoded E32 pins duplicate sensor_pins.h macros | ℹ️ Info | Divergence risk |
| src/command_handler.cpp | 529 | IN-04 (carried): interval bounds restated as literals | ℹ️ Info | Divergence risk |
| include/e32_lora.h | 91 | IN-07 (carried): lastReceiveTime never updated on single-byte read path | ℹ️ Info | Bogus printStatus "Last RX" |
| .planning/ROADMAP.md | 48 | Stale header: "5/6 plans executed … 01-06 pending" contradicts Wave 4 checkbox `[x]` at line 67 | ℹ️ Info | Update at next docs pass |

No TBD/FIXME/XXX markers in any 01-06-modified file (verifier grep clean, 0 matches across all seven).

### SUMMARY vs Reality

1. 01-06 SUMMARY claims reproduce on this verifier's own runs: all three fix commits exist with matching subjects (75b8514, 36674ff, 56704e2; docs commits 36fbf05/35e029a/fa921a7 present), the three code closures hold line-by-line, the harness passes 15/15, both targets build SUCCESS. The claimed auto-fix (const qualifier on frameSizeFromEsp definition) is visible in the source and validated by compilation.
2. 01-REVIEW.md (fa921a7) independently confirms all three closures; its remaining Critical (CR-04) was re-examined by this verifier and classified dormant/deferred rather than blocking, with dormancy proven rather than assumed.
3. REQUIREMENTS.md marks CTRL-01..04, CTRL-06, PRI-02 Complete — accurate at the code-level standard of this cycle; all six remain contingent on hardware UAT items 1-4, which is what `status: human_needed` records.

### Human Verification Required

Four hardware UAT items (the carried behavior-unverified truths) plus one flagged prohibition review. These are the only things between Phase 1 and `passed`; every code-level must-have is green.

1. **End-to-end command round trip over real radios** — Power both boards; trigger a capture. Expect ACK within 2 s, queue row "ACK Received", counters advance. Why human: RF delivery, AUX timing, mode switching (WR-04 applies).
2. **Retry/TIMEOUT on degraded link, including the duplicate-ACK edge** — Power balloon off; issue a command. Expect 3 paced attempts then "Timeout", LED red "No link", pending returns to 0 and stays there even when a late ACK follows a retry. Why human: state-machine runtime needs two radios. The prior verifier's precondition (terminal-state guard in place) is now met — this UAT is meaningful as of plan 01-06.
3. **Camera settings on the sensor** — Send each of the 7 settings; capture and inspect. Expect visible changes; NACK_BUSY on sensor failure; resolution options incl. CIF 400x296 succeed. Why human: physical camera required.
4. **Auto-capture interval and disable behavior** — Enable at 10 s and 60 s; disable; manual trigger between. Expect exact cadence, zero captures after ACKed disable, one continuous image-ID sequence. Why human: timing across the radio link.
5. **Prohibition review (flagged)** — Confirm no fabricated protocol/camera state reaches the wire or the UI: responses carry their documented type; GET_STATUS reflects actual module state. Why human: judgment-tier prohibition, autonomously judged PASS at code level but non-authoritative without enforcement.

### Gaps Summary

No gaps. All three structured gaps from re-verification #2 are closed and were proven closed by this verifier's own reads, greps, harness run, and builds — corroborated, not replaced, by the independent re-review. The phase has no FAILED truth, no missing or stub artifact, no broken key link, and no blocker anti-pattern in its delivered surface.

Status is `human_needed` rather than `passed` for exactly one reason: SC-2/SC-3/SC-4/SC-5 assert runtime behavior over physical radios and a physical camera, which no host-side verification can exercise. The four hardware UAT items (plus the flagged prohibition review) are structured in the frontmatter. CR-04 is a genuine crash-class defect but sits in an API with zero Phase 1 callers (verifier-proven), squarely inside Phase 2's "Thumbnail generation on balloon" deliverable — deferred there with a must-fix-before-first-caller note. New warnings WR-12 and WR-13 are verified present and recorded; neither is inside a Phase 1 must-have domain.

Recommended next step: hardware UAT items 1-4 (item 5 rides along), then `/gsd-secure-phase 1` (WR-08 scope), then mark the phase complete. Phase 2 planning should pick up CR-04/WR-11 as an entry task and consider WR-12 with the image-traffic protocol work.

---

_Verified: 2026-08-18T11:12:04Z_
_Verifier: Claude (gsd-verifier)_

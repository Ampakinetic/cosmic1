---
phase: 01-command-protocol-control
plan: "08"
subsystem: infra
tags: [e32, lora, uart, register-protocol, esp32, air-data-rate, eeprom-config]

# Dependency graph
requires: []
provides:
  - "Real E32 register protocol: readConfig (0xC1 0xC1 0xC1 -> parsed 6-byte return frame) and writeConfigRegisters (0xC0 permanent-set frame, echo-verified)"
  - "Decoded boot-config log line on both boards (uart baud, parity, air rate, channel) — the 01-09 bench confirmation artifact"
  - "Boot-time fail-open air-rate enforcement at 9.6 kbps via E32LoRa::begin -> ensureLinkConfig (zero main-file wiring)"
affects: [01-09 bench session, image transfer reliability, G-01-3 closure]

# Actuals (#2632) — pairs with the plan's estimate (24000 tokens) on the same chars/4 scale.
actuals:
  tokens: 4889
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Echo-verified config writes (success only on matching module return frame — no fabricated state)"
    - "Fail-open boot convergence (config failures warn/ERROR but never abort begin — 1223f46 lesson)"
    - "Masked read-modify-write over register bytes (only the owned field changes; everything else passes through byte-for-byte)"

key-files:
  created: []
  modified:
    - src/e32_lora.cpp
    - include/e32_lora.h

key-decisions:
  - "Official manual beats the plan's expected layout: SPED is [7:6] parity / [5:3] baud / [2:0] air rate (NOT [7:5]/[4:2]/[1:0]) and the config return frame's HEAD echo is 0xC0/0xC2 (0xC1 accepted for older revisions) — verified against the official cdebyte.com E32-T Series User Manual sections 6.1-6.7 before writing any mask"
  - "9.6 kbps target encoded as a single constexpr (E32_TARGET_AIR_DATA_RATE = RATE_9_6kbps, SPED bits [2:0] code 100) with the documented retune path (19.2k up / 4.8k down)"
  - "ensureLinkConfig attached inside begin() as its last step so both boards converge on every boot with zero main-file changes"

patterns-established:
  - "Register-level truth over assumed state: the driver reports config success only from the module's echoed return frame, and boot logs state the measured (never assumed) air rate"

requirements-completed: [IMG-01, IMG-04, PRI-03]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "Real E32 register protocol — readConfig sends 0xC1 0xC1 0xC1 and parses the 6-byte return frame; writeConfigRegisters sends the 0xC0 permanent-set frame with all five registers and reports success only on a verified echo; decoded config logged at boot"
    requirement: IMG-04
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation -e esp32-s3-balloon (both SUCCESS) + structure gates (0xC0 frame, 0xC1 0xC1 0xC1 command, leading-byte check, echo-verify strings)"
        status: pass
    human_judgment: true
    rationale: "Whether the physical modules actually return the documented frame (and accept the EEPROM write) is only provable with hardware in the loop — plan 01-09 bench step 3 re-verifies the command round-trip immediately after flashing both boards"
  - id: D2
    description: "Boot-time 9.6 kbps air-rate enforcement — begin()-integrated, fail-open, masked read-modify-write preserving every non-air-rate bit/register, loud modules-may-mismatch ERROR tripwire on write failure"
    requirement: PRI-03
    verification:
      - kind: other
        ref: "pio run both targets SUCCESS + gates (ensureLinkConfig wired into begin, E32_TARGET_AIR_DATA_RATE defined, 'config read failed' and 'modules may mismatch' strings present, diff confined to e32_lora.{h,cpp})"
        status: pass
    human_judgment: true
    rationale: "The transition log line (air data rate 2.4kbps -> 9.6kbps (config persisted)) and its effect on transfer timeouts are bench observations — the 01-09 session exists to confirm them on the flashed pair"

# Metrics
duration: 14min
completed: 2026-08-23
status: complete
---

# Phase 01 Plan 08: E32 Register Config + 9.6k Air-Rate Enforcement Summary

**Real E32-900T30D register protocol (echo-verified 0xC0/0xC1 frames) with fail-open boot-time enforcement of the 9.6 kbps air rate on both boards — the G-01-3 code remediation the 01-09 bench session flashes**

## Performance

- **Duration:** 14 min (2026-08-22T23:37Z -> 2026-08-22T23:51Z UTC)
- **Started:** 2026-08-22T23:37:23Z
- **Completed:** 2026-08-22T23:51:26Z
- **Tasks:** 2/2
- **Files modified:** 2

## Accomplishments
- readConfig now implements the documented protocol: drains stale RX, sends the three-byte 0xC1 0xC1 0xC1 register-read command in sleep mode, parses the 6-byte return frame (HEAD echo + ADDH/ADDL/SPED/CHAN/OPTION) with bounded reads and a leading-byte check, and logs one decoded boot line (e.g. `E32: cfg ADDH=00 ADDL=00 SPED=1A CHAN=06 OPT=04 -> uart 9600 8N1 air 2.4kbps ch 6`) — closing the debug session's unknown-config blind spot and providing the 01-09 bench confirmation artifact
- writeConfigRegisters sends the 0xC0 permanent-set frame (all five registers) and reports success ONLY when the module's return frame echoes every written register byte — the previous stub sent no command byte and could not change anything
- ensureLinkConfig runs as the last step of begin() on both boards: reads the config, compares the SPED air-rate bits to E32_TARGET_AIR_DATA_RATE (9.6 kbps), and patches only when they differ via a masked read-modify-write that preserves UART baud, parity, and all four other registers byte-for-byte; fail-open in every path (failed read -> "config read failed - using module as-is"; failed/mismatched write -> loud "modules may mismatch" ERROR tripwire; begin() returns true regardless — 1223f46 lesson)
- setParameters/setAddress rebuilt as read-modify-write over the real registers (no more partially-uninitialized E32Config); setChannel implemented via read-modify-write with the manual's 900 MHz band range 0x00-0x45 and a one-board-severs-the-pair warning (previously a silent no-op that reported success)
- Both firmware targets build green; changes confined to e32_lora.cpp / e32_lora.h — no main-file, image-manager, or timing changes (single-lever scope held)

## Task Commits

Each task was committed atomically:

1. **Task 1: Real E32 register protocol — readConfig/writeConfig with echo verification and decoded boot logging** - `e16845a` (feat)
2. **Task 2: Boot-time air-rate enforcement at 9.6 kbps — begin()-integrated, fail-open** - `ce24cd8` (feat)

## Files Created/Modified
- `src/e32_lora.cpp` - real register protocol (readConfig, writeConfigRegisters, rebuilt setters, ensureLinkConfig, decode helpers); dead fake-CRC helper removed
- `include/e32_lora.h` - E32Config reshaped to the five registers; SPED bit masks + decoders; E32_TARGET_AIR_DATA_RATE with retune rationale; air-rate/power enums corrected per the manual

## Decisions Made
- **Manual layout adopted over the plan's expectation (plan context pre-authorized "the manual wins").** The plan expected SPED [7:5] baud / [4:2] air / [1:0] parity and a 0xC1-leading return frame. The official manual's worked example (SPED=0x1A = 8N1 + 9600 + 2.4k) and register table prove [7:6] parity / [5:3] baud / [2:0] air rate, and the return frame's HEAD byte is 0xC0 or 0xC2 (the HEAD register itself, per 6.6; the 6.2 read example is `C0 00 00 1A 06 44`). The reader accepts {0xC0, 0xC1, 0xC2} so older firmware revisions that echo 0xC1 also verify; the five register bytes are compared strictly.
- **9.6 kbps chosen as the single code lever** — mitigates B1 (chunk air time ~0.3-0.4 s vs ~1.0-1.4 s, inside the 5 s AUX bound), B2 (quarters per-chunk sub-packet loss exposure), B4 (halves channel occupation); retune stays a one-line constant change.
- **Config-mode UART constraint honored implicitly**: the manual fixes config-mode UART at 9600 8N1; both boards' begin() already opens the module serial at 9600, so no reconfiguration was needed (documented in the header).

## Deviations from Plan

### Manual-Conformance Correction (plan-directed, not an auto-fix)

**1. SPED bit layout and return-frame format differ from the plan's expectation**
- **Found during:** Task 1 (the plan-mandated manual verification BEFORE writing masks)
- **Issue:** Plan expected SPED [7:5] baud / [4:2] air rate / [1:0] parity and a return frame led by 0xC1; its example (SPED=62 -> 9600/2.4k) is internally inconsistent under that layout and traces to stale hobby-documentation values. The official cdebyte.com E32-T Series User Manual (sections 6.1-6.7, fetched during execution) documents: SPED [7:6] parity / [5:3] baud / [2:0] air rate (worked example 0x1A); return frame = HEAD echo (0xC0/0xC2 per register table 6.6, 0xC1 accepted for older revisions) + five register bytes; factory default for the 900 MHz band `C0 00 00 1A 06 04` (9600 8N1, 2.4 kbps, channel 06 = 868.125 MHz, 30 dBm, FEC on); config-mode UART fixed at 9600 8N1.
- **Fix:** Masks/shifts written to the manual layout (E32_SPED_PARITY_MASK 0xC0 >> 6, E32_SPED_BAUD_MASK 0x38 >> 3, E32_SPED_AIR_RATE_MASK 0x07); leading-byte check accepts {0xC0, 0xC1, 0xC2}; enum values re-anchored as raw 3-bit codes (RATE_9_6kbps=0x04 is unchanged in value, now correct in position). The read-back echo verification would have caught a wrong layout at bench time; with the manual layout it should instead pass on first flash.
- **Files modified:** include/e32_lora.h, src/e32_lora.cpp
- **Verification:** pio builds green + structure gates; bench truth pending 01-09
- **Committed in:** e16845a (Task 1 commit)

### Auto-fixed Issues

**2. [Rule 1 - Bug] Misleading/dead driver surface cleaned up alongside the stub replacement**
- **Found during:** Task 1
- **Issue:** calculateConfigCRC (XOR fake "CRC") suggested config frames carry a CRC byte — the real 6-byte protocol has none; E32Power enumerated 20/17/14/11 dBm "default 20" (T30D is a 30 dBm module: 30/27/24/21, default 30); E32AirDataRate offered 0.3k/1.2k rates that do not exist on the T-series (codes 000/001 are 2.4 kbps aliases per the manual); setChannel validated <=31 then returned true having done nothing (the 01-REVIEW WR-07 class).
- **Fix:** Removed the dead CRC helper; corrected E32Power values/comments; dropped the phantom rates with an alias note; setChannel implemented via read-modify-write over the manual's 900 MHz band range 0x00-0x45 with its comment stating the choice and the pair-severing risk. No external callers existed for any of these (verified by grep before reshaping).
- **Files modified:** include/e32_lora.h, src/e32_lora.cpp
- **Verification:** Both pio targets build green; grep confirms no callers outside e32_lora.*
- **Committed in:** e16845a (Task 1 commit)

---

**Total deviations:** 1 manual-conformance correction (plan-directed) + 1 auto-fixed (Rule 1)
**Impact on plan:** None on scope or goals — the corrections were prerequisites for writing correct masks; the plan's own context section mandated the manual check and pre-authorized manual-wins.

## Issues Encountered
- The local environment could not render the manual PDF (no poppler) — extracted the text by inflating the PDF content streams and decoding the CID glyphs through the embedded ToUnicode CMaps with a stdlib-only script, then verified the SPED worked example (0x1A) arithmetically before trusting the layout.

## Known Stubs
None — setChannel was a silent no-op before this plan and is now a real read-modify-write; no placeholder values or unwired data paths remain in the driver.

## Threat Model Disposition (T-01-08-01..04)
- **T-01-08-01 (Tampering/write frame):** mitigated — ensureLinkConfig patches only bits [2:0] of SPED; ADDH/ADDL/CHAN/OPTION and all other SPED bits pass through from a fresh read; writeConfigRegisters reports success only on full register-byte echo match.
- **T-01-08-02 (DoS/boot transaction):** mitigated — reads bounded by the existing millisecond reader (1000 ms cap, typical <150 ms), fail-open on read failure, begin() returns true in every config-failure path.
- **T-01-08-03 (one-board-only severance):** mitigated — both mains call the same begin(), so both boards converge each boot; the loud "modules may mismatch" ERROR is the in-firmware tripwire; 01-09 bench step 3 re-verifies the round-trip first after flashing.
- **T-01-08-04 (Repudiation/fabricated state):** mitigated — success derives only from the echoed return frame; boot logs name the measured before/after rates.

## User Setup Required
None — no external service configuration. Hardware steps (flash both boards, bench discriminator run, transfer re-test) belong to plan 01-09.

## Next Phase Readiness
- The 01-09 bench session's code half is in place: flash both boards and expect the decoded-config boot line, then either "air rate already 9.6kbps" or "air data rate 2.4kbps -> 9.6kbps (config persisted)"; the ERROR tripwire line is a stop condition if it appears.
- **Explicit follow-up condition (plan Task 2 item 3):** pacing/stall-window changes stay out of scope unless the 01-09 discriminator run proves mechanism B4 (push-stream saturation) persists at 9.6 kbps — record that outcome there and re-plan the pacing levers (image_tx_manager/image_rx_manager margins) only then.
- G-01-3 root-cause-B remediation is code-complete; the gap closes fully when the 01-09 hardware run re-tests transfers (root-cause A, the SD wiring audit, is unchanged by this plan).

## Self-Check: PASSED

- src/e32_lora.cpp, include/e32_lora.h, 01-08-SUMMARY.md exist on disk
- Task commits e16845a (Task 1) and ce24cd8 (Task 2) present in git log
- Both pio targets green after each task; all plan-specified structure gates passed

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-23*

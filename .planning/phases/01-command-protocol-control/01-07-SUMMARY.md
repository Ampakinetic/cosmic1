---
phase: 01-command-protocol-control
plan: "07"
subsystem: ui
tags: [ajax, fetch, event-delegation, form-serialization, PROGMEM, esp32-webui, gap-closure]

# Dependency graph
requires:
  - phase: 01-command-protocol-control
    provides: control forms + POST routes (/capture, /set-*, /auto-capture*, /set-event-thresholds), sendResponse {status,message} shape, D-16 queue panel + pollOnce renderer
  - phase: 03-enhanced-web-interface
    provides: alerts-form (03-03) and wifi-form (03-05) direct submit handlers the delegated listener must never double-fire
provides:
  - G-01-4 closure at code level — one delegated in-page submit listener covering all 11 native control forms in section#capture (browser stays on the admin page; server verdict renders per-form; queue/status UI refreshes via pollOnce)
affects: [01-09 hardware UAT session (flashes this build; confirms the browser truth), future admin-page form work]

# Actuals (#2632) — estimate 22000 tokens vs realized 1057 (chars/4 over the code diff)
actuals:
  tokens: 1057
  tasks: 2
  commits: 1

tech-stack:
  added: []
  patterns:
    - "Delegated event submission: ONE listener on a section element intercepts every native form inside it — route from form.getAttribute('action'), generic serialization over form.elements, no route list, no new ids"
    - "Marker-class message-div recovery (js-capture-msg) — per-form feedback node created lazily and recovered on later submits without global state or dynamic-markup growth"

key-files:
  created: []
  modified:
    - src/main_basestation.cpp

key-decisions:
  - "Single commit placed at Task 2 per plan design: Task 2's diff gates (additions-only, hunk strictly inside the footer literal) require the change uncommitted to have teeth; Tasks 1+2 form one atomic 69-line insertion"
  - "Double guard against double-submission: ev.defaultPrevented return (alerts-form/wifi-form direct handlers run first during bubbling and prevent) plus explicit id skip as belt-and-braces if handler ordering ever changes"
  - "Empty-body serialization is correct for /capture and /auto-capture-stop — their routes take no arguments; the generic elements loop naturally yields an empty body"
  - "IN-03 discipline kept: message divs render only the server-returned message text (fallbacks 'Command sent'/'Failed to send command' only when the body carries none); ACK/terminal wording continues to come exclusively from the polled queue"

patterns-established:
  - "Delegated submit interception for sections of native forms (generalizes the alerts-form AJAX pattern; future control forms inside section#capture are covered automatically)"

requirements-completed: [CTRL-01, CTRL-02, CTRL-03, CTRL-04]

coverage:
  - id: D1
    description: "Delegated in-page AJAX submit handler for all 11 control forms in section#capture (G-01-4): preventDefault on every non-excluded form, generic urlencoded serialization, per-form server-verdict message div, in-flight disabling, pollOnce() after every settle"
    requirement: CTRL-01
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-basestation -e esp32-s3-balloon (both SUCCESS)"
        status: pass
      - kind: other
        ref: "structure gates: defaultPrevented / getElementById('capture') / form.getAttribute('action') greps + exactly 2 rawliteral openers + zero-deletion diff confined to HTML_FOOTER hunk @@ -1510,0 +1511,69 @@"
        status: pass
    human_judgment: true
    rationale: "The runtime truth of G-01-4 (browser stays on the admin page, queue panel shows the new command without Back) is a bench-browser observation; the plan folds final operator confirmation into the 01-09 hardware UAT session, which flashes this build"

# Metrics
duration: 7min
completed: 2026-08-22
status: complete
---

# Phase 01 Plan 07: Capture-Section In-Page AJAX Submit (G-01-4) Summary

**One delegated submit listener on section#capture turns all 11 native control forms (capture, 7 settings, auto-capture enable/stop, event thresholds) into in-page fetch posts with per-form server-verdict messages — the operator never lands on the raw JSON body again**

## Performance

- **Duration:** 7 min (417 s)
- **Started:** 2026-08-22T23:26:30Z
- **Completed:** 2026-08-22T23:33:27Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments

- Closed UAT gap G-01-4 (major, Test 4) at code level: pressing Trigger Camera Capture, any of the 7 camera-settings submits, Enable/Disable Auto-Capture, or Save Event Thresholds now submits in-page via fetch — the browser stays on the admin page
- ONE delegated listener on the `capture` section element covers all 11 forms with zero per-form wiring: route taken from each form's own `action` attribute, serialization generic over `form.elements` (empty body for /capture and /auto-capture-stop is correct — no route list to maintain, no new ids)
- Phase 3 handlers protected from double-submission by two guards: `ev.defaultPrevented` return (direct alerts-form/wifi-form listeners run first during bubbling and prevent) plus explicit id skip
- Per-form message div (lazily created, recovered via `js-capture-msg` marker class) renders only the server-returned `{status,message}` text through setText/setClass — IN-03 discipline intact; `pollOnce()` runs after every settle and catch so the Command Queue panel, pinned last-command row, autocapture-chip, and event-chip pick up the queued command without a reload
- 1223f46 constraint respected: the entire 69-line insertion lives inside the HTML_FOOTER PROGMEM raw literal (hunk `@@ -1510,0 +1511,69 @@`); the dynamic String-built handleRoot markup and HTML_HEADER are byte-identical (zero deletions in the diff); both firmware targets build green

## Task Commits

1. **Task 1: Delegated in-page submit handler for every control form in section#capture (G-01-4)** - implemented, verified (builds + 4 structure gates), held uncommitted for Task 2's diff gates
2. **Task 2: Structure gates — no navigation fallback, no dynamic-section growth, handlers intact** - `754f834` (fix) — single atomic commit per the plan's commit placement (Task 2 action step 4)

**Plan metadata:** (docs commit below)

## Files Created/Modified

- `src/main_basestation.cpp` - delegated submit listener (lines ~1511-1579) inserted in the HTML_FOOTER PROGMEM script between the wifi-form handler and renderWiFi: guards, generic serialization, marker-class message div, in-flight disabling, fetch to form action, pollOnce after settle/catch

## Decisions Made

- Commit placed at Task 2, not Task 1 — Task 2's automated diff gates (`grep -c '^-[^-]'` -eq 0, additions-only hunk) operate on the uncommitted working diff; committing after Task 1 would make them vacuous. Tasks 1+2 are one contiguous insertion, so the single commit is genuinely atomic.
- `js-capture-msg` marker class on the message divs for recovery on later submits — no global state, zero bytes added to the dynamic markup section, styling rides the existing `.message` classes.
- Response handling mirrors the alerts-form then-chain exactly (`r.json()` → `{ok: r.ok, msg: j.message}`) — a non-JSON body throws into the catch path and still re-enables + pollOnce().

## Deviations from Plan

None - plan executed exactly as written (single-commit placement at Task 2 is the plan's own design, not a deviation).

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- G-01-4 closed at code level; the operator-visible half (browser stays on the page, queue row appears without Back) is confirmed when the 01-09 hardware session flashes this build — 01-UAT.md Test 4 truth re-run rides that session
- G-01-3 (storage/bench discriminator, plan 01-08) and the 01-09 combined bench session remain for Phase 01 close-out

## Self-Check: PASSED

- Commit `754f834` exists (git log)
- `src/main_basestation.cpp` modified in `754f834` (69 insertions, 0 deletions)
- All Task 1/Task 2 automated gates re-run green (builds, greps, diff checks)

---
*Phase: 01-command-protocol-control*
*Completed: 2026-08-22*

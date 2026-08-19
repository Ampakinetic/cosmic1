---
phase: 03-enhanced-web-interface
plan: "05"
subsystem: ui
tags: [esp32, wifi, preferences, nvs, webserver, statemachine, arduino]

# Dependency graph
requires:
  - phase: 03-enhanced-web-interface
    provides: "D-45 single-page dashboard skeleton, /api/state serializer, settings card group, WR-07 POST patterns (03-01..03-04)"
provides:
  - "WiFiMgr() module — NVS-persisted AP/Station modes, boot STA-first with 20 s AP fallback, runtime switch that never strands the operator (WEB-05 / D-40)"
  - "POST /wifi route with WR-07 bounded credential validation"
  - "/api/state wifi block — queried radio truth (mode/ssid/ip/joining/errorSsid), never the password"
  - "📶 WiFi card — two-step inline confirm, queried mode line, join-failure copy"
  - "WEB-06 phase-closing negative-diff proof of the untouched balloon WiFi camera interface"
affects: [hardware-uat, phase-03-verification]

# Actuals (#2632) — pairs with the plan's estimate (35000 est. tokens) to calibrate future estimates.
actuals:
  tokens: 7320    # chars/4 over the realized src/platformio.ini diff (29,282 chars)
  tasks: 2
  commits: 2

# Tech tracking
tech-stack:
  added: []       # Preferences/WiFi/WebServer all ship in the Arduino-ESP32 core — no new deps
  patterns:
    - "keep-old-interface-until-join-confirms: WiFi.begin() while the AP serves lets the core enable STA alongside; update() converges via WiFi.mode(WIFI_STA/WIFI_AP) so the dual-mode constant never appears and the radio never rests dual-mode"
    - "loop-driven millis join deadline (wrap-safe subtraction) — server.handleClient() never blocks during a join"
    - "NVS persist FIRST, radio SECOND — a reboot mid-switch honors the operator's choice"

key-files:
  created:
    - src/wifi_manager.h
    - src/wifi_manager.cpp
  modified:
    - src/main_basestation.cpp
    - platformio.ini

key-decisions:
  - "Dual-mode discipline without the dual-mode constant: during a runtime AP→STA join the AP keeps serving because WiFi.begin() ORs STA into the running mode; the moment WL_CONNECTED arrives update() calls WiFi.mode(WIFI_STA) (or WIFI_AP on deadline fallback) — single-mode always, the WIFI_AP_STA token never appears in wifi_manager.cpp (grep gate green)"
  - "Join deadline is wrap-safe: joinStartMs + millis()-subtraction (the codebase D-26 idiom) instead of an absolute joinDeadlineMs comparison, immune to the ~49-day millis rollover"
  - "/api/state wifi block carries ssid in addition to the listed {mode, ip, joining, errorSsid} — the locked mode-line copy 'Mode: Station ({ssid}) · IP {ip}' cannot render without it; ssid is JSON-escaped (quotes/backslashes are legal in network names); the PASSWORD never appears anywhere (T-03-12)"
  - "An explicit switch to Access Point clears joinErrorSsid — an explicit switch is a successful switch, so the stale 'Could not join' row cannot outlive the operator's next action (successful STA joins clear it in update())"
  - "The WiFi form serializes switches: controls disable while wifi.joining is true (T-03-15 flapping mitigation layered on the two-step confirm)"

patterns-established:
  - "Radio-owning manager: single module owns every WiFi transition (persistent(false), mode changes, join state machine) — no radio calls scattered in the app"
  - "Queried-truth WiFi status: mode/ssid/IP read from WiFi.getMode()/SSID()/localIP()/softAPIP(), never from the submitted form (IN-03 extended to WiFi)"

requirements-completed: [WEB-05]

# Coverage metadata (#1602)
coverage:
  - id: D1
    description: "WiFiManager firmware module — NVS-persisted modes, boot STA-first with 20 s AP fallback, runtime switch keeping the old interface serving until the new one confirms"
    requirement: WEB-05
    verification:
      - kind: other
        ref: "pio run -e esp32-s3-balloon -e esp32-s3-basestation (both SUCCESS, final tree)"
        status: pass
      - kind: other
        ref: "grep gates: no WIFI_AP_STA / no delay( in wifi_manager.cpp; WiFi.persistent(false), Preferences, wifi_manager.cpp balloon exclusion present"
        status: pass
    human_judgment: true
    rationale: "Runtime join/fallback/NVS-persistence-across-reboot behavior needs the physical radio and a real network — hardware UAT (boot into AP with no saved station; save a real station; bogus-network fallback within ~20 s; reboot honors persisted mode)"
  - id: D2
    description: "POST /wifi route (WR-07 bounded mode/ssid/password validation, 400/500 locked copy) + /api/state wifi block read from the radio's actual state"
    requirement: WEB-05
    validation: {}
    verification:
      - kind: other
        ref: "base build SUCCESS + grep gates ('/wifi' route registered, wifi block in handleApiState, password absent from every response path)"
        status: pass
    human_judgment: true
    rationale: "HTTP validation paths (400 on bad bounds, 500 on persist failure, switch behavior) are exercisable only against the live device — hardware UAT"
  - id: D3
    description: "📶 WiFi card — two-step inline confirm (Apply → danger-red Confirm WiFi Switch, outside click reverts), queried mode line, static 20 s fallback note, join-failure error copy"
    requirement: WEB-05
    verification:
      - kind: other
        ref: "base build SUCCESS + literal-copy greps (Apply WiFi Settings / Confirm WiFi Switch / Mode: Access Point / within 20 s)"
        status: pass
    human_judgment: true
    rationale: "Interactive two-step confirm and visual card behavior need a live browser against the device — hardware UAT (per UI-SPEC E6 backstops)"
  - id: D4
    description: "WEB-06 phase-closing negative check — the balloon's WiFi camera interface is byte-identical to the pinned phase-start ref"
    verification:
      - kind: other
        ref: "git diff --name-only a5e6290dcd4284f4eb8c9382f8285651ef411d05 -- src/app_httpd.cpp include/camera_index.h include/wifi_config.h → EMPTY"
        status: pass
    human_judgment: false

# Metrics
duration: 12 min
completed: 2026-08-19
status: complete
---

# Phase 03 Plan 05: Runtime WiFi Mode Switching Summary

**Preferences-backed WiFiManager (boot STA-first with a loop-driven 20 s AP fallback, runtime switch that keeps the old interface serving until the new one confirms), POST /wifi + queried-truth wifi block, 📶 WiFi card with two-step inline confirm — plus the phase-closing WEB-06 proof that the balloon's WiFi camera interface never changed**

## Performance

- **Duration:** 12 min
- **Started:** 2026-08-19T17:22:51Z
- **Completed:** 2026-08-19T17:35:10Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- WiFiMgr() module (src/wifi_manager.h/.cpp): Preferences namespace "wifi" persists mode + STATION credentials only; the AP credentials relocated from main_basestation.cpp as compile-time constants (never persisted, never UI-editable); boot prefers a saved STA join with the 20 s non-blocking deadline and falls back to softAP on expiry — no path ends with no interface serving
- Runtime switching never strands the operator: requestSwitch persists to NVS FIRST (a reboot mid-switch honors the choice), then drives the radio — during an AP→STA join the AP keeps serving until WL_CONNECTED, at which point update() converges on single-mode STA; a failed join reverts to AP within 20 s with joinErrorSsid set for the card's error copy
- POST /wifi with WR-07 bounded validation (mode enum; Station ssid 1–32 required, password 8–63 required; 400/500 locked copy) and /api/state wifi block {mode, ssid, ip, joining, errorSsid} read from the radio's actual state — the password appears in no response, field, or log (T-03-12)
- 📶 WiFi card as the last settings card (D-45): always-visible 20 s fallback note, queried mode line ("Mode: Access Point (Cosmic1-BaseStation) · IP {ip}" / "Mode: Station ({ssid}) · IP {ip}"), AP-default mode select, maxlength-bounded SSID/password inputs, and the two-step inline confirm (Apply WiFi Settings → danger-red Confirm WiFi Switch; outside click reverts; no modal); form serializes switches while a join is in flight (T-03-15)
- WEB-06 negative check GREEN against the pinned phase-start ref — the balloon boundary promise closed (details below)

## Task Commits

Each task was committed atomically:

1. **Task 1: WiFiManager module — NVS modes, boot STA-first with 20 s AP fallback, runtime switch, POST /wifi (D-40)** - `ed5917b` (feat)
2. **Task 2: 📶 WiFi card UI (two-step confirm, queried mode line) + WEB-06 balloon-boundary negative check** - `a58016d` (feat)

**Plan metadata:** (docs commit — this file + STATE/ROADMAP/REQUIREMENTS)

## Files Created/Modified
- `src/wifi_manager.h` (NEW) — WifiMode/WifiStatus types, WIFI_JOIN_TIMEOUT_MS 20000, WR-07 credential bounds, WiFiManager API (begin/update/requestSwitch/getStatus/resetJoinError), WiFiMgr() singleton
- `src/wifi_manager.cpp` (NEW) — Preferences namespace "wifi" (mode u8 + station ssid/pass), compile-time AP constants (relocated verbatim), boot STA-first/AP-default, loop-driven join state machine, keep-old-until-confirm switching, queried-truth getStatus
- `src/main_basestation.cpp` — initWiFi() delegates to WiFiMgr().begin(); loop() ticks WiFiMgr().update(); POST /wifi route + handler; /api/state wifi block with JSON-escaped ssid; jsonEscape helper; 📶 WiFi card HTML + renderWiFi/two-step-confirm script; text/password inputs reuse the locked number-input CSS
- `platformio.ini` — balloon env excludes the base-only wifi_manager.cpp

## WEB-06 Negative Check (phase-closing verification)

- **Pinned phase-start ref (PHASE_BASE):** `a5e6290dcd4284f4eb8c9382f8285651ef411d05` — "fix(03): revise plans per checker feedback" (the last commit touching a phase PLAN file before any wave-1 source change)
- **Balloon WiFi camera interface diff — EMPTY (PASS):**
  `git diff --name-only a5e6290 -- src/app_httpd.cpp include/camera_index.h include/wifi_config.h` produced no output — the balloon's WiFi camera interface is byte-identical to pre-phase (WEB-06)
- **Balloon-side phase diff — only the pre-authorized 03-01 beacon-battery changes:**
  `git diff --stat a5e6290..HEAD -- src/main_balloon.cpp src/image_tx_manager.cpp src/command_protocol.cpp include/image_protocol.h` → 4 files, +35/−11, all beacon-battery: `IMG_TELEMETRY_BEACON_BODY_SIZE` 17→19, `batteryMilliV` u16 + flags bit1 validity, serializer/deserializer read/write + length arithmetic (28→30), plausibility-gated population (1.8–8.0 V AND nonzero raw ADC), and the 1 s `PowerMgr().update()` millis timer in main_balloon.cpp. No unrelated balloon change exists.

## Decisions Made
- Dual-mode discipline without the dual-mode constant: during a runtime AP→STA join the AP keeps serving because `WiFi.begin()` ORs STA into the running mode; update() converges via `WiFi.mode(WIFI_STA)` the moment WL_CONNECTED arrives (or WIFI_AP on deadline fallback) — the `WIFI_AP_STA` token never appears in wifi_manager.cpp, satisfying the "never a steady dual-mode state" grep gate while keeping the fallback UI reachable (Pitfall 6)
- Join deadline uses wrap-safe `joinStartMs` + millis subtraction (the D-26 codebase idiom) rather than an absolute deadline comparison — immune to the ~49-day rollover
- The wifi block carries `ssid` beyond the must-have's listed {mode, ip, joining, errorSsid}: the locked mode-line copy "Mode: Station ({ssid}) · IP {ip}" cannot render without it; JSON-escaped because quotes/backslashes are legal SSID bytes (hand-built String JSON stays unbroken)
- Explicit AP switch clears joinErrorSsid (an explicit switch is a successful switch); successful STA joins clear it inside update(); resetJoinError() remains the manual API
- text/password inputs extend the existing `input[type="number"], select` CSS selector verbatim — zero new design tokens on the touched surface

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 03 (enhanced-web-interface) is code-complete: all 5 plans executed (03-01 dashboard tracer, 03-02 map, 03-03 alerts, 03-04 gallery, 03-05 WiFi + WEB-06). Next: phase re-verification (`/gsd-verify-work 3`), hardware UAT (boot-AP/real-station/bogus-network/reboot items above + the phase's map/alerts/gallery UAT backstops), then the security gate
- Runtime halves of WEB-05 are hardware-UAT items: physical join, DHCP address discovery via card + serial log, bogus-network fallback within ~20 s with the locked error copy, reboot honoring the persisted mode, and the balloon's own camera page still serving (WEB-06 live check)

## Self-Check: PASSED

All 4 key files present (src/wifi_manager.h, src/wifi_manager.cpp, src/main_basestation.cpp, platformio.ini); both task commits verified in git log (ed5917b, a58016d).

---
*Phase: 03-enhanced-web-interface*
*Completed: 2026-08-19*

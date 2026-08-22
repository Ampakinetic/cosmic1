---
status: diagnosed
trigger: "When I press the Auto Capture Enable, or the 'Trigger Camera Capture' button, I end up looking at the JSON result string and have to go back to see the admin page."
created: 2026-08-23T00:00:00Z
updated: 2026-08-23T00:00:00Z
---

## Current Focus
<!-- OVERWRITE on each update - reflects NOW -->

hypothesis: CONFIRMED — capture/auto-capture (and settings) forms on the base station page are native <form action method="POST"> submit buttons with zero client-side interception; the handlers reply application/json, so the browser performs a full-page navigation to the raw JSON body. The footer script only wires fetch-submit handlers for the Phase 3 alerts-form and wifi-form.
test: complete — code read end to end (markup, script, handlers, routes); git -S history checked
expecting: n/a — diagnosis complete
next_action: none (goal find_root_cause_only) — fix direction recorded under Resolution
bug_class: Bohrbug (deterministic native browser behavior — every press navigates, every browser)

## Symptoms
<!-- Written during gathering, then IMMUTABLE -->

expected: "Pressing Auto Capture Enable or Trigger Camera Capture submits in-page (AJAX) and the operator stays on the admin page with the result reflected in the queue/status UI"
actual: "Browser navigates to the raw JSON response string of the endpoint; operator must press Back to return to the admin page"
errors: none reported — plain navigation to JSON body
reproduction: Phase 01 UAT Test 4 — press 'Trigger Camera Capture' or 'Auto Capture Enable' button on the admin page
started: present since the buttons were introduced (commit bd50a30, plan 01-01 T8); first noticed 2026-08-22 during Phase 01 UAT. Note: same-day commit 1223f46 fixed a script-less DASHBOARD page (3 stacked causes incl. handleRoot String truncation dropping footer script); this report concerns the ADMIN-page buttons and is NOT that bug class.

## Eliminated
<!-- APPEND only - prevents re-investigating -->

- hypothesis: Footer script truncated/failed to load again (same class as KB balloon-no-data-oled-blank cause 3 — String truncation dropping the script)
  evidence: The 57 KB footer script demonstrably loads and executes: renderQueueList/poll machinery works (UAT tests 1, 2, 5 passed with queue rows reaching terminal states and live UI truth), and the alerts-form and wifi-form on the SAME page submit via fetch with preventDefault (main_basestation.cpp:1412-1441, 1477-1509). The 1223f46 fix streams the static parts from PROGMEM; only the ~9 KB dynamic section is String-built.
  timestamp: 2026-08-23T00:00:00Z

- hypothesis: JS exists for these buttons but errors before attaching handlers
  evidence: No JS for these buttons exists anywhere. Exhaustive scan of main_basestation.cpp: submit listeners attach only to 'alerts-form' (1412) and 'wifi-form' (1477); fetch() targets only /api/state (694), /alerts/ack (1261), /alerts (1423), /wifi (1489), /gallery (1550, 1649). Zero references to /capture, /auto-capture, /auto-capture-stop, or /set-* in any script context; no generic form delegation (querySelectorAll('form'): zero hits); no inline onsubmit attributes; the capture/auto-capture forms have no id/class hook at all.
  timestamp: 2026-08-23T00:00:00Z

- hypothesis: A separate /admin page or external JS asset carries the wiring and is missing
  evidence: initWebServer route table (main_basestation.cpp:2064-2090) has no /admin route; handleRoot serves the single page (nav sections #alerts/#map/#capture/#queue/#gallery, lines 622-630). web_assets.h contains only generated Leaflet assets (embed_web_assets.mjs output, BSD header); no data/ filesystem dir exists.
  timestamp: 2026-08-23T00:00:00Z

## Evidence
<!-- APPEND only - facts discovered -->

- timestamp: 2026-08-23T00:00:00Z
  checked: .planning/debug/knowledge-base.md
  found: Prior resolved session balloon-no-data-oled-blank (2026-08-23) — cause 3: handleRoot built ~80KB dashboard as one Arduino String; operator+= silently dropped the append holding the 57KB footer script under heap fragmentation; page shipped script-less. Fixed by 3-part PROGMEM streaming.
  implication: Raised the script-delivery hypothesis; subsequently refuted for this symptom (see Eliminated) — the script loads and runs. Related only in that the page is the same one.

- timestamp: 2026-08-23T00:00:00Z
  checked: src/main_basestation.cpp:2196-2360 (handleRoot dynamic "Capture & Settings" section)
  found: Plain native forms with no interception hooks: capture form `<form action="/capture" method="POST"><button type="submit">Trigger Camera Capture</button>` (2202-2204); 7 settings forms action=/set-quality|brightness|contrast|resolution|saturation|exposure|wb all type="submit" (2212-2300); auto-capture enable `<form action="/auto-capture" method="POST">...<button type="submit">Enable Auto-Capture</button>` (2308-2314) and stop form /auto-capture-stop (2316-2320); event thresholds form /set-event-thresholds (2334+). None carry an id (except event-form) or onsubmit; capture/auto-capture buttons have no hookable attributes at all.
  implication: Default browser behavior for these forms is full-page POST navigation — exactly the reported symptom.

- timestamp: 2026-08-23T00:00:00Z
  checked: src/main_basestation.cpp:2529-2602, 2750-2798, 3582-3593 (handleCapture, handleAutoCaptureEnable/Disable, sendResponse)
  found: Every command handler ends in sendResponse(code, status, message) which does server.send(code, "application/json", {"status":...,"message":...}) — a bare JSON body, no redirect, no HTML wrapper.
  implication: The navigated-to "page" the operator lands on is the raw JSON string — matches "I end up looking at the JSON result string".

- timestamp: 2026-08-23T00:00:00Z
  checked: src/main_basestation.cpp footer script (lines ~640-1839, streamed from PROGMEM post-1223f46)
  found: Script wires AJAX submit ONLY for Phase 3 forms: alerts-form (1412: preventDefault + fetch /alerts) and wifi-form (1477: preventDefault + fetch /wifi). Queue UI (renderQueueList, 1108-1121), telemetry tiles, transfers, gallery all update from /api/state polling; autocapture-chip rendered display-only from polled ACKed truth (1774). No handler for any /capture, /auto-capture*, or /set-* form.
  implication: The in-page submission pattern exists in-repo (alerts/wifi) and the queue/status surfaces the truth expects are live — only the Phase 1 control forms were never migrated to it.

- timestamp: 2026-08-23T00:00:00Z
  checked: git -S history and .planning/phases/01-command-protocol-control/01-03-PLAN.md
  found: "Trigger Camera Capture" form introduced in bd50a30 (feat(01-01) T8) as a plain form POST; never changed since. The fetch/interception pattern first appears 92c8c47 (feat(03-03) alerts card), then wifi card. Phase 1 plan docs specify AJAX only for the 1s status poll (01-03-PLAN.md:46); no plan ever specified in-page submission for the capture/settings/auto-capture forms — the AJAX expectation enters the record only as the UAT truth (01-UAT.md:84).
  implication: Not a regression and not a dropped wiring — the in-page UX was never implemented for these buttons. Latent since 01-01, surfaced when hardware UAT finally ran (mirrors KB "why not caught": no gate asserts script-executed form behavior).

## Resolution
<!-- OVERWRITE as understanding evolves -->

root_cause: The capture/auto-capture (and all 7 settings + event-thresholds) forms on the base station page are native HTML form posts (`<form action="..." method="POST">` + `type="submit"`, src/main_basestation.cpp:2202-2360) with no client-side submit interception anywhere in the page's JavaScript — the footer script attaches fetch+preventDefault handlers only to the Phase 3 alerts-form and wifi-form (1412, 1477). Each POST handler responds with a bare application/json body via sendResponse (3582-3593), so the browser performs its default full-page navigation to the raw JSON response. In-page (AJAX) submission for these buttons was never implemented in any plan (present as plain form posts since commit bd50a30 / plan 01-01 T8); the UAT truth encodes the intended-but-unbuilt operator experience. NOT the 1223f46 script-truncation class — the script loads and executes (queue/telemetry/alerts/wifi all work).
candidate_causes: [code: forms built as native form-posts with no JS interception — CONFIRMED operative cause] [config: none] [environment: browser default form behavior is the mechanism, not a fault] [data: bare JSON response bodies shape what the operator lands on, but any non-HTML response navigates regardless]
and_gate: no — a single condition (no interception on these forms) deterministically produces the navigation on every browser; no simultaneous contributing condition required
fix: (not applied — find_root_cause_only)
verification: (n/a)
files_changed: []
fix_direction: Mirror the existing alerts-form/wifi-form pattern (main_basestation.cpp:1412-1441, 1477-1509) for the Phase 1 control forms: one delegated submit listener on the capture/settings sections (e.g., document.querySelector('section#capture').addEventListener('submit', ...)) — preventDefault, serialize the form's elements to x-www-form-urlencoded, fetch(form.action, {method:'POST',...}), render the returned {status,message} into a per-card message div, then pollOnce() so the Command Queue panel / autocapture-chip / link LED reflect the queued command. Event delegation covers all 10 forms (capture, 7 settings, auto-capture enable/stop, event thresholds) with one handler and no new form ids; the footer script is PROGMEM-streamed so the additions ride the existing 3-part streaming fix.

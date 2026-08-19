---
phase: 3
slug: enhanced-web-interface
status: approved
reviewed_at: 2026-08-19
shadcn_initialized: false
preset: none
created: 2026-08-19
---

# Phase 3 — UI Design Contract

> **Third contract on a locked visual language.** Phases 1–2 shipped the base-station web UI as embedded HTML/CSS/JS in `src/main_basestation.cpp` and locked its tokens in `01-UI-SPEC.md` (approved 2026-08-18) and `02-UI-SPEC.md` (approved + amended 2026-08-19). Phase 3 **restructures that page into the D-45 single-page mission dashboard** and adds five new surfaces: alerts bar, telemetry panel + map, image gallery, Alert Thresholds card, WiFi card. Every new surface composes from the locked tokens — **no new visual language** (four font sizes, two weights, standard spacing set, existing palette families, emoji card icons, system font stack).
>
> **Provenance:** tokens/colors/vocabularies inherited verbatim from `01-UI-SPEC.md` + `02-UI-SPEC.md`; section order, poll/backoff, map degradation, alert lifecycle, gallery behavior from `03-CONTEXT.md` decisions D-33..D-48; battery-in-beacon, E32-no-RSSI corrections, Leaflet embedding, alert-threshold defaults from `03-RESEARCH.md` (read against source this session — card order/poll interval verified at `main_basestation.cpp:806-986`, `:567`). Discretion items (stale badge, thresholds, beep details, Leaflet hosting) resolved below with RESEARCH.md recommendations as defaults.

---

## Design System

| Property | Value |
|----------|-------|
| Tool | none — raw HTML/CSS/JS in PROGMEM string literals served by ESP32 `WebServer`; no build system, no npm. shadcn gate not applicable (stack is C++ firmware + embedded HTML, not React/Next.js/Vite) |
| Preset | not applicable |
| Component library | none — hand-rolled CSS classes (inventory below); **Leaflet 1.9.4 is a vendored web asset (gzipped PROGMEM, ~46KB), not a component library** — its UI chrome is restyled to the locked tokens (see Typography) |
| Icon library | none — emoji in card/section headings (existing: 🎈 📸 ⚙️ ⏱ 📡 📦 🛰 🖼; Phase 3 adds 🗺️ ⚠️ 📶 and retires the standalone 🖼 Latest Capture card — 🖼 moves to the gallery heading) |
| Font | `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif` (system stack — no webfont download over AP-mode LAN) |

**Inherited component inventory (reuse verbatim):** `.container` (max-width 800px, centered) · `.header` (gradient `#1e293b→#334155`, radius 10px) · `.card` · `button` / `button.danger` · `.form-group` + `input`/`select` · `.message.success|.error|.info` · `.led.green|.red|.yellow` · `.transfer-row` family (02-UI-SPEC) · status-bar auto-fit grid pattern (`minmax(110px,1fr)`, ≤480px collapse to 2 columns).

**New components this phase (composed only from locked tokens):**

1. **`.section-nav`** — sticky top nav (D-45): full-width bar, `#1e293b` bg, 1px `#475569` bottom border, links in muted `#94a3b8` Body 14/700, hover + current-section link `#60a5fa`; vertical padding 8px; `position: sticky; top: 0` with z-index above the map. Links: Alerts · Map · Telemetry lives with Map (one link) · Capture · Queue · Gallery.
2. **Alert bar (D-42)** — stack of `.alert-banner` rows at the top of the content (first section, above map/telemetry): 4px severity left border, severity bg, radius 8px, 16px padding, flex-wrap row of title (Body 14/700, severity ink) + " — " + detail (Body 14/400, `#e2e8f0`); latched (safety-critical) rows append a standard **Acknowledge** button (accent gradient, 16px/700 — full ≥44px target, never a mini variant). Severity styling reuses the locked chip pairings exactly (see Color).
3. **Telemetry panel (WEB-01)** — the status-bar auto-fit grid pattern, graduated from the 6-item strip: items **Link (LED + Ready/No link/Unknown) · Altitude · Temperature · GPS · Battery · Data age**; values 18px/700 (`#60a5fa` per status-value pattern except LED-colored Link), labels 14px muted. The Sent/Acked/Failed/Pending counters move into the 📡 Command Queue card as an inline chip row (same numerals, 18px/700, muted labels).
4. **🗺️ Map & Trajectory (WEB-02)** — `.map-frame`: container-width, **360px tall** (component size, not spacing), radius 8px, 1px `#475569` border, `#0f172a` bg. Leaflet renders `L.circleMarker` + `L.polyline` only (no image marker assets — Pitfall 8). Leaflet chrome restyled: attribution + zoom-control text at Body 14px on `#1e293b` surfaces; attribution must retain "© OpenStreetMap contributors". Overlay chips inside the frame: **Recenter** button (visible only after user drag/zoom cancels auto-follow, D-39) and offline notice chip. The D-37 canvas fallback renders **inside the same frame** from the identical trajectory data (render-path swap, not a data change).
5. **🖼 Image Gallery (IMG-06)** — `.gallery-grid`: CSS grid `repeat(auto-fill, minmax(200px, 1fr))`, 16px gap → 3 columns at the 752px content width (D-46 "3–4 columns on a laptop" satisfied). `.gallery-item`: thumbnail `<img>` (existing `_T.JPG` via `/img/{id}_t.jpg`), width 100%, radius 8px; amber **Incomplete** chip overlaid top-right for D-48 images. `.pager`: Prev/Next standard buttons + "Page {n} of {m}" Body text between them; numbered page buttons styled as standard buttons. **Detail view (D-47):** inline `.card` replacing the grid within the gallery section (no modal — modal is a new component kind) with the image at max-width 100%/radius 8px + a definition list of sidecar fields (labels 14px muted, values 14px `#e2e8f0`) + a **Back to Gallery** standard button.
6. **⚠️ Alert Thresholds card (D-43)** — clone of the Phase 2 Event Capture card shape: number inputs + **Save Alert Thresholds** button (verb + noun, matches Set-*/Save-* pattern); inputs disable while the save is in flight.
7. **📶 WiFi card (D-40)** — mode select + SSID/password inputs + **Apply WiFi Settings** button (two-step inline confirm, see Copywriting); a display-only mode line showing the **queried** radio truth, never the submitted form (no-fabricated-state extends to WiFi).
8. **Stale badge (D-35)** — amber-family chip pinned in the telemetry panel's Link item position: appears on consecutive poll failures with live age, clears on recovery (copy below). Replaces nothing — the link LED keeps its IN-03 semantics.

**Section order (top→bottom, D-45 — SUPERSEDES the shipped order, queue/transfers move below capture/settings):** sticky `.section-nav` → `.header` → **Alerts bar → Telemetry panel + Map → capture/settings cards (📸 Manual Capture · ⚙️ Camera Settings · ⏱ Auto-Capture · 🛰 Event Capture · ⚠️ Alert Thresholds · 📶 WiFi) → 📡 Command Queue + 📦 Image Transfers → 🖼 Image Gallery**. Single-column card stack (scrolling page + sticky nav is the locked D-45 model; no multi-column card layout). The 🖼 Latest Capture card is absorbed by the gallery (newest image = first grid item).

**Primary focal point:** the **map + telemetry section** is this phase's informational anchor (top of page, largest surface); the **Trigger Camera Capture** button remains the single action anchor (Phase 1 contract, unchanged).

**Interaction model (locked):** one `fetch('/api/state')` poll every **5s** (the existing 1s `setInterval` is re-locked to 5000ms); on consecutive failures back off **5s→15s→30s** and show the stale badge with data age; snap back to 5s on recovery (D-35). Client renders only server-computed truth, **diff-checked** — DOM updates only on value change; the gallery list refreshes only when the finalized-image count changes (D-36). **All server-derived strings render via `textContent`, never `innerHTML`** (XSS boundary — sidecar and telemetry content is untrusted). Form POSTs follow the existing raw-JSON + panel-refresh pattern; durable feedback surfaces are the queue/transfer/threshold panels, not POST responses.

---

## Spacing Scale

**Locked scale — standard set only: {4, 8, 16, 24, 32, 48, 64}** (02-UI-SPEC amendment). The D-45 restructuring touches essentially every shipped surface, so **this phase executes the harmonize-on-touch obligations**: card padding/stack rhythm/header/container padding migrate 20px→**24px (lg)**; 15px h2-margin/message padding →16px; transfer panel 13/12/11px text →14px. No off-grid value may survive on a rebuilt surface, and no NEW off-grid value may be introduced.

| Token | Value | Usage (Phase 3 additions in bold) |
|-------|-------|-----------------------------------|
| xs | 4px | Label→input gap, status-label→value gap, **alert-banner left border width, banner internal title→detail spacing** |
| sm | 8px | Grid gaps (status/telemetry strip), LED→text gap, input/select inner padding, button-group gap, row rhythm, radius 8px, **section-nav vertical padding, nav link gap, map overlay chip inset, badge vertical padding** |
| md | 16px | `.form-group` spacing, button padding, status-bar padding, **alert-banner padding, gallery grid gap, pager control gap, telemetry-panel padding** |
| lg | 24px | Card padding, card stack rhythm, header padding, container padding, `<hr>` margins — **now the shipped value too (this phase migrates 20→24), section bottom margins, map-section vertical rhythm** |
| xl / 2xl / 3xl | 32/48/64px | Reserved — **not used; the dashboard is a single scrolling column, section rhythm is carried by lg** |

**Exceptions (component sizes, not spacing):** 12px LED diameter; 6px range/progress-track heights; 12px track corner radii; **360px map height; 200px gallery-item min width; 2px badge vertical padding (carried, optical alignment)**. Touch-target rule unchanged: all interactive controls ≥44px height (standard button ≈51px satisfied).

---

## Typography

**Locked scale — exactly 4 sizes, 2 weights. Unchanged; no additions.**

| Role | Size | Weight | Line Height |
|------|------|--------|-------------|
| Body | 14px | 400 | 1.5 |
| Label | 16px | 700 | 1.2 |
| Heading | 18px | 700 | 1.2 |
| Display | 24px | 700 | 1.2 |

- **Body 14px/400/1.5** — form labels, input values, telemetry labels, alert title+detail, stale badge, gallery/pager text, nav links, sidecar metadata values, **Leaflet attribution/zoom-control text (Leaflet's shipped ~12-13px chrome is restyled to 14px — an explicit harmonization obligation, not a deviation)**.
- **Label 16px/700** — button labels only (incl. Acknowledge, Recenter, Save Alert Thresholds, Apply WiFi Settings, pager buttons).
- **Heading 18px/700** — card/section `h2` titles, status-bar numerals, telemetry values, transfer counters.
- **Display 24px/700** — header `h1`.
- Weights 400/700 only; no italics. No text outside the four sizes — anything that doesn't fit consolidates into Body (the standing rule).

---

## Color

| Role | Value | Usage |
|------|-------|-------|
| Dominant (60%) | `#0f172a` | Page background, **map-frame bg, offline-canvas bg** |
| Secondary (30%) | `#1e293b` | Cards, header gradient base, **section-nav bg, Leaflet chrome surfaces**; raised surface `#334155` (inputs, messages, transfer rows, **alert banners use severity bg instead**) with `#475569` borders/tracks, **canvas grid lines**; body text `#e2e8f0`, muted text `#94a3b8` (**canvas coordinate labels, nav links**) |
| Accent (10%) | `#3b82f6 → #2563eb` (buttons); `#60a5fa` (text); `#3b82f6→#60a5fa` (progress gradient) | Reserved list below — extended minimally for Phase 3 |
| Destructive | `#ef4444 → #dc2626` | Reserved list below |

**Accent reserved for exactly these elements (Phase 1/2 lists + Phase 3 additions):**

1. Text `#60a5fa`: `h1`, card `h2` titles, status-bar numerals/telemetry values, COMPLETE transfer-row links, **current/hover section-nav link, current-position circleMarker stroke outline**.
2. Blue-gradient action buttons: Trigger Camera Capture, the 7 Set-* buttons, Enable Auto-Capture, Save Event Thresholds, **Apply WiFi Settings (incl. its Confirm relabel), Acknowledge, Recenter, pager Prev/Next/page buttons, Back to Gallery, gallery-detail link-out**.
3. `.message.info` left border.
4. `.progress-fill` gradient — active-transfer bar only.

Accent is NOT for: form labels, body copy, input borders, dividers, backgrounds, non-COMPLETE rows, **alert banners, trajectory track lines**.

**Destructive red reserved for exactly these elements (Phase 1/2 lists unchanged):** Disable Auto-Capture button; `.message.error` border on `#7f1d1d`; red link LED; `.progress-fill.failed`; `.transfer-state.INCOMPLETE`. **Phase 3 adds: critical-severity alert-banner left border** (see mapping).

**Alert severity mapping (D-42 "color-coded banners" — reuses the locked chip pairings verbatim, zero new hues; resolves 03-RESEARCH Open Question 2 per its own recommendation):**

| Severity (lifecycle per D-44) | Border | Background | Title ink | Anchored to |
|---|---|---|---|---|
| **critical** (latched until Acknowledge: Low Battery, GPS Lost, Landing Detected) | `#ef4444` | `#7f1d1d` | `#f87171` | INCOMPLETE chip pairing |
| **warning** (auto-clear + cooldown: Altitude Threshold, Ascent/Descent Rate, Signal Quality) | `#eab308` | `#713f12` | `#fbbf24` | RETRYING chip pairing |
| Stale badge (D-35) | `#eab308` | `#713f12` | `#fbbf24` | same amber family — "degraded", never red (a slow poll is not a failure) |

No third "informational banner" hue — non-alerting status stays in chips/LEDs (info-blue `#93c5fd`/`#1e3a5f` keeps its Phase 2 chip roles: QUEUED/RECEIVING/THUMB).

**Trajectory altitude coloring (D-37) — locked palette banding, not a ramp:** track polyline segments colored by altitude band — **green `#22c55e` below 1000 m · amber `#eab308` 1000–3000 m · red `#ef4444` above 3000 m** (informational bands, independent of alert thresholds); current-position circleMarker `#22c55e` fill with `#0f172a` stroke. Identical banding in the Leaflet and canvas render paths.

**Truth requirements (no-fabricated-state, extended):** telemetry/battery show honest-null copy until real beacons arrive; battery displays "—" while `batteryValid` is false (never a fabricated voltage); stale badge age derives from client-side poll timing only; map renders nothing before the first valid GPS fix; gallery and detail show only sidecar-verified fields (absent fields omitted, never zero-filled); WiFi card shows the queried mode; incomplete images keep their amber badge forever (D-48).

---

## Copywriting Contract

| Element | Copy |
|---------|------|
| Primary CTA | **Trigger Camera Capture** (unchanged) |
| Phase 3 CTAs | **Save Alert Thresholds** · **Apply WiFi Settings** (→ two-step confirm relabel **Confirm WiFi Switch**) · **Acknowledge** · **Recenter** · **Back to Gallery** · **Previous / Next** (pager) — all verb-first, matching the Set-*/Save-* pattern |
| Stale badge (D-35) | **Stale — data {N} s old** · retrying every {15\|30} s (amber chip; clears silently on recovery — no "recovered" banner) |
| Alert banner pattern | **{Title} — {detail with live values}** (severity styling per Color table; critical rows append the Acknowledge button) |
| ALRT-01 (warning) | **Altitude Threshold — {x} m, above the {threshold} m warning level** |
| ALRT-02 (critical) | **Low Battery — {x.x} V, below the {y.y} V threshold** · detail while invalid: **Battery voltage not reported by the balloon** |
| ALRT-03 (critical) | **GPS Signal Lost — no valid fix for {N} s** |
| ALRT-04 (critical) | **Landing Detected — altitude within {x} m of ground level and stable for {N} s** |
| ALRT-05 (warning) | **Ascent Rate — climbing at {x.x} m/s, above the {y} m/s limit** / **Descent Rate — descending at {x.x} m/s, above the {y} m/s limit** |
| ALRT-06 (warning) | **Signal Quality — {p}% of telemetry beacons missed in the last minute** (derived metric; the E32 has no RSSI — never display "RSSI") |
| Acknowledge action | Latched banner collapses on click; the condition re-latches only if it re-fires after clearing (D-44). No confirmation — acknowledging is reversible-by-refire, not destructive |
| Gallery empty | **No images received yet — trigger a capture to start.** (`.message.info`) |
| Gallery incomplete badge | **Incomplete** (amber chip overlay, D-48 — visible in grid and detail) |
| Pager | Page {n} of {m} · buttons Previous / Next; single page hides the pager entirely |
| Detail metadata labels | Captured · Trigger · Altitude · Position · Camera · Chunks {n}/{m} · {percent}% · Status: Complete/Incomplete — rendered only for fields present in the sidecar; "Position: GPS no fix" when the fix was invalid at capture |
| Map empty (no fix yet) | **Waiting for GPS fix — the track appears once the balloon reports a valid position.** (`.message.info` inside `.map-frame`) |
| Map offline notice | **Offline — tiles unavailable, showing plotted track.** (chip inside `.map-frame`, shown only while the canvas fallback is active) |
| Map attribution | © OpenStreetMap contributors (Leaflet default attribution — required; kept in both render paths when tiles are shown) |
| WiFi mode line | **Mode: Access Point (Cosmic1-BaseStation) · IP {ip}** / **Mode: Station ({ssid}) · IP {ip}** — queried truth only |
| WiFi pre-submit note (static, always visible in card) | **Switching modes restarts the base station WiFi. If the new network fails, the base returns to Access Point mode within 20 s.** |
| Error state (WiFi join failed, shown after AP fallback) | **Could not join "{ssid}" — running in Access Point mode. Check the network name and password, then try again.** |
| Error state (alert thresholds, 400) | Missing alert threshold parameters · Invalid altitude threshold ({range} m) · Invalid battery threshold ({range} V) · Invalid GPS-lost age ({range} s) · Invalid rate threshold ({range} m/s) · Invalid beacon-loss threshold ({range} %) — mirror of the Phase 2 pattern |
| Error state (send failure, 500) | Failed to save alert thresholds · Failed to apply WiFi settings |
| Error state (image route, 404) | Unknown image path (existing, unchanged) |
| Inherited vocabulary | Command states "Sent"→"ACK Received"/"Failed (retry {N})"→"Timeout"; transfer states QUEUED/RECEIVING/RETRYING/COMPLETE/INCOMPLETE; storage Unknown/OK/UNAVAILABLE/FULL; link Ready/No link/Unknown — **all LOCKED, not widened or renamed by the restructure** |
| Destructive confirmation | **Apply WiFi Settings**: two-step inline confirm — first click relabels the button to **Confirm WiFi Switch** (danger red, second click within the form submits; clicking anything else reverts). Rationale: the only Phase 3 action that can sever the operator's own connection; D-40's 20s AP fallback is the technical backstop, the confirm is the UI one. No modal (modals are a new component kind). Acknowledge and Disable Auto-Capture keep their existing no-confirmation contracts |

**Form labels (exact copy; ranges mirror firmware WR-07 full-long validation — defaults from 03-RESEARCH's balloon_config.h anchors, UAT-calibratable):**

| Label copy | Control | Constraint |
|------------|---------|------------|
| Altitude warning (100-10000 m): | number | min 100, max 10000, default 1000 |
| Low battery (2.5-4.5 V): | number, step 0.1 | min 2.5, max 4.5, default 3.3 |
| GPS lost after (10-300 s): | number, step 5 | min 10, max 300, default 30 |
| Rate limit (1-50 m/s): | number | min 1, max 50, default 15 |
| Beacon loss (10-90 %): | number, step 5 | min 10, max 90, default 20 |
| Landing rate below (0.5-5 m/s): | number, step 0.5 | min 0.5, max 5, default 1 |
| Landing stable for (30-600 s): | number, step 30 | min 30, max 600, default 60 |
| WiFi mode: | select | Access Point (default) / Station |
| Network name (SSID): | text | maxlength 32, required for Station |
| Password: | password input | minlength 8, maxlength 63, required for Station |

(Phase 1/2 form labels — Quality/Brightness/Contrast/Resolution/Saturation/Exposure/White balance/Interval/Event thresholds — unchanged; see `01/02-UI-SPEC.md`.)

**Beep rules (D-42, Pitfall 7):** Web Audio oscillator ~1000 Hz, 150 ms envelope, **only on the transition into a NEW critical alert** (never per-poll, never for warnings). Audio arms on the first user gesture; until armed, critical banners render visually with the hint **Audio muted until you interact with the page.** The **Mute beeps / Unmute** toggle (standard button, alert-bar header row) persists per browser session (`sessionStorage`).

---

## UI Considerations

> Populated per the ui-consideration probe (56 applicable considerations across E1–E7; E3 kinds confirmed at probe time as media + interactive-control + list-collection). Phase 3 surfaces: E1 alert bar · E2 telemetry panel + stale badge · E3 map (Leaflet + canvas fallback) · E4 gallery grid + pager · E5 gallery detail · E6 WiFi form · E7 alert-threshold form. Empty/error COPY lives in `## Copywriting Contract` — rows below reference it. Coverage: 56 applicable — 56 explicit (8 categories × 7 surfaces), plus 3 backstop markers, 0 unresolved.

| Category | Element(s) | Status | Resolution / Reason |
|----------|------------|--------|---------------------|
| empty | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: zero alerts = bar hidden entirely — no "all clear" placeholder (green LEDs/chips carry OK). E2: honest-null copy until beacons ("No telemetry received yet"); battery "—" while batteryValid false. E3: "Waiting for GPS fix…" message; map renders nothing before the first valid fix. E4: "No images received yet — trigger a capture to start."; pager hidden. E5: detail reachable only from a listed image — no empty-detail state. E6/E7: forms always render complete queried/default values, never unfilled |
| loading | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | Status-line model everywhere — no spinners/skeletons. E1: banners derive atomically from each 5s poll diff. E2: tiles fill from the first successful poll. E3: frame shows the waiting message until data exists; tiles load per Leaflet default. E4: previous grid stays until the new page arrives; list refreshes only when the finalized-image count changes (D-36). E5: definition list renders immediately from the sidecar; image loads progressively. E6: two-step relabel (Confirm WiFi Switch) is the in-flight affordance — no spinner. E7: inputs disable while the save command is Sent (Phase 2 pattern) |
| error | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: poll failure is NEVER a fabricated banner — it surfaces as E2's stale badge. E2: stale badge + 5→15→30s backoff, clears silently on recovery. E3: tile failure = canvas fallback of the identical track + offline chip. E4: missing routes keep the existing 404 copy; incomplete images keep the amber badge forever (D-48), never an error state. E5: only sidecar-verified fields render; absent fields omitted, never zero-filled. E6: AP-fallback revert copy after join failure; "Failed to apply WiFi settings" on 500. E7: per-field 400 validation copy (mirror of Phase 2); "Failed to save alert thresholds" on 500 |
| populated | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: banners stack newest-first; latched critical rows persist until Acknowledge. E2: six tiles with live 18px/700 values + counting data age. E3: full trajectory with banded track + current-position circleMarker. E4: 12 items newest-first per page, 3 columns at the 752px content width. E5: full image at max-width 100% + sidecar definition list + Back to Gallery. E6: queried mode line ("Mode: … · IP {ip}") + filled form. E7: seven inputs showing saved threshold values |
| partial | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: alerts fire only on complete server-computed truth — no partial-banner state. E2: per-field honest-null ("—"/Unknown), never a partial row. E3: short trajectories render whatever points exist — 1 point = marker only, no line. E4: incomplete images list normally with the amber badge, never rounded up (D-48). E5: only sidecar-verified fields; "Position: GPS no fix" when the fix was invalid at capture. E6: required-field validation catches partial Station input; the queried line always shows full truth. E7: range validation per field (WR-07) catches partial input — no partial-save state |
| overflow | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: banner rows flex-wrap at 14px/1.5 — Acknowledge wraps below text, never clipped. E2: auto-fit grid minmax(110px,1fr) collapses to 2 columns ≤480px. E3: map bounds contain the track; Recenter restores auto-follow after drag (D-39). E4: grid auto-fills down to 1 column; pager centers. E5: detail card grows vertically; image scales to max-width 100%. E6: 32-char SSIDs wrap in inputs; card grows vertically. E7: inputs stack vertically; labels wrap, never truncate |
| zero-one-many | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | E1: 0 alerts = hidden bar; 1..N banners render identically (severity-colored, stacked). E2: fixed six-tile grid — layout constant, only values change. E3: track identical at 0/1/many points (0 = waiting message, 1 = marker only, many = banded polyline). E4: numeric pager only — no singular/plural copy divergence; 1 page hides the pager. E5: always exactly one image. E6: exactly two mode options. E7: fixed seven inputs |
| long-text | E1 E2 E3 E4 E5 E6 E7 | ✅ resolved (explicit) | All surfaces wrap at 14px/1.5 and grow vertically — no truncation, ellipsis, or nowrap anywhere in the locked contract. E1: banner detail wraps within the flex row. E2: coordinate strings wrap in grid cells. E3: attribution + chip text wrap within the frame. E4: pager text is fixed-length; filenames never shown on grid items. E5: sidecar values wrap in the definition list. E6: long SSIDs wrap in inputs and in error copy. E7: validation errors wrap in the message row |
| cross-render parity | E3 | 🧪 resolved (backstop) | { statement: "The canvas fallback and the Leaflet view render the identical trajectory data with the identical altitude banding — verified visually by forcing tile failure (AP mode) with a populated track", verification: backstop } |
| stale cadence | E2 | 🧪 resolved (backstop) | { statement: "On forced poll failure the stale badge appears with a live age count and the poll backs off 5→15→30s; on recovery the badge clears and the cadence snaps back to 5s without a page reload", verification: backstop } |
| audio arm | E1 | 🧪 resolved (backstop) | { statement: "In a fresh tab with no prior interaction, a new critical alert shows the 'Audio muted until you interact' hint and beeps only after the first gesture; the mute toggle survives reloads within the session", verification: backstop } |

**Unresolved:** none (0). Landing-detection threshold values and the GPIO4 battery divider are UAT/calibration items (03-RESEARCH Open Questions 3–4), not UI-state gaps — the UI states above are fully specified.

---

## Registry Safety

| Registry | Blocks Used | Safety Gate |
|----------|-------------|-------------|
| shadcn official | not applicable — shadcn not initialized (embedded-firmware stack; no components.json/package.json exists) | not required |
| none (first-party) | All UI is first-party embedded HTML/CSS/JS in PROGMEM | not required — no third-party blocks declared |
| Vendored web asset (not a registry) | **Leaflet 1.9.4 dist** (leaflet.js + leaflet.css) — embedded gzipped in PROGMEM, served with `Content-Encoding: gzip` + `Cache-Control` | Provenance lock: download ONLY from leafletjs.com or unpkg pinned to `leaflet@1.9.4` (never a mirror); commit the generated header + a provenance note; include the BSD 2-Clause notice and "© OpenStreetMap contributors" attribution in the served page; no CDN reference at runtime (AP-mode hard requirement). Registry vetting gate not applicable — not a shadcn/registry block |

---

## Checker Sign-Off

- [x] Dimension 1 Copywriting: FLAG → approved (non-blocking: "Acknowledge"/"Recenter"/"Previous/Next" are single-word labels but contextually unambiguous in their inline positions)
- [x] Dimension 2 Visuals: PASS
- [x] Dimension 3 Color: PASS
- [x] Dimension 4 Typography: PASS
- [x] Dimension 5 Spacing: PASS
- [x] Dimension 6 Registry Safety: PASS

**Approval:** approved 2026-08-19 (gsd-ui-checker — 1 non-blocking FLAG on Copywriting; CONTEXT compliance D-33..D-48 verified)

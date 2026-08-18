---
phase: 2
slug: image-transmission
status: draft
shadcn_initialized: false
preset: none
created: 2026-08-19
amended: 2026-08-19
---

# Phase 2 — UI Design Contract

> **Baseline, not greenfield — retroactive.** Phase 2 was executed (status: gaps_found); the web UI it shipped already exists as embedded HTML/CSS/JS in `src/main_basestation.cpp` (`HTML_HEADER` lines 137–351, `HTML_FOOTER` script lines 353–547, card markup in `handleRoot` lines 760–984). Every token below is extracted from that shipped interface and declared as the contract. This spec governs the **gap-closure replan** (`/gsd-plan-phase 02 --gaps`, driven by `02-VERIFICATION.md`): the transfer state-machine fixes are mostly non-UI, but they must preserve — and in the CR-02 case restore — the UI contracts locked here. New or touched UI must reuse the tokens below exactly; no new visual language.
>
> **Provenance:** all colors/typography/spacing/copy from direct reads of `src/main_basestation.cpp` this session; transfer-state vocabulary from `transferStateToString` (`src/image_rx_manager.cpp:28-37`, locked D-20); storage states from `handleStatus` (`main_basestation.cpp:1479-1484`); card set and copy from `handleRoot` + poll script; Phase 1 inherited tokens from `01-UI-SPEC.md` (approved 2026-08-18). Decisions D-20/D-22/D-24/D-26 from `02-CONTEXT.md`; gap definitions from `02-VERIFICATION.md`.
>
> **Amendment (2026-08-19, checker-driven):** token-table changes only, shipped code unchanged. (1) Typography locked to exactly four sizes — 14/16/18/24; the 12px "Micro" tier was struck, and the shipped 11/12/13px transfer-panel text now lives solely in the off-scale deviation table with 14px harmonization targets. (2) Spacing `lg` changed 20→24px to match the standard set; 20px retired to the off-grid table with a 24px target.

---

## Design System

| Property | Value |
|----------|-------|
| Tool | none — raw HTML/CSS/JS in PROGMEM string literals served by ESP32 `WebServer`; no build system, no npm. shadcn gate not applicable (stack is C++ firmware + embedded HTML, not React/Next.js/Vite) |
| Preset | not applicable |
| Component library | none — hand-rolled CSS classes (inventory below) |
| Icon library | none — emoji in card headings (existing: 🎈 📡 📸 ⚙️ ⏱; Phase 2 adds 📦 🛰 🖼). Keep emoji; no icon fonts on flash-constrained firmware |
| Font | `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif` (system stack — no webfont download over AP-mode LAN) |

**Component inventory (all shipped — reuse verbatim; gap closure adds NO new components):**

Phase 1 base (unchanged): `.container` (max-width 800px, centered, 20px padding) · `.header` (gradient `#1e293b→#334155`, radius 10px) · `.status-bar` (auto-fit grid `minmax(110px,1fr)`, 6 items: Status/Sent/Acked/Failed/Pending/Storage) · `.card` (`#1e293b`, radius 10px, 20px padding) · `button` / `button.danger` (blue/red gradients, radius 8px, 16px bold) · `.form-group` + `input[type=number]` / `select` (dark styled) · `.message.success|.error|.info` (4px left-border callouts) · `.led.green|.red|.yellow` (12px glowing dot).

Phase 2 additions (shipped, now locked):

1. **📦 Image Transfers card** — `<div id="transfer-list">` filled by the poll script; one `.transfer-row` (flex, wrap, `#334155`, radius 8px) per occupied transfer slot containing: `.transfer-id` ("Image #{id}", an `<a>` to the bytes only when COMPLETE), `.transfer-kind` (+ `.full` variant) badge THUMB/FULL, `.progress-track`/`.progress-fill` (+ `.done`/`.failed` variants) 8px bar, `.transfer-chunks` "{n}/{total} chunks · {p}%", `.transfer-state` pill (QUEUED/RECEIVING/RETRYING/COMPLETE/INCOMPLETE).
2. **🛰 Event Capture card** — 3 number inputs + Enabled/Disabled select + **Save Event Thresholds** button (`#event-form`); display-only `#event-chip` showing balloon-reported GET_STATUS truth. Inputs disable while a Set Event Thresholds command is in flight.
3. **🖼 Latest Capture card** — `#thumb-label` message + `#thumb-img` (width 100%, max-width 320px, radius 8px, hidden until a NEW CRC-verified id arrives — dataset-id gate prevents stale image under new id) + `#telemetry-chip` beacon readout.
4. **Storage status item** in the status bar — `#storage-state` value, green `#22c55e` when OK, amber `#eab308` when degraded.

**Card order (top→bottom, shipped):** Status bar → 📡 Command Queue → 📦 Image Transfers → 📸 Manual Capture → ⚙️ Camera Settings → ⏱ Auto-Capture → 🛰 Event Capture → 🖼 Latest Capture. Monitoring surfaces sit above action surfaces.

**Primary focal point:** the **Trigger Camera Capture** button in the Manual Capture card remains the control panel's single action anchor (Phase 1 contract, unchanged). The **Latest Capture thumbnail** is this phase's primary informational anchor; every other Phase 2 surface (transfer rows, chips) is tertiary status.

**Interaction model (shipped, locked):** one 1-second `fetch('/status')` poll (`setInterval(updateStatus, 1000)`) drives every dynamic surface; the client only presents server-computed truth (no client-side state derivation). Form POSTs return a raw JSON status page (`sendResponse`); the durable feedback surface for every command is the pinned Last Command row + queue rows, not the POST response. The Event Capture form additionally disables its inputs while its command is `Sent`.

---

## Spacing Scale

**Locked scale — standard set only: {4, 8, 16, 24, 32, 48, 64}.** Every locked token below is a member of that set. Off-grid shipped values are documented facts flagged for harmonize-on-touch (gap closure must not add NEW off-grid values).

| Token | Value | Usage |
|-------|-------|-------|
| xs | 4px | Label→input gap, status-label→value gap, h1→subtitle gap, `.transfer-state` horizontal padding is NOT this (see exceptions) |
| sm | 8px | Status-bar grid gap, LED→text gap, input/select inner padding, button-group gap, `.transfer-row` margin-top, border-radius 8px (buttons/messages/rows/status-bar/img) |
| md | 16px | `.form-group` spacing, button padding, status-bar padding |
| lg | 24px | Card padding, card stack rhythm (`margin-bottom`), header padding, container padding, `<hr>` vertical margins — **locked target; the shipped code uses 20px for all of these (off-grid, see table below)**. New surfaces use 24px; touched surfaces migrate to 24px |
| xl / 2xl / 3xl | 32/48/64px | Not used (single-viewport control panel; reserved) |

**Exceptions (component sizes, not spacing):** 12px LED diameter; 6px range-track and progress-track heights; 12px progress/range corner radii (half of height).

**Off-grid as shipped — documented, harmonize-on-touch, NON-BLOCKING for gap closure:**

| Value | Where | Harmonization target |
|-------|-------|----------------------|
| 20px | `.card` padding, card stack `margin-bottom`, `.header` padding, `.container` padding, `<hr>` vertical margins — the shipped value of the `lg` role until the 2026-08-19 amendment struck it from the lock | **24px (`lg`)** |
| 15px | `.card h2` margin-bottom, `.message` padding | 16px (carried Phase 1 flag — never touched) |
| 13px | `.transfer-row` font-size (see Typography) | 14px |
| 12px | `.transfer-row` horizontal padding, inline `margin-top:12px` on `#event-chip`/`#thumb-img`/`#telemetry-chip` | 12px is acceptable as the row/chip rhythm — if touched, prefer 8px or 16px |
| 10px | `.transfer-row` gap, `.transfer-row` vertical padding | 8px |
| 6px | `.transfer-kind` horizontal padding | 8px |
| 2px | `.transfer-kind`/`.transfer-state` vertical padding | 2px stays (badge optical alignment; component sizing) |

Touch-target rule (Phase 1, unchanged): buttons keep ≥44px touch height (16px text + 2×16px padding ≈ 51px — satisfied). Badge chips and bar tracks are read-only displays, not targets.

---

## Typography

**Locked scale — exactly 4 sizes.** No other size may be introduced; any text that does not fit one of these roles must consolidate into Body.

| Role | Size | Weight | Line Height |
|------|------|--------|-------------|
| Body | 14px | 400 | 1.5 (declared on `body` — the Phase 1 "declare explicitly" note landed) |
| Label | 16px | 700 | 1.2 |
| Heading | 18px | 700 | 1.2 |
| Display | 24px | 700 | 1.2 |

- **Body 14px/400/1.5** — form labels, input values, status-bar labels, `.message` text, header subtitle, muted text at `#94a3b8`. **This is also the consolidation target for the transfer panel's off-scale text** (see deviation table below): once harmonized, `.transfer-row` base, `.transfer-chunks`, and both badge kinds are Body 14px (badges keep weight 700 for the pill).
- **Label 16px/700** — button labels only.
- **Heading 18px/700** — card `h2` titles and status-bar numerals (`.status-value`).
- **Display 24px/700** — header `h1`.
- Exactly 2 weights (400/700) — unchanged; no italics anywhere.

**Off-scale as shipped — documented, harmonize-on-touch, NON-BLOCKING for gap closure** (the former 12px "Micro" tier was struck from the lock in the 2026-08-19 amendment; these entries are where that text now lives):

| Shipped value | Where (source line in `main_basestation.cpp`) | Harmonization target |
|---------------|----------------------------------------------|----------------------|
| 13px | `.transfer-row` base font-size (line 300) | **14px — Body tier** |
| 12px | `.transfer-chunks` (line 327) | **14px — Body tier** (consolidate with the row base when the row is next touched) |
| 11px | `.transfer-kind` / `.transfer-state` badges (lines 305, 329) | **14px — Body tier** at weight 700; the pill padding/border already carries the badge affordance, no meaning is lost |

Gap closure and all future work must NOT introduce sizes outside the locked four; touching any row above obligates its harmonization.

---

## Color

| Role | Value | Usage |
|------|-------|-------|
| Dominant (60%) | `#0f172a` | Page background |
| Secondary (30%) | `#1e293b` | Cards, status bar, header gradient base; raised surface `#334155` (inputs, `.message`, `.transfer-row`) with `#475569` borders/tracks; body text `#e2e8f0`, muted text `#94a3b8` |
| Accent (10%) | `#3b82f6 → #2563eb` (buttons); `#60a5fa` (text); `#3b82f6→#60a5fa` (active progress gradient) | See reserved list below |
| Destructive | `#ef4444 → #dc2626` | See destructive reserved list below |

**Accent reserved for exactly these elements:**

1. Text `#60a5fa`: `h1` title, card `h2` titles, status-bar numerals, **COMPLETE transfer-row links** (JS sets `style.color '#60a5fa'`).
2. Blue-gradient action buttons: **Trigger Camera Capture**, the 7 Set-* settings buttons, **Enable Auto-Capture**, **Save Event Thresholds**.
3. `.message.info` left border (`#3b82f6`).
4. `.progress-fill` gradient (`#3b82f6→#60a5fa`) — the active-transfer bar only.

Accent is NOT for: form labels, body copy, input borders, dividers, backgrounds, or non-COMPLETE transfer rows.

**Destructive red reserved for exactly these elements:**

1. **Disable Auto-Capture** button (stop-action styling).
2. `.message.error` left border on `#7f1d1d` background — command failure/timeout copy only.
3. Red link LED (link down).
4. `.progress-fill.failed` (`#ef4444`) on INCOMPLETE rows.
5. `.transfer-state.INCOMPLETE` (`#f87171` on `#7f1d1d`) — red-family terminal failure.

**Semantic status set (neither accent nor destructive; LOCKED chip color↔state mapping for the transfer panel):**

| State / surface | Background | Text/ink | Meaning |
|---|---|---|---|
| `.transfer-state.QUEUED` / `.RECEIVING` | `#1e3a5f` | `#93c5fd` | In flight (info blue) |
| `.transfer-kind` THUMB | `#1e3a5f` | `#93c5fd` | Thumbnail kind |
| `.transfer-kind.full` FULL | `#312e81` | `#a5b4fc` | Full-image kind (indigo distinguishes from state blue) |
| `.transfer-state.RETRYING` | `#713f12` | `#fbbf24` | Recovering — window re-requests in progress (amber) |
| `.transfer-state.COMPLETE` | `#065f46` | `#34d399` | CRC32-verified terminal success |
| `.progress-fill.done` | `#22c55e` | — | Verified transfer bar |
| Storage chip OK | — | `#22c55e` | SD healthy |
| Storage chip UNAVAILABLE / FULL | — | `#eab308` | SD degraded (amber — distinct from transfer-failure red; storage failure does not fail transfers, it degrades them) |
| LED green / yellow | `#22c55e` / `#eab308` | — | Link up / unknown (Phase 1, unchanged) |

**Truth requirements (extend the Phase 1 IN-03 no-fabricated-state prohibition — the D-20 core):**

- Transfer percent/chunks/state derive ONLY from the server's chunk-bitmap / pass-counter / terminal-flag accounting (`getTransferSnapshot`); the client clamps width to 0–100 and presents, never computes.
- COMPLETE appears only after end-to-end CRC32 verification; a row is never linked to `/img/...` bytes unless COMPLETE.
- Storage chip, telemetry chip, event chip, and `latestThumbId` are honest-null until real data arrives ("Unknown", "No telemetry received yet", "Balloon values not received yet", 0/hidden image).
- The CR-02 zombie-slot defect violated this (stale counters shown against a new image's total). Restoring this contract is a gap-closure acceptance condition — see UI Considerations backstop rows.

---

## Copywriting Contract

| Element | Copy |
|---------|------|
| Primary CTA | **Trigger Camera Capture** (unchanged) |
| Phase 2 CTA | **Save Event Thresholds** (verb + noun, matches Set-* pattern) |
| Transfers empty state | No image transfers yet — trigger a capture to start one. (`.message.info`, poll-rendered) |
| Latest Capture empty | Waiting for first image... (`.message.info`; image hidden) |
| Latest Capture populated | Image #{id} — thumbnail received and verified (`.message.success`; image shown at ≤320px) |
| Event chip null | Balloon values not received yet (`.message.info`) |
| Event chip populated | Balloon: {ON\|OFF} · alt Δ {altM} m · dist Δ {distM} m · spacing {spacingS} s (`.message.success` when ON, `.message.info` when OFF) |
| Telemetry chip null | No telemetry received yet (`.message.info`) |
| Telemetry chip populated | Alt {x.x} m · {y.y} °C · {lat, lon} · {just now \| Ns ago} — "GPS no fix" replaces coordinates when invalid; `.message.success` while age < 15 s, else `.message.info` |
| Storage chip | Unknown (pre-poll) → OK / UNAVAILABLE / FULL (LOCKED three-state vocabulary, server-computed) |
| Transfer row | Image #{id} · {THUMB\|FULL} · {received}/{total} chunks · {percent}% · {state} |
| Transfer state vocabulary | **QUEUED / RECEIVING / RETRYING / COMPLETE / INCOMPLETE** — LOCKED by `transferStateToString` (D-20). The UI never invents, defaults, or widens a state. **Gap-closure rule: recovery/healing is expressed as RETRYING — do NOT add new display states (no "RECOVERING", "STALLED", "LOST") without amending this contract.** |
| Command status vocabulary | "Sent" → "ACK Received" / "Failed (retry N)" → "Timeout" (Phase 1 LOCKED mapping, shared by pinned row and queue rows) |
| Link status text | LED green + "Ready" / red + "No link" / yellow + "Unknown" (Phase 1, unchanged) |
| Error state (event thresholds, 400) | Missing event threshold parameters · Invalid altitude delta (10-5000 m) · Invalid distance delta (10-50000 m) · Invalid min spacing (5-3600 s) · Invalid enabled value (0 or 1) |
| Error state (send failure, 500) | Failed to send event threshold command |
| Success (200) | Event threshold command sent |
| Error state (image route, 404) | Unknown image path — absence reported honestly, never fabricated bytes |
| Destructive confirmation | **None this phase.** Disable Auto-Capture remains the only red stop action (Phase 1: no confirmation — immediately reversible). Disabling Event triggers rides the normal select + blue Save button, not destructive styling |

**Form labels (exact copy; ranges mirror firmware validation in `handleSetEventThresholds`, WR-07 full-long checks):**

| Label copy | Control | Constraint |
|------------|---------|------------|
| Altitude delta (10-5000 m): | number, step 10 | min 10, max 5000, default 150 |
| Distance delta (10-50000 m): | number, step 10 | min 10, max 50000, default 500 |
| Min spacing (5-3600 s): | number, step 5 | min 5, max 3600, default 20 |
| Event triggers: | select | Enabled (default) / Disabled |

(Phase 1 form labels — Quality/Brightness/Contrast/Resolution/Saturation/Exposure/White balance/Interval — unchanged; see `01-UI-SPEC.md`.)

---

## UI Considerations

> Populated by the ui-consideration probe against this phase's surfaces: Image Transfers panel (list-collection), Latest Capture thumbnail (media), Event Capture form (form + interactive-control), storage/telemetry/event/auto-capture chips (static-content), status bar (static-content). Empty/error COPY lives in `## Copywriting Contract` — this section covers shape-rooted STATE coverage. Resolution: 9 covered, 2 backstop, 0 unresolved.

| Category | Element(s) | Status | Resolution / Reason |
|----------|------------|--------|---------------------|
| empty | transfer-list; thumb-img; event-chip; telemetry-chip; storage value | ✅ covered | Zero transfers render the documented "No image transfers yet" copy; thumbnail hidden with "Waiting for first image..."; every chip has a documented honest-null string; storage renders "Unknown" pre-poll. Forms always render complete defaults — never unfilled. |
| loading | transfer-list; event-form | ✅ covered | In-flight = QUEUED/RECEIVING state pill + accent progress bar (server-computed percent); Event form inputs disable while Set Event Thresholds is `Sent`. No spinners/skeletons — status-line model per Phase 1 decision. |
| error | transfer-list; storage chip; link LED; POST routes | ✅ covered | RETRYING (amber) = degraded link recovering; INCOMPLETE (red) = terminal failure after bounded passes; storage UNAVAILABLE/FULL amber; poll failure → LED yellow "Unknown"; 400/500 JSON responses carry documented copy. |
| populated | transfer-list; thumb-img | ✅ covered | Up to `RX_TRANSFER_SLOTS` (8) rows, each: id link, kind badge, bar, chunks, state pill; verified thumbnail at max-width 320px (mirrors IMG-02's ≤320px bound). |
| partial | transfer-list | ✅ covered | D-24: an image finalized incomplete stays on SD and renders INCOMPLETE with truthful partial percent/chunks — partial is a displayed, honest terminal state, never rounded up to COMPLETE. |
| overflow | transfer-row; status-bar; chips | ✅ covered | `.transfer-row` is flex-wrap (row reflows, never clips); status-bar auto-fit grid collapses to 2 columns ≤480px; chips wrap at 14px/1.5; thumbnail scales width 100% capped at 320px. |
| zero-one-many | transfer-list | ✅ covered | 0 → documented empty copy; 1..8 rows render identically (numeric-only content, no singular/plural copy). |
| long-text | chips; labels; transfer-chunks | ✅ covered | Chip/label text wraps at 14px/1.5; `.transfer-chunks` is `white-space: nowrap` by design — bounded numeric string ("123/456 chunks · 27%") cannot overflow its wrapping flex row. |
| truthful accounting | transfer rows under slot recycle | 🧪 backstop | { statement: "After any slot recycle (including the CR-02 eviction path), a transfer row shows only the NEW image's accounting — 0/{totalChunks} at QUEUED/RECEIVING — never the previous occupant's receivedCount/percent/state", verification: backstop } |
| thumbnail recovery display | THUMB-kind rows | 🧪 backstop | { statement: "Once the D-22 gap closes (kind-addressable re-request), a THUMB row can pass through RETRYING to COMPLETE (healed) or INCOMPLETE (3-pass bound) using only the locked five-state vocabulary — no UI change required", verification: backstop } |

**Unresolved:** none (0). Hardware-timing surfaces (thumbnail within 10 s, telemetry age during transfer) are UAT items in `02-VERIFICATION.md`, not UI-state gaps — the UI states themselves are fully specified above.

---

## Registry Safety

| Registry | Blocks Used | Safety Gate |
|----------|-------------|-------------|
| shadcn official | not applicable — shadcn not initialized (embedded-firmware stack) | not required |
| none | No package/registry ecosystem exists in this project; all UI is first-party embedded HTML in PROGMEM | not required — no third-party blocks declared |

---

## Checker Sign-Off

- [ ] Dimension 1 Copywriting: PASS
- [ ] Dimension 2 Visuals: PASS (primary focal point declared in Design System)
- [ ] Dimension 3 Color: PASS
- [ ] Dimension 4 Typography: PASS (exactly 4 sizes locked: 14/16/18/24; shipped 11/12/13px transfer-panel text moved fully into the off-scale deviation table with explicit 14px harmonization targets; no sizes outside the four permitted)
- [ ] Dimension 5 Spacing: PASS (locked scale standard-set-only; `lg` = 24px, shipped 20px retired to the off-grid table with 24px target; off-grid shipped values documented with harmonization targets; no new off-grid values permitted)
- [ ] Dimension 6 Registry Safety: PASS

**Approval:** pending

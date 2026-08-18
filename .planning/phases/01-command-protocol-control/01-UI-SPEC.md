---
phase: 1
slug: command-protocol-control
status: draft
shadcn_initialized: false
preset: none
created: 2026-08-18
---

# Phase 1 — UI Design Contract

> **Baseline, not greenfield.** This phase was executed once; the web UI already exists as embedded HTML/CSS in `src/main_basestation.cpp` (`HTML_HEADER`, lines 102–264). Every token below is extracted from that existing interface and declared as the contract. This spec exists to govern the **gap-closure replan** (per `01-VERIFICATION.md`): per-command outcome surfacing, auto-capture controls, and the 4 missing settings forms. New controls must reuse the tokens below exactly — no new visual language.
>
> **Provenance:** colors/typography/spacing from `src/main_basestation.cpp`; status-string vocabulary and "raw ESP32 WebServer" from `01-CONTEXT.md` resolved decisions; form ranges from firmware validation (`src/command_handler.cpp`, `include/command_protocol.h`); command states from `include/command_sender.h` (`CommandState`: PENDING/SENT/ACKED/FAILED/TIMEOUT, queue depth 5); UI gap list from `01-VERIFICATION.md`.

---

## Design System

| Property | Value |
|----------|-------|
| Tool | none — raw HTML/CSS in PROGMEM string literals served by ESP32 `WebServer`; no build system, no npm. shadcn gate not applicable (stack is C++ firmware + embedded HTML, not React/Next.js/Vite) |
| Preset | not applicable |
| Component library | none — hand-rolled CSS classes (inventory below) |
| Icon library | none — emoji in card headings (existing: 🎈 📸 ⚙️). Keep emoji; no icon fonts on flash-constrained firmware. New cards: ⏱ Auto-Capture, 📡 Last Command |
| Font | `-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif` (system stack — no webfont download over AP-mode LAN) |

**Baseline component patterns (existing — reuse verbatim, extend only with what is listed):**

- `.container` — max-width 800px, centered, 20px padding
- `.header` — gradient `#1e293b → #334155`, radius 10px, h1 + subtitle
- `.status-bar` — 4-column grid of `.status-item` (label over value) + link LED
- `.card` — `#1e293b`, radius 10px, 20px padding, shadow, 20px bottom margin
- `button` / `button.danger` — blue/red gradients, radius 8px, 16px bold text
- `.form-group` — block label + input; `input[type=number]` dark styled
- `.message.success | .error | .info` — 4px left-border callouts
- `.led.green | .red | .yellow` — 12px glowing dot

**New components this phase (compose only from the patterns above):**

1. **Last Command card** (`📡 Last Command`) — read-only panel: command name, seq, state line. Closes the SC-4 gap "no per-command outcome surfaced" (poll `getCommandState(seq)` after POST; map states to the locked status strings).
2. **Auto-Capture card** (`⏱ Auto-Capture`) — interval number input + `Enable Auto-Capture` (accent) / `Disable Auto-Capture` (danger) buttons + ON/OFF state chip. Closes the CTRL-03/04 UI gap.
3. **Four settings forms** appended to the existing Camera Settings card: Resolution (`<select>`), Saturation (number), Exposure (number), White Balance (`<select>`). Closes the CTRL-02 gap (3 of 7 → 7 of 7).
4. `<select>` styling = same rules as `input[type=number]`: `#334155` bg, 1px `#475569` border, radius 5px, 14px text, 8px padding.

---

## Spacing Scale

Declared values (must be multiples of 4). Existing off-grid values (5/10/15px) are harmonized on touch during gap closure; 20px is already on-grid and unchanged.

| Token | Value | Usage |
|-------|-------|-------|
| xs | 4px | Label→input gap (was 5px), status-label→value gap (was 5px), h1→subtitle gap (was 5px) |
| sm | 8px | Status-bar grid gap (was 10px), LED→text gap, message top margin (was 10px), input/select inner padding (was 10px) |
| md | 16px | `.form-group` spacing (was 15px), button padding (was 15px), status-bar padding (was 15px) |
| lg | 20px | Card padding, card stack rhythm (`margin-bottom`), header padding, hr vertical margins — existing, unchanged |
| xl | 24px | Not used this phase (single-viewport control panel; reserved) |
| 2xl | 48px | Not used this phase |
| 3xl | 64px | Not used this phase |

Exceptions: buttons must keep ≥44px touch height (16px text + 2×16px padding ≈ 51px — satisfied). The 12px LED diameter and 6px range-track height are component sizes, not spacing. Emoji↔heading gap = 8px (sm).

---

## Typography

| Role | Size | Weight | Line Height |
|------|------|--------|-------------|
| Body | 14px | 400 | 1.5 (declare explicitly — existing CSS omits line-height and relies on defaults) |
| Label | 16px | 700 | 1.2 |
| Heading | 18px | 700 | 1.2 |
| Display | 24px | 700 | 1.2 |

- **Body 14px/400/1.5** — form labels, input values, status-bar labels (harmonized from 12px), `.message` text.
- **Label 16px/700/1.2** — button labels only (existing button size, unchanged).
- **Heading 18px/700/1.2** — card `h2` titles and status-bar numerals (existing `.status-value`, existing `h2`).
- **Display 24px/700/1.2** — header `h1` title.
- Exactly 4 sizes (14/16/18/24), exactly 2 weights (400/700). No other sizes/weights may be introduced.

---

## Color

| Role | Value | Usage |
|------|-------|-------|
| Dominant (60%) | `#0f172a` | Page background |
| Secondary (30%) | `#1e293b` | Cards, status bar, header gradient base; input surface `#334155` with `#475569` border and range track |
| Accent (10%) | `#3b82f6 → #2563eb` (buttons); `#60a5fa` (text) | See reserved list below |
| Destructive | `#ef4444 → #dc2626` | See destructive reserved list below |

**Accent reserved for exactly these elements:**

1. Text `#60a5fa`: `h1` title, card `h2` titles, status-bar numerals.
2. Blue-gradient action buttons: **Trigger Camera Capture**, **Set Quality / Set Brightness / Set Contrast / Set Resolution / Set Saturation / Set Exposure / Set White Balance**, **Enable Auto-Capture**.
3. `.message.info` left border (`#3b82f6`).

Accent is NOT for: form labels, body copy, input borders, dividers, or backgrounds.

**Destructive red (`#ef4444`/`#dc2626`) reserved for exactly these elements:**

1. **Disable Auto-Capture** button (stop-action styling).
2. `.message.error` left border on `#7f1d1d` background — command failure/timeout copy only.
3. Red link LED (link down).

**Supporting semantics (not accent, not destructive):** success `#10b981` border on `#065f46` bg (ACK confirmations); LED green `#22c55e` = link up; LED yellow `#eab308` = link unknown (e.g. `/status` poll failing).

**LED truth requirement (IN-03 fix):** the link LED must be driven by real link health (e.g. age of last ACK/LoRa activity), never a hardcoded `"connected":true`. Green + "Ready", red + "No link", yellow + "Unknown".

---

## Copywriting Contract

| Element | Copy |
|---------|------|
| Primary CTA | **Trigger Camera Capture** (existing, unchanged) |
| Secondary CTAs | Set Quality · Set Brightness · Set Contrast · Set Resolution · Set Saturation · Set Exposure · Set White Balance · Enable Auto-Capture (verb + noun, matches existing pattern) |
| Stop action | Disable Auto-Capture (red `button.danger`) |
| Status vocabulary | **"Sent" → "ACK Received" / "Failed (retry N)" → "Timeout"** — LOCKED by 01-CONTEXT.md decision ("Simple status" strings); Last Command panel maps `CommandState` PENDING/SENT→"Sent", ACKED→"ACK Received", FAILED→"Failed (retry {N})", TIMEOUT→"Timeout" |
| Empty state heading | No commands yet |
| Empty state body | Trigger a capture or change a setting — the result of your last command appears here. |
| Error state (timeout) | Timeout — no response from the balloon after 3 attempts. Check that the balloon is powered on and in range, then resend the command. |
| Error state (queue full) | Command queue full (5 pending). Wait for pending commands to complete, then retry. |
| Error state (send failure) | Command could not be sent. Check the LoRa module connection and retry. |
| Auto-capture enabled feedback | Auto-capture enabled — capturing every {N}s (`.message.success`) + chip: ON · every {N}s |
| Auto-capture disabled feedback | Auto-capture disabled (`.message.info`) + chip: OFF |
| Link status text | LED green + "Ready" / red + "No link" / yellow + "Unknown" — driven by real link state (see LED truth requirement) |
| Destructive confirmation | Disable Auto-Capture: **none** — immediately reversible by re-enabling; red styling alone signals the stop action |

**Form labels (exact copy; ranges = firmware-validated ranges, mirroring `command_handler.cpp`):**

| Label copy | Control | Constraint |
|------------|---------|------------|
| Quality (0-63, lower is better): | number (existing) | min 0, max 63, default 10 |
| Brightness (-2 to 2): | number (existing) | min -2, max 2, default 0 |
| Contrast (-2 to 2): | number (existing) | min -2, max 2, default 0 |
| Resolution: | select (new) | QQVGA 160x120 / QVGA 320x240 / HQVGA 240x176 / QXGA 400x296 / VGA 640x480 / SVGA 800x600 / XGA 1024x768 / SXGA 1280x1024 / UXGA 1600x1200 — default QVGA 320x240 (matches `BALLOON_CAMERA_FRAMESIZE`) |
| Saturation (-2 to 2): | number (new) | min -2, max 2, default 0 |
| Exposure (-2 to 2): | number (new) | min -2, max 2, default 0 |
| White balance: | select (new) | Auto / Sunny / Cloudy / Office / Home (enum 0–4) — default Auto |
| Interval (1-3600 seconds): | number (new, Auto-Capture card) | min 1, max 3600, default 10 (firmware range 1000–3,600,000 ms; UI sends value × 1000) |

Client-side `min`/`max` mirror firmware validation, but firmware must re-validate on the string before integer truncation (WR-07 — `quality=300` must not truncate to 44 and pass).

---

## UI Considerations

> Populated per the ui-consideration probe taxonomy. Empty-state and error-state COPY lives in `## Copywriting Contract` above — this section covers state coverage and references those rows.

Applicable state considerations resolved: 7 covered, 1 backstop, 0 unresolved

| Category | Element(s) | Status | Resolution / Reason |
|----------|------------|--------|---------------------|
| empty | last-command panel, settings forms, auto-capture form | ✅ covered | Before any command the Last Command panel renders the documented "No commands yet" copy (Copywriting Contract); every form renders a complete documented default value, so no form is ever shown unfilled |
| loading | last-command panel, action buttons | ✅ covered | In-flight command shows the locked "Sent" state string immediately after POST; the 1s `/status` poll (existing footer script, extended) advances the panel to a terminal state — status-line model per CONTEXT decision, no spinners/skeletons |
| error | last-command panel, message callouts | 🧪 backstop | Terminal "Timeout" / "Failed (retry N)" with documented error copy must render after retries are exhausted — held-out verification is the hardware UAT degraded-link test (power balloon off → 3 retries → TIMEOUT surfaced); the `CommandState`→string mapping itself is code-verifiable without hardware |
| populated | status-bar counters | ✅ covered | Sent/Acked/Failed/Pending counters advance as commands process; 4-column grid on desktop, 2×2 below 480px (see overflow) |
| partial | settings forms, auto-capture form | ✅ covered | Each setting is an independent single-field form — a multi-field partial state cannot occur; forms always display a complete value (default or last entered) |
| overflow | status-bar, last-command panel | ✅ covered | Status bar collapses 4→2 columns under a 480px media query (new rule, same tokens); counter cells never truncate; Last Command text wraps within card width — no clipping |
| zero-one-many | status-bar counters | ✅ covered | Numeric readouts only — no singular/plural copy; layout identical at 0, 1, and many |
| long-text | error/status messages, form labels | ✅ covered | `.message` and label text wrap at 14px/1.5 inside the card; no ellipsis or truncation anywhere in the Phase 1 UI |

---

## Registry Safety

| Registry | Blocks Used | Safety Gate |
|----------|-------------|-------------|
| shadcn official | not applicable — shadcn not initialized (embedded-firmware stack) | not required |
| none | No package/registry ecosystem exists in this project; no third-party UI code enters the firmware — all UI is first-party embedded HTML | not required — no third-party blocks declared |

---

## Checker Sign-Off

- [ ] Dimension 1 Copywriting: PASS
- [ ] Dimension 2 Visuals: PASS
- [ ] Dimension 3 Color: PASS
- [ ] Dimension 4 Typography: PASS
- [ ] Dimension 5 Spacing: PASS
- [ ] Dimension 6 Registry Safety: PASS

**Approval:** pending

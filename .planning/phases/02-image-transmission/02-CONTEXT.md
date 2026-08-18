# Phase 2: Image Transmission - Context

**Gathered:** 2026-08-19
**Status:** Ready for planning

<domain>
## Phase Boundary

This phase delivers **end-to-end image transfer from balloon to base station over LoRa**: after any capture (manual, interval, or event-triggered), a thumbnail appears on the base station quickly, the full image follows in the background without ever blocking telemetry, and both land on the base station's SD card with metadata. It also adds event-based capture triggers (CTRL-05) on the balloon. It does NOT include the image gallery UI, maps, telemetry dashboard, or alert system (Phase 3), and it does not change the command/retry protocol's existing semantics (Phase 1).

**Hard prerequisite carried from Phase 1:** CR-04 — `createThumbnail()` failure paths leave `currentThumbnail.buffer` dangling (double-free risk). IMG-02 wires its first caller; the fix MUST land in this phase before/with that wiring. See `01-VERIFICATION.md`.

</domain>

<decisions>
## Implementation Decisions

### Transfer Model
- **D-17:** Hybrid push/pull — balloon pushes the thumbnail immediately after capture (no round-trip, serves IMG-02's 10-second display window), then announces the full image with a manifest packet (image ID, total size, chunk count, end-to-end CRC32); the BASE station pulls the full image chunk-by-chunk via the Phase 1 command/response protocol. — **Reversibility:** costly — undo means rewriting both sides of the wire protocol plus the base UI's data flow after images have flowed.
- **D-18:** Automatic pacing only — no operator transfer controls (no pause/resume/skip commands, no UI for them). PRI-01 is enforced solely by the balloon's existing radio priority queue (Emergency > GPS > Telemetry > Camera > Status); the pull proceeds whenever no higher-priority traffic is waiting.
- **D-19:** FIFO transfer queue — full-image pulls complete in capture order; a new capture never abandons an in-progress pull (its thumbnail still pushes immediately). Gallery stays ordered, no airtime wasted.
- **D-20:** Transfer progress UI shows percent + chunks received/total per image (e.g., 143/380), derived from actual chunk accounting. Extends the Phase 1 D-16 Command Queue panel pattern; the no-fabricated-state prohibition applies — never show complete unless verified complete.

### Chunk Reliability
- **D-21:** Windowed request/response ARQ for the full-image pull — base requests a window of chunks (window size at planner's discretion, ~16 suggested), re-requests corrupt/missing chunks before advancing the window. Bounds balloon-side memory; progress is exact by construction. — **Reversibility:** costly — wire-level contract shared by both firmwares.
- **D-22:** One reliability mechanism system-wide — the pushed thumbnail, if it arrives with holes, falls back to the same windowed-pull re-request path (a thumbnail is just another pullable image). No parallel best-effort path.
- **D-23:** End-to-end integrity — manifest carries a CRC32 over the entire original image; base verifies after reassembly. Per-chunk CRC16 already rides the transport but cannot catch misordered/duplicated chunks.
- **D-24:** Bounded retransmit rounds — after a bounded number of passes over missing chunks (3 suggested), the image is finalized incomplete: kept on SD, flagged in its metadata, transfer slot freed for the next queued image (PRI-03 graceful degradation).

### Event-Based Capture Triggers
- **D-25:** Trigger events = altitude delta + horizontal distance delta + flight-phase transitions. The existing `SystemState` flight-phase state machine (GROUND→LAUNCH→ASCENT→APEX→DESCENT→LANDING) already computes the transitions — each costs one comparison.
- **D-26:** Thresholds are UI-configurable from the base station web UI via new command types, following the Phase 1 camera-settings pattern (settings forms + ACK + GET_STATUS reporting).
- **D-27:** Event captures reset the interval baseline — both modes coexist (PROJECT.md "hybrid auto-capture"), but an event firing near the interval deadline never produces a double-capture burst. All captures share the single `uint16` image-ID sequence owned by AutoCapture.
- **D-28:** Global minimum spacing between any two automatic captures (15–30 s suggested) regardless of trigger source — GPS jitter or fast phase changes cannot machine-gun the camera and flood the transfer queue.

### SD Storage (Base Station)
- **D-29:** Files named by the balloon-assigned image ID: `IMG_{id}.JPG` (zero-padded). The same ID rides the manifest, the transfer queue, and GET_STATUS `lastImageId` — one identifier end-to-end, immune to balloon clock issues.
- **D-30:** Sidecar JSON per image (`IMG_{id}.JSON`): capture time, altitude, GPS position, trigger source, camera settings at capture, RSSI at receipt, chunk statistics, completeness flag. Self-describing; Phase 3 gallery reads it directly.
- **D-31:** Separate thumbnail files (`IMG_{id}_T.JPG`) written the moment the thumbnail completes; the full image lands as `IMG_{id}.JPG` when its pull finishes. UI can show the thumbnail instantly and swap in the full image later — mirrors the two-stage transfer.
- **D-32:** Flat directory layout (single images directory; `_T` suffix convention for thumbnails). Simplest serving for Phase 3; no clock-dependent folder logic.

### Claude's Discretion
- Window size and chunk payload size (must fit the established 240-byte transport budget) — researcher validates via LoRa airtime math.
- Thumbnail dimensions/JPEG quality target — constrained by IMG-02 ("320x240 or smaller") and the 10-second display window; verify airtime feasibility, propose defaults.
- Default threshold values for altitude/distance deltas and the minimum-spacing constant — propose sensible flight defaults.
- Which flight-phase transitions fire captures (all six vs. launch/apex/landing only).
- Manifest packet field encoding details (binary layout within the 240-byte budget).
- SD-card-full behavior (oldest-image rollover vs. stop-storing-and-warn) — pick the safe default; flag the choice in the plan.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Planning
- `.planning/PROJECT.md` — Core value, constraints (240-byte packets, telemetry priority, single-threaded loop, PSRAM), validated existing capabilities
- `.planning/REQUIREMENTS.md` — Full requirement list; Phase 2 owns IMG-01..05, CTRL-05, PRI-01, PRI-03
- `.planning/ROADMAP.md` — Phase 2 scope, success criteria, deliverables

### Phase 1 (transport and protocol this phase builds on)
- `.planning/phases/01-command-protocol-control/01-CONTEXT.md` — Decisions D-01..D-16 (packet structure, retry design, settings encoding, UI layout)
- `.planning/phases/01-command-protocol-control/01-VERIFICATION.md` — CR-04 must-fix-before-first-caller (thumbnail double-free) + open advisories (WR-12 receive-side packet-type validation is newly relevant: this phase adds heavy base→balloon traffic)
- `include/command_protocol.h` + `src/command_protocol.cpp` — Packet types (0x10/0x11), CRC16 serializer, 240-byte limit, length-driven framing
- `src/e32_lora.cpp` — The actual transport (E32-900T30D UART). NOTE: `.planning/codebase/ARCHITECTURE.md` predates Phase 1 and still describes SPI/Sandeep-Mistry LoRa — where they disagree, the source and STATE.md are authoritative
- `src/command_sender.cpp` / `src/command_handler.cpp` — Request/response machinery both sides (ACK timeouts, terminal states, duplicate-ACK guard)
- `src/auto_capture.cpp` — Interval authority + single image-ID sequence (allocateImageId) this phase must reuse, not duplicate
- `src/camera_manager.cpp` — createThumbnail() (QVGA) and the CR-04 defect site
- `src/main_basestation.cpp` — Web UI patterns: settings forms, Command Queue panel, link LED

### Codebase Maps
- `.planning/codebase/CONVENTIONS.md` — Naming patterns (camelCase functions, PascalCase structs), singleton globals
- `.planning/codebase/STACK.md` — PlatformIO dual-target build, esp32-camera, ArduinoJson

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Command protocol transport** — 240-byte packets, CRC16, length-driven framing, established packet-type byte: image/manifest/chunk packet types extend the same `PacketType` enum
- **Request/response machinery** — `CommandSender` (base) and `CommandHandler` (balloon) already do typed requests with ACK/terminal states; the chunk pull rides this pattern
- **`LoRaComm` priority queue** — PRI-01 enforcement already exists (Emergency > GPS > Telemetry > Camera > Status); image chunks slot in at CAMERA priority
- **`AutoCapture` module** — owns the single image-ID sequence and interval timing; event triggers extend this module (D-25..D-28) rather than adding a parallel timer
- **`SystemState` flight-phase machine** — transition events are already computed; no new sensing needed
- **`CameraManager::createThumbnail()`** — QVGA downscale exists; CR-04 must be fixed before its first Phase 2 caller
- **Base station web UI** — settings-form + queue-panel patterns directly extend to event-threshold config and transfer-progress rows

### Established Patterns
- Singleton access via global functions (Sensors(), Camera(), LoRaComm(), SysState())
- Header + Type + Sequence + Payload + CRC packet structure; `packet.type` assigned by the factory (Phase 1 CR-01 lesson)
- Locked terminal-state vocabulary + terminal-state guards against duplicate responses (Phase 1 CR-03 lesson — apply the same guard discipline to chunk ACKs)
- No-fabricated-state prohibition: UI shows only verified truth (extends to transfer progress and completeness flags)
- Single-threaded event loop, no blocking; time-sliced operations

### Integration Points
- New `PacketType`s: image manifest, image chunk, chunk-window request/response — on both `command_handler` (balloon) and `command_sender`/receiver path (base)
- Balloon: capture pipeline (CAPTURE_NOW / AutoCapture / new event triggers) → manifest + thumbnail push → chunk request servicing
- Base station: receiver → reassembly buffer → SD writer (new module; SD hardware is a stated project constraint) → progress state for UI
- Web UI: transfer progress rows + event-threshold settings card in `main_basestation.cpp`

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches consistent with existing codebase patterns. (All discussion selections were from presented options, no freeform additions.)

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 2-Image Transmission*
*Context gathered: 2026-08-19*

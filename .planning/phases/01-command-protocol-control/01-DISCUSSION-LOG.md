# Phase 1: Command Protocol & Control - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2025-08-18
**Phase:** 1-Command Protocol & Control
**Areas discussed:** Command packet structure, Retry mechanism design, Camera settings format, Base station UI layout

---

## Command packet structure

| Option | Description | Selected |
|--------|-------------|----------|
| Extend PacketType enum | Reuse existing telemetry packet types and add command types (clean extension) | ✓ |
| New command header | Separate command packet header structure from telemetry (independent protocol) | |
| Flexible key-value | Use JSON-like key-value pairs for flexibility (parsing overhead) | |

**User's choice:** Extend PacketType enum

| Option | Description | Selected |
|--------|-------------|----------|
| Numeric action codes | Each camera action gets unique numeric ID (TRIGGER=1, SET_RESOLUTION=2, etc.) | ✓ |
| Hierarchical IDs | Group commands by type with sub-actions (CAMERA.TRIGGER, CAMERA.SET_RESOLUTION) | |
| String commands | Human-readable command names in packets (CAMERA_TRIGGER, larger packets) | |

**User's choice:** Numeric action codes

| Option | Description | Selected |
|--------|-------------|----------|
| Binary struct | Binary struct matching existing sensor data format (efficient parsing) | ✓ |
| TLV encoded | Tag-Length-Value for variable-length settings like exposure modes | |
| JSON format | ArduinoJson for complex nested settings (memory overhead) | |

**User's choice:** Binary struct

| Option | Description | Selected |
|--------|-------------|----------|
| Simple ack | ACK/NACK only - balloon confirms command received (simple) | |
| Echo value | ACK + current setting value - confirms and echoes back (more feedback) | ✓ |
| Execution result | ACK + execution result - confirms command was applied (full confirmation) | |

**User's choice:** Echo value

**Notes:** User chose to extend existing packet infrastructure rather than create independent protocol, prioritizing consistency with existing codebase patterns.

---

## Retry mechanism design

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed 5 seconds | Fixed 5-second wait for all commands (simple, may be too slow/quick) | |
| Command-specific | Vary by command type (trigger: 2s, settings: 5s, complex: 10s) | ✓ |
| Adaptive to SF | Calculate based on current LoRa spreading factor and payload size | |

**User's choice:** Command-specific

| Option | Description | Selected |
|--------|-------------|----------|
| 3 retries | 3 attempts then give up (balanced persistence) | ✓ |
| 5 retries | 5 attempts for more reliability (longer wait on failure) | |
| Until cancel | Keep retrying until user cancels (max persistence) | |

**User's choice:** 3 retries

| Option | Description | Selected |
|--------|-------------|----------|
| Immediate retry | Retry immediately on timeout (aggressive but may flood LoRa) | |
| Exponential backoff | Double delay each retry (2s, 4s, 8s - standard exponential) | ✓ |
| Fixed delay | Fixed 1-second delay between retries (predictable timing) | |

**User's choice:** Exponential backoff

| Option | Description | Selected |
|--------|-------------|----------|
| Final result only | Show only final success/failure (clean UI, less feedback) | |
| Retry progress | Show each retry attempt (transparent, shows progress) | ✓ |
| Each timeout | Show after each timeout (detailed, may be verbose) | |

**User's choice:** Retry progress

**Notes:** User wanted command-aware retry logic with exponential backoff and transparent progress reporting.

---

## Camera settings format

| Option | Description | Selected |
|--------|-------------|----------|
| Enum codes | Predefined values (QVGA=0, VGA=1, SVGA=2, etc.) | ✓ |
| Width x height | Send width/height as two uint16 values (4 bytes) | |
| Resolution index | Index into ESP32 supported resolutions list (single byte) | |

**User's choice:** Enum codes

| Option | Description | Selected |
|--------|-------------|----------|
| 0-10 scale | 0-10 integer (0=low, 10=high, matches ESP32 quality range) | ✓ |
| ESP32 range | 0-31 integer (raw ESP32 quality value) | |
| 3-level enum | Low/Medium/High enum (3 values only) | |

**User's choice:** 0-10 scale

| Option | Description | Selected |
|--------|-------------|----------|
| Signed -2 to +2 | Signed int8 (-2 to +2, ESP32 range) | ✓ |
| 0-100 percent | 0-100 percentage (more intuitive for users) | |
| Raw value | 0-255 (raw ESP32 value, less intuitive) | |

**User's choice:** Signed -2 to +2

| Option | Description | Selected |
|--------|-------------|----------|
| Mode + gain | Exposure mode enum + gain value (separate fields) | ✓ |
| Combined value | Single combined exposure value (simpler but less precise) | |
| Mode only | Only send exposure mode, use defaults for gain | |

**User's choice:** Mode + gain

**Notes:** User chose formats balancing ESP32 compatibility with user-friendly encoding where appropriate (like 0-10 quality scale).

---

## Base station UI layout

| Option | Description | Selected |
|--------|-------------|----------|
| Separate page | Camera controls on separate page from telemetry (clear separation) | ✓ |
| Camera on top | Camera section at top, telemetry below (camera focus) | |
| Telemetry on top | Telemetry at top, camera controls below (primary focus map/data) | |

**User's choice:** Separate page

| Option | Description | Selected |
|--------|-------------|----------|
| Big button | Large prominent 'CAPTURE' button (easy access, primary action) | ✓ |
| Toolbar style | Compact toolbar with multiple camera functions | |
| Dropdown menu | Dropdown menu with camera options (saves space, less direct) | |

**User's choice:** Big button

| Option | Description | Selected |
|--------|-------------|----------|
| Accordion panels | Expandable accordion sections (Resolution, Quality, Exposure, etc.) | ✓ |
| Single form | All settings visible at once in form (longer scroll) | |
| Tabbed | Tabbed interface (Basic / Advanced tabs) | |

**User's choice:** Accordion panels

| Option | Description | Selected |
|--------|-------------|----------|
| Command queue | Live command queue with pending/in-progress states (full visibility) | ✓ |
| Last command | Show last command sent and acknowledgment received (confirm only) | |
| Status indicator | Status indicator only (busy/idle/error states) | |

**User's choice:** Command queue

**Notes:** User wanted clear separation from telemetry, prominent capture trigger, organized settings, and full visibility of command states.

---

## Claude's Discretion

No areas delegated to Claude's discretion — all decisions explicitly specified by user.

## Deferred Ideas

None — discussion stayed within phase scope.

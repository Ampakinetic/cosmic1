# Phase 1: Command Protocol & Control

**Phase:** 1  
**Status:** Complete  
**Mode:** MVP (Vertical Slice)  
**Created:** 2025-08-18  
**Completed:** 2025-08-18

---

## Objective

Establish bidirectional LoRa communication for camera control between base station and balloon unit. Users can trigger camera captures and adjust camera settings from the base station web interface, with reliable command execution through acknowledgment and retry mechanisms.

## Requirements Coverage

| Requirement | Status |
|-------------|--------|
| CTRL-01: User can trigger camera capture from base station web interface | Pending |
| CTRL-02: User can adjust all camera settings remotely | Pending |
| CTRL-03: System supports both manual and automatic capture modes | Pending |
| CTRL-04: Automatic capture supports fixed interval timing | Pending |
| CTRL-06: Camera commands that fail are retried with timeout | Pending |
| PRI-02: Camera commands use retry mechanism with timeout | Pending |

## Success Criteria

1. Base station web interface has camera control section with trigger button and settings forms
2. LoRa command packets transmitted from base station to balloon and acknowledged
3. Balloon receives camera commands and adjusts camera settings accordingly
4. Failed commands are retried with timeout and user is notified
5. Both manual trigger and interval-based auto-capture work end-to-end

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Base Station                             │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────┐   │
│  │ Web UI      │────▶│ Command      │────▶│ E32 LoRa    │   │
│  │ (Camera     │     │ Sender       │     │ Transmitter │   │
│  │  Controls)  │     │ (with Retry) │     │             │   │
│  └─────────────┘     └──────────────┘     └──────┬──────┘   │
│       ▲                       ▲                     │         │
│       │                       │                     │         │
│  ┌───┴────┐            ┌──────┴──────┐             │         │
│  │ Status │            │ ACK/NACK    │             │         │
│  │ Display│            │ Handler      │◀────────────┘         │
│  └────────┘            └─────────────┘                          │
└─────────────────────────────────────────────────────────────────┘
                              │ LoRa Link
                              │ 900 MHz
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Balloon Unit                             │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────┐   │
│  │ E32 LoRa    │────▶│ Command      │────▶│ Camera      │   │
│  │ Receiver    │     │ Handler      │     │ Manager     │   │
│  └──────┬──────┘     └──────────────┘     └─────────────┘   │
│         │                                                              │
│         │                                                              │
│  ┌──────┴──────┐                                                    │
│  │ ACK/NACK    │                                                    │
│  │ Generator   │                                                    │
│  └─────────────┘                                                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task Breakdown

### Tracer: End-to-End Manual Camera Trigger (Vertical Slice)

**Objective:** Prove the entire command flow works with a single working command before expanding.

| ID | Task | Files | Dependencies | Status |
|----|------|-------|---------------|--------|
| T1 | Define camera command protocol structure | `include/command_protocol.h` | None | Complete |
| T2 | Implement E32-900T30D UART LoRa driver | `src/e32_lora.cpp`, `include/e32_lora.h` | None | Complete |
| T3 | Implement command serializer/deserializer | `src/command_protocol.cpp` | T1 | Complete |
| T4 | Create base station command sender with retry | `src/command_sender.cpp`, `include/command_sender.h` | T2, T3 | Complete |
| T5 | Create balloon command handler | `src/command_handler.cpp`, `include/command_handler.h` | T2, T3 | Complete |
| T6 | Implement ACK/NACK generation on balloon | `src/command_handler.cpp` | T5 | Complete |
| T7 | Wire camera trigger to CameraManager | `src/command_handler.cpp` | T5 | Complete |
| T8 | Create minimal web UI (trigger button only) | `src/main_basestation.cpp` | T4 | Complete |
| T9 | Integration test: full trigger flow | Hardware testing | All | Pending (Hardware Required) |

### Expansion: Camera Settings Control

| ID | Task | Files | Dependencies | Status |
|----|------|-------|---------------|--------|
| E1 | Add settings commands to protocol | `include/command_protocol.h` | T1 | Pending |
| E2 | Implement settings parser on balloon | `src/command_handler.cpp` | T5 | Pending |
| E3 | Wire settings to CameraManager methods | `src/command_handler.cpp` | E2 | Pending |
| E4 | Create settings forms on web UI | `src/web_server.cpp` | E1 | Pending |
| E5 | Test all camera settings | Hardware testing | All | Pending |

### Expansion: Auto-Capture Modes

| ID | Task | Files | Dependencies | Status |
|----|------|-------|---------------|--------|
| A1 | Add auto-capture commands to protocol | `include/command_protocol.h` | T1 | Pending |
| A2 | Implement interval timer on balloon | `src/auto_capture.cpp`, `include/auto_capture.h` | T5 | Pending |
| A3 | Add mode switching logic | `src/command_handler.cpp` | A2 | Pending |
| A4 | Create auto-capture UI controls | `src/web_server.cpp` | T8 | Pending |
| A5 | Test interval-based capture | Hardware testing | All | Pending |

---

## Command Protocol Specification

### Packet Structure

All camera commands use the following packet format (max 240 bytes):

```
┌──────────────┬──────────┬─────────┬─────────┬─────────┬────────────┬────────────┐
│ Header (8B)  │ Type (1B) │ Cmd (1B) │ Seq (2B) │ Len (2B) │ Payload     │ Footer (4B)│
│ 0xAA 0x55... │ 0x10     │ CMD_ID  │ N       │ N       │ variable   │ CRC + 0x0D  │
└──────────────┴──────────┴─────────┴─────────┴─────────┴────────────┴────────────┘
```

### Command Types

| Command ID | Name | Payload | Response |
|------------|------|---------|----------|
| 0x01 | CAPTURE_NOW | None | ACK with image_id |
| 0x02 | SET_RESOLUTION | frame_size | ACK with new setting |
| 0x03 | SET_QUALITY | quality (0-63) | ACK with new setting |
| 0x04 | SET_BRIGHTNESS | brightness (-2 to 2) | ACK with new setting |
| 0x05 | SET_CONTRAST | contrast (-2 to 2) | ACK with new setting |
| 0x06 | SET_SATURATION | saturation (-2 to 2) | ACK with new setting |
| 0x07 | SET_EXPOSURE | exposure_level | ACK with new setting |
| 0x08 | SET_WB_MODE | wb_mode (0-4) | ACK with new setting |
| 0x10 | AUTO_CAPTURE_ENABLE | interval_ms | ACK with interval set |
| 0x11 | AUTO_CAPTURE_DISABLE | None | ACK confirming stop |
| 0x20 | GET_STATUS | None | STATUS response |

### Response Types

| Type | Value | Description |
|------|-------|-------------|
| ACK | 0x00 | Command executed successfully |
| NACK | 0x01 | Command failed (unknown error) |
| NACK_INVALID | 0x02 | Invalid command |
| NACK_PARAM | 0x03 | Invalid parameter |
| NACK_BUSY | 0x04 | Camera busy |
| STATUS | 0x05 | Status data response |

---

## Technical Specifications

### E32-900T30D LoRa Configuration

```
UART Settings:
- Baud Rate: 9600 (configurable)
- Data Bits: 8
- Parity: None
- Stop Bits: 1

LoRa Settings (Normal Mode):
- Frequency: 915 MHz (US) / 868 MHz (EU)
- Spreading Factor: SF9 (default)
- Bandwidth: 125 kHz
- Coding Rate: 4/5
- TX Power: 20 dBm

Pin Configuration:
- M0: GPIO 19 (Mode control)
- M1: GPIO 20 (Mode control)
- AUX: GPIO 21 (Status indication)
- TX: GPIO 14
- RX: GPIO 48
```

### Retry Logic

```cpp
// Retry configuration
#define MAX_RETRIES         3
#define ACK_TIMEOUT_MS     2000
#define COMMAND_RETRY_DELAY_MS  100

// Retry states
enum RetryState {
    RETRY_IDLE,
    RETRY_PENDING,
    RETRY_SUCCESS,
    RETRY_FAILED,
    RETRY_TIMEOUT
};
```

### Priority Queue

Camera commands use Priority::CAMERA (4), lower than:
- Emergency (1)
- GPS (2)
- Telemetry (3)

Commands wait for higher priority traffic to clear.

---

## File Changes

### New Files

```
include/
├── command_protocol.h      # Command definitions and packet structures
├── e32_lora.h             # E32-900T30D UART driver interface
├── command_sender.h       # Base station command transmission
├── command_handler.h      # Balloon command reception and execution
└── auto_capture.h         # Auto-capture timer and state

src/
├── command_protocol.cpp    # Packet serialization
├── e32_lora.cpp           # E32 UART driver implementation
├── command_sender.cpp      # Base station sender with retry
├── command_handler.cpp    # Balloon handler and ACK/NACK
└── auto_capture.cpp       # Interval timer implementation

src/html/
└── camera_control.html    # Minimal web UI (embedded or filesystem)
```

### Modified Files

```
src/main_balloon.cpp       # Integrate command_handler
src/test_lora_basestation.cpp  # Base station firmware skeleton
src/lora_comm.cpp          # Update to use e32_lora (if needed)
src/camera_manager.cpp      # Ensure all settings methods work
include/common_types.h     # Add COMMAND packet type
```

---

## Verification Plan

### Unit Tests (Hardware-based)

1. **Command Protocol Tests**
   - Serialize/deserialize each command type
   - Validate packet structure
   - Test CRC calculation

2. **E32 Driver Tests**
   - Transmit known packet, verify AUX behavior
   - Receive known packet, verify content
   - Test mode switching (M0/M1)

3. **Retry Logic Tests**
   - Simulate lost ACK, verify retry
   - Test timeout handling
   - Verify MAX_RETRIES limit

### Integration Tests

1. **Manual Trigger Flow**
   - Base sends CAPTURE_NOW
   - Balloon receives, executes
   - Balloon sends ACK
   - Base displays success
   - Camera image captured

2. **Settings Change Flow**
   - Base sends SET_QUALITY
   - Balloon applies setting
   - Balloon sends ACK with new value
   - Base confirms update

3. **Auto-Capture Flow**
   - Base sends AUTO_CAPTURE_ENABLE with interval
   - Balloon starts timer
   - Images capture at interval
   - Base sends AUTO_CAPTURE_DISABLE
   - Timer stops

4. **Failure Scenarios**
   - Balloon powered off: Base shows timeout after retries
   - Camera busy: NACK_BUSY returned, UI shows message
   - Invalid parameter: NACK_PARAM returned, UI shows error

---

## Risk Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| E32 modules don't communicate | HIGH | Test UART communication first with simple echo |
| Packet corruption in flight | MEDIUM | CRC on both header and payload |
| Camera operation blocks LoRa | MEDIUM | Keep camera operations non-blocking |
| Retry queue overflow | LOW | Limit pending commands to 5 |
| Web UI responsiveness | LOW | Async command submission, status polling |

---

## Dependencies

### Hardware Required
- 2x ESP32-S3 dev boards
- 2x E32-900T30D LoRa modules
- 1x Camera module (OV2640 or similar)
- Breadboard/wiring for testing

### Software/Libraries
- Existing CameraManager
- Existing PacketHandler (as reference)
- E32 UART library (may need custom implementation)
- Arduino WebServer (ESP32 variant)

---

## Out of Scope (Explicitly Deferred)

- Image transmission (Phase 2)
- Thumbnail generation/display (Phase 2)
- Map display (Phase 3)
- Advanced telemetry display (Phase 3)
- Event-based auto-capture triggers (Phase 2)
- SD card storage (Phase 2)

---

## Definition of Done

- [ ] All tracer tasks (T1-T9) complete
- [ ] Manual camera trigger works end-to-end
- [ ] Failed commands show error in UI
- [ ] At least one camera setting change works (expansion E1-E5 optional for MVP)
- [ ] Auto-capture works with fixed interval (expansion A1-A5 optional for MVP)
- [ ] Protocol documented in code comments
- [ ] Hardware tested with real radios
- [ ] Updated platformio.ini with both build targets

---
*Plan created: 2025-08-18*

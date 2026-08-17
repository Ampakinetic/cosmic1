# Phase 1 Context: Command Protocol & Control

**Phase:** 1  
**Status:** Ready to Execute  
**Last Updated:** 2025-08-18

---

## Project Context

**Cosmic1** is an ESP32-S3 balloon tracking system with remote camera control. This phase establishes the bidirectional LoRa communication that enables the base station to control the balloon's camera.

**What Exists:**
- ESP32-S3 balloon firmware with camera, GPS, BMP280 sensors
- Basic telemetry transmission working
- LoRa E32-900T30D modules (UART-based)
- Existing manager classes (CameraManager, PacketHandler, LoRaManager stub)

**What's Missing:**
- Working LoRa communication (E32 driver incomplete)
- Camera command protocol
- Base station firmware
- Web interface for camera control

---

## Current System State

### Balloon Unit (Transmitter)
- **CameraManager:** Fully functional, can capture images and adjust settings
- **PacketHandler:** Has packet structure definitions but LoRa driver incomplete
- **LoRaComm:** Stub implementation designed for SPI-based LoRa (needs E32 UART rewrite)
- **Main Loop:** `main_balloon.cpp` - needs command handler integration

### Base Station (Receiver)
- **Firmware:** `test_lora_basestation.cpp` - basic receiver test only
- **Web Interface:** None yet (planned for this phase)
- **Command Sending:** None yet (planned for this phase)

### LoRa Communication
- **Hardware:** E32-900T30D (UART-based, not SPI)
- **Current Status:** Test code (`test_lora_balloon.cpp`) proves basic UART communication works
- **Gap:** Need reliable packet driver with ACK/NACK support

---

## Key Technical Constraints

1. **LoRa Packet Size:** Maximum 240 bytes per packet
2. **UART Speed:** 9600 baud default (configurable but slower than SPI)
3. **Camera Settings:** All settings already supported by CameraManager
4. **Single-threaded:** Cannot block during camera operations
5. **Power:** Balloon runs on battery - LoRa TX consumes significant power

---

## Known Good References

### Working Code Examples

1. **`src/test_lora_balloon.cpp`** - Proves E32 UART communication works
   - Pin mappings verified: TX=14, RX=48, M0=19, M1=20, AUX=21
   - Baud rate 9600 confirmed
   - Basic packet transmission successful

2. **`src/camera_manager.cpp`** - Camera operations proven working
   - `captureImage()` - captures full resolution
   - `setQuality()`, `setBrightness()`, etc. - all settings methods exist
   - Non-blocking capture possible

3. **`src/packet_handler.cpp`** - Packet structure reference
   - Header format with start bytes (0xAA 0x55)
   - CRC calculation methods
   - Priority queue implementation

---

## Phase 1 User Story

**As a** balloon operator  
**I want to** trigger camera captures and adjust settings from the ground station  
**So that** I can control what images are captured during flight without physical access to the balloon

**Acceptance Criteria:**
1. I can press a button on the web interface to trigger a capture
2. I see confirmation that the balloon received the command
3. If the command fails, I see an error message with retry status
4. I can change camera resolution, quality, brightness, etc.
5. I can enable/disable automatic capture at a fixed interval

---

## Architecture Constraints

### Existing Patterns to Follow

1. **Manager Pattern:** All subsystems use Manager classes (CameraManager, SensorManager, etc.)
2. **Singleton Access:** `Camera()`, `PacketMgr()` style global accessors
3. **Non-blocking Operations:** Event-driven main loop, no delays in main path
4. **Packet Structure:** Already defined in `common_types.h` - extend for commands

### Integration Points

**Balloon Side (`main_balloon.cpp`):**
```cpp
void loop() {
    // Existing
    readGPS();
    readSensors();
    transmitTelemetry();
    
    // New for Phase 1
    CommandHandler().processIncoming();  // Check for commands
    AutoCapture().check();               // Check if interval elapsed
}
```

**Base Station (new firmware):**
```cpp
void loop() {
    // Check for LoRa packets (ACKs, status)
    BaseStationLoRa().process();
    
    // Serve web clients
    WebServer().handleClient();
    
    // Check for command timeouts
    CommandSender().checkTimeouts();
}
```

---

## Critical Dependencies

### Must Have
1. E32-900T30D UART driver - no existing library works well
2. Command protocol definition - must fit in 240 bytes
3. Reliable ACK/NACK mechanism - retry logic is core requirement

### Nice to Have
1. Command queueing on base station
2. Command history in web UI
3. Real-time command status display

### Can Defer
1. Command queuing on balloon (handle one at a time is OK)
2. Complex error recovery (basic retry is sufficient)
3. Command batching (single command per packet is fine)

---

## Testing Strategy

### Hardware-in-Loop Testing Required

1. **Unit Tests:**
   - Command serialization/deserialization (can test without radios)
   - CRC calculation (math is deterministic)
   - Retry logic (simulated timeouts)

2. **Integration Tests:**
   - Radio-to-radio communication (real E32 modules)
   - Camera trigger (real camera module)
   - Web UI to radio flow (full stack)

3. **Regression Tests:**
   - Existing telemetry must still work
   - Camera local capture must still work

### Test Configuration

```
┌──────────────┐          LoRa          ┌──────────────┐
│  Base        │◀───────────────────────▶│  Balloon     │
│  Station     │   915 MHz, ~2km range  │  Unit        │
│  (USB power) │                          │ (Battery)    │
└──────────────┘                          └──────────────┘
       ▲                                          ▲
       │ Serial/USB                              │ Serial
       │                                          │
   Developer PC                              Developer PC
   (Web UI, logs)                            (Debug logs)
```

---

## Success Metrics

- ✅ Manual camera trigger completes in < 3 seconds (command → capture → ACK)
- ✅ Camera setting change confirmed in < 2 seconds
- ✅ Failed commands timeout after ~6 seconds (3 retries × 2s)
- ✅ 95%+ command success rate at short range (< 100m)
- ✅ Zero regression in existing telemetry transmission

---

## Open Questions to Resolve

1. **Web Framework:** Use raw ESP32 WebServer or more advanced framework?
   - **Decision:** Raw ESP32 WebServer (built-in, sufficient for simple forms)

2. **Command Queue Size:** How many pending commands on base station?
   - **Decision:** Start with 5, expand if needed

3. **Auto-Capture Storage:** Where to store interval setting?
   - **Decision:** In memory, lost on reboot (preferences added in later phase)

4. **Error Display:** How detailed should error messages be in UI?
   - **Decision:** Simple status: "Sent", "ACK Received", "Failed (retry N)", "Timeout"

---

## Related Documentation

- **`.planning/PROJECT.md`** - Overall project context
- **`.planning/REQUIREMENTS.md`** - All 24 v1 requirements
- **`.planning/ROADMAP.md`** - 3-phase roadmap
- **`src/test_lora_balloon.cpp`** - Working E32 reference implementation

---

## Next Actions

1. ✅ Phase 1 plan created (this file)
2. ⏭️ Implement E32 UART driver (foundation)
3. ⏭️ Define command protocol headers (specification)
4. ⏭️ Implement tracer flow (manual trigger)
5. ⏭️ Test end-to-end with hardware

---
*Context captured: 2025-08-18*

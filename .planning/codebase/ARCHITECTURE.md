<!-- refreshed: 2026-06-08 -->
# Architecture

**Analysis Date:** 2026-06-08

## System Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Main Application Layer                           │
│                         `main_balloon.cpp`                             │
├──────────────────┬──────────────────┬──────────────────┬──────────────┤
│   SensorManager  │   CameraManager  │   LoRaManager    │ PowerManager │
│  `sensor_manager`│ `camera_manager` │   `lora_comm`    │`power_manager`│
├──────────────────┴──────────────────┴──────────────────┴──────────────┤
│                      PacketHandler (packet_handler)                    │
│                         `packet_handler`                               │
├────────────────────────────────────────────────────────────────────────┤
│                       SystemState (system_state)                       │
│                         `system_state`                                 │
├────────────────────────────────────────────────────────────────────────┤
│                      DebugUtils (debug_utils)                          │
│                         `debug_utils`                                  │
├────────────────────────────────────────────────────────────────────────┤
│                      Hardware Abstraction Layer                         │
│     I2C (BMP280) │ UART (GPS) │ SPI (LoRa) │ Camera (DVP/I2C)          │
│     `sensor_pins.h` │ `camera_pins.h`                                 │
└────────────────────────────────────────────────────────────────────────┘
         │                      │                      │
         ▼                      ▼                      ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ BMP280 Sensor    │  │ MAX-M10S GPS     │  │ LoRa 900T30D     │
│ (I2C @ 0x76)     │  │ (UART1)          │  │ (SPI3)           │
└──────────────────┘  └──────────────────┘  └──────────────────┘
┌──────────────────┐  ┌──────────────────┐
│ ESP32-S3 Camera  │  │ Battery Monitor  │
│ (DVP Interface)  │  │ (ADC)            │
└──────────────────┘  └──────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| Main Application | System initialization, main loop coordination, subsystem orchestration | `src/main_balloon.cpp` |
| SensorManager | BMP280 pressure/temperature sensor, GPS data collection, I2C/UART management | `src/sensor_manager.cpp` |
| CameraManager | Camera initialization, image capture, thumbnail generation, adaptive settings | `src/camera_manager.cpp` |
| LoRaManager | LoRa radio communication, packet transmission, adaptive transmission settings | `src/lora_comm.cpp` |
| PowerManager | Battery monitoring, power state management, deep sleep control | `src/power_manager.cpp` |
| PacketHandler | Packet serialization, queue management, CRC validation, priority handling | `src/packet_handler.cpp` |
| SystemState | Flight phase detection, mode management, emergency conditions, statistics | `src/system_state.cpp` |
| DebugUtils | Logging, performance monitoring, watchdog, debug command processing | `src/debug_utils.cpp` |
| Global Instances | Centralized singleton access to all managers | `src/balloon_instances.cpp` |
| Web Server | HTTP streaming, camera controls, base station interface | `src/app_httpd.cpp` |

## Pattern Overview

**Overall:** Layered Architecture with Global Singleton Managers

**Key Characteristics:**
- Single-threaded event loop in `main_balloon.cpp`
- Global singleton access pattern via `balloon_instances.cpp`
- Hardware abstraction through dedicated manager classes
- Priority-based packet queuing for communication
- State machine for flight phases and system modes
- Modular subsystem design with clear separation of concerns

## Layers

**Main Application Layer:**
- Purpose: Orchestrate all subsystems, manage timing, handle system state
- Location: `src/main_balloon.cpp`
- Contains: Arduino `setup()` and `loop()`, initialization sequence, timing functions
- Depends on: All subsystem managers (via global singleton access)
- Used by: PlatformIO (as entry point via `build_src_filter`)

**Manager Layer:**
- Purpose: Abstract hardware interfaces, provide high-level APIs for sensors and peripherals
- Location: `src/` (manager .cpp/.h files)
- Contains: SensorManager, CameraManager, LoRaManager, PowerManager, PacketHandler, SystemState, DebugUtils
- Depends on: Hardware abstraction layer (I2C, SPI, UART, camera driver)
- Used by: Main application layer

**Hardware Abstraction Layer:**
- Purpose: Direct hardware interfacing, pin configuration, low-level protocols
- Location: `include/sensor_pins.h`, `include/camera_pins.h`, `include/balloon_config.h`
- Contains: Pin definitions, hardware constants, peripheral configuration
- Depends on: ESP32-S3 hardware (I2C, SPI, UART, DVP camera interface)
- Used by: All manager classes

**External Library Layer:**
- Purpose: Third-party sensor and protocol implementations
- Location: `.pio/libdeps/` (managed by PlatformIO)
- Contains: Adafruit_BMP280, TinyGPSPlus, LoRa, esp32-camera, ArduinoJson
- Depends on: ESP32-S3 platform framework
- Used by: Manager layer (via include statements)

## Data Flow

### Primary Request Path (Balloon Main Loop)

1. **Loop entry** (`main_balloon.cpp:224` - `loop()`)
2. **State update** (`main_balloon.cpp:558` - `updateSystemState()`)
   - Calls `SysState().update()` → `system_state.cpp:98`
3. **Sensor processing** (`main_balloon.cpp:592` - `processSensors()`)
   - Calls `Sensors().update()` → `sensor_manager.cpp` (not fully implemented)
   - Retrieves data via `getBMP280Data()` and `getGPSData()`
4. **Camera processing** (`main_balloon.cpp:619` - `processCamera()`)
   - Checks capture interval via `Camera().isTimeToCapture()`
   - Calls `Camera().captureImage()` → `camera_manager.cpp`
5. **Communication processing** (`main_balloon.cpp:653` - `processCommunications()`)
   - Currently stub - would call `LoRaComm()` methods
6. **Power management** (`main_balloon.cpp:685` - `processPowerManagement()`)
   - Checks battery thresholds, disables camera on low power
7. **Telemetry transmission** (`main_balloon.cpp:771` - `sendTelemetryData()`)
   - Calls `PacketMgr().createTelemetryPacket()` → `packet_handler.cpp`
8. **Timing maintenance** (`main_balloon.cpp:284` - delay for loop interval)

### LoRa Packet Transmission Path

1. **Packet creation** (`packet_handler.cpp` - `createTelemetryPacket()`)
2. **Queue insertion** (`packet_handler.cpp` - `addToBuffer()`)
3. **Priority sorting** (`packet_handler.cpp` - `sortQueueByPriority()`)
4. **Serialization** (`packet_handler.cpp` - `assemblePacket()`)
5. **LoRa transmission** (`lora_comm.cpp` - `sendPacket()`)
6. **ACK/NACK handling** (`lora_comm.cpp` - `handleAcknowledgment()`)

### Sensor Data Collection Path

1. **BMP280 read** (`sensor_manager.cpp` - `updateBMP280Data()`)
   - I2C read via Adafruit_BMP280 library
   - Data stored in `currentBMP280Data`
2. **GPS read** (`sensor_manager.cpp` - `updateGPSData()`)
   - UART1 read via TinyGPSPlus library
   - NMEA parsing, data stored in `currentGPSData`
3. **Data validation** (`sensor_manager.cpp` - `validateBMP280Data()`)
4. **Altitude calculation** (`sensor_manager.cpp` - `calculateAltitude()`)

**State Management:**
- Centralized in `SystemState` class with mode/phase enums
- Event-driven updates via `processEvent()` method
- Circular buffer for event log (50 events max)
- Persistent state via NVS (not yet implemented)

## Key Abstractions

**Manager Singleton Pattern:**
- Purpose: Provide global access to subsystem managers
- Examples: `Sensors()`, `Camera()`, `LoRaComm()`, `PowerMgr()`, `SysState()`, `Debug`
- Pattern: Static instances in `balloon_instances.cpp` with accessor functions

**Packet Protocol:**
- Purpose: Structured communication over LoRa with error detection
- Examples: `PacketType` enum, `PacketHeader` struct, priority queues
- Pattern: Serialize → Queue → Transmit → ACK/NACK

**Flight Phase Machine:**
- Purpose: Track balloon flight progress for adaptive behavior
- Examples: `FlightPhase` enum (GROUND, LAUNCH, ASCENT, APEX, DESCENT, LANDING)
- Pattern: State transitions based on altitude/velocity thresholds

**Emergency Detection:**
- Purpose: Automatically trigger emergency protocols
- Examples: Altitude threshold, temperature threshold, velocity threshold
- Pattern: Continuous monitoring in `SystemState::update()` → `detectEmergencyConditions()`

## Entry Points

**Balloon Firmware Entry Point:**
- Location: `src/main_balloon.cpp`
- Triggers: PlatformIO build filter (`build_src_filter = +<main_balloon.cpp>`)
- Responsibilities:
  - System initialization (serial, debug, hardware, subsystems)
  - Main loop execution at 10 Hz
  - Telemetry and heartbeat transmission
  - Emergency handling

**Base Station Entry Point:**
- Location: Not yet implemented (placeholder in `platformio.ini`)
- Triggers: Alternative build filter for base station mode
- Responsibilities:
  - LoRa packet reception
  - WiFi AP mode
  - Web server hosting (`app_httpd.cpp`)

**Web Server Entry Point:**
- Location: `src/app_httpd.cpp`
- Triggers: Base station firmware initialization
- Responsibilities:
  - HTTP streaming server
  - Camera control interface
  - WebSocket for real-time updates

## Architectural Constraints

- **Threading:** Single-threaded Arduino framework (no FreeRTOS tasks explicitly created)
- **Global state:** Module-level singletons in `balloon_instances.cpp` (all managers)
- **Circular imports:** None detected - clean layer separation
- **Pin conflicts:** Validated in `sensor_pins.h` (no conflicts with camera pins)
- **Memory:** PSRAM required for camera (`BOARD_HAS_PSRAM`, `CAMERA_REQUIRES_PSRAM=1`)
- **Flash:** Custom partition scheme (`partitions.csv`) with 3MB APP space
- **Timing:** Main loop runs at 10 Hz (100ms interval), sensor reads at configurable intervals

## Anti-Patterns

### Missing Update Methods

**What happens:** Main loop calls methods like `LoRaComm().update()` or `PowerMgr().update()` that don't exist in the implementation
**Why it's wrong:** Causes compilation errors or requires commented-out code
**Do this instead:** Implement the missing `update()` methods in each manager class or remove the calls from main loop

### Header Include Conflicts

**What happens:** Camera headers and sensor headers both define `sensor_t` type, causing compilation conflicts
**Why it's wrong:** Type redefinition and ambiguous symbol errors
**Do this instead:** Forward declare sensor classes in headers (`class Adafruit_BMP280; class TinyGPSPlus;`) and include actual headers only in .cpp files (as done in `sensor_manager.h`)

### Stub implementations

**What happens:** Many methods are stub implementations or commented out (e.g., `processCommunications()`)
**Why it's wrong:** Appears functional but doesn't actually perform work, misleading developers
**Do this instead:** Implement full functionality or use TODO comments with clear indications of incomplete status

## Error Handling

**Strategy:** Try-catch in main loop with error counting, debug logging throughout

**Patterns:**
- Main loop: `try { ... } catch (...) { SYS_ERROR("Exception in main loop"); }`
- Subsystem init: Return `bool` with SYS_ERROR logging on failure
- Critical failures: Return from setup(), preventing loop execution
- Emergency conditions: Trigger emergency mode in SystemState with system-wide impact

## Cross-Cutting Concerns

**Logging:** Centralized via `DebugUtils` with category-based filtering (SYSTEM, SENSORS, CAMERA, LORA, POWER, GPS)
**Validation:** Packet CRC validation, sensor data range checking, state transition validation
**Authentication:** Device ID in packet headers (basic authentication only)
**Timing:** Interval-based scheduling for telemetry (5s), heartbeat (30s), status (60s)
**Configuration:** Compile-time defines in `balloon_config.h`, runtime in SystemState

---

*Architecture analysis: 2026-06-08*

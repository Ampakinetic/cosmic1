<!-- refreshed: 2026-06-28 -->
# Architecture

**Analysis Date:** 2026-06-28

## System Overview

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                      Main Application Layer                             │
│                    `src/main_balloon.cpp`                              │
├──────────────────┬──────────────────┬─────────────────────┬────────────┤
│   Sensor Manager │   Camera Manager │    LoRa Manager     │ Power Mgr  │
│  `src/sensor_    │  `src/camera_    │  `src/lora_comm.cpp`│`src/power_ │
│   manager.cpp`   │   manager.cpp`   │                     │manager.cpp`│
└────────┬─────────┴────────┬─────────┴──────────┬──────────┴────────────┘
         │                  │                     │
         ▼                  ▼                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    System State & Coordination                         │
│         `src/system_state.cpp` + `src/packet_handler.cpp`               │
└─────────────────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Hardware Abstraction Layer (ESP32-S3)                     │
│    I2C (BMP280) | UART (GPS) | SPI (LoRa) | Camera (DVP)              │
└─────────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| **Main Application** | System initialization, main loop coordination, subsystem orchestration | `src/main_balloon.cpp` |
| **Sensor Manager** | BMP280 pressure/temperature sensor, GPS data acquisition and validation | `src/sensor_manager.cpp` |
| **Camera Manager** | ESP32 camera control, image capture, thumbnail generation | `src/camera_manager.cpp` |
| **LoRa Manager** | LoRa radio communication, packet transmission, adaptive SF | `src/lora_comm.cpp` |
| **Power Manager** | Battery monitoring, power state management, deep sleep control | `src/power_manager.cpp` |
| **System State** | Flight phase tracking, mode management, emergency detection | `src/system_state.cpp` |
| **Packet Handler** | Packet serialization, deserialization, CRC validation, priority queuing | `src/packet_handler.cpp` |
| **Debug Utils** | Logging, performance monitoring, watchdog feeding | `src/debug_utils.cpp` |

## Pattern Overview

**Overall:** Layered Architecture with Singleton Access Pattern

**Key Characteristics:**
- **Singleton instances** accessed via global functions (Sensors(), Camera(), LoRaComm(), PowerMgr(), SysState(), Debug)
- **Manager classes** encapsulate hardware-specific functionality
- **Event-driven** system state changes and emergency handling
- **Priority-based** packet queue for LoRa transmission
- **State machine** for flight phases and system modes

## Layers

**Application Layer:**
- Purpose: Main system orchestration and coordination
- Location: `src/main_balloon.cpp`
- Contains: setup(), loop(), subsystem coordination logic
- Depends on: All manager classes
- Used by: PlatformIO (entry point)

**Manager Layer:**
- Purpose: Hardware abstraction and subsystem management
- Location: `src/*.cpp` (sensor_manager.cpp, camera_manager.cpp, lora_comm.cpp, power_manager.cpp)
- Contains: Device-specific initialization, data acquisition, hardware control
- Depends on: Hardware peripherals (I2C, SPI, UART, Camera)
- Used by: Application layer

**State & Coordination Layer:**
- Purpose: System state tracking, packet handling, inter-module communication
- Location: `src/system_state.cpp`, `src/packet_handler.cpp`
- Contains: Flight phase detection, mode transitions, packet serialization
- Depends on: Manager layer for data
- Used by: Application layer

**Hardware Abstraction Layer:**
- Purpose: Direct hardware interface
- Location: ESP32-S3 Arduino framework, sensor libraries
- Contains: I2C (Wire), SPI, UART1, Camera driver
- Depends on: Physical hardware (BMP280, MAX-M10S, LoRa module, Camera)
- Used by: Manager layer

## Data Flow

### Primary Request Path (Sensor → LoRa)

1. **Sensor Acquisition** (`src/sensor_manager.cpp:136-150`)
   - BMP280 pressure/temperature reading via I2C
   - GPS NMEA parsing via UART1
2. **Data Validation** (`src/sensor_manager.cpp:236-272`)
   - Range checks for pressure (-40°C to +85°C, 300-1200 hPa)
   - GPS validation (minimum 4 satellites, HDOP check)
3. **System State Update** (`src/system_state.cpp:98-122`)
   - Update altitude, velocity, temperature
   - Flight phase detection (GROUND → LAUNCH → ASCENT → APEX → DESCENT → LANDING)
4. **Packet Creation** (`src/packet_handler.cpp`)
   - Serialize telemetry/GPS data to packet format
   - Calculate CRC-16, add headers
5. **LoRa Transmission** (`src/lora_comm.cpp`)
   - Queue packet by priority (EMERGENCY > GPS > TELEMETRY > CAMERA > STATUS)
   - Transmit with adaptive spreading factor based on RSSI

### Image Capture Flow

1. **Camera Capture** (`src/camera_manager.cpp:captureImage()`)
   - Trigger image capture via esp_camera_fb_get()
   - Store in PSRAM buffer
2. **Thumbnail Generation** (`src/camera_manager.cpp:createThumbnail()`)
   - Resize to QVGA (320x240) for transmission
3. **Packet Creation** (`src/packet_handler.cpp:createCameraPacket()`)
   - Chunk image data into 200-byte packets
4. **LoRa Transmission** (`src/lora_comm.cpp`)
   - Queue at CAMERA priority (lower than telemetry/GPS)

### Power Management Flow

1. **Voltage Monitoring** (`src/power_manager.cpp:157-176`)
   - ADC read from battery sense pin (GPIO 4)
   - Convert to voltage (voltage divider ratio)
2. **State Determination** (`src/power_manager.cpp:204-249`)
   - Compare against thresholds (CRITICAL: 3.2V, LOW: 3.4V, NORMAL: 3.7V)
3. **Adaptive Response** (`src/power_manager.cpp:358-400`)
   - CPU frequency scaling (240MHz → 40MHz based on battery)
   - Camera/disable in low power
   - Emergency shutdown at critical voltage
4. **Deep Sleep Entry** (`src/power_manager.cpp:453-463`)
   - esp_sleep_enable_timer_wakeup()
   - esp_deep_sleep_start()

**State Management:**
- System state tracked in SystemState singleton (`src/system_state.cpp`)
- Persistent state stored in NVS/Preferences
- Flight phase detection based on altitude and velocity

## Key Abstractions

**Sensor Data Abstraction:**
- Purpose: Unified sensor data representation
- Examples: `BMP280Data`, `GPSData`, `SensorGPSData` in `src/sensor_manager.h`, `include/common_types.h`
- Pattern: Struct-based data containers with validity flags and timestamps

**Packet Abstraction:**
- Purpose: LoRa communication protocol
- Examples: `Packet`, `QueuedPacket`, `LoRaPacketHeader` in `src/lora_comm.h`, `src/packet_handler.h`
- Pattern: Header-payload-footer structure with CRC validation

**Power State Abstraction:**
- Purpose: Power management state machine
- Examples: `PowerState`, `PowerSource`, `BatteryStatus` in `src/power_manager.h`
- Pattern: Enum-based state with thresholds and callbacks

**System Mode Abstraction:**
- Purpose: Flight phase and mode tracking
- Examples: `SystemMode`, `FlightPhase`, `SystemStatus` in `src/system_state.h`
- Pattern: Enum-based states with event-driven transitions

## Entry Points

**setup() - Arduino Entry Point:**
- Location: `src/main_balloon.cpp:150-222`
- Triggers: PlatformIO/Arduino framework on boot
- Responsibilities:
  - Initialize debug system
  - Initialize hardware (pins, SPI, UART)
  - Initialize all subsystem managers
  - Perform system checks
  - Set initial mode (PRE_FLIGHT, GROUND)

**loop() - Main Loop:**
- Location: `src/main_balloon.cpp:224-292`
- Triggers: Continuous execution at ~10 Hz
- Responsibilities:
  - Feed watchdog
  - Update system state
  - Process sensors, camera, communications, power
  - Send periodic telemetry (5s), heartbeat (30s), status (60s)
  - Maintain loop timing (100ms target)

## Architectural Constraints

- **Threading:** Single-threaded event loop (no FreeRTOS tasks)
- **Global state:** Singleton instances in `src/balloon_instances.cpp` (6 global objects)
- **Circular imports:** None (forward declarations used where needed)
- **Hardware constraints:**
  - PSRAM required for camera operations (8MB)
  - 16MB flash with 3MB APP partition
  - CPU frequency scaling (40-240MHz) for power management
  - LoRa payload limit 240 bytes per packet

## Anti-Patterns

### Duplicate I2C Initialization

**What happens:** Multiple calls to `Wire.begin()` in different parts of the code
**Why it's wrong:** ESP32 Wire library generates "Bus already started" warnings and can cause crashes
**Do this instead:** Initialize I2C once in the manager that owns it (`src/sensor_manager.cpp:83`)
**Reference:** `docs/DEBUG_CRASH_SOLUTION.md`

### Missing Error Handling in Packet Transmission

**What happens:** Packet creation failures logged but not retried
**Why it's wrong:** Critical telemetry can be lost without retry or queuing
**Do this instead:** Implement priority queue with retry logic in `src/lora_comm.cpp`

### Blocking Calls in Main Loop

**What happens:** Long-running sensor reads or camera captures block the loop
**Why it's wrong:** Misses watchdog deadlines, prevents timely state updates
**Do this instead:** Use time-sliced operations, limit sensor read duration to 100ms

## Error Handling

**Strategy:** Graceful degradation with emergency modes

**Patterns:**
- **Validation flags:** Data structures include `valid` boolean to indicate sensor read success
- **Error counting:** Each manager tracks error counts (bmp280ErrorCount, gpsErrorCount)
- **Fallback values:** Use last known good data when sensor fails temporarily
- **Emergency triggering:** SystemState monitors conditions and triggers emergency mode
- **Callback registration:** PowerManager supports callbacks for low battery events

## Cross-Cutting Concerns

**Logging:** `src/debug_utils.cpp` provides categorized logging (SYS_INFO, SYS_ERROR, etc.)
**Validation:** Each manager validates sensor data before use (range checks, NaN detection)
**Authentication:** None (LoRa transmission is unencrypted)
**Power Management:** CPU frequency scaling, deep sleep, component disable in low power

---

*Architecture analysis: 2026-06-28*

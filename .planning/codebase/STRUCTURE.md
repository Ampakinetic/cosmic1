# Codebase Structure

**Analysis Date:** 2026-06-08

## Directory Layout

```
C:\Work\Prog\Cosmic1\
├── .pio/                    # PlatformIO build artifacts and dependencies
│   ├── libdeps/             # Downloaded libraries per environment
│   │   ├── esp32-s3-balloon/LoRa/
│   │   ├── esp32-s3-balloon/esp32-camera/
│   │   └── esp32-s3-devkitc-1/... (mirror structure)
│   └── build/               # Compiled build outputs
├── .planning/               # Project planning and documentation
│   └── codebase/            # Architecture documentation (this file)
├── docs/                    # Project documentation
│   ├── BALLOON_IMPLEMENTATION_PLAN.md
│   ├── COMMUNICATION_PROTOCOL.md
│   ├── DEBUG_CRASH_SOLUTION.md
│   ├── WiFi_Troubleshooting.md
│   ├── PIN_MAPPING_GUIDE.md
│   └── PlatformIO.md
├── include/                 # Header files and configuration
│   ├── balloon_config.h     # Balloon-specific configuration
│   ├── base_station_config.h
│   ├── board_config.h       # Board-specific definitions
│   ├── camera_index.h       # HTML/CSS for web interface
│   ├── camera_pins.h        # Camera pin definitions (multi-board)
│   ├── common_types.h       # Shared data structures
│   ├── debug_utils.h        # Debug system header
│   ├── sensor_pins.h        # Sensor pin definitions
│   └── wifi_config.h
├── src/                     # Main source files
│   ├── app_httpd.cpp        # HTTP/web server (base station)
│   ├── balloon_instances.cpp # Global singleton instances
│   ├── camera_manager.cpp   # Camera subsystem implementation
│   ├── camera_manager.h
│   ├── debug_utils.cpp      # Debug/logging system
│   ├── debug_utils.h
│   ├── lora_comm.cpp        # LoRa communication
│   ├── lora_comm.h
│   ├── main_balloon.cpp     # Balloon firmware entry point
│   ├── packet_handler.cpp   # Packet protocol handler
│   ├── packet_handler.h
│   ├── power_manager.cpp    # Power management
│   ├── power_manager.h
│   ├── sensor_manager.cpp   # Sensor subsystem (BMP280, GPS)
│   ├── sensor_manager.h
│   ├── system_state.cpp     # System state machine
│   └── system_state.h
├── lib/                     # Local libraries (currently empty)
├── partitions.csv           # Custom partition table for ESP32-S3
├── platformio.ini           # PlatformIO configuration
├── README.md                # Project readme
└── PROJECT_SUMMARY.md      # Project overview
```

## Directory Purposes

**`.pio/`:**
- Purpose: PlatformIO build system outputs and library dependencies
- Contains: Compiled object files, downloaded libraries, build artifacts
- Generated: Yes (managed by PlatformIO, not committed)
- Committed: No (in `.gitignore`)

**`docs/`:**
- Purpose: Project documentation including implementation plans, protocols, guides
- Contains: Implementation phases, communication protocol, pin mapping, troubleshooting
- Generated: No
- Committed: Yes

**`include/`:**
- Purpose: Shared header files and configuration constants
- Contains: Hardware pin definitions, protocol structures, configuration values
- Key files: `balloon_config.h`, `sensor_pins.h`, `camera_pins.h`, `common_types.h`
- Generated: No
- Committed: Yes

**`src/`:**
- Purpose: Main application source code for balloon and base station firmware
- Contains: Manager classes, entry points, web server, packet handling
- Key files: `main_balloon.cpp`, `balloon_instances.cpp`, all manager .cpp/.h pairs
- Generated: No
- Committed: Yes

**`lib/`:**
- Purpose: Local/project-specific libraries (not from PlatformIO registry)
- Contains: Currently empty
- Generated: No
- Committed: Yes

## Key File Locations

**Entry Points:**
- `src/main_balloon.cpp`: Balloon firmware main entry (setup/loop)
- `src/app_httpd.cpp`: Web server for base station (not currently active in balloon build)

**Configuration:**
- `platformio.ini`: Build environments, library dependencies, compile flags
- `include/balloon_config.h`: Balloon-specific settings (intervals, thresholds, packet types)
- `include/sensor_pins.h`: Sensor and LoRa pin definitions
- `include/camera_pins.h`: Camera pin definitions for multiple board types
- `include/common_types.h`: Shared data structures (GPSData, PacketType, etc.)

**Core Logic:**
- `src/balloon_instances.cpp`: Global singleton accessors for all managers
- `src/sensor_manager.cpp`: BMP280 pressure/temperature, MAX-M10S GPS
- `src/camera_manager.cpp`: ESP32-S3 camera control, image capture
- `src/lora_comm.cpp`: LoRa radio communication, packet transmission
- `src/power_manager.cpp`: Battery monitoring, power state management
- `src/packet_handler.cpp`: Packet protocol, queue management, CRC
- `src/system_state.cpp`: Flight phases, system modes, emergency detection
- `src/debug_utils.cpp`: Logging system, performance monitoring

**Documentation:**
- `docs/BALLOON_IMPLEMENTATION_PLAN.md`: Phase-by-phase implementation plan
- `docs/COMMUNICATION_PROTOCOL.md`: LoRa packet protocol specification
- `docs/PIN_MAPPING_GUIDE.md`: Hardware pin assignments and validation

**Build Configuration:**
- `partitions.csv`: Custom partition table for ESP32-S3 (3MB APP + other)
- `platformio.ini`: Dual build environments (balloon + base station)

## Naming Conventions

**Files:**
- Source files: `snake_case.cpp` (e.g., `sensor_manager.cpp`)
- Header files: `snake_case.h` (e.g., `sensor_manager.h`)
- Config files: `snake_case.h` in `include/` (e.g., `balloon_config.h`)
- Documentation: `TITLE_CASE.md` or `SNAKE_CASE.md` (mixed)

**Classes:**
- PascalCase: `SensorManager`, `CameraManager`, `LoRaManager`, `PowerManager`, `PacketHandler`, `SystemState`, `DebugUtils`

**Functions:**
- camelCase: `begin()`, `update()`, `isTimeToCapture()`, `createTelemetryPacket()`

**Variables:**
- camelCase: `currentBMP280Data`, `lastCaptureTime`, `batteryVoltage`
- Member variables: camelCase with `m_` prefix (rarely used)

**Constants:**
- UPPER_CASE with underscores: `BMP280_SDA_PIN`, `LORA_FREQUENCY`, `MAX_PACKET_SIZE`
- Enums: PascalCase for types (`SystemMode`, `PacketType`), UPPER_CASE for values

**Macros:**
- UPPER_CASE with underscores: `SYS_INFO`, `DEBUG_CAMERA`, `DEVICE_TYPE`

## Where to Add New Code

**New Feature (sensor/peripheral):**
- Primary code: `src/<name>_manager.cpp` and `src/<name>_manager.h`
- Pin definitions: `include/<name>_pins.h` (if hardware-specific)
- Configuration: `include/balloon_config.h` (if balloon-specific)
- Tests: Not yet implemented (no test directory)
- Singleton accessor: Add to `src/balloon_instances.cpp`

**New Packet Type:**
- Packet type enum: `include/common_types.h` (PacketType enum)
- Creation method: `src/packet_handler.cpp` (create*Packet method)
- Processing: `src/packet_handler.cpp` (extraction method)
- Protocol doc: `docs/COMMUNICATION_PROTOCOL.md`

**New System Mode/Flight Phase:**
- Enum values: `src/system_state.h` (SystemMode or FlightPhase)
- Transition logic: `src/system_state.cpp` (handler methods)
- String conversion: `src/system_state.cpp` (toString methods)

**New Web Interface Page:**
- HTML/CSS/JS: `include/camera_index.h` (embedded strings)
- Handler: `src/app_httpd.cpp` (HTTP endpoint registration)
- Data endpoint: `src/app_httpd.cpp` (URI handler)

**Utility Functions:**
- Implementation: `src/debug_utils.cpp` (if debug-related)
- Header: `src/debug_utils.h` (function declarations)
- Standalone: New `src/<name>.cpp/.h` files (if independent)

## Special Directories

**`.pio/libdeps/`:**
- Purpose: PlatformIO-managed library dependencies
- Contains: LoRa library, esp32-camera, ArduinoJson, Adafruit_BMP280, TinyGPSPlus
- Generated: Yes (auto-downloaded by PlatformIO)
- Committed: No (in `.gitignore`)

**`.pio/build/`:**
- Purpose: Compiled build outputs (.o files, .elf, .bin)
- Generated: Yes (compiled by PlatformIO)
- Committed: No (in `.gitignore`)

**`.vscode/`:**
- Purpose: Visual Studio Code IDE configuration
- Contains: workspace settings, extensions recommendations
- Generated: No
- Committed: Yes (for development environment consistency)

**`docs/`:**
- Purpose: Project documentation external to code
- Contains: Implementation plans, protocol specifications, guides
- Generated: No
- Committed: Yes

**`include/`:**
- Purpose: Configuration headers and shared type definitions
- Contains: Pin definitions, protocol structures, compile-time settings
- Generated: No
- Committed: Yes

**`src/`:**
- Purpose: All application source code
- Contains: Manager implementations, entry points, web server
- Generated: No
- Committed: Yes

---

*Structure analysis: 2026-06-08*

# Coding Conventions

**Analysis Date:** 2026-06-28

## Naming Patterns

**Files:**
- Header files (`.h`): Use `snake_case` for source module headers: `sensor_manager.h`, `camera_manager.h`
- Source files (`.cpp`): Match header names: `sensor_manager.cpp`, `camera_manager.cpp`
- Configuration headers in `include/`: Use `snake_case` with descriptive names: `balloon_config.h`, `sensor_pins.h`, `camera_pins.h`
- Main application entry point: `main_balloon.cpp` (not `main.cpp` for balloon firmware)

**Functions:**
- Use `camelCase` for all function names and methods
- Getters use `get` prefix: `getBMP280Data()`, `getGPSData()`, `getStatistics()`
- Setters use `set` prefix: `setSeaLevelPressure()`, `setDebugLevel()`, `setMode()`
- Boolean predicates use `is/has` prefix: `isBMP280Ready()`, `isTimeToCapture()`, `hasValidImage()`, `isWatchdogEnabled()`
- Action methods use imperative verbs: `update()`, `begin()`, `captureImage()`, `resetErrorCounts()`
- Event handlers prefix with `on`: `onSystemEvent()`, `onModeChanged()`, `onEmergencyTriggered()`

**Variables:**
- Member variables use `camelCase`: `currentBMP280Data`, `seaLevelPressure`, `lastUpdateTime`
- Local variables use `camelCase`: `currentTime`, `loopStartTime`, `success`
- Constants use `UPPER_SNAKE_CASE` with `#define`: `DEBUG_SENSORS`, `BMP280_READ_INTERVAL_MS`, `MAX_PACKET_SIZE`
- Static class constants use `UPPER_SNAKE_CASE`: `LOG_BUFFER_SIZE`, `DEFAULT_WATCHDOG_TIMEOUT`
- No `g_` prefix for globals (use static with access functions)

**Types:**
- Struct names use `PascalCase`: `BMP280Data`, `GPSData`, `ImageData`, `PacketHeader`
- Struct suffix `Data` for data structures: `BMP280Data`, `GPSData`, `TelemetryData`
- Enum class names use `PascalCase`: `SystemMode`, `FlightPhase`, `PacketType`, `DebugLevel`
- Enum values use `PascalCase`: `SystemMode::ASCENT`, `DebugLevel::ERROR`, `EventType::SYSTEM_BOOT`
- Class names use `PascalCase`: `SensorManager`, `CameraManager`, `DebugUtils`, `SystemState`

## Code Style

**Formatting:**
- No formal formatter detected (no `.clang-format`, `.prettierrc`)
- 4-space indentation for classes/structs
- Consistent brace style: opening brace on same line for functions/controls
- Section headers use comment banners with `=` borders
- Line length appears to be around 80-120 characters based on visible code

**Linting:**
- No linting configuration detected
- PlatformIO build warnings/errors act as primary quality check
- Build flags include `-DCORE_DEBUG_LEVEL=3` for debug output

**Comment Style:**
- C++ `//` single-line comments preferred
- Section headers use boxed comment style:
  ```cpp
  // ===========================
  // Section Name
  // ===========================
  ```
- File headers use multi-line comment block describing module purpose
- End-of-line comments for field descriptions in structs
- Function-level comments for major operations
- Non-obvious optimizations get explanation comments
- Temporary workarounds marked with `// Note:` comments

## Import Organization

**Order:**
1. Module's own header (if .cpp file)
2. Arduino framework headers (`<Arduino.h>`)
3. C/Arduino standard library headers (`<stdint.h>`, `<stdarg.h>`)
4. Third-party library headers (`<Adafruit_BMP280.h>`, `<TinyGPSPlus.h>`)
5. Project configuration headers (`balloon_config.h`, `sensor_pins.h`)
6. Module headers from `src/` (`sensor_manager.h`, `camera_manager.h`)

**Path Aliases:**
- No path aliases configured
- Headers use relative includes: `#include "../include/debug_utils.h"`
- Source-to-source includes use simple paths: `#include "sensor_manager.h"`

**Include Guards:**
- Traditional guards used: `#ifndef SENSOR_MANAGER_H` / `#define SENSOR_MANAGER_H` / `#endif`
- Guard format: `UPPERCASE_H` matching filename
- End comment includes guard name: `#endif // SENSOR_MANAGER_H`

## Error Handling

**Patterns:**
- Boolean return values for success/failure: `bool begin()`, `bool captureImage()`
- Error count tracking with getter methods: `getBMP280ErrorCount()`, `getCaptureErrorCount()`
- Status struct with `valid` flag: `BMP280Data.valid`, `ImageData.valid`
- Validation functions return false on invalid data: `validateBMP280Data()`, `validatePacket()`
- Exception handling in main loop only: `try { ... } catch (...) { SYS_ERROR(...); }`

**Validation:**
- Range checking in sensor validation: `validateBMP280Data()`, `validateGPSData()`
- NaN checking with `isnan()`
- Null pointer checks before use
- Status queries: `isReady()`, `isBMP280Ready()`, `hasValidImage()`

**Error Recovery:**
- Reset methods available: `resetErrorCounts()`, `resetStatistics()`
- Reinitialization pattern: `bool reinitialize()` on manager classes
- Graceful degradation: subsystems marked inactive on init failure

## Logging

**Framework:** Custom `DebugUtils` class (`src/debug_utils.h`, `src/debug_utils.cpp`)

**Debug Levels:**
- `DebugLevel::NONE` - No output
- `DebugLevel::ERROR` - Critical errors requiring attention
- `DebugLevel::WARNING` - Non-critical issues
- `DebugLevel::INFO` - Normal operational messages
- `DebugLevel::DEBUG` - Detailed debugging info
- `DebugLevel::VERBOSE` - Extensive output for development

**Categories:**
- `DebugCategory::SYSTEM` - System-wide events
- `DebugCategory::SENSORS` - Sensor-related messages
- `DebugCategory::CAMERA` - Camera operations
- `DebugCategory::LORA` - LoRa communication
- `DebugCategory::POWER` - Power management
- `DebugCategory::GPS` - GPS operations
- `DebugCategory::COMMUNICATION` - General communication
- `DebugCategory::STATE` - System state changes
- `DebugCategory::MEMORY` - Memory management
- `DebugCategory::PERFORMANCE` - Performance metrics

**Logging Macros:**
```cpp
// Main logging macros
DEBUG_ERROR(cat, ...)
DEBUG_WARNING(cat, ...)
DEBUG_INFO(cat, ...)
DEBUG_LOG(cat, ...)
DEBUG_VERBOSE(cat, ...)

// Category-specific macros
SYS_ERROR(...)      // System errors
SYS_WARNING(...)    // System warnings
SYS_INFO(...)       // System information
SENSOR_ERROR(...)   // Sensor errors
SENSOR_WARNING(...)
SENSOR_INFO(...)
CAMERA_ERROR(...)   // Camera errors
LORA_ERROR(...)     // LoRa errors
POWER_ERROR(...)    // Power errors
GPS_ERROR(...)      // GPS errors
```

**Conditional Debug Compilation:**
```cpp
#ifndef DEBUG_SENSORS
#define DEBUG_SENSORS true
#endif

if (DEBUG_SENSORS) {
    Serial.println("BMP280: Initialized successfully");
}
```

## Comments

**When to Comment:**
- File/module headers describe module purpose and functionality
- Section banners group related functions
- Complex algorithms get explanatory comments (e.g., barometric altitude formula)
- Hardware-specific notes explain pin mappings and configurations
- Non-obvious optimizations get explanation comments
- Disabled code comments explain why (e.g., `// Method doesn't exist`)

**JSDoc/TSDoc:**
- No formal documentation standard detected
- Function parameters documented in comments near declarations
- Return values documented in comments

## Function Design

**Size:**
- Functions typically kept under 50 lines
- Complex functions broken into smaller helper functions
- Private methods in `private:` section handle implementation details
- Manager classes have many methods (20-40 public methods typical)

**Parameters:**
- Pass by const reference for large objects: `const TelemetryData& data`
- Pass by value for primitives and small structs
- Pointer parameters for output buffers: `uint8_t* data, size_t length`
- Output parameters by reference: `uint16_t& count`, `size_t& paramLength`
- Optional parameters use defaults: `uint32_t timeoutMs = 30000`

**Return Values:**
- `bool` for success/failure operations
- `void` for procedures with no meaningful return
- Data structs for getters: `BMP280Data getBMP280Data() const`
- Pointers/references for buffer access: `ImageData& getCurrentImage()`
- Const correctness: Getter methods marked `const`

## Module Design

**Exports:**
- Header files contain public interface
- Implementation in corresponding .cpp file
- Global instance access via reference-returning functions:
  ```cpp
  SensorManager& Sensors();
  CameraManager& Camera();
  PacketHandler& PacketMgr();
  PowerManager& PowerMgr();
  SystemState& SysState();
  DebugUtils& Debug;
  ```

**Barrel Files:**
- Not used
- Each module has its own header file
- Common types in `include/common_types.h`
- Pin definitions in `include/sensor_pins.h`, `include/camera_pins.h`

**Global Instance Pattern:**
- Singleton-style accessor function: `Sensors()`, `Camera()`, `Debug`, `SysState()`, `PacketMgr()`
- Instance defined in `src/balloon_instances.cpp`
- Reference return: `SensorManager& Sensors()` (not pointer)

**Initialization Pattern:**
All manager classes follow same lifecycle:
1. Constructor initializes member variables to safe defaults
2. `begin()` performs hardware initialization
3. `end()` performs cleanup
4. `update()` called from main loop for periodic work

**Class Organization:**
- Public interface first: constructors, lifecycle, main operations
- Getters/setters grouped together
- Private section at bottom with member variables and helper methods
- Static constants at end of class definition

## Memory Management

**Patterns:**
- Manual `malloc`/`free` for dynamic buffers
- Ownership transfer documented in comments
- RAII NOT consistently used (embedded constraints)
- Buffer cleanup in destructors and `end()` methods
- Memory validation checks before allocation

**Buffer Management:**
- Fixed-size arrays for queues: `packetQueue[16]`
- Circular buffers for logging: `logBuffer[500]`
- Dynamic allocation for images: `currentImage.buffer = (uint8_t*)malloc(size)`
- Error handling on allocation failure

## Hardware Abstraction

**Pin Definitions:**
- Centralized in `include/sensor_pins.h` and `include/camera_pins.h`
- Use `#define` for pin numbers
- Pin validation comments to avoid conflicts
- Hardware-specific settings in same files

**Sensor Interfaces:**
- Abstracted through manager classes
- Validation of sensor data before use
- Error counting for failed reads
- Configurable update intervals

**Configuration:**
- Device type selection in `balloon_config.h`:
  ```cpp
  #define DEVICE_BALLOON      1
  #define DEVICE_BASE_STATION 2
  #define DEVICE_TYPE DEVICE_BALLOON
  ```
- Feature flags using `#define`: `ENABLE_DEEP_SLEEP`, `DEBUG_SENSORS`
- Compile-time configuration via `platformio.ini` build flags

## Platform-Specific Patterns

**ESP32/Arduino Framework:**
- `setup()` and `loop()` functions in main application
- Use of Arduino APIs: `Serial.begin()`, `Wire.begin()`, `digitalRead()`
- ESP32-specific APIs: `ESP.getFreeHeap()`, `ESP.getCpuFreqMHz()`
- millis()-based timing for non-blocking delays

**Build System:**
- PlatformIO configuration in `platformio.ini`
- Multiple environments defined (devkit vs balloon)
- Build flags for firmware version and device type
- Library dependencies managed through PlatformIO

---

*Convention analysis: 2026-06-28*

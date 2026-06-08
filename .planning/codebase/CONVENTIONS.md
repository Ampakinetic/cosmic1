# Coding Conventions

**Analysis Date:** 2026-06-08

## Naming Patterns

**Files:**
- Snake_case for all source files: `sensor_manager.cpp`, `debug_utils.h`
- Header files use `.h` extension for both C and C++ headers
- Source files use `.cpp` extension for C++ implementation files
- Configuration headers in `include/` directory: `balloon_config.h`, `sensor_pins.h`
- Main application entry point: `main_balloon.cpp` (not `main.cpp` for balloon firmware)

**Functions:**
- camelCase for all function names: `updateSystemState()`, `processSensors()`, `getCurrentImage()`
- Getter functions prefix with `get`: `getBMP280Data()`, `getGPSData()`, `getStatistics()`
- Setter functions prefix with `set`: `setDebugLevel()`, `setMode()`, `setFrequency()`
- Boolean query functions use `is`: `isBMP280Ready()`, `isTimeToCapture()`, `isValid()`
- Event handlers prefix with `on`: `onSystemEvent()`, `onModeChanged()`, `onEmergencyTriggered()`

**Variables:**
- camelCase for local and member variables: `currentMode`, `lastUpdateTime`, `batteryVoltage`
- Prefix `g_` NOT used for globals (use static with access functions)
- Constants use UPPER_SNAKE_CASE: `MAX_PACKET_SIZE`, `DEFAULT_DEBUG_LEVEL`, `LORA_FREQUENCY`
- Macro definitions use UPPER_SNAKE_CASE: `DEBUG_SENSORS`, `BMP280_ADDRESS`
- Enum class names use PascalCase: `SystemMode`, `FlightPhase`, `DebugLevel`

**Types:**
- PascalCase for class/struct names: `SensorManager`, `CameraManager`, `PacketHandler`
- Struct suffix `Data` for data structures: `BMP280Data`, `GPSData`, `TelemetryData`
- Enum class values use PascalCase: `SystemMode::ASCENT`, `DebugLevel::INFO`
- Typedefs/using aliases follow PascalCase convention

## Code Style

**Formatting:**
- No explicit formatter configuration detected (no `.clang-format`, `.editorconfig`)
- Indentation: 4 spaces (inferred from code structure)
- Line length: No strict limit observed, generally kept under 120 characters
- Brace style: K&R style - opening brace on same line for functions/controls
- Spacing: Spaces around operators, after commas, in function parameter lists

**Linting:**
- No formal linting configuration detected
- PlatformIO build warnings used for error detection
- `monitor_filters = esp32_exception_decoder` in `platformio.ini` for runtime debugging

**Comment Style:**
- C++ `//` single-line comments preferred
- Section headers use banner-style comments with asterisks
- File headers describe module purpose
- Function grouping marked with section comment banners

## Import Organization

**Order:**
1. Module's own header (if .cpp file)
2. Arduino framework headers (`<Arduino.h>`)
3. ESP32/PlatformIO specific headers
4. Project include headers (relative path in quotes)
5. Third-party library headers

**Path Aliases:**
- No path aliases configured
- Relative includes used: `#include "balloon_config.h"`
- Platform-specific headers: `#include "sensor_pins.h"` (in include/ directory)

**Include Guard Pattern:**
```cpp
#ifndef FILENAME_H
#define FILENAME_H

// Content

#endif // FILENAME_H
```

## Error Handling

**Patterns:**
- Boolean return values for success/failure: `bool begin()`, `bool captureImage()`
- Error count tracking in managers: `bmp280ErrorCount`, `captureErrorCount`, `transmitErrorCount`
- Validation functions return false on invalid data: `validateBMP280Data()`, `validatePacket()`
- Serial output for debug/warning/error messages
- Try-catch NOT used (embedded C++ constraints)

**Error Categories:**
- Initialization failures: Return false, log to Serial
- Sensor read failures: Increment error count, mark data invalid
- Communication failures: Increment error counters, retry logic
- Critical failures: Emergency mode activation in `SystemState`

**Logging Hierarchy:**
```cpp
SYS_ERROR("Hardware initialization failed");       // System-level errors
DEBUG_ERROR(SENSORS, "Sensor read failed");        // Categorized errors
SYS_WARNING("Some hardware issues detected");      // Warnings
SYS_INFO("System initialization complete");       // Information
```

## Logging

**Framework:** Custom `DebugUtils` class in `src/debug_utils.cpp` and `include/debug_utils.h`

**Debug Levels:**
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

**Patterns:**
```cpp
// Category-specific macros (defined in debug_utils.h)
SYS_ERROR("System error: %s", error);
SENSOR_INFO("BMP280: P=%.2fPa, T=%.2f°C", pressure, temperature);
LORA_LOG("Packet transmitted, sequence: %d", sequenceNumber);
POWER_WARNING("Low battery: %d%%", percentage);

// Direct debug instance usage
Debug.logError(DebugCategory::SYSTEM, __FUNCTION__, __LINE__, "Format", args);
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
- File headers describe module purpose and functionality
- Section banners group related functions
- Complex algorithms get explanatory comments
- Hardware-specific notes explain pin mappings and configurations
- Temporary workarounds marked with `// Note:` comments
- Non-obvious optimizations get explanation comments

**JSDoc/TSDoc:**
- Not used (C++ project)
- Function parameters documented in comments near declarations
- Return values documented in comments

**Comment Banner Pattern:**
```cpp
// ===========================
// Section Name
// ===========================
```

## Function Design

**Size:**
- Functions typically kept under 50 lines
- Complex functions broken into smaller helper functions
- Private methods in `private:` section handle implementation details

**Parameters:**
- Pass by const reference for large objects: `const TelemetryData& data`
- Pass by value for primitives and small structs
- Pointer parameters for output buffers: `uint8_t* buffer, size_t& length`
- Optional parameters use defaults: `bool begin()`, `uint32_t timeoutMs = 30000`

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
  LoRaManager& LoRaComm();
  PowerManager& PowerMgr();
  SystemState& SysState();
  ```

**Barrel Files:**
- Not used
- Each module has its own header file
- Common types in `include/common_types.h`
- Pin definitions in `include/sensor_pins.h`, `include/camera_pins.h`

**Global Instances:**
- Defined in `src/balloon_instances.cpp` (file-level static)
- Accessed via accessor functions to avoid linking issues
- Pattern: static instance + reference-returning function

**Initialization Pattern:**
```cpp
// Constructor
ClassName::ClassName() {
    // Initialize member variables
}

// Begin method (called in setup)
bool ClassName::begin() {
    // Hardware initialization
    return true;
}

// End method
void ClassName::end() {
    // Cleanup
}
```

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
- ESP32-specific APIs: `ESP.getFreeHeap()`, `setCpuFrequencyMhz()`
- millis()-based timing for non-blocking delays

**Build System:**
- PlatformIO configuration in `platformio.ini`
- Multiple environments defined (devkit vs balloon)
- Build flags for firmware version and device type
- Library dependencies managed through PlatformIO

---

*Convention analysis: 2026-06-08*

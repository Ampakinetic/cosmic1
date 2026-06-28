# Testing Patterns

**Analysis Date:** 2026-06-28

## Test Framework

**Runner:**
- PlatformIO Native Test Framework (Unity-based, available but not configured)
- No dedicated test environment in `platformio.ini`
- No external test runner configured
- Testing currently hardware-based/manual

**Assertion Library:**
- PlatformIO's built-in Unity test framework (available but not used)
- No custom assertion wrappers detected

**Run Commands:**
```bash
# No test commands currently configured
# PlatformIO testing would use:
pio test                    # Run all tests
pio test -e esp32-s3-balloon    # Run tests for specific environment
```

## Test File Organization

**Location:**
- Not applicable (no test directory structure present)
- Would typically be `test/` directory in PlatformIO project structure

**Naming:**
- Not applicable (no test files present)
- PlatformIO convention: `test_*.cpp` or `*_test.cpp`

**Structure:**
```
[Typical PlatformIO test structure - not present]
test/
├── test_sensor_manager.cpp
├── test_packet_handler.cpp
└── test_main.cpp
```

## Test Structure

**Suite Organization:**
- Not applicable (no tests present)
- Would use Unity framework structure if implemented:
  ```cpp
  void setUp(void) { }
  void tearDown(void) { }
  
  void test_function_name(void) {
      TEST_ASSERT_EQUAL(expected, actual);
  }
  ```

**Patterns:**
- No test setup/teardown patterns detected
- No fixture patterns observed

## Mocking

**Framework:** None detected

**Patterns:**
- No mocking framework used
- Hardware dependencies not abstracted for testing
- Real hardware used for validation (in-field testing)

**What to Mock:**
- Sensors (BMP280, GPS) for data-independent testing
- LoRa communication for packet handling tests
- Camera for image processing tests
- Power management for state machine tests

**What NOT to Mock:**
- Packet serialization/deserialization logic
- CRC calculation algorithms
- Data structure validation
- State transition logic

## Fixtures and Factories

**Test Data:**
- No test fixtures detected
- Hard-coded test values in validation functions:
  ```cpp
  // From src/sensor_manager.cpp
  if (pressure < 30000.0f || pressure > 120000.0f) {
      return false;  // Pressure range validation
  }
  if (temperature < -40.0f || temperature > 85.0f) {
      return false;  // Temperature range validation
  }
  ```

**Location:**
- No dedicated test data directory
- Configuration constants serve as fixture-like data:
  - `MAX_PACKET_SIZE`
  - `DEFAULT_DEBUG_LEVEL`
  - `LORA_FREQUENCY`

## Coverage

**Requirements:** None enforced

**View Coverage:**
```bash
# No coverage configured
# PlatformIO coverage would require:
pio test --coverage
```

**Current Coverage Estimate:**
- 0% automated test coverage
- Manual/hardware testing only
- Runtime validation through Serial output

## Test Types

**Unit Tests:**
- Not implemented
- Would test individual class methods in isolation
- Would mock hardware dependencies

**Integration Tests:**
- Limited to hardware integration
- Manual testing via Serial monitor
- System validation through field testing

**E2E Tests:**
- Hardware-in-the-loop testing approach
- Field testing with actual balloon flights
- Manual verification of communication range
- Power consumption measured in real conditions

**Hardware-Specific Testing:**
- Serial monitor debugging with `esp32_exception_decoder`
- LED status indicators for visual debugging
- Real-time Serial output for system state validation

## Common Patterns

**Async Testing:**
- Not applicable (no async tests)
- Main loop handles asynchronous operations:
  ```cpp
  void loop() {
      if (millis() - lastUpdate >= interval) {
          update();
      }
  }
  ```

**Error Testing:**
- Manual validation through error counters
- Serial output indicates error conditions:
  ```cpp
  SYS_ERROR("Hardware initialization failed");
  SENSOR_ERROR("BMP280: Could not find sensor at 0x76");
  ```

**Validation Testing:**
- Built-in validation functions in managers:
  ```cpp
  bool validateBMP280Data(float pressure, float temperature);
  bool validatePacket(const uint8_t* packet, size_t length);
  bool validateSystemState() const;
  ```

## Hardware Testing Approaches

**Serial Monitor Debugging:**
- Primary debugging interface
- Configured in `platformio.ini`:
  ```ini
  monitor_speed = 115200
  monitor_filters = esp32_exception_decoder
  ```
- Debug levels controlled via `DEBUG_*` macros

**Status LED Indicators:**
- Visual feedback for system state:
  ```cpp
  #define LED_GPS_LOCK_PIN  38  // GPS Lock Status
  #define LED_LORA_TX_PIN   39  // LoRa Transmit Status
  #define LED_ERROR_PIN     40  // Error Status
  ```
- LED blink patterns defined in configuration:
  ```cpp
  #define LED_PATTERN_GPS_LOCK        1000, 1000   // Slow blink
  #define LED_PATTERN_LORA_TX         100, 900     // Quick blink
  #define LED_PATTERN_ERROR           200, 200     // Fast blink
  ```

**In-Field Testing:**
- Real-world balloon flights
- Communication range testing
- Battery life validation
- Temperature/altitude testing

**Power Measurement:**
- Current consumption monitoring via ADC
- Battery voltage tracking:
  ```cpp
  // From src/power_manager.cpp
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  ```

## Debug-Enabled Testing

**Conditional Compilation:**
- Debug features controlled by preprocessor directives in `balloon_config.h`:
  ```cpp
  #ifndef DEBUG
  #define DEBUG_SERIAL              true
  #define DEBUG_SENSORS             true
  #define DEBUG_GPS                 true
  #define DEBUG_LORA                true
  #define DEBUG_CAMERA              true
  #define DEBUG_POWER               true
  #define DEBUG_PACKETS             true
  #else
  #define DEBUG_SERIAL              false
  #define DEBUG_SENSORS             false
  #define DEBUG_GPS                 false
  #define DEBUG_LORA                false
  #define DEBUG_CAMERA              false
  #define DEBUG_POWER               false
  #define DEBUG_PACKETS             false
  #endif
  ```

## Manual Testing Procedures

**Power-On Test:**
- Verify LED indicators activate
- Check Serial output for initialization messages
- Confirm all subsystems initialize

**Sensor Test:**
- Monitor Serial for sensor readings
- Verify GPS lock acquisition
- Check BMP280 pressure/temperature output

**Communication Test:**
- Verify LoRa packet transmission
- Check for ACK responses
- Monitor RSSI/SNR values

**Integration Test:**
- Field test with actual balloon hardware
- Range testing for LoRa communication
- Battery life measurement

## Testing Infrastructure Gaps

**Missing Components:**
- No unit test framework integration
- No automated test execution
- No hardware mocking layer
- No CI/CD testing pipeline
- No code coverage measurement

**Recommended Additions:**
- PlatformIO test environment configuration
- Unity framework setup for unit tests
- Mock hardware interfaces for sensor testing
- Automated test execution in CI
- Integration tests for packet handling

## Runtime Validation

**Health Checks:**
- System health monitoring in `SystemState::performHealthCheck()`
- Subsystem status tracking
- Memory usage monitoring
- Error count tracking

**Statistics Tracking:**
- Packet send/receive counters
- Error rate calculation:
  ```cpp
  // From src/packet_handler.h
  float getPacketLossRate() const;
  uint32_t getPacketsSent() const;
  uint32_t getPacketsReceived() const;
  uint32_t getPacketsDropped() const;
  ```

**Performance Metrics:**
- Loop time tracking in `DebugUtils`
- Free heap monitoring
- CPU temperature tracking:
  ```cpp
  // From include/common_types.h
  struct TelemetryData {
      // ...
      uint16_t freeHeap;
      float cpuTemperature;
      // ...
  };
  ```

## Validation Patterns

**Data Validation:**
- Sensor data range checking:
  ```cpp
  // From src/sensor_manager.cpp
  if (pressure < 30000.0f || pressure > 120000.0f) {
      return false;  // Invalid pressure
  }
  if (temperature < -40.0f || temperature > 85.0f) {
      return false;  // Invalid temperature
  }
  ```
- NaN checking for sensor readings:
  ```cpp
  if (isnan(pressure) || isnan(temperature)) {
      return false;
  }
  ```

**Packet Validation:**
- CRC verification for received packets
- Start/end byte checking:
  ```cpp
  // From src/packet_handler.h
  #define PACKET_START_BYTE1     0xAA
  #define PACKET_START_BYTE2     0x55
  #define PACKET_END_BYTE1       0x0D
  #define PACKET_END_BYTE2       0x0A
  ```

**State Validation:**
- Mode transition validation in `SystemState::isModeTransitionAllowed()`
- Phase transition rules enforced
- Emergency condition detection:
  ```cpp
  // From src/system_state.h
  bool detectEmergencyConditions();
  bool executeEmergencyProtocol();
  ```

---

*Testing analysis: 2026-06-28*

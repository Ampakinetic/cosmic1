# Testing Patterns

**Analysis Date:** 2026-06-08

## Test Framework

**Runner:**
- PlatformIO Native Test Framework (Unity-based)
- No dedicated test configuration detected (no `platformio.ini` test environment)
- No external test runner configured

**Assertion Library:**
- PlatformIO's built-in Unity test framework (available but not configured)
- No custom assertion wrappers detected

**Run Commands:**
```bash
# No test commands configured
# PlatformIO testing would use:
pio test                    # Run all tests
pio test -e esp32-s3-balloon    # Run tests for specific environment
```

**Current Status:**
- No test files detected in the project
- No test configuration in `platformio.ini`
- Testing appears to be hardware-based/manual

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
- Would use Unity framework structure if implemented

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
- Hard-coded test values in some validation functions:
  ```cpp
  // From sensor_manager.cpp
  if (pressure < 30000.0f || pressure > 120000.0f) {
      return false;  // Pressure range validation
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
  DEBUG_ERROR(SENSORS, "Sensor read failed");
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

**In-Field Testing:**
- Real-world balloon flights
- Communication range testing
- Battery life validation
- Temperature/altitude testing

**Power Measurement:**
- Current consumption monitoring via ADC
- Battery voltage tracking:
  ```cpp
  float voltage = (rawValue / 4095.0f) * 3.3f * 2.0f;  // Voltage divider
  ```

## Debug-Enabled Testing

**Conditional Compilation:**
- Debug features controlled by preprocessor directives:
  ```cpp
  #ifndef DEBUG_SENSORS
  #define DEBUG_SENSORS true
  #endif

  if (DEBUG_SENSORS) {
      Serial.println("BMP280: Initialized successfully");
  }
  ```

**Debug Macros:**
- Category-based debug output:
  ```cpp
  #define DEBUG_SERIAL true
  #define DEBUG_SENSORS true
  #define DEBUG_GPS true
  #define DEBUG_LORA true
  #define DEBUG_CAMERA true
  #define DEBUG_POWER true
  #define DEBUG_PACKETS true
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
  float getPacketLossRate() const {
      uint32_t total = packetsSent + packetsDropped;
      return (static_cast<float>(packetsDropped) / total) * 100.0f;
  }
  ```

**Performance Metrics:**
- Loop time tracking in `DebugUtils`
- Free heap monitoring
- CPU temperature tracking

## Validation Patterns

**Data Validation:**
- Sensor data range checking:
  ```cpp
  if (pressure < 30000.0f || pressure > 120000.0f) {
      return false;  // Invalid pressure
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
  if (packet[0] != PACKET_START_BYTE1 || 
      packet[1] != PACKET_START_BYTE2) {
      return false;
  }
  ```

**State Validation:**
- Mode transition validation in `SystemState`
- Phase transition rules enforced
- Emergency condition detection

---

*Testing analysis: 2026-06-08*

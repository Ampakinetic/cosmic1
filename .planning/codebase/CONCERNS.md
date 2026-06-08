# Codebase Concerns

**Analysis Date:** 2026-06-08

## Tech Debt

### Memory Management in Packet Handler
**Issue:** Unsafe dynamic memory allocation without proper error handling
- Files: `src/packet_handler.cpp`, `src/packet_handler.h`
- Impact: Memory leaks and potential heap fragmentation over time
- Details:
  - `assemblePacket()` allocates with `malloc()` (line 634) but may fail silently
  - `disassemblePacket()` allocates payload memory (line 683) without caller cleanup contract
  - No clear ownership model for allocated memory
  - Packet queue management with manual memory tracking
- Fix approach:
  - Use fixed-size pools or pre-allocated buffers
  - Implement RAII-style resource management
  - Add memory leak detection in debug builds
  - Document ownership contracts for all allocations

### Hardcoded Battery Voltage
**Issue:** Battery level hardcoded instead of reading actual value
- Files: `src/lora_comm.cpp:829`
- Impact: Battery monitoring reports incorrect values, power management unreliable
- Current code: `packet.header.batteryLevel = 330;  // TODO: Get actual battery voltage`
- Fix approach:
  - Integrate with PowerManager's `getBatteryVoltage()` method
  - Add voltage-to-percentage conversion
  - Implement actual ADC reading in PowerManager

### Incomplete Persistence Layer
**Issue:** State persistence functions return true without actually saving data
- Files: `src/system_state.cpp:1027-1044`
- Impact: System state not preserved across reboots, configuration lost on power cycle
- Functions affected:
  - `saveState()` - "Placeholder for NVS save functionality"
  - `loadState()` - "Placeholder for NVS load functionality" 
  - `saveStatistics()` - "Placeholder for statistics save"
  - `loadStatistics()` - "Placeholder for statistics load"
- Fix approach:
  - Implement ESP32 NVS (Non-Volatile Storage) integration
  - Use Preferences library for key-value storage
  - Add version migration for stored data structures
  - Implement backup/recovery mechanism

### Pin Conflict Resolution
**Issue:** Power control pins removed due to conflicts, sensors always on
- Files: `include/sensor_pins.h:40`
- Impact: Cannot implement power saving, sensors draw power continuously
- Current state: `// POWER_ENABLE_PIN removed due to pin conflicts - sensors always on`
- Fix approach:
  - Re-evaluate pin assignment with complete pin audit
  - Consider using I2C GPIO expander for additional control lines
  - Implement software-based power cycling over I2C where possible

## Known Bugs

### I2C Bus Initialization Conflict
**Symptoms:** "Bus already started in Master Mode" warnings, potential crashes
- Files: `src/sensor_manager.cpp:83`, `docs/DEBUG_CRASH_SOLUTION.md`
- Trigger: Multiple initialization calls from Adafruit_BMP280 library
- Workaround: Initialize Wire before creating BMP280 object, pass &Wire to constructor
- Status: Documented as fixed but shows fragility in initialization order

### WiFi Connection Failures
**Symptoms:** NO_AP_FOUND error (201), connection failures
- Files: `docs/WiFi_Troubleshooting.md`
- Trigger: Various causes - network visibility, credentials, 2.4GHz band
- Workaround: Access point fallback mode, network scanning
- Impact: Base station functionality limited without reliable WiFi

### GPS Not Locking
**Symptoms:** GPS fails to acquire satellite lock, no position data
- Files: `PHASE1_HARDWARE_INTEGRATION.md:117-119`
- Trigger: Antenna placement, cold start time, insufficient satellites
- Workaround: Allow sufficient time for cold start, verify antenna placement
- Impact: Cannot track balloon position without GPS fix

## Security Considerations

### WiFi Credentials in Source
**Risk:** WiFi credentials stored in plaintext configuration files
- Files: `include/wifi_config.h`
- Impact: Credentials exposed in source code repository
- Current mitigation: None documented
- Recommendations:
  - Use NVS for encrypted credential storage
  - Implement secure provisioning via captive portal
  - Add compile-time separation from credentials
  - Document credential management workflow

### Unencrypted LoRa Communication
**Risk:** LoRa packets transmitted without encryption
- Files: `src/lora_comm.cpp`, `docs/COMMUNICATION_PROTOCOL.md`
- Impact: Position and telemetry data visible to any receiver
- Current mitigation: None
- Recommendations:
  - Implement AES encryption for packet payloads
  - Add message authentication codes (MAC)
  - Consider frequency hopping if regulations allow
  - Document security model and threat assessment

### No Firmware Signature Verification
**Risk:** No OTA signature verification, no secure boot
- Files: N/A (feature not implemented)
- Impact: Vulnerable to malicious firmware updates
- Recommendations:
  - Enable ESP32 secure boot if supported
  - Implement signature verification for OTA updates
  - Add rollback mechanism for corrupted updates

## Performance Bottlenecks

### Blocking LoRa Transmissions
**Problem:** LoRa transmission blocks main loop for extended periods
- Files: `src/lora_comm.cpp`
- Impact: Missed sensor readings, delayed response to events
- Cause: Synchronous packet transmission without timeout
- Improvement path:
  - Implement interrupt-driven transmission
  - Use non-blocking state machines
  - Add transmission queue with background processing
  - Implement priority-based preemption

### Camera Image Processing
**Problem:** JPEG capture and thumbnail creation block execution
- Files: `src/camera_manager.cpp`
- Impact: Main loop blocked during image operations (potentially seconds)
- Cause: Synchronous image capture and compression
- Improvement path:
  - Offload to separate core if available
  - Implement progressive capture with yield()
  - Use lower resolution/thumbnails for real-time needs
  - Queue image operations for background processing

### Debug Output Overhead
**Problem:** Extensive debug logging in production builds
- Files: `src/debug_utils.h`, Multiple source files
- Impact: Serial output slows main loop, buffer overflows possible
- Cause: Debug macros enabled in production, high-volume logging
- Improvement path:
  - Ensure debug builds are separate from release
  - Add log rate limiting
  - Implement circular buffer with conditional flush
  - Use compile-time switches with proper defaults

### Main Loop Complexity
**Problem:** Main loop at 10Hz with numerous subsystem updates
**Files:** `src/main_balloon.cpp`
- Impact: Jitter in timing, potential missed deadlines
- Cause: Sequential execution without priority scheduling
- Improvement path:
  - Implement task scheduler with priorities
  - Use FreeRTOS or similar for task management
  - Separate time-critical from background tasks
  - Add watchdog timer for loop duration

## Fragile Areas

### System State Machine
**Files:** `src/system_state.cpp`, `src/system_state.h`
- Why fragile: Complex state transition logic, 1045 lines of code
- Safe modification:
  - Only modify through `setMode()` and `setFlightPhase()` methods
  - Test all transition paths before deploying
  - Use state diagram to validate transitions
  - Add unit tests for state machine logic
- Test coverage: No automated tests found, relies on manual testing

### Sensor Initialization Sequence
**Files:** `src/sensor_manager.cpp:78-100`
- Why fragile: I2C initialization order-dependent, known conflicts with BMP280 library
- Safe modification:
  - Always initialize I2C before creating sensor objects
  - Add delays between initialization steps
  - Verify each sensor initialization before proceeding
  - Handle sensor initialization failures gracefully
- Test coverage: Manual testing only

### Power Management State Transitions
**Files:** `src/power_manager.cpp`, `src/power_manager.h`
- Why fragile: Power state changes affect all subsystems, no validation
- Safe modification:
  - Test power state transitions with all subsystems active
  - Verify voltage thresholds match hardware characteristics
  - Test emergency shutdown procedures
  - Validate sleep/wake cycles
- Test coverage: Limited testing documented

### Packet Handler Memory
**Files:** `src/packet_handler.cpp`, `src/packet_handler.h`
- Why fragile: Manual memory management, complex buffer operations
- Safe modification:
  - Test with packet sizes approaching maximum
  - Verify no memory leaks over extended operation
  - Test fragmentation and reassembly edge cases
  - Monitor heap usage during testing
- Test coverage: No memory leak testing identified

## Scaling Limits

### Memory Constraints
**Current capacity:**
- ESP32-S3 with PSRAM: ~8MB PSRAM + 512KB SRAM
- Current usage: Not monitored in production
- Packet buffers: Fixed-size queues with manual limits
- Image buffers: Single image storage in memory

**Limit:** Multiple image storage or complex buffering will exhaust memory
**Scaling path:**
- Implement image streaming instead of full storage
- Use PSRAM for large buffers only
- Add memory monitoring and limits enforcement
- Consider external storage options (SD card)

### LoRa Bandwidth Limitations
**Current capacity:**
- Maximum packet size: 240 bytes
- Spreading factor SF7: ~6 kbps effective throughput
- Transmission interval: 10 seconds default

**Limit:** Cannot transmit full-resolution images at high frequency
**Scaling path:**
- Implement image compression
- Use adaptive spreading factor based on link quality
- Prioritize telemetry over images when bandwidth limited
- Consider image chunking with selective transmission

### Battery Life Constraints
**Current capacity:**
- 2000mAh battery (specified)
- Expected lifetime: 8+ hours (design target)
- Current consumption: ~361mA idle + peaks

**Limit:** Cannot extend flight beyond 8 hours without larger battery
**Scaling path:**
- Implement aggressive power saving in low-battery states
- Reduce camera frequency and resolution
- Optimize transmission schedule
- Consider solar charging for extended missions

### Storage Limitations
**Current capacity:** No persistent storage implemented
**Limit:** Cannot store flight logs or images locally
**Scaling path:**
- Implement SPIFFS or LittleFS for log storage
- Add SD card support for image archival
- Implement circular buffers for telemetry logging
- Add data export functionality

## Dependencies at Risk

### Adafruit BMP280 Library
**Risk:** Internal Wire.begin() calls cause I2C conflicts
- Impact: I2C bus initialization failures
- Migration plan:
  - Fork library with custom Wire handling
  - Implement raw I2C communication for BMP280
  - Use alternative BMP280 library without Wire conflicts

### ESP32 Camera Library
**Risk:** Sensor_t type conflicts with other sensor libraries
- Files: `src/camera_manager.h:6-7`
- Impact: Must include camera headers before any other sensor headers
- Migration plan:
  - Document include order requirements
  - Wrap camera includes in isolation layer
  - Consider alternative camera library if conflicts persist

### LoRa Library
**Risk:** Single library version, no fallback options
- Files: PlatformIO lib_deps `sandeepmistry/LoRa@^0.8.0`
- Impact: No alternative if library has bugs or compatibility issues
- Migration plan:
  - Test RadioLib as alternative LoRa library
  - Implement abstraction layer for LoRa operations
  - Document SX1276 register-level fallback

### TinyGPSPlus Library
**Risk:** Fixed to version 1.0.3, may have parsing issues
- Files: PlatformIO lib_deps `mikalhart/TinyGPSPlus@^1.0.3`
- Impact: GPS data parsing failures, incorrect position data
- Migration plan:
  - Add GPS data validation after parsing
  - Implement custom NMEA parser as fallback
  - Test with various GPS modules and conditions

## Missing Critical Features

### Error Recovery Mechanisms
**Problem:** Limited automatic recovery from sensor or communication failures
- Blocks: Long-term autonomous operation, reliable balloon tracking
- Status: Some placeholder implementations in system_state.cpp
- Requirements:
  - Automatic sensor reinitialization on failure
  - LoRa link recovery with adaptive parameters
  - GPS hot-restart after signal loss
  - Camera recovery after capture failures

### Watchdog Timer Implementation
**Problem:** No watchdog timer to detect and recover from hangs
- Blocks: Reliable operation over extended periods
- Status: `DEBUG_WATCHDOG_ENABLED` defined but set to false
- Requirements:
  - Enable ESP32 watchdog timer
  - Add watchdog kick in main loop
  - Implement safe shutdown on watchdog trigger
  - Log watchdog events for debugging

### Over-The-Air (OTA) Updates
**Problem:** No OTA update capability for deployed systems
- Blocks: Firmware updates without physical access
- Status: Not implemented
- Requirements:
  - Implement secure OTA update mechanism
  - Add rollback capability for failed updates
  - Implement version checking and validation
  - Add update verification and testing

### Data Logging to Persistent Storage
**Problem:** No flight data logging, all data lost on power cycle
- Blocks: Post-flight analysis, debugging, mission review
- Status: Save/load functions are placeholders
- Requirements:
  - Implement file system (SPIFFS/LittleFS/SD card)
  - Add structured logging with timestamps
  - Implement log rotation and size management
  - Add data export functionality

### Battery Voltage Monitoring
**Problem:** Battery voltage hardcoded, not actually measured
- Blocks: Accurate power management, low-battery warnings
- Status: TODO comment in lora_comm.cpp:829
- Requirements:
  - Implement ADC-based voltage reading
  - Add voltage divider if needed for measurement range
  - Calibrate ADC readings
  - Implement battery percentage calculation

## Test Coverage Gaps

### No Unit Tests
**What's not tested:** All individual module functions
- Files: All source files lack corresponding test files
- Risk: Logic errors only caught during integration or field testing
- Priority: High
- Recommendations:
  - Add unit test framework (PlatformIO supports Unity)
  - Test critical functions: CRC calculation, packet parsing, state transitions
  - Mock hardware dependencies for isolated testing
  - Implement continuous integration testing

### No Integration Tests
**What's not tested:** Interactions between subsystems
- Risk: Integration issues discovered late, difficult to debug
- Priority: High
- Recommendations:
  - Test sensor manager with LoRa communication
  - Test power management with all subsystems active
  - Test state machine transitions with all modes
  - Test packet handling with real hardware loopback

### No Hardware-in-Loop Tests
**What's not tested:** Real hardware behavior, timing constraints
- Risk: Timing issues, race conditions, hardware-specific bugs
- Priority: Medium
- Recommendations:
  - Implement automated hardware testing fixtures
  - Test with actual sensors and LoRa modules
  - Validate timing constraints on real hardware
  - Test power consumption under various conditions

### No Performance Tests
**What's not tested:** Memory usage, loop timing, power consumption
- Risk: Performance regressions, resource exhaustion
- Priority: Medium
- Recommendations:
  - Add memory usage monitoring and limits
  - Test main loop timing under all conditions
  - Measure power consumption in all states
  - Test worst-case scenarios (all sensors active, transmission, etc.)

### No Field Testing Protocol
**What's not tested:** Real-world conditions, environmental factors
- Risk: Field failures, unexpected behavior in actual use
- Priority: High
- Recommendations:
  - Design field testing protocol for balloon missions
  - Test in various weather conditions
  - Test with various GPS and signal conditions
  - Document and analyze field test results

### No Regression Testing
**What's not tested:** Code changes don't break existing functionality
- Risk: Bugs introduced during feature development
- Priority: Medium
- Recommendations:
  - Implement automated regression test suite
  - Add tests for known bugs and fixes
  - Document expected behavior for all features
  - Run regression tests before commits

---

*Concerns audit: 2026-06-08*

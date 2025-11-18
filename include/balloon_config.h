#ifndef BALLOON_CONFIG_H
#define BALLOON_CONFIG_H

// ===========================
// Balloon-Specific Configuration
// ESP32-S3 Balloon Project
// ===========================

#include "sensor_pins.h"
#include "camera_pins.h"

// ===========================
// Device Type Selection
// ===========================
#define DEVICE_BALLOON      1
#define DEVICE_BASE_STATION 2

// Uncomment the device type you're building:
#define DEVICE_TYPE DEVICE_BALLOON
// #define DEVICE_TYPE DEVICE_BASE_STATION

// ===========================
// Balloon Operation Modes
// ===========================

// Power Management
#define ENABLE_DEEP_SLEEP          true
#define SLEEP_DURATION_SEC         30    // Sleep between sensor readings
#define MAX_AWAKE_TIME_MS          5000  // Maximum awake time per cycle
#define BATTERY_LOW_THRESHOLD      3.3   // Volts - below this, enable power saving
#define BATTERY_CRITICAL_THRESHOLD 3.0   // Volts - below this, emergency mode

// Sensor Reading Intervals
#define BMP280_READ_INTERVAL_MS    1000  // Read pressure/temp every 1 second
#define GPS_READ_INTERVAL_MS       2000  // Read GPS every 2 seconds
#define CAMERA_CAPTURE_INTERVAL_MS 30000 // Capture image every 30 seconds
#define LORA_TRANSMIT_INTERVAL_MS  10000 // Transmit data every 10 seconds

// Camera Settings for Balloon
#define BALLOON_CAMERA_FRAMESIZE   FRAMESIZE_QVGA  // 320x240 for low bandwidth
#define BALLOON_CAMERA_QUALITY     10             // JPEG quality (0-63, lower=better)
#define BALLOON_CAMERA_BRIGHTNESS  0              // -2 to 2
#define BALLOON_CAMERA_CONTRAST    0              // -2 to 2

// ===========================
// Data Packet Configuration
// ===========================

// Packet Types
#define PACKET_TYPE_TELEMETRY    0x01
#define PACKET_TYPE_GPS          0x02
#define PACKET_TYPE_CAMERA_THUMB 0x03
#define PACKET_TYPE_CAMERA_FULL  0x04
#define PACKET_TYPE_STATUS       0x05
#define PACKET_TYPE_EMERGENCY    0xFF

// Packet Priorities
#define PRIORITY_EMERGENCY       1   // Battery critical, system failure
#define PRIORITY_GPS              2   // Position data
#define PRIORITY_TELEMETRY        3   // Sensor data
#define PRIORITY_CAMERA           4   // Image data
#define PRIORITY_STATUS           5   // Heartbeat, diagnostics

// Maximum Packet Sizes
#define MAX_PACKET_SIZE           240   // LoRa payload limit
#define MAX_TELEMETRY_SIZE        50    // Telemetry packet size
#define MAX_GPS_SIZE              60    // GPS packet size
#define MAX_THUMBNAIL_SIZE        200   // Camera thumbnail chunk size
#define MAX_STATUS_SIZE           30    // Status packet size

// ===========================
// Balloon-Specific Features
// ===========================

// Altitude-Based Features
#define ALTITUDE_ABOVE_SEA_LEVEL    true   // Use absolute altitude
#define ALTITUDE_THRESHOLD_HIGH     1000   // Above this, enable special modes
#define ALTITUDE_THRESHOLD_CRITICAL 5000   // Above this, emergency protocols

// Temperature-Based Features
#define TEMPERATURE_COMPENSATION    true   // Compensate pressure for temperature
#define TEMPERATURE_THRESHOLD_LOW   -20    // Below this, cold weather mode
#define TEMPERATURE_THRESHOLD_HIGH  50     // Above this, heat protection

// GPS Features
#define GPS_SMART_SAVE              true   // Only transmit when position changes
#define GPS_MIN_MOVEMENT_DISTANCE   10     // Minimum movement in meters
#define GPS_MAX_AGE_MS              30000  // Maximum GPS data age in ms

// Camera Features
#define CAMERA_AUTO_BRIGHTNESS      true   // Adjust based on altitude
#define CAMERA_NIGHT_MODE_THRESHOLD 500    // Below this lux, use night settings
#define CAMERA_MOTION_DETECTION     false  // Disable for power saving

// ===========================
// Communication Settings
// ===========================

// Retry and Acknowledgment
#define MAX_RETRANSMIT_ATTEMPTS     3      // Max retransmission attempts
#define ACK_TIMEOUT_MS_VAL          2000   // Wait for ACK this long
#define PACKET_SEQUENCE_TIMEOUT    60000  // Reset sequence after this time

// Adaptive Transmission
#define ENABLE_ADAPTIVE_SF         true   // Adjust spreading factor based on signal
#define ADAPTIVE_SF_HIGH_THRESHOLD  -80   // Above this RSSI, use SF7
#define ADAPTIVE_SF_LOW_THRESHOLD  -110  // Below this RSSI, use SF12

// ===========================
// Balloon Status LEDs
// ===========================

// LED Blink Patterns (milliseconds ON, OFF)
#define LED_PATTERN_GPS_LOCK        1000, 1000   // Slow blink when GPS locked
#define LED_PATTERN_LORA_TX         100, 900     // Quick blink when transmitting
#define LED_PATTERN_ERROR            200, 200     // Fast blink for errors
#define LED_PATTERN_LOW_BATTERY      500, 500     // Medium blink for low battery
#define LED_PATTERN_NORMAL           2000, 2000   // Very slow blink for normal operation

// ===========================
// Emergency and Safety
// ===========================

// Emergency Conditions
#define EMERGENCY_BATTERY_VOLTAGE    2.8         // Critical battery voltage
#define EMERGENCY_ALTITUDE_RATE      15          // m/s - too fast descent
#define EMERGENCY_MAX_FLIGHT_TIME    14400       // 4 hours maximum flight time
#define EMERGENCY_NO_GPS_TIME        300         // 5 minutes without GPS

// Emergency Actions
#define EMERGENCY_ENABLE_BEACON      true        // Send emergency beacon
#define EMERGENCY_SAVE_LAST_DATA     true        // Save final data packet
#define EMERGENCY_CONTINUE_CAMERA    false      // Stop camera in emergency

// ===========================
// Debug and Development
// ===========================

#ifdef DEBUG
#define DEBUG_SERIAL              true
#define DEBUG_SENSORS             true
#define DEBUG_GPS                 true
#define DEBUG_LORA                 true
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

// Serial Debug Settings
#define DEBUG_BAUD_RATE           115200
#define DEBUG_BUFFER_SIZE         1024

#endif // BALLOON_CONFIG_H

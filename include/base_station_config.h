#ifndef BASE_STATION_CONFIG_H
#define BASE_STATION_CONFIG_H

// ===========================
// Base Station Configuration
// ESP32-S3 Balloon Project
// ===========================

#include "sensor_pins.h"

// ===========================
// Device Type Selection
// ===========================
#define DEVICE_BALLOON      1
#define DEVICE_BASE_STATION 2

// Uncomment the device type you're building:
// #define DEVICE_TYPE DEVICE_BALLOON
#define DEVICE_TYPE DEVICE_BASE_STATION

// ===========================
// Base Station Hardware
// ===========================

// LoRa Module (Same as balloon but receive mode)
#define LORA_RECEIVE_MODE    true
#define LORA_CONTINUOUS_LISTEN true

// WiFi Configuration
#define WIFI_MODE_AP         true    // Create Access Point for laptops
#define WIFI_MODE_STA        false   // Optional: Connect to existing network
#define WIFI_AP_SSID         "BalloonBaseStation"
#define WIFI_AP_PASSWORD     "balloon123"
#define WIFI_AP_CHANNEL      6
#define WIFI_AP_MAX_CLIENTS  4

// Web Server Configuration
#define WEB_SERVER_PORT      80
#define STREAM_SERVER_PORT    81
#define MAX_WEB_CLIENTS      4
#define WEB_TIMEOUT_MS        30000

// ===========================
// Data Storage and Buffering
// ===========================

// Memory Management
#define MAX_STORED_PACKETS    100     // Max packets to keep in memory
#define MAX_STORED_IMAGES     20      // Max images to keep in memory
#define MAX_IMAGE_SIZE        50000   // Max image size in bytes

// Flash Storage (optional)
#define ENABLE_FLASH_STORAGE  true
#define MAX_FLASH_PACKETS     1000    // Max packets to store in flash
#define MAX_FLASH_IMAGES      100     // Max images to store in flash
#define FLASH_STORAGE_PATH    "/balloon_data"

// ===========================
// Base Station Features
// ===========================

// Data Processing
#define ENABLE_DATA_LOGGING     true    // Log all received data
#define ENABLE_IMAGE_PROCESSING  true    // Process and store images
#define ENABLE_REAL_TIME_DISPLAY true    // Show live data on web interface
#define ENABLE_HISTORICAL_DATA  true    // Keep historical records

// Map Integration
#define ENABLE_MAP_DISPLAY      true    // Show balloon position on map
#define MAP_UPDATE_INTERVAL_MS  5000    // Update map every 5 seconds
#define DEFAULT_MAP_LATITUDE    0.0      // Default map center
#define DEFAULT_MAP_LONGITUDE   0.0      // Default map center
#define DEFAULT_MAP_ZOOM       10       // Default map zoom level

// Alert System
#define ENABLE_ALERTS          true     // Enable alerts and notifications
#define ALERT_LOW_BATTERY      true     // Alert when balloon battery is low
#define ALERT_SIGNAL_LOST      true     // Alert when signal is lost
#define ALERT_ALTITUDE_HIGH    true     // Alert when balloon reaches high altitude
#define ALERT_NO_DATA_TIMEOUT  60000    // Alert after 60 seconds without data

// ===========================
// Web Interface Configuration
// ===========================

// Dashboard Settings
#define DASHBOARD_UPDATE_RATE_MS  1000   // Update dashboard every second
#define MAX_GRAPH_DATAPOINTS      100    // Max datapoints in graphs
#define GRAPH_HISTORY_MINUTES     60     // Keep 60 minutes of graph history

// Image Gallery
#define GALLERY_THUMBNAIL_SIZE   150     // Thumbnail size in pixels
#define GALLERY_IMAGES_PER_PAGE   12      // Images per gallery page
#define MAX_GALLERY_HISTORY      100     // Max images in gallery

// Data Export
#define ENABLE_DATA_EXPORT       true    // Allow data export
#define EXPORT_FORMATS_CSV        true    // CSV export
#define EXPORT_FORMATS_JSON       true    // JSON export
#define EXPORT_FORMATS_KML        true    // KML for Google Earth

// ===========================
// Communication Settings
// ===========================

// LoRa Reception
#define LORA_RECEIVE_TIMEOUT_MS  30000   // Timeout for receiving packets
#define LORA_RETRY_DELAY_MS      1000    // Delay between retry attempts
#define EXPECTED_PACKET_TYPES     0x1F    // Expected packet types mask

// Packet Validation
#define ENABLE_PACKET_VALIDATION true    // Validate packet integrity
#define PACKET_TIMEOUT_MS        120000  // Packet considered stale after 2 minutes
#define SEQUENCE_RESET_TIMEOUT   300000  // Reset sequence after 5 minutes

// Signal Quality Monitoring
#define RSSI_HISTORY_SIZE        50      // Keep 50 RSSI measurements
#define SNR_HISTORY_SIZE         50      // Keep 50 SNR measurements
#define SIGNAL_QUALITY_UPDATE_MS 2000    // Update signal quality every 2 seconds

// ===========================
// Base Station Status LEDs
// ===========================

// LED Patterns (milliseconds ON, OFF)
#define LED_PATTERN_WIFI_ACTIVE      100, 100     // Fast blink when WiFi active
#define LED_PATTERN_RECEIVING        200, 800     // Blink when receiving data
#define LED_PATTERN_SIGNAL_GOOD      2000, 2000   // Slow blink for good signal
#define LED_PATTERN_SIGNAL_POOR      500, 500     // Medium blink for poor signal
#define LED_PATTERN_NO_SIGNAL        100, 100     // Fast blink for no signal
#define LED_PATTERN_ERROR            100, 100     // Fast blink for errors

// ===========================
// Data Processing Settings
// ===========================

// Telemetry Processing
#define TELEMETRY_AVERAGING_WINDOW 5       // Average over 5 samples
#define TELEMETRY_FILTER_ENABLED   true    // Enable data filtering
#define TELEMETRY_OUTLIER_THRESHOLD 3.0     // Standard deviations for outlier detection

// GPS Processing
#define GPS_ALTITUDE_SMOOTHING     true    // Smooth altitude data
#define GPS_POSITION_SMOOTHING     true    // Smooth position data
#define GPS_VELOCITY_CALCULATION   true    // Calculate velocity from position changes
#define GPS_PREDICTION_ENABLED     true    // Predict position based on velocity

// Camera Processing
#define IMAGE_DECOMPRESSION        true    // Decompress images for display
#define IMAGE_RESIZING             true    // Create thumbnails
#define IMAGE_METADATA_EXTRACTION  true    // Extract timestamp and settings

// ===========================
// Safety and Recovery
// ===========================

// Data Backup
#define ENABLE_AUTO_BACKUP        true    // Automatic backup of important data
#define BACKUP_INTERVAL_MINUTES   10      // Backup every 10 minutes
#define BACKUP retention_DAYS      7       // Keep backups for 7 days

// Emergency Procedures
#define EMERGENCY_DATA_SAVE       true    // Save all data on emergency
#define EMERGENCY_SIGNAL_BEACON   true    // Send beacon if balloon lost
#define EMERGENCY_RETRY_COUNT     10      // Retry emergency transmission 10 times

// ===========================
// Development and Debug
// ===========================

#ifdef DEBUG
#define DEBUG_SERIAL              true
#define DEBUG_LORA                 true
#define DEBUG_WIFI                 true
#define DEBUG_WEB_SERVER           true
#define DEBUG_DATA_PROCESSING      true
#define DEBUG_PACKETS              true
#else
#define DEBUG_SERIAL              false
#define DEBUG_LORA                false
#define DEBUG_WIFI                 false
#define DEBUG_WEB_SERVER           false
#define DEBUG_DATA_PROCESSING      false
#define DEBUG_PACKETS              false
#endif

// Performance Monitoring
#define ENABLE_PERFORMANCE_MONITOR true   // Monitor system performance
#define PERFORMANCE_UPDATE_MS      5000    // Update performance metrics every 5 seconds
#define MEMORY_WARNING_THRESHOLD   80      // Warn at 80% memory usage

// Serial Debug Settings
#define DEBUG_BAUD_RATE           115200
#define DEBUG_BUFFER_SIZE         2048

#endif // BASE_STATION_CONFIG_H

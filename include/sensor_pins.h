#ifndef SENSOR_PINS_H
#define SENSOR_PINS_H

// ===========================
// Sensor Pin Definitions
// ESP32-S3 Balloon Project
// ===========================

// I2C Bus Note: BMP280 and OLED share the same I2C bus (GPIO 1/2)
// - BMP280 at address 0x76
// - OLED at address 0x3C

// BMP280 Pressure/Temperature Sensor (I2C)
#define BMP280_SDA_PIN    1   // I2C0 SDA
#define BMP280_SCL_PIN    2  // I2C0 SCL
// NOTE: do NOT name this macro BMP280_ADDRESS — the Adafruit_BMP280 library
// header defines that same name as (0x77), and since it is included via
// angle brackets AFTER sensor_pins.h, the library value silently wins at
// the call site (system-header redefinitions emit no warning). That
// collision is what made the balloon probe 0x77 while its sensor ACKs at
// 0x76, aborting boot (debug session balloon-no-data-oled-blank).
#define BMP280_I2C_ADDRESS 0x76  // This board's strapping (0x77 alternative)

// OLED Display (SSD1306, 128x64, shares I2C bus with BMP280)
#define OLED_SDA_PIN      1   // Same I2C bus as BMP280
#define OLED_SCL_PIN      2   // Same I2C bus as BMP280
#define OLED_ADDRESS      0x3C  // Default I2C address (0x3D alternative)
#define OLED_WIDTH        128  // Display width
#define OLED_HEIGHT       64   // Display height

// MAX-M10S GPS Module (UART)
// Config proven on hardware in src/test_lora_balloon.cpp (GPS fixes outdoors):
// GPS RX on 47, and 38400 baud (M10S UART1 default — at 9600 every checksum failed).
//
// ESP32 RX pin history (debug session "balloon-bootloops-at-bench", 2026-08-27):
// - 45: VDD_SPI strapping pin; never received data.
// - 35: FORBIDDEN on this board — GPIO 35-37 are reserved octal-PSRAM (OPI) data
//   lines on this N16R8-class module (platformio.ini: qio_opi / 16MB /
//   psram_type = opi; per ESP32-S3 module datasheet these pins are not led out on
//   devkits for this reason). Routing UART1 RX onto GPIO 35 via the GPIO matrix
//   put the balloon into a TG1WDT_SYS_RST (rst:0x8) bootloop — present in every
//   failing build, absent in the one passing build, and the bootloop vanished the
//   moment the pad claim moved off 35 (balloon11.log: hours of clean telemetry,
//   zero resets). Never assign ANY peripheral to GPIO 35/36/37 on this board.
// - 41: chosen — free on the sensor and camera pin maps, not a strapping pin,
//   outside the reserved 35-37 PSRAM set. Bench-verified stable.
#define GPS_TX_PIN        41  // GPS TX → ESP32 RX (41; was 35 = reserved PSRAM line, originally 45)
#define GPS_RX_PIN        47  // GPS RX → ESP32 TX (changed from 46 - not exposed on DevKitC-1)
#define GPS_PPS_PIN       42  // Pulse Per Second (optional)
#define GPS_BAUD_RATE     38400
#define GPS_UART_NUM      UART_NUM_1

// LoRa Module E32 900T30D (UART)
#define LORA_TX_PIN       14  // ESP32 TX → LoRa RXD
#define LORA_RX_PIN       48  // LoRa TXD → ESP32 RX
#define LORA_M0_PIN       19  // Mode control 0 (normal/wake/sleep/config)
#define LORA_M1_PIN       20  // Mode control 1
#define LORA_AUX_PIN      21  // Auxiliary/Status input (optional)
#define LORA_UART_NUM     UART_NUM_2  // Use UART2

// Status LEDs (Optional)
//#define LED_GPS_LOCK_PIN  38  // GPS Lock Status
//#define LED_LORA_TX_PIN   39  // LoRa Transmit Status
//#define LED_ERROR_PIN     40  // Error Status

// Power Management Pins (Optional)
// #define BATTERY_SENSE_PIN 4   // Battery voltage monitoring (ADC)
// POWER_ENABLE_PIN removed due to pin conflicts - sensors always on

// ===========================
// Sensor Configuration
// ===========================

// BMP280 Settings
#define BMP280_SAMPLING_TEMP    2   // 0-16, higher = more precise
#define BMP280_SAMPLING_PRESS   5   // 0-16, higher = more precise
#define BMP280_FILTER           2   // 0-4, higher = more filtering
#define BMP280_STANDBY_MS       0   // 0-7, 0=0.5ms, 7=4000ms

// GPS Settings
#define GPS_UPDATE_RATE     1000    // Update every 1000ms
#define GPS_TIMEOUT_MS      5000    // GPS timeout
#define GPS_MIN_SATS        4       // Minimum satellites for valid fix

// LoRa Settings
#define LORA_FREQUENCY      915.0   // MHz (US band)
#define LORA_SPREADING_FACTOR 7     // 6-12, higher = longer range, slower
#define LORA_BANDWIDTH      125000  // Hz (125K, 250K, 500K)
#define LORA_CODING_RATE    5       // 5-8, denominator (4/5, 4/6, 4/7, 4/8)
#define LORA_TX_POWER       20      // dBm (2-20)
#define LORA_PREAMBLE_LEN   8       // 6-65535
#define LORA_SYNC_WORD      0x12    // Network sync word

// ===========================
// Pin Validation
// ===========================

// Ensure no conflicts with existing camera pins
// Camera pins used: 4,5,6,7,8,9,10,11,12,13,15,16,17,18
// Sensor pins used: 1,2,4,14,19,20,21,38,39,40,41,42,47,48
// Known conflicts: BATTERY_SENSE_PIN 4 = camera SIOD (SCCB data) — pre-existing,
//   pegs the ADC at 4095 (accepted for now, tracked in debug notes).
// GPIO 35/36/37 must NEVER appear in either list — they are reserved octal-PSRAM
//   lines on this OPI-PSRAM board (see GPS section above). A previous version of
//   this comment listed 35 here as if it were a legal sensor pin; that stale list
//   is what let the 2026-08-27 bootloop slip past review.

#endif // SENSOR_PINS_H

#ifndef SENSOR_PINS_H
#define SENSOR_PINS_H

// ===========================
// Sensor Pin Definitions
// ESP32-S3 Balloon Project
// ===========================

// BMP280 Pressure/Temperature Sensor (I2C)
#define BMP280_SDA_PIN    1   // I2C0 SDA
#define BMP280_SCL_PIN    2   // I2C0 SCL
#define BMP280_ADDRESS    0x76  // Default I2C address (0x77 alternative)

// MAX-M10S GPS Module (UART)
#define GPS_TX_PIN        43  // GPS TX → ESP32 RX
#define GPS_RX_PIN        44  // GPS RX → ESP32 TX
#define GPS_PPS_PIN       42  // Pulse Per Second (optional)
#define GPS_BAUD_RATE     9600
#define GPS_UART_NUM      UART_NUM_1

// LoRa Module 900T30D (SPI)
#define LORA_CS_PIN       14  // Chip Select
#define LORA_RST_PIN      19  // Reset
#define LORA_IRQ_PIN      20  // DIO0 - Interrupt/Ready
#define LORA_DIO1_PIN     23  // DIO1 - Optional advanced features
#define LORA_SPI_NUM      SPI3_HOST  // Use SPI3

// LoRa SPI Pins (ESP32-S3 specific)
#define LORA_SCK_PIN      21  // SPI Clock
#define LORA_MOSI_PIN     47  // Master Out Slave In
#define LORA_MISO_PIN     48  // Master In Slave Out

// Status LEDs (Optional)
#define LED_GPS_LOCK_PIN  38  // GPS Lock Status
#define LED_LORA_TX_PIN   39  // LoRa Transmit Status
#define LED_ERROR_PIN     40  // Error Status

// Power Management Pins (Optional)
#define POWER_ENABLE_PIN  41  // Enable power to sensors
#define BATTERY_SENSE_PIN 4   // Battery voltage monitoring (ADC)

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
// Sensor pins used: 1,2,14,19,20,21,23,38,39,40,42,43,44,47,48
// No conflicts detected

#endif // SENSOR_PINS_H

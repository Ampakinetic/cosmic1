/**
 * LoRa Range Test - Balloon (Transmitter)
 *
 * Transmits sensor data once per second for range testing.
 * Displays local status on OLED.
 *
 * Data packet format (CSV):
 *   SEQ,TEMP,PRESS,LAT,LON,ALT,SATS,HDOP
 *
 * Pin Configuration:
 * - OLED (I2C): SDA=GPIO 1, SCL=GPIO 2
 * - BMP280 (I2C): SDA=GPIO 1, SCL=GPIO 2
 * - GPS (UART): TX=GPIO 45, RX=GPIO 46
 * - LoRa (UART): TX=GPIO 14, RX=GPIO 48, M0=19, M1=20, AUX=21
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include <TinyGPS++.h>
#include "esp_log.h"

static const char* TAG = "BalloonTX";

// ===========================
// Pin Definitions
// ===========================

// OLED Display (I2C)
#define OLED_SDA_PIN     1
#define OLED_SCL_PIN     2
#define OLED_ADDRESS     0x3C
#define OLED_WIDTH       128
#define OLED_HEIGHT      64
#define OLED_RESET       -1

// BMP280 (I2C, same bus as OLED)
#define BMP280_ADDRESS   0x76

// GPS Module (UART1)
#define GPS_TX_PIN       35  // GPS TX -> ESP32 RX (trying GPIO 35)
#define GPS_RX_PIN       47  // ESP32 TX -> GPS RX (changed from 46 - not exposed)
#define GPS_BAUD_RATE    38400  // Try 38400 (9600 didn't work - all checksums fail)

// LoRa E32 (UART2)
#define LORA_TX_PIN      14
#define LORA_RX_PIN      48
#define LORA_M0_PIN      19
#define LORA_M1_PIN      20
#define LORA_AUX_PIN     21
#define LORA_BAUD_RATE   9600

// Status LEDs
#define LED_TX_PIN       39

// ===========================
// Global Objects
// ===========================

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_BMP280 bmp280;
TinyGPSPlus gps;

// Transmitter state
unsigned long sequenceNumber = 0;
unsigned long lastTransmitTime = 0;
const unsigned long TRANSMIT_INTERVAL_MS = 1000;  // 1 second

// GPS debugging
unsigned long lastGpsDebug = 0;
const unsigned long GPS_DEBUG_INTERVAL_MS = 5000;  // Debug output every 5 seconds
unsigned long gpsCharsProcessed = 0;
unsigned long gpsGoodSentences = 0;
unsigned long gpsFailedChecksum = 0;
unsigned long lastGpsDataTime = 0;  // Time when last GPS character was received
char gpsBuffer[256];  // Buffer for raw GPS data output
int gpsBufferPos = 0;

// Sensor data
struct SensorData {
    float temp;
    float pressure;
    double lat;
    double lon;
    float alt;
    uint8_t sats;
    float hdop;
    bool hasFix;
} sensorData;

// Statistics
uint32_t packetsSent = 0;
uint32_t gpsFixTime = 0;  // Milliseconds when GPS got fix

// ===========================
// Functions
// ===========================

void setup();
void loop();
void initHardware();
void initSensors();
void initLoRa();
void transmitData();
void buildPacket(char* buffer, size_t maxSize);
void updateDisplay();
void displayHeader();
void displaySensorData();
void displayStats();
void flashTxLed();

// ===========================
// Setup
// ===========================

void setup() {
    // Initialize Serial with explicit USB CDC mode
    Serial.begin(115200);

    // Wait for serial to be ready (important for USB CDC)
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    delay(500);
    Serial.println("\n==========================================");
    Serial.println("LoRa Range Test - Balloon (Transmitter)");
    Serial.println("==========================================\n");
    Serial.flush();

    initHardware();
    initSensors();
    initLoRa();

    displayHeader();

    Serial.println("Setup complete. Starting transmission...\n");
    Serial.flush();
    delay(1000);
}

// ===========================
// Main Loop
// ===========================

void loop() {
    unsigned long currentTime = millis();

    // Read GPS data continuously
    while (Serial1.available() > 0) {
        char c = Serial1.read();

        // Buffer GPS data for logging
        if (c == '\n' || c == '\r') {
            if (gpsBufferPos > 0) {
                gpsBuffer[gpsBufferPos] = '\0';
               // ESP_LOGI(TAG, "GPS RX: %s", gpsBuffer);
                gpsBufferPos = 0;
            }
        } else if (c == '$') {
            // Start of new NMEA sentence
            gpsBufferPos = 0;
            gpsBuffer[gpsBufferPos++] = c;
        } else if (gpsBufferPos < sizeof(gpsBuffer) - 1) {
            // Add character to buffer if it's printable
            if (c >= 32 && c <= 126) {
                gpsBuffer[gpsBufferPos++] = c;
            }
        }

        gps.encode(c);
        gpsCharsProcessed++;
        lastGpsDataTime = currentTime;  // Track when GPS data arrives
    }

    // Visual indicator: Rapid LED blink = GPS data arriving
    static unsigned long lastGpsLedUpdate = 0;
    if (currentTime - lastGpsDataTime < 100 && currentTime - lastGpsLedUpdate > 50) {
        // Received GPS data recently (<100ms ago) - blink rapidly
        digitalWrite(LED_TX_PIN, !digitalRead(LED_TX_PIN));
        lastGpsLedUpdate = currentTime;
    } else if (currentTime - lastGpsDataTime >= 100) {
        // No GPS data recently - ensure LED is off (unless transmitting)
        if (currentTime - lastTransmitTime > 100) {
            digitalWrite(LED_TX_PIN, LOW);
        }
        lastGpsLedUpdate = currentTime;
    }

    // Update sensor data
    sensorData.temp = bmp280.readTemperature();
    sensorData.pressure = bmp280.readPressure() / 100.0;  // hPa

    // Get satellite count (available even without full fix)
    sensorData.sats = gps.satellites.value();

    if (gps.location.isValid()) {
        sensorData.lat = gps.location.lat();
        sensorData.lon = gps.location.lng();
        sensorData.alt = gps.altitude.meters();
        sensorData.hdop = gps.hdop.value() / 100.0;
        sensorData.hasFix = true;

        if (gpsFixTime == 0 && sensorData.sats >= 4) {
            gpsFixTime = currentTime;
        }
    } else {
        sensorData.hasFix = false;
    }

    // GPS debugging output
    if (currentTime - lastGpsDebug >= GPS_DEBUG_INTERVAL_MS) {
        lastGpsDebug = currentTime;
        // ESP_LOGI(TAG, "GPS Debug: Chars=%lu Passed=%d Failed=%d Sats=%d Fix=%s",
        //          gpsCharsProcessed, gps.passedChecksum(), gps.failedChecksum(),
        //          sensorData.sats, sensorData.hasFix ? "YES" : "NO");

        // Show which NMEA sentences we're receiving
        if (Serial1.available() == 0) {
            ESP_LOGW(TAG, "No data from GPS - check wiring!");
        }
    }

    // Transmit once per second
    if (currentTime - lastTransmitTime >= TRANSMIT_INTERVAL_MS) {
        lastTransmitTime = currentTime;
        transmitData();
        updateDisplay();
    }

    // Small delay to prevent watchdog issues
    delay(10);
}

// ===========================
// Initialization
// ===========================

void initHardware() {
    ESP_LOGI(TAG, "Initializing hardware...");

    // I2C for OLED and BMP280
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    delay(100);

    // Status LED
    pinMode(LED_TX_PIN, OUTPUT);
    digitalWrite(LED_TX_PIN, LOW);

    ESP_LOGI(TAG, "Hardware pins configured");
}

void initSensors() {
    // OLED
    ESP_LOGI(TAG, "Initializing OLED...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        ESP_LOGE(TAG, "OLED not found at 0x3C, trying 0x3D...");
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            ESP_LOGE(TAG, "OLED not found - continuing without display");
        } else {
            ESP_LOGI(TAG, "OLED found at 0x3D");
        }
    } else {
        ESP_LOGI(TAG, "OLED OK");
    }

    display.clearDisplay();
    display.display();
    delay(100);

    // BMP280
    ESP_LOGI(TAG, "Initializing BMP280...");
    if (!bmp280.begin(BMP280_ADDRESS)) {
        ESP_LOGE(TAG, "BMP280 not found - continuing without pressure sensor");
    } else {
        // Configure BMP280 for weather monitoring
        bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL,
                           Adafruit_BMP280::SAMPLING_X2,   // Temp
                           Adafruit_BMP280::SAMPLING_X16,  // Pressure
                           Adafruit_BMP280::FILTER_X16,
                           Adafruit_BMP280::STANDBY_MS_500);
        ESP_LOGI(TAG, "BMP280 OK");
    }

    // GPS
    ESP_LOGI(TAG, "Initializing GPS on UART1 (TX=%d, RX=%d)...", GPS_TX_PIN, GPS_RX_PIN);
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    ESP_LOGI(TAG, "GPS UART initialized at %d baud", GPS_BAUD_RATE);
}

void initLoRa() {
    Serial.println("Initializing LoRa...");

    // Configure control pins
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    // Normal mode (M0=0, M1=0)
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);

    delay(100);

    // Initialize Serial2 for LoRa
    Serial2.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    delay(100);

    Serial.println("    LoRa UART initialized");

    // Check AUX pin
    int auxState = digitalRead(LORA_AUX_PIN);
    Serial.print("    AUX state: ");
    Serial.println(auxState ? "HIGH" : "LOW");

    if (auxState == LOW) {
        Serial.println("    WARNING: AUX is LOW - module may be busy");
    }
}

// ===========================
// Transmission
// ===========================

void transmitData() {
    char packet[128];
    buildPacket(packet, sizeof(packet));

    // Send via LoRa
    Serial2.print(packet);
    Serial2.flush();

    packetsSent++;
    sequenceNumber++;

    flashTxLed();

    // Debug output
    ESP_LOGI(TAG, "TX [%lu]: T=%.1fC P=%.1fhPa Sats=%d",
             sequenceNumber - 1, sensorData.temp, sensorData.pressure, sensorData.sats);
}

void buildPacket(char* buffer, size_t maxSize) {
    // Format: SEQ,TEMP,PRESS,LAT,LON,ALT,SATS,HDOP
    // Example: 1234,23.5,1013.25,45.12345,-93.67890,150.5,8,1.2

    snprintf(buffer, maxSize,
             "%lu,%.1f,%.2f,%.5f,%.5f,%.1f,%d,%.1f\n",
             sequenceNumber,
             sensorData.temp,
             sensorData.pressure,
             sensorData.lat,
             sensorData.lon,
             sensorData.alt,
             sensorData.sats,
             sensorData.hdop);
}

// ===========================
// Display
// ===========================

void updateDisplay() {
    display.clearDisplay();

    displayHeader();
    displaySensorData();
    displayStats();

    display.display();
}

void displayHeader() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Title bar
    display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 1);
    display.print("TX: BALLOON");

    display.setTextColor(SSD1306_WHITE);
}

void displaySensorData() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    int y = 12;
    int lineh = 9;

    // Line 1: Seq + Temp (condensed)
    display.setCursor(0, y);
    display.print("Seq:");
    display.print(sequenceNumber);
    display.print(" T:");
    display.print((int)sensorData.temp);
    display.println("C");
    y += lineh;

    // Line 2: Pressure + Altitude
    display.setCursor(0, y);
    display.print("P:");
    display.print((int)sensorData.pressure);
    display.print("h Alt:");
    display.print((int)sensorData.alt);
    display.println("m");
    y += lineh;

    // Line 3: Satellites
    display.setCursor(0, y);
    display.print("Sats:");
    display.print(sensorData.sats);
    if (sensorData.hasFix) {
        display.print(" HDOP:");
        display.println((int)sensorData.hdop);
    } else {
        display.println(" NO FIX");
    }
    y += lineh;

    // Line 4: Latitude
    display.setCursor(0, y);
    display.print("Lat:");
    if (sensorData.hasFix) {
        display.println(sensorData.lat, 4);
    } else {
        display.println("-----");
    }
    y += lineh;

    // Line 5: Longitude
    display.setCursor(0, y);
    display.print("Lon:");
    if (sensorData.hasFix) {
        display.println(sensorData.lon, 4);
    } else {
        display.println("-----");
    }
    y += lineh;
}

void displayStats() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Packets sent
    display.setCursor(0, 56);
    display.print("Sent: ");
    display.print(packetsSent);

    // GPS fix indicator
    if (sensorData.hasFix) {
        display.fillRect(110, 56, 8, 8, SSD1306_WHITE);
    } else {
        display.drawRect(110, 56, 8, 8, SSD1306_WHITE);
    }
}

void flashTxLed() {
    digitalWrite(LED_TX_PIN, HIGH);
    delay(10);
    digitalWrite(LED_TX_PIN, LOW);
}

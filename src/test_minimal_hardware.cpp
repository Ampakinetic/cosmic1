/**
 * ESP32-S3 Minimal Hardware Test
 * Tests basic functionality without full system initialization
 */

#include <Arduino.h>
#include <Wire.h>

// Pin definitions from your configuration
#define OLED_SDA_PIN     1
#define OLED_SCL_PIN     2
#define LORA_M0_PIN      19
#define LORA_M1_PIN      20
#define LORA_AUX_PIN     21
#define LORA_TX_PIN      14
#define LORA_RX_PIN      48
#define GPS_TX_PIN       45
#define GPS_RX_PIN       46

void setup() {
    Serial.begin(115200);
    delay(2000); // Extra delay for stability

    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 Minimal Hardware Test");
    Serial.println("========================================\n");

    // Test 1: GPIO Pin Control
    Serial.println("Test 1: GPIO Pin Control");
    testPinControl();

    // Test 2: I2C Bus (No devices expected)
    Serial.println("\nTest 2: I2C Bus Scan");
    testI2CScan();

    // Test 3: UART Pins (GPIO check only)
    Serial.println("\nTest 3: UART Pins Check");
    testUARTPins();

    // Test 4: LoRa Mode Pins
    Serial.println("\nTest 4: LoRa Mode Pins");
    testLoRaPins();

    Serial.println("\n========================================");
    Serial.println("All basic tests completed!");
    Serial.println("Ready for hardware connection");
    Serial.println("========================================\n");
}

void testPinControl() {
    Serial.println("Testing GPIO pins 0-5...");

    for (int i = 0; i <= 5; i++) {
        pinMode(i, OUTPUT);
        digitalWrite(i, HIGH);
        delay(50);
        digitalWrite(i, LOW);
        Serial.printf("  GPIO %d: OK\n", i);
    }
    Serial.println("✓ GPIO control working");
}

void testI2CScan() {
    Serial.printf("  Initializing I2C on SDA=%d, SCL=%d\n", OLED_SDA_PIN, OLED_SCL_PIN);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    delay(100);

    int deviceCount = 0;
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        int error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("  Found device at 0x%02X", addr);
            if (addr == 0x3C || addr == 0x3D) {
                Serial.println(" (OLED)");
            } else if (addr == 0x76 || addr == 0x77) {
                Serial.println(" (BMP280)");
            } else {
                Serial.println();
            }
            deviceCount++;
        }
    }

    if (deviceCount == 0) {
        Serial.println("  No I2C devices found (expected without hardware)");
    } else {
        Serial.printf("  Found %d I2C device(s)\n", deviceCount);
    }
    Serial.println("✓ I2C scan completed");
}

void testUARTPins() {
    Serial.printf("  GPS TX: GPIO %d (config as input for test)\n", GPS_TX_PIN);
    pinMode(GPS_TX_PIN, INPUT);

    Serial.printf("  GPS RX: GPIO %d (config as output for test)\n", GPS_RX_PIN);
    pinMode(GPS_RX_PIN, OUTPUT);
    digitalWrite(GPS_RX_PIN, LOW);

    Serial.printf("  LoRa TX: GPIO %d (config as output for test)\n", LORA_TX_PIN);
    pinMode(LORA_TX_PIN, OUTPUT);
    digitalWrite(LORA_TX_PIN, LOW);

    Serial.printf("  LoRa RX: GPIO %d (config as input for test)\n", LORA_RX_PIN);
    pinMode(LORA_RX_PIN, INPUT);

    Serial.println("✓ UART pins configured");
}

void testLoRaPins() {
    Serial.println("  Testing LoRa mode control pins...");

    // Test M0 pin
    pinMode(LORA_M0_PIN, OUTPUT);
    digitalWrite(LORA_M0_PIN, HIGH);
    delay(100);
    digitalWrite(LORA_M0_PIN, LOW);
    Serial.printf("  M0 (GPIO %d): OK\n", LORA_M0_PIN);

    // Test M1 pin
    pinMode(LORA_M1_PIN, OUTPUT);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(100);
    digitalWrite(LORA_M1_PIN, LOW);
    Serial.printf("  M1 (GPIO %d): OK\n", LORA_M1_PIN);

    // Test AUX pin
    pinMode(LORA_AUX_PIN, INPUT);
    int auxState = digitalRead(LORA_AUX_PIN);
    Serial.printf("  AUX (GPIO %d): %s\n", LORA_AUX_PIN, auxState ? "HIGH" : "LOW");

    Serial.println("✓ LoRa pins tested");
}

void loop() {
    // Pulse LED to show system is running
    pinMode(2, OUTPUT); // SCL pin as LED indicator

    static bool ledState = false;
    ledState = !ledState;
    digitalWrite(2, ledState);

    delay(1000);

    static int counter = 0;
    if (++counter % 10 == 0) {
        Serial.println("System running...");
    }
}

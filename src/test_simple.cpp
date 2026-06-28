#include <Arduino.h>
#include <Wire.h>

#define SDA_PIN 1
#define SCL_PIN 2

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 I2C Bus Scanner");
    Serial.println("========================================");
    Serial.println("Scanning I2C bus...");
    Serial.printf("SDA: GPIO %d\n", SDA_PIN);
    Serial.printf("SCL: GPIO %d\n", SCL_PIN);

    Wire.begin(SDA_PIN, SCL_PIN);
    delay(100);

    int deviceCount = 0;

    Serial.println("\nScanning...");
    for (int addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        int error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("Found device at 0x%02X - ", addr);

            if (addr == 0x3C || addr == 0x3D) {
                Serial.println("OLED Display (SSD1306)");
            } else if (addr == 0x76 || addr == 0x77) {
                Serial.println("BMP280 Pressure Sensor");
            } else {
                Serial.println("Unknown device");
            }
            deviceCount++;
        }
    }

    Serial.println("\n========================================");
    if (deviceCount == 0) {
        Serial.println("No I2C devices found!");
        Serial.println("\nCheck:");
        Serial.println("- SDA/SCL connections");
        Serial.println("- Pull-up resistors");
        Serial.println("- Power supply");
    } else {
        Serial.printf("Found %d I2C device(s)\n", deviceCount);
    }
    Serial.println("========================================");
}

void loop() {
    delay(5000);
}

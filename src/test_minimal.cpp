#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 Minimal Test");
    Serial.println("========================================");
    Serial.println("Serial communication OK");

    // Test GPIO pins
    Serial.println("\nTesting GPIO pins...");
    for (int i = 0; i < 5; i++) {
        pinMode(i, OUTPUT);
        digitalWrite(i, HIGH);
        delay(100);
        digitalWrite(i, LOW);
        Serial.printf("GPIO %d: OK\n", i);
    }

    Serial.println("\n========================================");
    Serial.println("Basic test complete!");
    Serial.println("========================================");
}

void loop() {
    Serial.println("Loop running...");
    delay(5000);
}

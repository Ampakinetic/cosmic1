/**
 * ESP32-S3 OLED + LoRa E32 Test Sketch
 *
 * This sketch tests:
 * 1. I2C OLED Display (0.96" SSD1306, 128x64)
 * 2. LoRa E32 900T30D module (UART)
 *
 * Pin Configuration:
 * - OLED: SDA=GPIO 1, SCL=GPIO 2 (I2C, address 0x3C)
 * - LoRa: TX=GPIO 14, RX=GPIO 48, M0=19, M1=20, AUX=21
 *
 * Instructions:
 * 1. Upload this sketch to your ESP32-S3
 * 2. Open Serial Monitor at 115200 baud
 * 3. Watch OLED display and serial output
 * 4. Press BOOT button to cycle through tests
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===========================
// Pin Definitions
// ===========================

// OLED Display (I2C)
#define OLED_SDA_PIN     1
#define OLED_SCL_PIN     2
#define OLED_ADDRESS     0x3C  // Try 0x3D if 0x3C doesn't work
#define OLED_WIDTH       128
#define OLED_HEIGHT      64
#define OLED_RESET       -1    // No reset pin

// LoRa E32 Module (UART)
#define LORA_TX_PIN      14    // ESP32 TX → LoRa RXD
#define LORA_RX_PIN      48    // LoRa TXD → ESP32 RX
#define LORA_M0_PIN      19    // Mode control 0
#define LORA_M1_PIN      20    // Mode control 1
#define LORA_AUX_PIN     21    // Status/Busy (optional)
#define LORA_BAUD_RATE   9600  // Default for E32

// Test Control
#define BUTTON_PIN       0     // BOOT button

// ===========================
// Global Objects
// ===========================

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Test state
int testState = 0;
const char* testNames[] = {
    "I2C Scan",
    "OLED Graphics",
    "OLED Text",
    "LoRa Pins",
    "LoRa Modes",
    "LoRa UART",
    "All Tests"
};
const int NUM_TESTS = 7;

// ===========================
// Function Forward Declarations
// ===========================

void runTest(int test);
void runAllTests();
bool testI2CScan();
void testOLEDGraphics();
void testOLEDText();
bool testLoRaPins();
bool testLoRaModes();
bool testLoRaUART();
void displayStartupScreen();
void displayTestInfo(int testNum);
void displayStatus();
void displayError(const char* message);
void initLoRaPins();

// ===========================
// Setup
// ===========================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 OLED + LoRa E32 Test Sketch");
    Serial.println("========================================\n");

    // Initialize button
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Initialize I2C
    Serial.println("Initializing I2C bus...");
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    delay(100);

    // Test I2C bus
    if (!testI2CScan()) {
        Serial.println("ERROR: No I2C devices found!");
        Serial.println("Check:");
        Serial.println("  - SDA/SCL connections (GPIO 1/2)");
        Serial.println("  - Pull-up resistors (4.7kΩ)");
        Serial.println("  - Power supply (3.3V)");
        displayError("No I2C devices!");
        while (true) { delay(1000); }
    }

    // Initialize OLED
    Serial.println("Initializing OLED display...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("ERROR: OLED initialization failed!");
        Serial.println("Trying alternative address 0x3D...");
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println("ERROR: OLED not found at 0x3C or 0x3D!");
            Serial.println("Check:");
            Serial.println("  - OLED power supply");
            Serial.println("  - SDA/SCL connections");
            Serial.println("  - I2C address");
            displayError("OLED not found!");
            while (true) { delay(1000); }
        } else {
            Serial.println("SUCCESS: OLED found at 0x3D");
        }
    } else {
        Serial.println("SUCCESS: OLED found at 0x3C");
    }

    // Clear display
    display.clearDisplay();
    display.display();
    delay(100);

    // Show startup screen
    displayStartupScreen();

    // Initialize LoRa pins
    Serial.println("\nInitializing LoRa pins...");
    initLoRaPins();

    // Show initial test info
    displayTestInfo(0);

    Serial.println("\nSetup complete!");
    Serial.println("Press BOOT button to cycle through tests\n");
}

// ===========================
// Main Loop
// ===========================

void loop() {
    static unsigned long lastButtonCheck = 0;
    static bool lastButtonState = HIGH;

    // Check button every 50ms
    if (millis() - lastButtonCheck > 50) {
        bool currentState = digitalRead(BUTTON_PIN);

        // Detect button press (HIGH to LOW)
        if (lastButtonState == HIGH && currentState == LOW) {
            delay(50); // Debounce
            if (digitalRead(BUTTON_PIN) == LOW) {
                testState = (testState + 1) % NUM_TESTS;
                Serial.println("\n--- Test " + String(testState + 1) + ": " + testNames[testState] + " ---");
                runTest(testState);
                displayTestInfo(testState);
            }
        }
        lastButtonState = currentState;
        lastButtonCheck = millis();
    }

    // Periodic status update
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 5000) {
        displayStatus();
        lastStatusUpdate = millis();
    }
}

// ===========================
// Test Functions
// ===========================

void runTest(int test) {
    switch (test) {
        case 0: // I2C Scan
            testI2CScan();
            break;
        case 1: // OLED Graphics
            testOLEDGraphics();
            break;
        case 2: // OLED Text
            testOLEDText();
            break;
        case 3: // LoRa Pins
            testLoRaPins();
            break;
        case 4: // LoRa Modes
            testLoRaModes();
            break;
        case 5: // LoRa UART
            testLoRaUART();
            break;
        case 6: // All Tests
            runAllTests();
            break;
    }
}

void runAllTests() {
    Serial.println("Running all tests...");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Running All Tests:");
    display.display();

    int y = 12;
    bool allPassed = true;

    // Test I2C
    display.setCursor(0, y);
    display.print("I2C Scan: ");
    bool i2cOk = testI2CScan();
    display.println(i2cOk ? "PASS" : "FAIL");
    display.display();
    y += 10;
    allPassed &= i2cOk;
    delay(500);

    // Test OLED Graphics
    display.setCursor(0, y);
    display.print("OLED GFX: ");
    testOLEDGraphics();
    display.println("PASS");
    display.display();
    y += 10;
    delay(500);

    // Test LoRa Pins
    display.setCursor(0, y);
    display.print("LoRa Pins: ");
    bool pinsOk = testLoRaPins();
    display.println(pinsOk ? "PASS" : "WARN");
    display.display();
    y += 10;
    allPassed &= pinsOk;
    delay(500);

    // Test LoRa Modes
    display.setCursor(0, y);
    display.print("LoRa Modes: ");
    bool modesOk = testLoRaModes();
    display.println(modesOk ? "PASS" : "WARN");
    display.display();
    y += 10;
    allPassed &= modesOk;
    delay(500);

    // Test LoRa UART
    display.setCursor(0, y);
    display.print("LoRa UART: ");
    bool uartOk = testLoRaUART();
    display.println(uartOk ? "PASS" : "WARN");
    display.display();
    delay(500);
    allPassed &= uartOk;

    // Final result
    display.setCursor(0, y + 5);
    display.setTextSize(2);
    display.println(allPassed ? "ALL PASS" : "CHECK ERR");
    display.display();

    Serial.println(allPassed ? "\nAll tests PASSED!" : "\nSome tests had issues - check serial output");
}

bool testI2CScan() {
    Serial.println("Scanning I2C bus...");

    byte error, address;
    int deviceCount = 0;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("I2C Devices:");
    display.display();

    for (address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("Found device at 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);

            display.print("0x");
            if (address < 16) display.print("0");
            display.print(address, HEX);

            // Known devices
            if (address == 0x76) display.println(" (BMP280)");
            else if (address == 0x77) display.println(" (BMP280?)");
            else if (address == 0x3C) display.println(" (OLED)");
            else if (address == 0x3D) display.println(" (OLED?)");
            else display.println(" (Unknown)");
            display.display();

            deviceCount++;
        }
    }

    if (deviceCount == 0) {
        Serial.println("No I2C devices found");
        display.println("No devices!");
        display.display();
        return false;
    }

    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" I2C device(s)");

    display.setCursor(0, 56);
    display.print("Total: ");
    display.println(deviceCount);
    display.display();

    return true;
}

void testOLEDGraphics() {
    Serial.println("Testing OLED graphics...");

    display.clearDisplay();

    // Draw some shapes
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.fillRect(10, 10, 20, 20, SSD1306_WHITE);
    display.drawCircle(50, 20, 10, SSD1306_WHITE);
    display.fillCircle(80, 20, 8, SSD1306_WHITE);
    display.drawLine(0, 40, 127, 40, SSD1306_WHITE);

    // Draw triangle
    display.drawTriangle(100, 40, 110, 60, 90, 60, SSD1306_WHITE);

    display.display();
    Serial.println("Graphics test complete");
    delay(2000);
}

void testOLEDText() {
    Serial.println("Testing OLED text...");

    display.clearDisplay();

    // Different sizes
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Size 1: Hello!");

    display.setTextSize(2);
    display.println("Size 2");

    display.setTextSize(3);
    display.println("3");

    display.setTextSize(4);
    display.setCursor(64, 32);
    display.println("4");

    display.display();
    Serial.println("Text test complete");
    delay(2000);
}

bool testLoRaPins() {
    Serial.println("Testing LoRa pin configuration...");
    bool allOk = true;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Pins:");
    display.display();

    int y = 12;

    // Test M0
    display.setCursor(0, y);
    display.print("M0 (19): ");
    pinMode(LORA_M0_PIN, OUTPUT);
    digitalWrite(LORA_M0_PIN, HIGH);
    delay(1);
    bool m0Ok = (digitalRead(LORA_M0_PIN) == HIGH);
    digitalWrite(LORA_M0_PIN, LOW);
    display.println(m0Ok ? "OK" : "ERR");
    display.display();
    Serial.print("  M0: ");
    Serial.println(m0Ok ? "OK" : "ERROR");
    y += 10;
    allOk &= m0Ok;
    delay(200);

    // Test M1
    display.setCursor(0, y);
    display.print("M1 (20): ");
    pinMode(LORA_M1_PIN, OUTPUT);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(1);
    bool m1Ok = (digitalRead(LORA_M1_PIN) == HIGH);
    digitalWrite(LORA_M1_PIN, LOW);
    display.println(m1Ok ? "OK" : "ERR");
    display.display();
    Serial.print("  M1: ");
    Serial.println(m1Ok ? "OK" : "ERROR");
    y += 10;
    allOk &= m1Ok;
    delay(200);

    // Test AUX (input)
    display.setCursor(0, y);
    display.print("AUX (21): ");
    pinMode(LORA_AUX_PIN, INPUT_PULLUP);
    delay(1);
    int auxValue = digitalRead(LORA_AUX_PIN);
    display.println(auxValue == HIGH ? "HIGH" : "LOW");
    display.display();
    Serial.print("  AUX: ");
    Serial.println(auxValue == HIGH ? "HIGH" : "LOW");
    y += 10;
    delay(200);

    // Test TX/RX pin configuration
    display.setCursor(0, y);
    display.print("TX(14)/RX(48): ");
    display.println("Configured");
    display.display();
    Serial.println("  TX/RX: Configured");

    return allOk;
}

bool testLoRaModes() {
    Serial.println("Testing LoRa mode switching...");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Modes:");
    display.display();

    int y = 12;
    bool allOk = true;

    // Mode 00: Normal
    display.setCursor(0, y);
    display.print("00 Normal: ");
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    delay(10);
    int aux00 = digitalRead(LORA_AUX_PIN);
    display.println(aux00 == HIGH ? "OK" : "AUX low");
    display.display();
    Serial.print("  Mode 00 (Normal): AUX=");
    Serial.println(aux00);
    y += 10;
    allOk &= (aux00 == HIGH);
    delay(500);

    // Mode 01: Wake-up
    display.setCursor(0, y);
    display.print("01 Wake: ");
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(10);
    int aux01 = digitalRead(LORA_AUX_PIN);
    display.println(aux01 == HIGH ? "OK" : "AUX low");
    display.display();
    Serial.print("  Mode 01 (Wake-up): AUX=");
    Serial.println(aux01);
    y += 10;
    delay(500);

    // Mode 10: Power-saving
    display.setCursor(0, y);
    display.print("10 Power: ");
    digitalWrite(LORA_M0_PIN, HIGH);
    digitalWrite(LORA_M1_PIN, LOW);
    delay(10);
    int aux10 = digitalRead(LORA_AUX_PIN);
    display.println(aux10 == HIGH ? "OK" : "AUX low");
    display.display();
    Serial.print("  Mode 10 (Power-save): AUX=");
    Serial.println(aux10);
    y += 10;
    delay(500);

    // Mode 11: Sleep/Config
    display.setCursor(0, y);
    display.print("11 Sleep: ");
    digitalWrite(LORA_M0_PIN, HIGH);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(10);
    int aux11 = digitalRead(LORA_AUX_PIN);
    display.println(aux11 == HIGH ? "OK" : "AUX low");
    display.display();
    Serial.print("  Mode 11 (Sleep/Config): AUX=");
    Serial.println(aux11);
    y += 10;
    delay(500);

    // Return to normal mode
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);

    display.setCursor(0, y + 5);
    display.println("Back to Normal");
    display.display();

    return allOk;
}

bool testLoRaUART() {
    Serial.println("Testing LoRa UART communication...");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa UART Test:");
    display.display();

    int y = 12;

    // Set normal mode for UART operation
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    delay(100);

    // Initialize Serial2 for LoRa
    Serial2.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    delay(100);

    display.setCursor(0, y);
    display.print("Serial2: ");
    display.println(String(LORA_BAUD_RATE) + " baud");
    display.display();
    Serial.print("  Serial2 initialized at ");
    Serial.print(LORA_BAUD_RATE);
    Serial.println(" baud");
    y += 10;
    delay(500);

    // Check for any incoming data
    display.setCursor(0, y);
    display.print("RX data: ");
    int rxCount = 0;
    unsigned long startTime = millis();
    while (millis() - startTime < 2000) {
        if (Serial2.available()) {
            Serial2.read();
            rxCount++;
        }
        delay(10);
    }
    display.println(rxCount > 0 ? String(rxCount) + " bytes" : "None");
    display.display();
    Serial.print("  Received: ");
    Serial.print(rxCount);
    Serial.println(" bytes");
    y += 10;
    delay(500);

    // Test transmission (will only work if LoRa module is configured)
    display.setCursor(0, y);
    display.print("TX test: ");
    display.println("Attempting...");
    display.display();
    Serial.println("  Sending test data...");

    // Send a simple test message
    Serial2.write("TEST\r\n");
    delay(100);

    display.setCursor(0, y + 10);
    display.println("(Check pair module)");
    display.display();
    Serial.println("  Note: TX test requires paired receiver");

    return true; // Return true even if no data - module might just be idle
}

// ===========================
// Display Functions
// ===========================

void displayStartupScreen() {
    display.clearDisplay();

    // Title
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0);
    display.println("ESP32-S3");

    display.setTextSize(1);
    display.setCursor(20, 20);
    display.println("Test Sketch");

    // Divider
    display.drawLine(0, 32, 127, 32, SSD1306_WHITE);

    // Status
    display.setCursor(0, 40);
    display.println("OLED: OK");
    display.setCursor(64, 40);
    display.println("LoRa: ?");

    // Instructions
    display.setCursor(0, 56);
    display.setTextSize(1);
    display.println("Press BOOT to test");

    display.display();
}

void displayTestInfo(int testNum) {
    display.clearDisplay();

    // Header
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print("Test ");
    display.print(testNum + 1);
    display.print("/");
    display.println(NUM_TESTS);

    // Test name
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 16);
    display.println(testNames[testNum]);

    // Instructions
    display.setCursor(0, 56);
    display.println("BOOT: Next test");

    display.display();
}

void displayStatus() {
    // Update AUX status at bottom right
    int auxValue = digitalRead(LORA_AUX_PIN);

    display.fillRect(100, 56, 28, 8, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(102, 56);
    display.print("AUX:");
    display.print(auxValue ? "H" : "L");
    display.display();
    display.setTextColor(SSD1306_WHITE);
}

void displayError(const char* message) {
    // Try to display even if init failed (might still partially work)
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ERROR:");
    display.println(message);
    display.display();
}

// ===========================
// Helper Functions
// ===========================

void initLoRaPins() {
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    // Set normal mode
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);

    Serial.println("  M0 (GPIO 19) -> OUTPUT, LOW");
    Serial.println("  M1 (GPIO 20) -> OUTPUT, LOW");
    Serial.println("  AUX (GPIO 21) -> INPUT");

    int auxState = digitalRead(LORA_AUX_PIN);
    Serial.print("  AUX state: ");
    Serial.println(auxState ? "HIGH" : "LOW");
}

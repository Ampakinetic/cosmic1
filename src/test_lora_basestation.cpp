/**
 * LoRa Range Test - Base Station (Receiver)
 *
 * Receives sensor data from balloon and displays on OLED.
 * Tracks packet loss and signal quality for range testing.
 *
 * Data packet format (CSV):
 *   SEQ,TEMP,PRESS,LAT,LON,ALT,SATS,HDOP
 *
 * Pin Configuration:
 * - OLED (I2C): SDA=GPIO 1, SCL=GPIO 2
 * - LoRa (UART): TX=GPIO 14, RX=GPIO 48, M0=19, M1=20, AUX=21
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
#define OLED_ADDRESS     0x3C
#define OLED_WIDTH       128
#define OLED_HEIGHT      64
#define OLED_RESET       -1

// LoRa E32 (UART2)
#define LORA_TX_PIN      14
#define LORA_RX_PIN      48
#define LORA_M0_PIN      19
#define LORA_M1_PIN      20
#define LORA_AUX_PIN     21
#define LORA_BAUD_RATE   9600

// Status LEDs
#define LED_RX_PIN       40
#define LED_ERR_PIN      39

// Button
#define BUTTON_PIN       0  // BOOT button to cycle views

// ===========================
// Global Objects
// ===========================

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Receiver state
unsigned long lastSequence = 0;
unsigned long expectedSequence = 0;
unsigned long lastReceiveTime = 0;

// Statistics
uint32_t packetsReceived = 0;
uint32_t packetsLost = 0;
uint32_t totalPacketsLost = 0;  // Cumulative since startup

// Last received data
struct TelemetryData {
    unsigned long sequence;
    float temp;
    float pressure;
    double lat;
    double lon;
    float alt;
    uint8_t sats;
    float hdop;
    bool valid;
} telemetry;

// Display views
int displayView = 0;  // 0=summary, 1=telemetry, 2=stats
const int NUM_VIEWS = 3;
const char* viewNames[] = {"Summary", "Telemetry", "Stats"};

// ===========================
// Functions
// ===========================

void setup();
void loop();
void initHardware();
void initLoRa();
void processIncomingData();
bool parsePacket(const char* packet, TelemetryData* data);
void updateDisplay();
void displaySummary();
void displayTelemetry();
void displayStats();
void displayHeader(const char* title);
void displayPacketStats();
void displaySignalQuality();
void flashRxLed();
void showError(const char* message);
void checkForLostPackets();

// ===========================
// Setup
// ===========================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n==========================================");
    Serial.println("LoRa Range Test - Base Station (Receiver)");
    Serial.println("==========================================\n");

    initHardware();
    initLoRa();

    Serial.println("Setup complete. Waiting for data...\n");

    // Show waiting screen
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Base Station RX");
    display.println();
    display.println("Waiting for");
    display.println("balloon data...");
    display.display();

    delay(1000);
}

// ===========================
// Main Loop
// ===========================

void loop() {
    // Check button for view change
    static bool lastButtonState = HIGH;
    bool currentState = digitalRead(BUTTON_PIN);

    if (lastButtonState == HIGH && currentState == LOW) {
        delay(50);  // Debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
            displayView = (displayView + 1) % NUM_VIEWS;
            Serial.print("View: ");
            Serial.println(viewNames[displayView]);
            updateDisplay();
        }
    }
    lastButtonState = currentState;

    // Process incoming LoRa data
    processIncomingData();

    // Check for packet loss (no data for >2 seconds)
    if (millis() - lastReceiveTime > 2000 && expectedSequence > 0) {
        checkForLostPackets();
    }

    // Update display periodically
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 500) {
        updateDisplay();
        lastDisplayUpdate = millis();
    }

    delay(10);
}

// ===========================
// Initialization
// ===========================

void initHardware() {
    Serial.println("Initializing hardware...");

    // I2C for OLED
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    delay(100);

    // Initialize OLED
    Serial.println("Initializing OLED...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("    Trying alternative address 0x3D...");
        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
            Serial.println("    ERROR: OLED not found!");
            // Continue anyway - might still work
        } else {
            Serial.println("    OLED found at 0x3D");
        }
    } else {
        Serial.println("    OLED OK");
    }

    display.clearDisplay();
    display.display();
    delay(100);

    // Status LEDs
    pinMode(LED_RX_PIN, OUTPUT);
    pinMode(LED_ERR_PIN, OUTPUT);
    digitalWrite(LED_RX_PIN, LOW);
    digitalWrite(LED_ERR_PIN, LOW);

    // Button
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    Serial.println("  Hardware pins configured");
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
}

// ===========================
// Data Processing
// ===========================

void processIncomingData() {
    static char buffer[256];
    static int bufferPos = 0;

    while (Serial2.available() > 0) {
        char c = Serial2.read();

        if (c == '\n' || c == '\r') {
            if (bufferPos > 0) {
                buffer[bufferPos] = '\0';

                TelemetryData data;
                if (parsePacket(buffer, &data)) {
                    // Valid packet received
                    lastReceiveTime = millis();

                    // Check for lost packets
                    if (expectedSequence > 0 && data.sequence > expectedSequence) {
                        uint32_t lost = data.sequence - expectedSequence;
                        packetsLost += lost;
                        totalPacketsLost += lost;

                        Serial.print("PACKET LOSS: ");
                        Serial.print(lost);
                        Serial.println(" packets");
                        digitalWrite(LED_ERR_PIN, HIGH);
                        delay(50);
                        digitalWrite(LED_ERR_PIN, LOW);
                    }

                    // Update state
                    telemetry = data;
                    telemetry.valid = true;
                    lastSequence = data.sequence;
                    expectedSequence = data.sequence + 1;
                    packetsReceived++;
                    packetsLost = 0;  // Reset consecutive loss counter

                    flashRxLed();

                    Serial.print("RX [");
                    Serial.print(data.sequence);
                    Serial.print("]: T=");
                    Serial.print(data.temp, 1);
                    Serial.print("C P=");
                    Serial.print(data.pressure, 1);
                    Serial.print(" Sats=");
                    Serial.print(data.sats);
                    Serial.print(" HDOP=");
                    Serial.println(data.hdop, 1);
                } else {
                    Serial.print("PARSE ERROR: ");
                    Serial.println(buffer);
                }

                bufferPos = 0;
            }
        } else if (bufferPos < sizeof(buffer) - 1) {
            buffer[bufferPos++] = c;
        }
    }
}

bool parsePacket(const char* packet, TelemetryData* data) {
    // Format: SEQ,TEMP,PRESS,LAT,LON,ALT,SATS,HDOP
    // Example: 1234,23.5,1013.25,45.12345,-93.67890,150.5,8,1.2

    unsigned long seq;
    float temp, press, alt, hdop;
    double lat, lon;
    int sats;

    int parsed = sscanf(packet, "%lu,%f,%f,%lf,%lf,%f,%d,%f",
                       &seq, &temp, &press, &lat, &lon, &alt, &sats, &hdop);

    if (parsed == 8) {
        data->sequence = seq;
        data->temp = temp;
        data->pressure = press;
        data->lat = lat;
        data->lon = lon;
        data->alt = alt;
        data->sats = (uint8_t)sats;
        data->hdop = hdop;
        return true;
    }

    return false;
}

// ===========================
// Display
// ===========================

void updateDisplay() {
    display.clearDisplay();

    switch (displayView) {
        case 0:
            displaySummary();
            break;
        case 1:
            displayTelemetry();
            break;
        case 2:
            displayStats();
            break;
    }

    display.display();
}

void displayHeader(const char* title) {
    display.setTextSize(1);
    display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 1);
    display.print(title);
    display.setTextColor(SSD1306_WHITE);
}

void displaySummary() {
    displayHeader("RX: Summary");

    int y = 12;
    int lineh = 9;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: Seq + Temp
    display.setCursor(0, y);
    display.print("Seq:");
    if (telemetry.valid) {
        display.print(telemetry.sequence);
    } else {
        display.print("--");
    }
    display.print(" T:");
    if (telemetry.valid) {
        display.print((int)telemetry.temp);
    } else {
        display.print("--");
    }
    display.println("C");
    y += lineh;

    // Line 2: Pressure + Altitude
    display.setCursor(0, y);
    display.print("P:");
    if (telemetry.valid) {
        display.print((int)telemetry.pressure);
    } else {
        display.print("--");
    }
    display.print("h Alt:");
    if (telemetry.valid) {
        display.print((int)telemetry.alt);
    } else {
        display.print("--");
    }
    display.println("m");
    y += lineh;

    // Line 3: Satellites
    display.setCursor(0, y);
    display.print("Sats:");
    if (telemetry.valid) {
        display.println(telemetry.sats);
    } else {
        display.println("--");
    }
    y += lineh;

    // Line 4: Latitude
    display.setCursor(0, y);
    display.print("Lat:");
    if (telemetry.valid) {
        display.println(telemetry.lat, 4);
    } else {
        display.println("-----");
    }
    y += lineh;

    // Line 5: Longitude
    display.setCursor(0, y);
    display.print("Lon:");
    if (telemetry.valid) {
        display.println(telemetry.lon, 4);
    } else {
        display.println("-----");
    }
    y += lineh;

    // Signal quality
    displaySignalQuality();
}

void displayTelemetry() {
    displayHeader("RX: Telemetry");

    int y = 12;
    int lineh = 9;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (telemetry.valid) {
        // GPS Position (4 decimal places for ~11m precision)
        display.setCursor(0, y);
        display.print("Lat:");
        display.println(telemetry.lat, 4);
        y += lineh;

        display.setCursor(0, y);
        display.print("Lon:");
        display.println(telemetry.lon, 4);
        y += lineh;

        // Environment
        display.setCursor(0, y);
        display.print("T:");
        display.print((int)telemetry.temp);
        display.print("C P:");
        display.print((int)telemetry.pressure);
        display.println("h");
        y += lineh;

        // GPS Quality
        display.setCursor(0, y);
        display.print("Alt:");
        display.print((int)telemetry.alt);
        display.print("m S:");
        display.print(telemetry.sats);
        display.print(" H:");
        display.println((int)telemetry.hdop);
        y += lineh;

        // Signal quality
        displaySignalQuality();
    } else {
        display.setCursor(0, y);
        display.println("No data received");
        y += lineh;
        display.setCursor(0, y);
        display.println("Check balloon");
        y += lineh;
        display.setCursor(0, y);
        display.println("is powered on");
    }

    // Instructions
    display.setCursor(0, 56);
    display.print("BOOT: View");
}

void displayStats() {
    displayHeader("RX: Statistics");

    int y = 14;
    int lineh = 9;

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Packets received
    display.setCursor(0, y);
    display.print("Rcvd: ");
    display.println(packetsReceived);
    y += lineh;

    // Total lost
    display.setCursor(0, y);
    display.print("Lost: ");
    display.println(totalPacketsLost);
    y += lineh;

    // Loss rate
    display.setCursor(0, y);
    if (packetsReceived > 0) {
        float lossRate = (float)totalPacketsLost * 100.0 / (packetsReceived + totalPacketsLost);
        display.print("Rate: ");
        display.print(lossRate, 1);
        display.println("%");
    } else {
        display.println("Rate: --%");
    }
    y += lineh;

    // Last receive
    display.setCursor(0, y);
    unsigned long ago = millis() - lastReceiveTime;
    if (telemetry.valid && ago < 60000) {
        display.print("Last: ");
        display.print(ago / 1000);
        display.println("s ago");
    } else if (telemetry.valid) {
        display.print("Last: ");
        display.print((ago / 60000));
        display.println("m ago");
    } else {
        display.println("Last: Never");
    }
    y += lineh;

    // Time since last packet
    if (ago > 3000) {
        display.setTextColor(SSD1306_WHITE);
        display.fillRect(0, y, 128, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(2, y);
        display.print("SIGNAL LOST!");
        display.setTextColor(SSD1306_WHITE);
        y += lineh;
    } else {
        // Signal strength indicator
        display.setCursor(0, y);
        display.print("Signal: ");
        int bars = ago < 500 ? 4 : ago < 1000 ? 3 : ago < 2000 ? 2 : 1;
        for (int i = 0; i < 4; i++) {
            if (i < bars) {
                display.write(0xFF);  // Filled block
            } else {
                display.write(0xAA);  // Empty block
            }
        }
        display.println();
        y += lineh;
    }

    // Instructions
    display.setCursor(0, 56);
    display.print("BOOT: View");
}

void displaySignalQuality() {
    unsigned long ago = millis() - lastReceiveTime;

    display.setCursor(70, 56);
    if (!telemetry.valid) {
        display.println("NO SIG");
    } else if (ago < 1000) {
        display.println("EXCEL");
    } else if (ago < 2000) {
        display.println("GOOD");
    } else if (ago < 3000) {
        display.println("FAIR");
    } else {
        display.println("POOR");
    }
}

void flashRxLed() {
    digitalWrite(LED_RX_PIN, HIGH);
    delay(10);
    digitalWrite(LED_RX_PIN, LOW);
}

void checkForLostPackets() {
    // Signal lost indicator
    static bool wasLost = false;

    if (millis() - lastReceiveTime > 5000) {
        if (!wasLost) {
            Serial.println("SIGNAL LOST - Check range!");
            digitalWrite(LED_ERR_PIN, HIGH);
            wasLost = true;
        }
    } else {
        if (wasLost) {
            digitalWrite(LED_ERR_PIN, LOW);
            wasLost = false;
        }
    }
}

void showError(const char* message) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ERROR:");
    display.println(message);
    display.display();
}

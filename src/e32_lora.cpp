#include "e32_lora.h"

// Debug configuration
#ifndef DEBUG_E32
#define DEBUG_E32 true
#endif

// ===========================
// Static Instance
// ===========================

static E32LoRa e32Instance;
E32LoRa& E32LoRaModule() {
    return e32Instance;
}

// ===========================
// Constructor/Destructor
// ===========================

E32LoRa::E32LoRa()
    : serial(nullptr)
    , rxPin(-1)
    , txPin(-1)
    , m0Pin(-1)
    , m1Pin(-1)
    , auxPin(-1)
    , currentMode(E32Mode::MODE_NORMAL)
    , initialized(false)
    , lastTransmitTime(0)
    , lastReceiveTime(0)
    , transmitErrors(0)
    , receiveErrors(0)
{
}

E32LoRa::~E32LoRa() {
    end();
}

// ===========================
// Initialization
// ===========================

bool E32LoRa::begin(HardwareSerial* serial, int8_t rxPin, int8_t txPin,
                    int8_t m0Pin, int8_t m1Pin, int8_t auxPin,
                    uint32_t baudRate) {
    if (!serial) {
        return false;
    }

    this->serial = serial;
    this->rxPin = rxPin;
    this->txPin = txPin;
    this->m0Pin = m0Pin;
    this->m1Pin = m1Pin;
    this->auxPin = auxPin;

    // Configure control pins
    pinMode(m0Pin, OUTPUT);
    pinMode(m1Pin, OUTPUT);
    pinMode(auxPin, INPUT);

    // Start in normal mode
    setPinsForMode(E32Mode::MODE_NORMAL);

    // Initialize serial
    serial->begin(baudRate, SERIAL_8N1, rxPin, txPin);

    // Wait for module to initialize
    delay(100);

    // Check if AUX is high (module ready)
    if (!isAuxHigh()) {
        if (DEBUG_E32) {
            Serial.println("E32: Warning - AUX not high after init");
        }
    }

    initialized = true;
    currentMode = E32Mode::MODE_NORMAL;

    if (DEBUG_E32) {
        Serial.println("E32: Initialized successfully");
        Serial.printf("  RX=%d, TX=%d, M0=%d, M1=%d, AUX=%d\n",
                     rxPin, txPin, m0Pin, m1Pin, auxPin);
        Serial.printf("  Baud=%lu\n", baudRate);
    }

    return true;
}

void E32LoRa::end() {
    if (serial) {
        serial->end();
    }
    initialized = false;
}

// ===========================
// Mode Control
// ===========================

bool E32LoRa::setMode(E32Mode mode) {
    if (!initialized) {
        return false;
    }

    currentMode = mode;
    setPinsForMode(mode);

    // Wait for mode transition
    delay(50);

    // Verify AUX is high (module ready in new mode)
    if (mode == E32Mode::MODE_NORMAL || mode == E32Mode::MODE_WAKEUP) {
        if (!waitForAuxHigh(1000)) {
            if (DEBUG_E32) {
                Serial.println("E32: Mode change timeout - AUX not high");
            }
            return false;
        }
    }

    if (DEBUG_E32) {
        const char* modeNames[] = {"Normal", "Wakeup", "PowerSave", "Sleep"};
        Serial.printf("E32: Mode set to %s\n", modeNames[static_cast<int>(mode)]);
    }

    return true;
}

void E32LoRa::setPinsForMode(E32Mode mode) {
    switch (mode) {
        case E32Mode::MODE_NORMAL:
            digitalWrite(m0Pin, LOW);
            digitalWrite(m1Pin, LOW);
            break;
        case E32Mode::MODE_WAKEUP:
            digitalWrite(m0Pin, HIGH);
            digitalWrite(m1Pin, LOW);
            break;
        case E32Mode::MODE_POWER_SAVE:
            digitalWrite(m0Pin, LOW);
            digitalWrite(m1Pin, HIGH);
            break;
        case E32Mode::MODE_SLEEP:
            digitalWrite(m0Pin, HIGH);
            digitalWrite(m1Pin, HIGH);
            break;
    }
}

// ===========================
// Transmission
// ===========================

bool E32LoRa::transmit(const uint8_t* data, size_t length) {
    if (!initialized || !data || length == 0) {
        transmitErrors++;
        return false;
    }

    // Check if module is ready
    if (!waitForAuxHigh(1000)) {
        if (DEBUG_E32) {
            Serial.println("E32: Transmit failed - module not ready (AUX timeout)");
        }
        transmitErrors++;
        return false;
    }

    // Send data
    size_t sent = serial->write(data, length);
    serial->flush();

    // Wait for transmission to complete
    if (!waitForAuxLow(1000)) {
        if (DEBUG_E32) {
            Serial.println("E32: Transmit timeout - AUX didn't go low");
        }
        transmitErrors++;
        return false;
    }

    // Wait for AUX to go high again (transmission complete)
    if (!waitForAuxHigh(5000)) {
        if (DEBUG_E32) {
            Serial.println("E32: Transmit timeout - AUX didn't go high");
        }
        transmitErrors++;
        return false;
    }

    lastTransmitTime = millis();

    if (DEBUG_E32) {
        Serial.printf("E32: Transmitted %zu bytes\n", sent);
    }

    return (sent == length);
}

bool E32LoRa::transmitToAddress(uint16_t addressHigh, uint16_t addressLow,
                                const uint8_t* data, size_t length) {
    // E32 automatically prepends address header in this mode
    // For directed transmission, we need to add the address prefix
    uint8_t buffer[length + 4];

    // Add high address
    buffer[0] = (addressHigh >> 8) & 0xFF;
    buffer[1] = addressHigh & 0xFF;

    // Add low address
    buffer[2] = (addressLow >> 8) & 0xFF;
    buffer[3] = addressLow & 0xFF;

    // Copy data
    memcpy(buffer + 4, data, length);

    return transmit(buffer, length + 4);
}

// ===========================
// Reception
// ===========================

int E32LoRa::available() {
    if (!initialized) {
        return 0;
    }

    return serial->available();
}

int E32LoRa::read(uint8_t* buffer, size_t maxLength) {
    if (!initialized || !buffer) {
        receiveErrors++;
        return -1;
    }

    int bytesRead = 0;

    while (serial->available() && bytesRead < static_cast<int>(maxLength)) {
        buffer[bytesRead++] = serial->read();
        delayMicroseconds(100); // Small delay for UART stability
    }

    if (bytesRead > 0) {
        lastReceiveTime = millis();

        if (DEBUG_E32) {
            Serial.printf("E32: Received %d bytes\n", bytesRead);
        }
    }

    return bytesRead;
}

// ===========================
// Configuration
// ===========================

bool E32LoRa::readConfig(E32Config& config) {
    if (!enterConfigMode()) {
        return false;
    }

    // Send read configuration command
    uint8_t readCmd[] = {0xC1, 0xC1, 0xC1};
    serial->write(readCmd, sizeof(readCmd));
    serial->flush();

    delay(100);

    // Read configuration response
    uint8_t buffer[6];
    if (readConfigurationBytes(buffer, sizeof(buffer))) {
        // Parse configuration
        // Note: This is a simplified implementation
        // Full configuration reading is more complex
        exitConfigMode();
        return true;
    }

    exitConfigMode();
    return false;
}

bool E32LoRa::writeConfig(const E32Config& config) {
    if (!enterConfigMode()) {
        return false;
    }

    uint8_t buffer[6];

    // Build configuration buffer
    buffer[0] = (config.addressHigh >> 8) & 0xFF;
    buffer[1] = config.addressHigh & 0xFF;
    buffer[2] = (config.addressLow >> 8) & 0xFF;
    buffer[3] = config.addressLow & 0xFF;
    buffer[4] = config.uartSpeed;
    buffer[5] = config.airDataRate;

    // Write configuration
    if (writeConfigurationBytes(buffer, sizeof(buffer))) {
        delay(100);
        exitConfigMode();
        return true;
    }

    exitConfigMode();
    return false;
}

bool E32LoRa::setParameters(uint8_t uartSpeed, uint8_t airDataRate, uint8_t option) {
    E32Config config;
    config.addressHigh = 0x0000;
    config.addressLow = 0xFFFF; // Broadcast address
    config.uartSpeed = uartSpeed;
    config.airDataRate = airDataRate;
    config.option = option;

    return writeConfig(config);
}

bool E32LoRa::setAddress(uint16_t addressHigh, uint16_t addressLow) {
    E32Config config;
    config.addressHigh = addressHigh;
    config.addressLow = addressLow;

    return writeConfig(config);
}

bool E32LoRa::setChannel(uint8_t channel) {
    // Channel setting requires full configuration write
    // This is a placeholder for channel setting
    if (channel > 31) {
        return false;
    }

    // Full implementation would read current config, modify channel, write back
    return true;
}

// ===========================
// AUX Pin Monitoring
// ===========================

bool E32LoRa::isAuxHigh() const {
    return (digitalRead(auxPin) == HIGH);
}

bool E32LoRa::waitForAuxHigh(uint32_t timeoutMs) {
    uint32_t startTime = millis();

    while (millis() - startTime < timeoutMs) {
        if (isAuxHigh()) {
            return true;
        }
        delay(10);
    }

    return false;
}

bool E32LoRa::waitForAuxLow(uint32_t timeoutMs) {
    uint32_t startTime = millis();

    while (millis() - startTime < timeoutMs) {
        if (!isAuxHigh()) {
            return true;
        }
        delay(10);
    }

    return false;
}

// ===========================
// Status
// ===========================

bool E32LoRa::isReady() const {
    return initialized && isAuxHigh();
}

bool E32LoRa::isTransmitting() const {
    return initialized && !isAuxHigh();
}

void E32LoRa::resetErrorCounts() {
    transmitErrors = 0;
    receiveErrors = 0;
}

// ===========================
// Debug
// ===========================

void E32LoRa::printConfig(const E32Config& config) const {
    Serial.println("=== E32 Configuration ===");
    Serial.printf("Address: %04X %04X\n", config.addressHigh, config.addressLow);
    Serial.printf("UART Speed: %d\n", config.uartSpeed);
    Serial.printf("Air Data Rate: %d\n", config.airDataRate);
    Serial.printf("Channel: %d\n", config.channel);
}

void E32LoRa::printStatus() const {
    Serial.println("=== E32 Status ===");
    Serial.printf("Initialized: %s\n", initialized ? "Yes" : "No");
    Serial.printf("Mode: %d\n", static_cast<int>(currentMode));
    Serial.printf("AUX: %s\n", isAuxHigh() ? "HIGH" : "LOW");
    Serial.printf("Ready: %s\n", isReady() ? "Yes" : "No");
    Serial.printf("TX Errors: %lu\n", transmitErrors);
    Serial.printf("RX Errors: %lu\n", receiveErrors);
    Serial.printf("Last TX: %lu ms ago\n", millis() - lastTransmitTime);
    Serial.printf("Last RX: %lu ms ago\n", millis() - lastReceiveTime);
}

// ===========================
// Private Methods
// ===========================

bool E32LoRa::enterConfigMode() {
    // Save current mode
    E32Mode previousMode = currentMode;

    // Switch to sleep/program mode
    if (!setMode(E32Mode::MODE_SLEEP)) {
        return false;
    }

    // Wait for mode transition
    delay(50);

    return true;
}

bool E32LoRa::exitConfigMode() {
    // Return to previous mode (usually normal)
    return setMode(E32Mode::MODE_NORMAL);
}

bool E32LoRa::readConfigurationBytes(uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return false;
    }

    size_t bytesRead = 0;
    uint32_t startTime = millis();

    while (bytesRead < length && (millis() - startTime) < 1000) {
        if (serial->available()) {
            buffer[bytesRead++] = serial->read();
        }
        delay(1);
    }

    return (bytesRead == length);
}

bool E32LoRa::writeConfigurationBytes(const uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return false;
    }

    size_t written = serial->write(buffer, length);
    serial->flush();

    return (written == length);
}

uint8_t E32LoRa::calculateConfigCRC(const uint8_t* buffer, size_t length) {
    uint8_t crc = 0;

    for (size_t i = 0; i < length; i++) {
        crc ^= buffer[i];
    }

    return crc;
}

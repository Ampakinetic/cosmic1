#ifndef E32_LORA_H
#define E32_LORA_H

#include <Arduino.h>
#include <HardwareSerial.h>

// ===========================
// E32-900T30D LoRa Driver
// UART-based LoRa transceiver module
// ===========================

// E32 Module Modes (M0, M1 pins)
enum class E32Mode : uint8_t {
    MODE_NORMAL = 0x00,      // M0=0, M1=0 - Normal transmission mode
    MODE_WAKEUP = 0x01,      // M0=1, M1=0 - Wake-up mode
    MODE_POWER_SAVE = 0x02,  // M0=0, M1=1 - Power saving mode
    MODE_SLEEP = 0x03        // M0=1, M1=1 - Sleep/program mode
};

// E32 Module Configuration Structure
struct E32Config {
    uint16_t addressHigh;    // High byte of address (0x0000-0xFFFF)
    uint16_t addressLow;     // Low byte of address (0x0000-0xFFFF)
    uint8_t uartSpeed;       // UART baud rate
    uint8_t airDataRate;     // Air data rate
    uint8_t option;          // Option bits ( FEC, pull-up, etc.)
    uint8_t transmissionType;// Transmission mode
    uint8_t channel;         // Communication channel (0-31)
    uint8_t transparentTransmission; // 0x00 for transparent
    uint8_t optionBits;      // Additional options
};

// Transmit Power Levels
enum class E32Power : uint8_t {
    POWER_20dBm = 0x00,  // 20dBm (default)
    POWER_17dBm = 0x01,  // 17dBm
    POWER_14dBm = 0x02,  // 14dBm
    POWER_11dBm = 0x03   // 11dBm
};

// UART Speed Codes
enum class E32UARTSpeed : uint8_t {
    BAUD_1200 = 0x00,
    BAUD_2400 = 0x01,
    BAUD_4800 = 0x02,
    BAUD_9600 = 0x03,    // Default
    BAUD_19200 = 0x04,
    BAUD_38400 = 0x05,
    BAUD_57600 = 0x06,
    BAUD_115200 = 0x07
};

// Air Data Rate Codes
enum class E32AirDataRate : uint8_t {
    RATE_0_3kbps = 0x00,
    RATE_1_2kbps = 0x01,
    RATE_2_4kbps = 0x02,
    RATE_4_8kbps = 0x03,
    RATE_9_6kbps = 0x04,   // Default
    RATE_19_2kbps = 0x05
};

// ===========================
// E32 LoRa Driver Class
// ===========================

class E32LoRa {
public:
    // Constructor
    E32LoRa();
    ~E32LoRa();

    // Initialization
    bool begin(HardwareSerial* serial, int8_t rxPin, int8_t txPin,
               int8_t m0Pin, int8_t m1Pin, int8_t auxPin,
               uint32_t baudRate = 9600);
    void end();

    // Mode control
    bool setMode(E32Mode mode);
    E32Mode getCurrentMode() const { return currentMode; }

    // Basic transmission
    bool transmit(const uint8_t* data, size_t length);
    bool transmitToAddress(uint16_t addressHigh, uint16_t addressLow,
                           const uint8_t* data, size_t length);

    // Reception
    int available();
    int read(uint8_t* buffer, size_t maxLength);
    uint8_t read() { return serial->read(); }
    void flush() { serial->flush(); }

    // Configuration
    bool readConfig(E32Config& config);
    bool writeConfig(const E32Config& config);
    bool setParameters(uint8_t uartSpeed, uint8_t airDataRate, uint8_t option);
    bool setAddress(uint16_t addressHigh, uint16_t addressLow);
    bool setChannel(uint8_t channel);

    // AUX pin monitoring
    bool isAuxHigh() const;
    bool waitForAuxHigh(uint32_t timeoutMs = 5000);
    bool waitForAuxLow(uint32_t timeoutMs = 5000);

    // Status
    bool isReady() const;
    bool isTransmitting() const;
    uint32_t getLastTransmitTime() const { return lastTransmitTime; }
    uint32_t getLastReceiveTime() const { return lastReceiveTime; }

    // Error handling
    uint32_t getTransmitErrorCount() const { return transmitErrors; }
    uint32_t getReceiveErrorCount() const { return receiveErrors; }
    void resetErrorCounts();

    // Debug
    void printConfig(const E32Config& config) const;
    void printStatus() const;

private:
    HardwareSerial* serial;
    int8_t rxPin;
    int8_t txPin;
    int8_t m0Pin;
    int8_t m1Pin;
    int8_t auxPin;

    E32Mode currentMode;
    bool initialized;

    // Timing
    uint32_t lastTransmitTime;
    uint32_t lastReceiveTime;

    // Error tracking
    uint32_t transmitErrors;
    uint32_t receiveErrors;

    // Private methods
    void setPinsForMode(E32Mode mode);
    bool enterConfigMode();
    bool exitConfigMode();
    bool readConfigurationBytes(uint8_t* buffer, size_t length);
    bool writeConfigurationBytes(const uint8_t* buffer, size_t length);

    // CRC calculation for configuration
    uint8_t calculateConfigCRC(const uint8_t* buffer, size_t length);
};

// ===========================
// Global Instance Access
// ===========================

extern E32LoRa& E32LoRaModule();

#endif // E32_LORA_H

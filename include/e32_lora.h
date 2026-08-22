#ifndef E32_LORA_H
#define E32_LORA_H

#include <Arduino.h>
#include <HardwareSerial.h>

// ===========================
// E32-900T30D LoRa Driver
// UART-based LoRa transceiver module
// ===========================
//
// Register-protocol ground truth: E32-T Series User Manual (official
// cdebyte.com PDF for E32-900T30D, sections 6.1-6.7, verified 2026-08-23):
//   - Sleep mode (M0=1, M1=1) accepts 6-byte control frames. Config-mode
//     UART is FIXED at 9600 baud 8N1 (6.1) — begin() must open the module
//     serial at 9600 for config commands to work (both boards do).
//   - 0xC0 + ADDH ADDL SPED CHAN OPTION : set parameters permanently
//     (EEPROM). 0xC2 + ... : temporary set (lost on power-down).
//   - 0xC1 0xC1 0xC1 : read current parameters.
//   - Both commands return a 6-byte frame: HEAD echo followed by the five
//     register bytes. HEAD is 0xC0 or 0xC2 per the register table (6.6) and
//     the manual's read example "C0 00 00 1A 06 44" (6.2); 0xC1 is also
//     accepted for older firmware revisions.
//   - Factory default, 900 MHz band (6.7): C0 00 00 1A 06 04
//     (ADDH=00 ADDL=00 SPED=1A CHAN=06 OPT=04 -> 9600 8N1, 2.4 kbps air,
//     868.125 MHz, 30 dBm, FEC on).

// E32 Module Modes (M0, M1 pins)
enum class E32Mode : uint8_t {
    MODE_NORMAL = 0x00,      // M0=0, M1=0 - Normal transmission mode
    MODE_WAKEUP = 0x01,      // M0=1, M1=0 - Wake-up mode
    MODE_POWER_SAVE = 0x02,  // M0=0, M1=1 - Power saving mode
    MODE_SLEEP = 0x03        // M0=1, M1=1 - Sleep/program mode
};

// SPED register (0x03) bit layout — manual 6.6 (worked example: 0x1A =
// 8N1 + 9600 + 2.4 kbps air):
//   bits [7:6] serial parity | bits [5:3] UART baud | bits [2:0] air rate
constexpr uint8_t E32_SPED_PARITY_MASK   = 0xC0;  // bits [7:6]
constexpr uint8_t E32_SPED_BAUD_MASK     = 0x38;  // bits [5:3]
constexpr uint8_t E32_SPED_AIR_RATE_MASK = 0x07;  // bits [2:0]

// SPED field decoders (manual 6.6)
inline uint8_t e32ParityBits(uint8_t sped) {
    return (sped & E32_SPED_PARITY_MASK) >> 6;      // 0=8N1 1=8O1 2=8E1 3=8N1
}
inline uint8_t e32UartBaudIndex(uint8_t sped) {
    return (sped & E32_SPED_BAUD_MASK) >> 3;        // 0..7, see E32UARTSpeed
}
inline uint8_t e32AirRateIndex(uint8_t sped) {
    return sped & E32_SPED_AIR_RATE_MASK;           // see E32AirDataRate
}

// E32 Module Configuration — mirrors the module's five configuration
// registers (manual 6.5/6.6). Each byte maps to/from the wire frame
// byte-for-byte; decoded views come from the SPED helpers above.
struct E32Config {
    uint8_t addh;    // 0x01 ADDH: module address high byte (default 0x00)
    uint8_t addl;    // 0x02 ADDL: module address low byte (default 0x00)
    uint8_t sped;    // 0x03 SPED: parity/baud/air-rate bits (default 0x1A)
    uint8_t chan;    // 0x04 CHAN: RF channel, 900 MHz band 0x00-0x45 (default 0x06)
    uint8_t option;  // 0x05 OPTION: fixed-transmit/wake-up/FEC/power bits (default 0x04)
};

// Transmit Power Levels — OPTION register bits [1:0], 30 dBm modules (manual 6.6)
enum class E32Power : uint8_t {
    POWER_30dBm = 0x00,  // 30dBm (T30D default)
    POWER_27dBm = 0x01,  // 27dBm
    POWER_24dBm = 0x02,  // 24dBm
    POWER_21dBm = 0x03   // 21dBm
};

// UART Speed Codes — SPED register bits [5:3] (manual 6.6)
enum class E32UARTSpeed : uint8_t {
    BAUD_1200 = 0x00,
    BAUD_2400 = 0x01,
    BAUD_4800 = 0x02,
    BAUD_9600 = 0x03,    // Factory default (config mode is FIXED here)
    BAUD_19200 = 0x04,
    BAUD_38400 = 0x05,
    BAUD_57600 = 0x06,
    BAUD_115200 = 0x07
};

// Air Data Rate Codes — SPED register bits [2:0] (manual 6.6).
// The T-series has NO 0.3k/1.2k settings: codes 000-010 are ALL 2.4 kbps
// and 110/111 are both 19.2 kbps, so only the distinct rates are
// enumerated. Factory default is 2.4 kbps (code 010) per manual 6.7 —
// the previous "9.6k default" comment here was wrong.
enum class E32AirDataRate : uint8_t {
    RATE_2_4kbps = 0x02,   // 010: 2.4kbps — FACTORY DEFAULT
    RATE_4_8kbps = 0x03,   // 011: 4.8kbps
    RATE_9_6kbps = 0x04,   // 100: 9.6kbps — link target (plan 01-08)
    RATE_19_2kbps = 0x05   // 101: 19.2kbps
};

// Link target air data rate (plan 01-08, G-01-3 root-cause-B remediation):
// 9.6 kbps. At the 2.4 kbps factory default each 216-byte chunk frame
// sub-packs into 4x58-byte RF packets costing ~1.0-1.4 s of air time; at
// 9.6 kbps that drops to ~0.3-0.4 s (4x less time-on-air), comfortably
// inside transmit()'s 5 s waitForAuxHigh bound (mechanism B1), quartering
// the per-chunk sub-packet loss exposure (B2) and halving channel
// occupation so window requests and GET_STATUS regain airtime (B4).
// Retune is this single line: 19.2 kbps (RATE_19_2kbps) is the next step
// up if bench traces still show starvation, 4.8 kbps (RATE_4_8kbps) the
// step down if range proves marginal.
constexpr uint8_t E32_TARGET_AIR_DATA_RATE =
    static_cast<uint8_t>(E32AirDataRate::RATE_9_6kbps);

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
    // Raw register frame: 0xC0 + the five registers, echo-verified.
    // Returns true ONLY when the module's return frame matches every
    // written register byte (no fabricated success).
    bool writeConfigRegisters(uint8_t addh, uint8_t addl, uint8_t sped,
                              uint8_t chan, uint8_t option);
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
    void ensureLinkConfig();
    bool enterConfigMode();
    bool exitConfigMode();
    bool readConfigurationBytes(uint8_t* buffer, size_t length);
    bool writeConfigurationBytes(const uint8_t* buffer, size_t length);
};

// ===========================
// Global Instance Access
// ===========================

extern E32LoRa& E32LoRaModule();

#endif // E32_LORA_H

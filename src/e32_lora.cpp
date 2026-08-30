#include "e32_lora.h"
// G-01-10 round #12 (01-28): uart_ll_is_tx_idle for the yielding TX-drain in
// transmit() — see .planning/debug/d1-crash-regression-push-start.md §7.6.
#include "hal/uart_ll.h"

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
// Register Decode Helpers (manual 6.6 tables)
// ===========================

// Valid HEAD echo bytes of a config return frame (manual 6.2/6.6:
// 0xC0/0xC2 per the register table; 0xC1 accepted for older revisions)
static bool isConfigHeadByte(uint8_t b) {
    return b == 0xC0 || b == 0xC1 || b == 0xC2;
}

static const char* uartBaudToString(uint8_t index) {
    static const char* const names[] = {
        "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"
    };
    return (index < 8) ? names[index] : "?";
}

static const char* airRateToString(uint8_t code) {
    // Codes 000-010 are all 2.4k and 101-111 all 19.2k on the T-series
    switch (code & 0x07) {
        case 3:  return "4.8kbps";
        case 4:  return "9.6kbps";
        case 0:
        case 1:
        case 2:  return "2.4kbps";
        default: return "19.2kbps";
    }
}

static const char* parityToString(uint8_t bits) {
    switch (bits & 0x03) {
        case 1:  return "8O1";
        case 2:  return "8E1";
        default: return "8N1";   // 0 and 3 are both 8N1 (manual 6.6)
    }
}

// Drain stale RX bytes so a config return frame parses from a clean start
static void drainConfigRx(HardwareSerial* serial) {
    while (serial->available()) {
        serial->read();
    }
}

// ===========================
// Constructor/Destructor
// ===========================

E32LoRa::E32LoRa()
    : serial(nullptr)
    , uartPort(2)
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
                    uint32_t baudRate, uint8_t uartPort) {
    if (!serial) {
        return false;
    }

    this->serial = serial;
    this->uartPort = uartPort;
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

    // Boot-time link convergence (plan 01-08): enforce the target air
    // data rate with zero main-file wiring — both boards call this same
    // begin(), so both converge on every boot. Fail-open by design.
    ensureLinkConfig();

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

    // G-01-10 D1 round #12 fix, Lever A (01-28) per
    // .planning/debug/d1-crash-regression-push-start.md §7.6: a YIELDING,
    // BOUNDED TX-drain replacing serial->flush(). The arduino core's flush
    // (uartFlushTxOnly, esp32-hal-uart.c:1474-1486) is a bare
    // `while(!uart_ll_is_tx_idle(...))` busy-spin with NO yield and NO
    // timeout — at 9600 baud a 240-byte chunk frame spins loopTask ~250 ms
    // per transmit, and a UART that never goes idle would hang it forever.
    // This drain polls the same hardware condition (TX fully shifted out)
    // with delay(1) yields, bounded at 1000 ms (worst legitimate drain at
    // 9600 baud is ~226 ms — the bound only trips on hardware failure, and
    // the AUX handshake below tolerates the residual FIFO). HONEST SCOPE per
    // §7.3/§7.5: the flush was ELIMINATED as session-8's direct starver —
    // this removes the fatal path's only non-yielding unbounded stretch; it
    // does not claim to remove the kernel-level stall §7.5 ranks.
    {
        uint32_t drainStart = millis();
        while (!uart_ll_is_tx_idle(UART_LL_GET_HW(uartPort))) {
            if (millis() - drainStart >= 1000) {
                if (DEBUG_E32) {
                    Serial.println("E32: TX-drain bound hit (1000 ms) - "
                                   "UART not idle (G-01-10)");
                }
                break;
            }
            delay(1);
        }
    }

    // Short write: a true failure — the module did not receive every byte,
    // so nothing may be forgiven below (01-11 branch c: only a COMPLETE
    // write may be treated as sent despite an AUX miss)
    if (sent != length) {
        if (DEBUG_E32) {
            Serial.printf("E32: Transmit short write - %zu of %zu bytes\n",
                          sent, length);
        }
        transmitErrors++;
        return false;
    }

    // AUX-low handshake after a COMPLETE write (01-11 branch c / G-01-5): a
    // miss here is NOT booked as a transmit failure. The 01-10 dual-console
    // discriminator proved the bytes have already left the radio — the
    // balloon printed 'E32: Transmit timeout - AUX didn't go low' on ~half
    // its sends (v3: 37 sent / 41 FAILED booked) while the base received
    // 72/72 chunks and finalized 4/4 kinds COMPLETE. Booking FAILED here
    // manufactured the phantom loss signal that drove spurious window
    // re-requests. Real air loss stays covered where it belongs: the
    // protocol's END-MARKER/CRC checks and the D-22 window heal on the base.
    if (!waitForAuxLow(1000)) {
        if (DEBUG_E32) {
            Serial.println("E32: AUX-low missed after complete write - "
                           "treated as sent (bytes left the radio)");
        }
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

    return true;
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
    // Config-mode UART is fixed at 9600 8N1 (manual 6.1); callers run the
    // module serial at 9600, so the port is usable as-is here.
    if (!enterConfigMode()) {
        return false;
    }

    // Discard any stale RX bytes so the return frame parses from byte 0
    drainConfigRx(serial);

    // Register-read command (manual 6.2): three 0xC1 bytes in sleep mode
    uint8_t readCmd[] = {0xC1, 0xC1, 0xC1};
    serial->write(readCmd, sizeof(readCmd));
    serial->flush();

    delay(100);

    // Return frame (manual 6.2/6.6): HEAD echo + ADDH ADDL SPED CHAN OPTION
    uint8_t buffer[6];
    if (!readConfigurationBytes(buffer, sizeof(buffer))) {
        if (DEBUG_E32) {
            Serial.println("E32: config read - return frame timeout");
        }
        exitConfigMode();
        return false;
    }
    if (!isConfigHeadByte(buffer[0])) {
        if (DEBUG_E32) {
            Serial.printf("E32: config read - bad head byte 0x%02X\n", buffer[0]);
        }
        exitConfigMode();
        return false;
    }

    config.addh   = buffer[1];
    config.addl   = buffer[2];
    config.sped   = buffer[3];
    config.chan   = buffer[4];
    config.option = buffer[5];

    exitConfigMode();

    // Decoded boot-config line: the 01-09 bench confirmation artifact and
    // the close of the debug session's unknown-module-config blind spot.
    if (DEBUG_E32) {
        Serial.printf("E32: cfg ADDH=%02X ADDL=%02X SPED=%02X CHAN=%02X OPT=%02X"
                      " -> uart %s %s air %s ch %u\n",
                      config.addh, config.addl, config.sped, config.chan,
                      config.option,
                      uartBaudToString(e32UartBaudIndex(config.sped)),
                      parityToString(e32ParityBits(config.sped)),
                      airRateToString(e32AirRateIndex(config.sped)),
                      static_cast<unsigned>(config.chan));
    }

    return true;
}

bool E32LoRa::writeConfig(const E32Config& config) {
    return writeConfigRegisters(config.addh, config.addl, config.sped,
                                config.chan, config.option);
}

bool E32LoRa::writeConfigRegisters(uint8_t addh, uint8_t addl, uint8_t sped,
                                   uint8_t chan, uint8_t option) {
    if (!enterConfigMode()) {
        return false;
    }

    // Discard any stale RX bytes so the echo frame parses from byte 0
    drainConfigRx(serial);

    // Permanent-set frame (manual 6.1/6.6): 0xC0 + the five registers.
    // 0xC0 persists across power loss (a 0xC2 head would be temporary).
    uint8_t frame[6] = {0xC0, addh, addl, sped, chan, option};
    if (!writeConfigurationBytes(frame, sizeof(frame))) {
        exitConfigMode();
        return false;
    }

    delay(100);   // module commits to EEPROM, then answers

    // Echo verification: the module returns HEAD + the five registers.
    // Success is reported ONLY when every register byte matches what was
    // written — no fabricated configuration state (01-08 prohibition).
    uint8_t echo[6];
    if (!readConfigurationBytes(echo, sizeof(echo))) {
        if (DEBUG_E32) {
            Serial.println("E32: config write - no return frame");
        }
        exitConfigMode();
        return false;
    }
    if (!isConfigHeadByte(echo[0]) ||
        echo[1] != addh || echo[2] != addl || echo[3] != sped ||
        echo[4] != chan || echo[5] != option) {
        if (DEBUG_E32) {
            Serial.printf("E32: config write - echo mismatch"
                          " (got %02X %02X %02X %02X %02X %02X)\n",
                          echo[0], echo[1], echo[2], echo[3], echo[4], echo[5]);
        }
        exitConfigMode();
        return false;
    }

    exitConfigMode();
    return true;
}

bool E32LoRa::setParameters(uint8_t uartSpeed, uint8_t airDataRate, uint8_t option) {
    // Read-modify-write: patch ONLY the baud and air-rate fields of SPED;
    // parity and every other register pass through from the fresh read.
    E32Config config;
    if (!readConfig(config)) {
        return false;
    }
    config.sped = static_cast<uint8_t>(
        (config.sped & ~(E32_SPED_BAUD_MASK | E32_SPED_AIR_RATE_MASK)) |
        ((uartSpeed << 3) & E32_SPED_BAUD_MASK) |
        (airDataRate & E32_SPED_AIR_RATE_MASK));
    config.option = option;
    return writeConfig(config);
}

bool E32LoRa::setAddress(uint16_t addressHigh, uint16_t addressLow) {
    // Read-modify-write. ADDH/ADDL are one byte each (manual 6.5); the
    // uint16_t parameters keep the legacy signature — only the low bytes
    // are used.
    E32Config config;
    if (!readConfig(config)) {
        return false;
    }
    config.addh = static_cast<uint8_t>(addressHigh & 0xFF);
    config.addl = static_cast<uint8_t>(addressLow & 0xFF);
    return writeConfig(config);
}

bool E32LoRa::setChannel(uint8_t channel) {
    // IMPLEMENTED via read-modify-write (not a no-op): CHAN is a whole
    // register and the factory default 06H confirms the raw channel value
    // IS the register value. 900 MHz band range 0x00-0x45 (manual 6.5).
    // NOTE: a channel change severs the pair until BOTH modules match —
    // no caller uses this today (plan 01-08 keeps the single-lever scope).
    if (channel > 0x45) {
        return false;
    }
    E32Config config;
    if (!readConfig(config)) {
        return false;
    }
    config.chan = channel;
    return writeConfig(config);
}

// Boot-time air-rate enforcement (plan 01-08, G-01-3): converges BOTH
// boards to E32_TARGET_AIR_DATA_RATE on every boot via a read-modify-write
// that touches ONLY the SPED air-rate bits. Fail-open in every path — a
// config failure never aborts boot and begin() still returns true (the
// 1223f46 boot-abort lesson: optional hardware failures must never block
// boot).
void E32LoRa::ensureLinkConfig() {
    E32Config config;
    if (!readConfig(config)) {
        // Fail-open: keep the module exactly as-is and continue booting.
        Serial.println("E32: config read failed - using module as-is");
        return;
    }

    uint8_t current = e32AirRateIndex(config.sped);
    if (current == E32_TARGET_AIR_DATA_RATE) {
        Serial.printf("E32: air rate already %s\n", airRateToString(current));
        return;
    }

    // Patch ONLY the air-rate bits [2:0]: UART baud index, parity bits and
    // every other register (ADDH/ADDL/CHAN/OPTION) pass through from the
    // fresh read byte-for-byte — the pair stays matched on everything the
    // firmware does not own.
    uint8_t patched = static_cast<uint8_t>(
        (config.sped & ~E32_SPED_AIR_RATE_MASK) | E32_TARGET_AIR_DATA_RATE);
    if (writeConfigRegisters(config.addh, config.addl, patched,
                             config.chan, config.option)) {
        Serial.printf("E32: air data rate %s -> %s (config persisted)\n",
                      airRateToString(current),
                      airRateToString(E32_TARGET_AIR_DATA_RATE));
    } else {
        // Loud tripwire: a one-board-only rate change severs the pair
        // until the other board runs this same firmware. The 01-09 bench
        // session treats this line as a stop condition.
        Serial.println("E32: ERROR air-rate set FAILED - modules may mismatch,"
                       " link check required");
    }
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
    Serial.printf("Address: %02X %02X\n", config.addh, config.addl);
    Serial.printf("UART: %s %s\n",
                  uartBaudToString(e32UartBaudIndex(config.sped)),
                  parityToString(e32ParityBits(config.sped)));
    Serial.printf("Air Data Rate: %s\n",
                  airRateToString(e32AirRateIndex(config.sped)));
    Serial.printf("Channel: %u\n", static_cast<unsigned>(config.chan));
    Serial.printf("Registers: ADDH=%02X ADDL=%02X SPED=%02X CHAN=%02X OPT=%02X\n",
                  config.addh, config.addl, config.sped, config.chan, config.option);
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

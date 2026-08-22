#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H

#include <Arduino.h>

// ===========================
// SSD1306 Status Display
// 128x64 OLED on the shared I2C bus (sensor_pins.h: SDA=1, SCL=2, 0x3C)
// Diagnostic aid for bench bring-up (UAT G-03-1): exposes radio, sensor,
// and beacon health so a dead link is readable without a laptop attached.
//
// Both firmwares render a snapshot struct their main loop fills from the
// same live sources the transmit/receive paths use — this module never
// reaches into other managers, and absent hardware degrades to no-ops
// (degrade, never halt).
// ===========================

// Balloon screen: radio + subsystem + beacon-transmit health
struct BalloonOledStatus {
    bool     e32Ready;
    bool     auxHigh;
    uint32_t txErrors;        // E32 transmit error counter
    bool     bmpOk;
    bool     camOk;
    uint8_t  gpsSats;         // 0 = no fix
    float    batteryV;        // 0 when the sense line is invalid
    uint16_t beaconSeq;       // last attempted 0x14 sequence number
    uint32_t beaconsSent;     // successful beacon transmits
    bool     lastBeaconOk;    // last attempt result
    uint32_t lastBeaconAgeMs;
    uint32_t upMs;
    size_t   freeHeap;
};

// Base screen: radio + received-beacon + serving-interface health
struct BaseOledStatus {
    bool     e32Ready;
    bool     auxHigh;
    bool     beaconValid;     // at least one CRC-verified 0x14 ever
    uint16_t beaconSeq;
    uint32_t beaconAgeMs;
    float    altitudeM;
    float    tempC;
    bool     wifiJoining;
    const char* wifiMode;     // "AP" | "STA"
    char     ip[16];
    bool     sdOk;
    uint16_t cmdsSent;
    uint16_t cmdsAcked;
    const char* link;         // "RDY" | "NO LINK" | "UNK"
};

class StatusDisplay {
public:
    enum class Board : uint8_t { BALLOON, BASE };

    StatusDisplay();

    // BALLOON: the sensor manager already brought the shared bus up
    // (Wire.begin in Sensors().begin()) — call AFTER sensors init.
    // BASE: this module owns the bus and brings it up itself.
    // Returns false when no panel answers — every other call becomes a no-op.
    bool begin(Board board);

    // One-line boot progress: lights the panel through the init sequence so
    // a hang is visible as the stage it stopped at
    void showBootStage(const char* stage);

    // Main screens — call ~1 Hz from the main loop (a full I2C frame costs
    // ~25-90 ms depending on bus clock; once a second that is well within
    // the loop budget the synchronous E32 transmit already sets)
    void render(const BalloonOledStatus& s);
    void render(const BaseOledStatus& s);

private:
    bool present;
    const char* boardName;
};

// ===========================
// Global Instance Access
// ===========================

extern StatusDisplay& StatusOLED();

#endif // STATUS_DISPLAY_H

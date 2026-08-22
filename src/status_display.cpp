#include "status_display.h"
#include "sensor_pins.h"
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// ===========================
// Static Instance
// ===========================

static StatusDisplay statusDisplayInstance;
StatusDisplay& StatusOLED() {
    return statusDisplayInstance;
}

// Shared-bus panel (SDA=1/SCL=2 per sensor_pins.h; 0x3C first, 0x3D fallback)
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// Text rows: 6x8 px glyphs -> 21 columns, 8 rows on 128x64. Every row below
// is snprintf'd to fit 21 chars so nothing runs off the panel.
static const uint8_t ROW_H = 8;

// OK/-- flags: one glyph pair that survives the narrow columns
static const char* flagStr(bool ok) { return ok ? "OK" : "--"; }

StatusDisplay::StatusDisplay()
    : present(false)
    , boardName("?") {}

bool StatusDisplay::begin(Board board) {
    present = false;

    if (board == Board::BASE) {
        // No other base-side module touches I2C — this panel owns the bus
        Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
        Wire.setClock(400000);  // full-frame refresh ~25 ms instead of ~90
    }
    // BALLOON: Sensors().begin() already owns Wire (shared with the BMP280).

    // Bring-up truth on UART0 (the CH343 USB bridge): a one-shot 7-bit bus
    // scan answers "which devices actually answer" without a logic analyzer
    // — expect 0x76 (BMP280, balloon) and 0x3C (this panel)
    Serial0.begin(115200);
    Serial0.print("[OLED] I2C scan:");
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial0.printf(" 0x%02X", addr);
        }
    }
    Serial0.println();

    // periphBegin=false — Adafruit's internal Wire.begin() would drop the
    // explicit pin choice above (and re-begin an already-running bus)
    uint8_t addrUsed = 0;
    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
        addrUsed = OLED_ADDRESS;
    } else if (oled.begin(SSD1306_SWITCHCAPVCC, 0x3D, true, false)) {
        addrUsed = 0x3D;
    }
    if (addrUsed == 0) {
        // Absent hardware: inert from here on, never a boot failure
        Serial0.println("[OLED] no panel at 0x3C/0x3D - screen disabled");
        return false;
    }
    Serial0.printf("[OLED] panel at 0x%02X\n", addrUsed);
    present = true;

    boardName = (board == Board::BASE) ? "BASE" : "BALLOON";
    showBootStage("BOOT");
    return true;
}

void StatusDisplay::showBootStage(const char* stage) {
    if (!present) return;

    char line[24];
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 28);
    snprintf(line, sizeof(line), "> %s", stage);
    oled.printf("%s", line);
    oled.setCursor(0, 44);
    oled.printf("%lu ms", static_cast<unsigned long>(millis()));
    // Header: inverted bar so a boot hang reads as the stopped stage
    oled.fillRect(0, 0, OLED_WIDTH, ROW_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(0, 0);
    oled.printf("COSMIC1 %s", boardName);
    oled.display();
}

// Uptime as H:MM:SS from a millis value
static void formatUp(uint32_t ms, char* out, size_t len) {
    uint32_t s = ms / 1000;
    snprintf(out, len, "%lu:%02lu:%02lu",
             static_cast<unsigned long>(s / 3600),
             static_cast<unsigned long>((s / 60) % 60),
             static_cast<unsigned long>(s % 60));
}

void StatusDisplay::render(const BalloonOledStatus& s) {
    if (!present) return;

    char up[12];
    formatUp(s.upMs, up, sizeof(up));

    oled.clearDisplay();
    oled.setTextSize(1);

    // Row 0 (inverted): identity + uptime
    oled.fillRect(0, 0, OLED_WIDTH, ROW_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(0, 0);
    oled.printf("BALN %-11s", up);
    oled.setTextColor(SSD1306_WHITE);

    // Row 1: radio health — the G-03-1 diagnosis line. E32 not ready or
    // AUX low means the balloon beacons into a dead radio.
    oled.setCursor(0, ROW_H);
    oled.printf("E32:%s AUX:%c ERR:%lu",
                flagStr(s.e32Ready), s.auxHigh ? 'H' : 'L',
                static_cast<unsigned long>(s.txErrors));

    // Row 2: subsystems + battery (0 V renders as absent)
    oled.setCursor(0, ROW_H * 2);
    if (s.batteryV >= 1.8f) {
        oled.printf("BMP:%s CAM:%s %.2fV",
                    flagStr(s.bmpOk), flagStr(s.camOk), s.batteryV);
    } else {
        oled.printf("BMP:%s CAM:%s BAT --",
                    flagStr(s.bmpOk), flagStr(s.camOk));
    }

    // Row 3: GPS
    oled.setCursor(0, ROW_H * 3);
    if (s.gpsSats > 0) {
        oled.printf("GPS:%u SATS", s.gpsSats);
    } else {
        oled.printf("GPS:NO FIX");
    }

    // Row 4: beacon truth — sequence advancing + last attempt result
    oled.setCursor(0, ROW_H * 4);
    if (s.beaconsSent == 0) {
        oled.printf("BCN NONE #%u", s.beaconSeq);
    } else if (s.lastBeaconOk) {
        oled.printf("BCN #%-5u %lus AGO",
                    s.beaconSeq,
                    static_cast<unsigned long>(s.lastBeaconAgeMs / 1000));
    } else {
        oled.printf("BCN #%u TX FAIL", s.beaconSeq);
    }

    // Row 5: heap
    oled.setCursor(0, ROW_H * 5);
    oled.printf("HEAP %uK", static_cast<unsigned>(s.freeHeap / 1024));

    oled.display();
}

void StatusDisplay::render(const BaseOledStatus& s) {
    if (!present) return;

    char up[12];
    // BaseOledStatus carries no uptime field of its own — the caller keeps
    // the clock; age lines below use beaconAgeMs only.
    formatUp(millis(), up, sizeof(up));

    oled.clearDisplay();
    oled.setTextSize(1);

    // Row 0 (inverted): identity + uptime
    oled.fillRect(0, 0, OLED_WIDTH, ROW_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(0, 0);
    oled.printf("BASE %-11s", up);
    oled.setTextColor(SSD1306_WHITE);

    // Row 1: radio health
    oled.setCursor(0, ROW_H);
    oled.printf("E32:%s AUX:%c %s",
                flagStr(s.e32Ready), s.auxHigh ? 'H' : 'L', s.link);

    // Row 2: newest CRC-verified beacon
    oled.setCursor(0, ROW_H * 2);
    if (s.beaconValid) {
        oled.printf("BCN #%-5u %lus AGO",
                    s.beaconSeq,
                    static_cast<unsigned long>(s.beaconAgeMs / 1000));
    } else {
        oled.printf("BCN NONE");
    }

    // Row 3: beacon contents (absent telemetry renders as absent)
    oled.setCursor(0, ROW_H * 3);
    if (s.beaconValid) {
        oled.printf("ALT %.0fM T %.1fC", s.altitudeM, s.tempC);
    } else {
        oled.printf("ALT --  T --");
    }

    // Row 4: serving WiFi interface (join in progress shows as JOINING)
    oled.setCursor(0, ROW_H * 4);
    if (s.wifiJoining) {
        oled.printf("WIFI JOINING...");
    } else {
        oled.printf("%s %.15s", s.wifiMode ? s.wifiMode : "--", s.ip);
    }

    // Row 5: storage + command accounting
    oled.setCursor(0, ROW_H * 5);
    oled.printf("SD:%s CMD %u/%u",
                flagStr(s.sdOk), s.cmdsAcked, s.cmdsSent);

    oled.display();
}

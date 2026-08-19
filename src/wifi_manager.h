#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

// ===========================
// WiFi Manager
// Base Station - NVS-persisted AP/Station mode switching (WEB-05, D-40)
// Phase 3: Enhanced Web Interface (plan 03-05)
// ===========================
// Replaces the hardcoded AP-only initWiFi(): the boot path prefers a
// persisted Station join with a bounded deadline and falls back to the
// Access Point on expiry so the operator is NEVER locked out; the runtime
// switch path keeps the current interface serving until the new one
// confirms, then tears the old one down the moment it does — the radio
// never parks in a dual-mode steady state.
//
// Persistence (Preferences namespace "wifi"): mode (u8, 0 = AP default,
// 1 = STA) and the STATION credentials only — the Access Point runs on
// compile-time constants in wifi_manager.cpp (never persisted, never
// editable from the UI; include/wifi_config.h belongs to the balloon's
// legacy camera app and is deliberately untouched, WEB-06).
//
// Every join deadline is a loop-driven millis-idiom state machine ticked
// from update() — never a blocking wait, so server.handleClient() keeps
// running throughout a join.

// D-40: every join attempt gets this long, then the module reverts to the
// Access Point. 20 s — long enough for a slow DHCP join, short enough to
// keep the fallback prompt.
static constexpr uint32_t WIFI_JOIN_TIMEOUT_MS = 20000;

// Station credential bounds (WR-07) — shared by POST /wifi and the module
static constexpr uint8_t WIFI_STA_SSID_MAX_LEN  = 32;
static constexpr uint8_t WIFI_STA_PASS_MIN_LEN  = 8;
static constexpr uint8_t WIFI_STA_PASS_MAX_LEN  = 63;

enum class WifiMode : uint8_t {
    ACCESS_POINT = 0,   // NVS default — the lockout-proof fallback mode
    STATION      = 1
};

// Queried radio truth for /api/state and the 📶 card: every field is read
// from the actually-serving interface (WiFi.getMode / localIP / softAPIP),
// never from the submitted form (IN-03 no-fabricated-state, extended to
// WiFi). The password is intentionally absent — it never appears in any
// response, card, or log (T-03-12).
struct WifiStatus {
    const char* mode;   // "AP" | "STA" — the serving interface
    String      ssid;   // serving network name (AP constant or station ssid)
    IPAddress   ip;     // serving interface address
    bool        joining;      // true while a join is inside its deadline
    String      errorSsid;    // non-empty while the last join failed (AP fallback)
};

class WiFiManager {
public:
    WiFiManager();

    // Load the NVS mode/credentials and bring up the radio: a saved
    // Station config joins with the 20 s deadline; anything else boots
    // the Access Point on the compile-time constants.
    bool begin();

    // Non-blocking state machine tick (loop-driven): in JOINING,
    // WL_CONNECTED confirms the station (and drops any interface kept up
    // during the join — single-mode the moment it confirms); the expired
    // deadline reverts to the Access Point with joinErrorSsid set.
    void update();

    // Persist the new mode (+ station credentials) to NVS FIRST — a
    // reboot mid-switch honors the operator's choice — then drive the
    // radio: to Station, keep the current interface serving until the
    // join confirms; to Access Point, bring softAP up (synchronous) and
    // drop Station. Returns false only when the NVS persist failed.
    bool requestSwitch(WifiMode mode, const String& ssid, const String& pass);

    // Queried truth from the radio's actual serving state
    WifiStatus getStatus() const;

    // Clear the last join-failure error row (also cleared automatically
    // by the next successful switch)
    void resetJoinError() { joinErrorSsid = ""; }

private:
    enum class State : uint8_t {
        ACTIVE_AP,   // Access Point serving on the compile-time constants
        JOINING,     // station join inside its deadline
        ACTIVE_STA   // station connected — the single serving interface
    };

    bool savePrefs(WifiMode mode);
    bool startAccessPoint();

    bool     initialized;
    State    state;
    uint32_t joinStartMs;     // deadline base (wrap-safe millis subtraction)
    String   staSsid;         // persisted station credentials (NVS "wifi")
    String   staPass;
    String   joinErrorSsid;   // ssid of the last failed join ("" when none)
};

// ===========================
// Global Instance Access
// ===========================

extern WiFiManager& WiFiMgr();

#endif // WIFI_MANAGER_H

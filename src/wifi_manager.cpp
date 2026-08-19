#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>

// ===========================
// Access Point credentials — COMPILE-TIME CONSTANTS (D-40)
// ===========================
// The AP is the lockout-proof fallback: its credentials are fixed at
// build time, never persisted to NVS, never editable from the UI — only
// STATION credentials are operator data. Relocated verbatim from
// main_basestation.cpp so the fallback definition lives beside the state
// machine that depends on it. (include/wifi_config.h belongs to the
// balloon's legacy camera app and is deliberately untouched, WEB-06.)

static const char* WIFI_AP_SSID           = "Cosmic1-BaseStation";
static const char* WIFI_AP_PASSWORD       = "balloontrack";
static const int   WIFI_AP_CHANNEL        = 6;
static const int   WIFI_AP_MAX_CONNECTIONS = 4;

// Preferences namespace + keys — the mode byte and station credentials
static const char* WIFI_PREFS_NAMESPACE = "wifi";
static const char* WIFI_PREFS_KEY_MODE  = "mode";   // u8 WifiMode
static const char* WIFI_PREFS_KEY_SSID  = "ssid";
static const char* WIFI_PREFS_KEY_PASS  = "pass";

// ===========================
// Static Instance
// ===========================

static WiFiManager wifiManagerInstance;
WiFiManager& WiFiMgr() {
    return wifiManagerInstance;
}

// ===========================
// Constructor
// ===========================

WiFiManager::WiFiManager()
    : initialized(false)
    , state(State::ACTIVE_AP)
    , joinStartMs(0)
{
}

// ===========================
// Lifecycle
// ===========================

bool WiFiManager::begin() {
    // D-40: never let the core auto-connect from its own stale NVS
    // credentials — this module owns every radio transition
    WiFi.persistent(false);

    Preferences prefs;
    WifiMode saved = WifiMode::ACCESS_POINT;
    if (prefs.begin(WIFI_PREFS_NAMESPACE, true)) {   // read-only open
        saved = static_cast<WifiMode>(prefs.getUChar(
            WIFI_PREFS_KEY_MODE, static_cast<uint8_t>(WifiMode::ACCESS_POINT)));
        staSsid = prefs.getString(WIFI_PREFS_KEY_SSID, "");
        staPass = prefs.getString(WIFI_PREFS_KEY_PASS, "");
        prefs.end();
    }

    // Boot prefers a persisted Station join; a missing/empty ssid means
    // the AP default. Either way a serving interface exists: the join
    // resolves within WIFI_JOIN_TIMEOUT_MS, the AP comes up immediately.
    if (saved == WifiMode::STATION && staSsid.length() > 0) {
        Serial.printf("WiFi: joining station \"%s\" (20 s deadline, then AP fallback)\n",
                      staSsid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(staSsid.c_str(), staPass.c_str());
        state = State::JOINING;
        joinStartMs = millis();
    } else {
        if (!startAccessPoint()) {
            return false;
        }
        state = State::ACTIVE_AP;
    }

    initialized = true;
    return true;
}

void WiFiManager::update() {
    if (!initialized) {
        return;
    }
    switch (state) {
    case State::JOINING: {
        if (WiFi.status() == WL_CONNECTED) {
            // The join confirmed: converge on the SINGLE station interface —
            // this drops any AP half that was kept serving during the join
            // (the radio never rests in a steady dual-mode state)
            WiFi.mode(WIFI_STA);
            state = State::ACTIVE_STA;
            joinErrorSsid = "";   // a successful switch clears the error row
            Serial.printf("WiFi: joined \"%s\" — station active at %s\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        } else if (millis() - joinStartMs >= WIFI_JOIN_TIMEOUT_MS) {
            // D-40 fallback: the deadline expired — halt the attempt and
            // bring the Access Point back on the constants so the operator
            // is never locked out; the card shows the error copy for THIS
            // ssid (cleared by resetJoinError or a later successful switch)
            WiFi.disconnect(false);   // halt the station join attempt
            startAccessPoint();       // WIFI_AP single mode — station half dropped
            state = State::ACTIVE_AP;
            joinErrorSsid = staSsid;
            Serial.printf("WiFi: could not join \"%s\" within %u s — Access Point fallback at %s\n",
                          staSsid.c_str(),
                          static_cast<unsigned>(WIFI_JOIN_TIMEOUT_MS / 1000),
                          WiFi.softAPIP().toString().c_str());
        }
        break;
    }
    case State::ACTIVE_AP:
    case State::ACTIVE_STA:
    default:
        break;
    }
}

// ===========================
// Runtime switching
// ===========================

bool WiFiManager::requestSwitch(WifiMode mode, const String& ssid, const String& pass) {
    if (!initialized) {
        return false;
    }

    // The serving address BEFORE anything changes — printed across the
    // whole switch so a DHCP address change stays discoverable in the log
    WifiStatus before = getStatus();
    Serial.printf("WiFi: switch to %s requested — currently %s at %s\n",
                  (mode == WifiMode::STATION) ? "Station" : "Access Point",
                  before.mode, before.ip.toString().c_str());

    if (mode == WifiMode::STATION) {
        // Persist FIRST (D-40): a reboot mid-switch lands in the chosen mode
        staSsid = ssid;
        staPass = pass;
        if (!savePrefs(WifiMode::STATION)) {
            return false;
        }
        // Fresh 20 s deadline on a non-blocking join. The current
        // interface keeps serving until the join confirms: WiFi.begin()
        // enables the station ALONGSIDE a running AP (the core ORs STA
        // into the current mode), and update() drops the AP half the
        // moment WL_CONNECTED arrives — or reverts to the AP when the
        // deadline expires. From ACTIVE_STA this simply re-associates.
        WiFi.disconnect(false);
        WiFi.begin(staSsid.c_str(), staPass.c_str());
        state = State::JOINING;
        joinStartMs = millis();
        Serial.printf("WiFi: joining \"%s\" — current interface stays up until the join resolves\n",
                      staSsid.c_str());
    } else {
        if (!savePrefs(WifiMode::ACCESS_POINT)) {
            return false;
        }
        // softAP() is synchronous on its fixed IP — the new interface is
        // serving the moment this returns, so the station half is dropped
        // in the same step (WIFI_AP single mode)
        WiFi.disconnect(false);
        if (!startAccessPoint()) {
            return false;
        }
        joinErrorSsid = "";   // an explicit switch is a successful switch
        state = State::ACTIVE_AP;
    }
    return true;
}

// ===========================
// Truth
// ===========================

WifiStatus WiFiManager::getStatus() const {
    WifiStatus s;
    s.joining = (state == State::JOINING);
    s.errorSsid = joinErrorSsid;

    // Queried truth (IN-03 extended to WiFi): the mode/IP come from the
    // radio's ACTUAL serving state — never the submitted form
    uint8_t m = static_cast<uint8_t>(WiFi.getMode());
    if ((m & static_cast<uint8_t>(WIFI_MODE_STA)) != 0
            && WiFi.status() == WL_CONNECTED) {
        s.mode = "STA";
        s.ssid = WiFi.SSID();
        s.ip = WiFi.localIP();
    } else if ((m & static_cast<uint8_t>(WIFI_MODE_AP)) != 0) {
        s.mode = "AP";
        s.ssid = WIFI_AP_SSID;
        s.ip = WiFi.softAPIP();
    } else {
        // Boot-time join in progress: the station radio is up but nothing
        // is serving yet — report the radio's own state (no address until
        // the join confirms; the 20 s fallback is coming)
        s.mode = "STA";
        s.ssid = staSsid;
        s.ip = WiFi.localIP();
    }
    return s;
}

// ===========================
// Internals
// ===========================

bool WiFiManager::savePrefs(WifiMode mode) {
    Preferences prefs;
    if (!prefs.begin(WIFI_PREFS_NAMESPACE, false)) {   // read-write open
        return false;
    }
    size_t okMode = prefs.putUChar(WIFI_PREFS_KEY_MODE, static_cast<uint8_t>(mode));
    size_t okSsid = prefs.putString(WIFI_PREFS_KEY_SSID, staSsid);
    size_t okPass = prefs.putString(WIFI_PREFS_KEY_PASS, staPass);
    prefs.end();
    return okMode == sizeof(uint8_t)
        && okSsid == staSsid.length()
        && okPass == staPass.length();
}

bool WiFiManager::startAccessPoint() {
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0,
                          WIFI_AP_MAX_CONNECTIONS);
    Serial.printf("WiFi: Access Point \"%s\" %s at %s\n",
                  WIFI_AP_SSID, ok ? "up" : "FAILED TO START",
                  WiFi.softAPIP().toString().c_str());
    return ok;
}

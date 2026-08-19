#ifndef ALERT_ENGINE_H
#define ALERT_ENGINE_H

#include <Arduino.h>
#include "image_rx_manager.h"   // TelemetrySnapshot — the single beacon truth

// ===========================
// Alert Engine
// Base Station - Six base-side alert conditions (ALRT-01..ALRT-06)
// Phase 3: Enhanced Web Interface (plan 03-03, D-41..D-44)
// ===========================
// Every condition evaluates BASE-SIDE from data the base already has:
// altitude / GPS / data age / battery from the 0x14 snapshot, ascent rate
// from altitude deltas between beacons, link quality from BE16 seq-gap
// accounting over the received beacon stream. Zero extra balloon airtime
// (D-41); thresholds persist base-side in NVS and never touch LoRa
// traffic (D-43); the module never reads the radio driver.
//
// Lifecycle (D-44): safety-critical alerts (BATTERY, GPS_LOST, LANDING)
// LATCH until acknowledged (ack()) and re-latch only when the condition
// re-fires after clearing; informational warnings (ALTITUDE, RATE,
// SIGNAL) auto-clear when the condition resolves, with a cooldown so a
// flapping condition cannot re-fire every evaluation.
//
// Evaluation is ALWAYS ON: begin() loads NVS-persisted thresholds over
// the compiled defaults — there is no opt-in flag anywhere.

enum class AlertType : uint8_t {
    ALTITUDE = 0,   // warning  — strictly above the warning level
    BATTERY,        // critical — below the low-battery threshold
    GPS_LOST,       // critical — sustained no-valid-fix age
    LANDING,        // critical — sustained low rate after altitude gain
    RATE,           // warning  — ascent/descent above the rate limit
    SIGNAL          // warning  — derived beacon-loss over the window
};
static constexpr uint8_t ALERT_TYPE_COUNT = 6;

// D-44 lifecycle class: critical latches until acknowledged, warnings
// auto-clear on resolve
inline bool alertTypeIsCritical(AlertType type) {
    return type == AlertType::BATTERY || type == AlertType::GPS_LOST
        || type == AlertType::LANDING;
}

// D-44 anti-flap: once a warning auto-clears, the same condition may not
// re-fire until this cooldown has elapsed
static constexpr uint32_t ALERT_REARM_COOLDOWN_MS = 30000;

// process() cadence — the base loop drives this from its millis idiom
static constexpr uint32_t ALERT_PROCESS_INTERVAL_MS = 1000;

// ALRT-06: derived link quality — percent of beacons missed over this
// sliding window (seq-gap accounting; the beacon stream itself is the
// measurement, the radio exposes no link metric)
static constexpr uint32_t ALERT_SIGNAL_WINDOW_MS = 60000;

// ALRT-04: landing requires this much altitude gain over the launch
// baseline before the sustained-low-rate test can arm
static constexpr int32_t ALERT_LANDING_MIN_GAIN_M = 50;

// WR-07 threshold ranges — shared verbatim by POST /alerts and the
// in-module revalidation (T-03-08: a bypassed writer cannot poison NVS)
static constexpr int32_t  ALERT_ALT_WARN_MIN_M    = 100;
static constexpr int32_t  ALERT_ALT_WARN_MAX_M    = 10000;
static constexpr float    ALERT_BATT_LOW_MIN_V    = 2.5f;
static constexpr float    ALERT_BATT_LOW_MAX_V    = 4.5f;
static constexpr uint16_t ALERT_GPS_LOST_MIN_S    = 10;
static constexpr uint16_t ALERT_GPS_LOST_MAX_S    = 300;
static constexpr uint8_t  ALERT_RATE_MIN_MPS      = 1;
static constexpr uint8_t  ALERT_RATE_MAX_MPS      = 50;
static constexpr uint8_t  ALERT_LOSS_MIN_PCT      = 10;
static constexpr uint8_t  ALERT_LOSS_MAX_PCT      = 90;
static constexpr float    ALERT_LAND_RATE_MIN_MPS = 0.5f;
static constexpr float    ALERT_LAND_RATE_MAX_MPS = 5.0f;
static constexpr uint16_t ALERT_LAND_STABLE_MIN_S = 30;
static constexpr uint16_t ALERT_LAND_STABLE_MAX_S = 600;

// Threshold set (D-43) — defaults anchored in the project's own config
// (ALTITUDE_THRESHOLD_HIGH, BATTERY_LOW_THRESHOLD, GPS_MAX_AGE_MS,
// EMERGENCY_ALTITUDE_RATE; the landing pair are conservative,
// UAT-calibratable placeholders per the 03-03 flagged assumptions).
struct AlertThresholds {
    int32_t  altWarnM       = 1000;  // ALRT-01 warning level (m)
    float    battLowV       = 3.3f;  // ALRT-02 low-battery floor (V)
    uint16_t gpsLostS       = 30;    // ALRT-03 sustained no-fix age (s)
    uint8_t  rateLimitMps   = 15;    // ALRT-05 ascent/descent limit (m/s)
    uint8_t  beaconLossPct  = 20;    // ALRT-06 loss warning level (%)
    float    landingRateMps = 1.0f;  // ALRT-04 "flat" rate bound (m/s)
    uint16_t landingStableS = 60;    // ALRT-04 sustained-flat time (s)
};

// One row of the alert snapshot handed to /api/state: type, lifecycle
// state, and the live values the browser composes banner copy from.
// v1/v2 mean per type — ALTITUDE: altitude m / level m · BATTERY: volts
// (or -1 while the balloon reports no valid voltage) / threshold V ·
// GPS_LOST: no-fix age s / threshold s · LANDING: m over the launch
// baseline / stable s · RATE: signed m/s / limit m/s · SIGNAL: loss % /
// threshold %.
struct AlertRow {
    AlertType type;
    bool latched;   // true only for critical rows awaiting acknowledge
    float v1;
    float v2;
};

// Full-set range validation (WR-07) — used by setThresholds and available
// to routes
bool alertThresholdsValid(const AlertThresholds& t);

class AlertEngine {
public:
    AlertEngine();

    // Load NVS-persisted thresholds over the defaults; evaluation is on
    // from the first process() call — no opt-in exists
    bool begin();

    // Loop-driven (1 s cadence from the caller's millis timer): consumes
    // the telemetry snapshot, advances the beacon-derived state, and
    // steps the six D-44 lifecycles
    void process();

    // WR-07 revalidation + NVS persist; false when any field is out of
    // range or the write failed
    bool setThresholds(const AlertThresholds& t);

    AlertThresholds getThresholds() const { return thresholds; }

    // Clear the latch of ONE type (POST /alerts/ack); returns false only
    // for an out-of-range type
    bool ack(AlertType type);

    // Active + latched rows, NEWEST first (fire time descending) — the
    // order the alert bar renders; returns the row count (<= maxRows)
    uint8_t getAlertSnapshot(AlertRow* rows, uint8_t maxRows) const;

private:
    // Ring of (reception time, seq) beacon arrivals feeding the ALRT-06
    // window — ~12 samples per 60 s at the locked 5 s cadence, 16 with
    // margin. Appends are seq-gated, so a re-polled duplicate snapshot
    // never double-counts.
    static constexpr uint8_t ALERT_SIGNAL_SAMPLES = 16;
    struct SignalSample {
        uint32_t tMs;
        uint16_t seq;
    };

    bool initialized;
    AlertThresholds thresholds;

    // D-44 lifecycle state, indexed by AlertType
    bool     condPrev[ALERT_TYPE_COUNT];       // last condition value (rising-edge latch)
    bool     warningActive[ALERT_TYPE_COUNT];  // warnings: condition currently shown
    bool     latched[ALERT_TYPE_COUNT];        // criticals: awaiting acknowledge
    uint32_t clearUntilMs[ALERT_TYPE_COUNT];   // warnings: cooldown gate after clear
    uint32_t fireMs[ALERT_TYPE_COUNT];         // newest-first ordering key
    float    rowV1[ALERT_TYPE_COUNT];          // live values while the row exists
    float    rowV2[ALERT_TYPE_COUNT];

    // Beacon-derived state (advanced only when snapshot.seq changes)
    bool     hasLastSeq;
    uint16_t lastSeq;
    uint32_t lastValidFixMs;      // 0 = no valid fix ever seen (ALRT-03 gate)
    int32_t  prevAltCm;           // previous beacon altitude (ALRT-05 deltas)
    uint32_t prevBeaconMs;
    bool     hasPrevBeacon;
    float    lastRateMps;         // signed m/s between the last two beacons
    bool     hasRate;
    int32_t  baselineAltCm;       // ALRT-04 launch baseline (first valid fix)
    bool     hasBaseline;
    int32_t  maxAltCm;            // ALRT-04 flight maximum
    bool     hasMaxAlt;
    bool     landingFlatRunning;  // sustained-|rate|-below-bound timer state
    uint32_t landingFlatSinceMs;
    bool     hasFirstBeacon;      // ALRT-06 window-establishment anchor
    uint32_t firstBeaconMs;

    SignalSample signalSamples[ALERT_SIGNAL_SAMPLES];  // oldest first
    uint8_t signalCount;

    void stepWarning(AlertType type, bool cond, uint32_t now, float v1, float v2);
    void stepCritical(AlertType type, bool cond, uint32_t now, float v1, float v2);
    void appendSignalSample(uint32_t tMs, uint16_t seq);
    float signalLossPct(uint32_t now) const;   // <0 while the window lacks data
};

// ===========================
// Global Instance Access
// ===========================

extern AlertEngine& Alerts();

#endif // ALERT_ENGINE_H

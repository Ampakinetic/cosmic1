#ifndef AUTO_CAPTURE_H
#define AUTO_CAPTURE_H

#include <Arduino.h>
#include "camera_manager.h"

// ===========================
// Auto Capture
// Balloon Unit - Single automatic-capture authority (interval + events)
// Phase 1: Command Protocol & Control (CTRL-03/CTRL-04)
// Phase 2: event triggers — altitude/distance deltas + flight-phase
//          first-entry (CTRL-05, D-25..D-28)
// ===========================

// Interval bounds (mirrored by CommandHandler validation and re-validated
// here so a bogus interval cannot drive the timer even if handler
// validation is bypassed)
static constexpr uint32_t AUTO_CAPTURE_MIN_INTERVAL_MS = 1000;    // 1 second
static constexpr uint32_t AUTO_CAPTURE_MAX_INTERVAL_MS = 3600000; // 1 hour

// Event-trigger bounds (D-25/D-26) — mirrored by CommandHandler validation
// (SET_EVENT_THRESHOLDS) and re-validated in setEventConfig so a bypassed
// handler cannot set absurd values (T-02-11 double validation: a crafted
// command cannot drive the spacing below 5 s or the deltas outside sane
// flight ranges)
static constexpr uint16_t EVENT_MIN_ALT_DELTA_M = 10;      // meters
static constexpr uint16_t EVENT_MAX_ALT_DELTA_M = 5000;    // meters
static constexpr uint16_t EVENT_MIN_DIST_DELTA_M = 10;     // meters
static constexpr uint16_t EVENT_MAX_DIST_DELTA_M = 50000;  // meters
static constexpr uint16_t EVENT_MIN_SPACING_S = 5;         // seconds (D-28)
static constexpr uint16_t EVENT_MAX_SPACING_S = 3600;      // seconds

// Flight defaults (research sketch constants; D-28 band midpoint for spacing)
static constexpr uint16_t EVENT_DEFAULT_ALT_DELTA_M = 150;   // meters
static constexpr uint16_t EVENT_DEFAULT_DIST_DELTA_M = 500;  // meters
static constexpr uint16_t EVENT_DEFAULT_MIN_SPACING_S = 20;  // seconds

// ===========================
// Auto Capture Class
// ===========================

// Event-trigger configuration (D-25/D-26) — commanded via SET_EVENT_THRESHOLDS
struct AutoCaptureEventConfig {
    uint16_t altDeltaM;    // altitude-delta trigger threshold (m)
    uint16_t distDeltaM;   // horizontal-distance delta threshold (m)
    uint16_t minSpacingS;  // D-28 global minimum spacing between ANY two
                           // automatic captures — interval and event alike
    bool eventsEnabled;    // event triggers stay off until explicitly enabled
};

class AutoCapture {
public:
    AutoCapture();

    // Initialization
    bool begin(CameraManager* camera);

    // Interval control (commanded via AUTO_CAPTURE_ENABLE/DISABLE)
    bool enable(uint32_t intervalMs);  // Validates bounds, resets baseline
    void disable();
    bool isEnabled() const { return enabled; }
    uint32_t getInterval() const { return intervalMs; }

    // Event-trigger control (commanded via SET_EVENT_THRESHOLDS). Idempotent:
    // re-sending the same config just re-sets it. Bounds are re-validated
    // in-module so a bypassed handler cannot set absurd values.
    bool setEventConfig(uint16_t altDeltaM, uint16_t distDeltaM,
                        uint16_t minSpacingS, bool eventsEnabled);
    AutoCaptureEventConfig getEventConfig() const { return eventConfig; }

    // Main processing - call from main loop
    void process();

    // Shared image ID sequence (manual and automatic captures)
    uint16_t allocateImageId();        // Pre-increments and returns the counter
    uint16_t getLastImageId() const { return lastImageId; }

    // WR-03 (01-19): a MANUAL capture resets the interval baseline exactly
    // as every automatic capture does inside fire() — D-27 arithmetic (any
    // capture resets the interval clock) and D-28 spacing (any two captures)
    // then hold for manual triggers too, so an interval capture can never
    // fire moments after a manual one. Safe to call while disabled: the
    // interval branch is gated on enabled, and enable() resets the baseline
    // regardless.
    void markCaptureBaseline();

private:
    CameraManager* camera;
    bool enabled;
    uint32_t intervalMs;
    uint32_t lastCaptureTime;
    uint16_t lastImageId;

    // Event-trigger state (D-25..D-28) — baselines track independently of
    // the ImageTx telemetry beacon's clock
    AutoCaptureEventConfig eventConfig;
    float lastEventAltM;
    double lastEventLat;
    double lastEventLon;
    bool eventBaselinesInit;
    uint8_t phaseSeenMask;   // one bit per FlightPhase value (8 max); a phase
                             // is "seen" exactly once per boot

    // The single gated capture path for every automatic source (interval and
    // events): enforces the D-28 spacing gate, stamps the capture source
    // (D-30) BEFORE capturing, advances the shared baseline before the
    // attempt (T-01-09), and allocates the image ID on success. Returns true
    // only when a capture actually happened.
    bool fire(uint8_t captureSource);

    // Event evaluation — runs from process() when enabled && eventsEnabled
    void processEvents();
};

// ===========================
// Global Instance Access
// ===========================

extern AutoCapture& AutoCap();

#endif // AUTO_CAPTURE_H

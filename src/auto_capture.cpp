#include "auto_capture.h"
#include "sensor_manager.h"   // Sensors() — GPS feed for the delta triggers
#include "system_state.h"     // SysState() — flight-phase machine
#include "image_protocol.h"   // CaptureSource values for source stamping
#include <TinyGPSPlus.h>      // distanceBetween static helper (research A2)

// Debug configuration
#ifndef DEBUG_AUTO_CAPTURE
#define DEBUG_AUTO_CAPTURE true
#endif

// ===========================
// Static Instance
// ===========================

static AutoCapture autoCaptureInstance;
AutoCapture& AutoCap() {
    return autoCaptureInstance;
}

// ===========================
// Constructor
// ===========================

AutoCapture::AutoCapture()
    : camera(nullptr)
    , enabled(false)
    , intervalMs(0)
    , lastCaptureTime(0)
    , lastImageId(0)
    , eventConfig{EVENT_DEFAULT_ALT_DELTA_M, EVENT_DEFAULT_DIST_DELTA_M,
                  EVENT_DEFAULT_MIN_SPACING_S, false}
    , lastEventAltM(0.0f)
    , lastEventLat(0.0)
    , lastEventLon(0.0)
    , eventBaselinesInit(false)
    , phaseSeenMask(0)
{
}

// ===========================
// Initialization
// ===========================

bool AutoCapture::begin(CameraManager* camera) {
    if (!camera) {
        return false;
    }

    this->camera = camera;

    if (DEBUG_AUTO_CAPTURE) {
        Serial.println("AutoCapture: Initialized");
    }

    return true;
}

// ===========================
// Interval Control
// ===========================

bool AutoCapture::enable(uint32_t intervalMs) {
    // Re-validate bounds (T-01-08): the timer cannot be driven below 1 Hz
    // even if the command handler validation is bypassed
    if (intervalMs < AUTO_CAPTURE_MIN_INTERVAL_MS || intervalMs > AUTO_CAPTURE_MAX_INTERVAL_MS) {
        return false;
    }

    this->intervalMs = intervalMs;
    enabled = true;

    // Reset the baseline so the first periodic capture fires one full
    // interval after enable, not immediately (also applies when already
    // enabled and only the interval changes)
    lastCaptureTime = millis();

    if (DEBUG_AUTO_CAPTURE) {
        Serial.printf("AutoCapture: Enabled with %lu ms interval\n", intervalMs);
    }

    return true;
}

void AutoCapture::disable() {
    // Master off-switch (Phase 1 SC-5): no automatic capture of ANY kind —
    // interval or event — fires while disabled
    enabled = false;

    if (DEBUG_AUTO_CAPTURE) {
        Serial.println("AutoCapture: Disabled");
    }
}

// ===========================
// Event-Trigger Control
// ===========================

bool AutoCapture::setEventConfig(uint16_t altDeltaM, uint16_t distDeltaM,
                                 uint16_t minSpacingS, bool eventsEnabled) {
    // Re-validate bounds (T-02-11): the handler validates the same ranges,
    // but the module enforces them too so a bypassed handler cannot set
    // absurd values (e.g. a sub-5 s spacing machine-gunning the camera)
    if (altDeltaM < EVENT_MIN_ALT_DELTA_M || altDeltaM > EVENT_MAX_ALT_DELTA_M ||
        distDeltaM < EVENT_MIN_DIST_DELTA_M || distDeltaM > EVENT_MAX_DIST_DELTA_M ||
        minSpacingS < EVENT_MIN_SPACING_S || minSpacingS > EVENT_MAX_SPACING_S) {
        return false;
    }

    eventConfig.altDeltaM = altDeltaM;
    eventConfig.distDeltaM = distDeltaM;
    eventConfig.minSpacingS = minSpacingS;
    eventConfig.eventsEnabled = eventsEnabled;

    if (DEBUG_AUTO_CAPTURE) {
        Serial.printf("AutoCapture: Event config — alt %u m, dist %u m, spacing %u s, events %s\n",
                      altDeltaM, distDeltaM, minSpacingS, eventsEnabled ? "ON" : "OFF");
    }

    return true;
}

// ===========================
// Main Processing
// ===========================

void AutoCapture::process() {
    // Master gate: AUTO_CAPTURE_DISABLE stops ALL automatic capture — the
    // interval branch and every event trigger live below this guard (SC-5)
    if (!enabled || camera == nullptr) {
        return;
    }

    // Interval branch (Phase 1 semantics, wraparound-safe millis idiom —
    // never absolute time comparison). D-28: the global minimum spacing
    // gates the interval branch too. Both conditions compare against
    // lastCaptureTime, which EVERY automatic capture advances — so an event
    // near the deadline resets the interval baseline (D-27: the next
    // interval capture counts a full interval from the event) with no
    // special-casing and no double-capture burst.
    uint32_t minSpacingMs = static_cast<uint32_t>(eventConfig.minSpacingS) * 1000UL;
    uint32_t elapsed = millis() - lastCaptureTime;
    if (elapsed >= intervalMs && elapsed >= minSpacingMs) {
        fire(static_cast<uint8_t>(CaptureSource::INTERVAL));
    }

    // Event evaluation only when the event triggers are separately enabled
    // (the master gate above has already confirmed enabled == true)
    if (eventConfig.eventsEnabled) {
        processEvents();
    }
}

// ===========================
// Event Evaluation (D-25..D-28)
// ===========================

void AutoCapture::processEvents() {
    // Flight-phase first-entry: independent of GPS — the phase machine runs
    // on its own logic. All eight phases set their seen bit exactly once per
    // boot; only the four operationally meaningful moments capture (research
    // Q3 — GROUND/RECOVERY and the two ascent sub-phases stay silent, since
    // steady ascent is the altitude trigger's job).
    FlightPhase phase = SysState().getFlightPhase();
    uint8_t phaseBit = static_cast<uint8_t>(1u << (static_cast<uint8_t>(phase) & 0x07u));
    if (!(phaseSeenMask & phaseBit)) {
        // Set the bit regardless of outcome: a spacing-gated-out phase event
        // is still a passed moment — stale intent is never queued
        phaseSeenMask |= phaseBit;
        switch (phase) {
            case FlightPhase::LAUNCH:
            case FlightPhase::APEX:
            case FlightPhase::PARACHUTE_DESCENT:
            case FlightPhase::LANDING:
                fire(static_cast<uint8_t>(CaptureSource::EVENT_PHASE));
                break;
            default:
                break; // non-triggering phase: seen bit set, no capture
        }
    }

    // Delta evaluation requires a valid GPS fix (Pitfall 12) — the same
    // validity signal the telemetry beacon uses (satellites > 0)
    GPSData gps = Sensors().getGPSData();
    if (gps.satellites == 0) {
        return;
    }

    // Baseline seeding: the first valid fix seeds the baselines WITHOUT
    // firing — deltas count movement from that point on
    if (!eventBaselinesInit) {
        lastEventAltM = gps.altitude;
        lastEventLat = gps.latitude;
        lastEventLon = gps.longitude;
        eventBaselinesInit = true;
        return;
    }

    // Altitude delta: |current - baseline| >= threshold. Hysteresis — the
    // baseline resets only on a FIRED event, so movement counts only beyond
    // the threshold from the last fired baseline
    if (fabsf(gps.altitude - lastEventAltM) >= static_cast<float>(eventConfig.altDeltaM)) {
        if (fire(static_cast<uint8_t>(CaptureSource::EVENT_ALTITUDE))) {
            lastEventAltM = gps.altitude;
            return;
        }
        // Gated out by spacing: baseline unchanged — the exceeded delta
        // re-arms and fires as soon as spacing allows
    }

    // Distance delta: TinyGPSPlus::distanceBetween static helper (research
    // A2, verified in the balloon env's bundled v1.0.3)
    double distM = TinyGPSPlus::distanceBetween(lastEventLat, lastEventLon,
                                                gps.latitude, gps.longitude);
    if (distM >= static_cast<double>(eventConfig.distDeltaM)) {
        if (fire(static_cast<uint8_t>(CaptureSource::EVENT_DISTANCE))) {
            // One movement event, one coherent baseline reset: position AND
            // altitude baselines move to the current fix
            lastEventLat = gps.latitude;
            lastEventLon = gps.longitude;
            lastEventAltM = gps.altitude;
        }
    }
}

// ===========================
// Gated Capture Path
// ===========================

bool AutoCapture::fire(uint8_t captureSource) {
    // D-28 global gate: minimum spacing between ANY two automatic captures
    // regardless of trigger source — GPS jitter or fast phase changes cannot
    // machine-gun the camera or flood the transfer queue
    uint32_t minSpacingMs = static_cast<uint32_t>(eventConfig.minSpacingS) * 1000UL;
    if (millis() - lastCaptureTime < minSpacingMs) {
        return false;
    }

    // D-30: stamp the true trigger BEFORE capturing so the manifest and
    // sidecar carry it (ImageTx reads getLastCaptureSource at enqueue)
    camera->setLastCaptureSource(captureSource);

    // Baseline updates BEFORE the attempt (T-01-09): a failed capture does
    // not reset the baseline early, preventing a tight failure loop
    lastCaptureTime = millis();

    if (camera->captureImage()) {
        lastImageId = allocateImageId();

        if (DEBUG_AUTO_CAPTURE) {
            Serial.printf("AutoCapture: Capture (source %u), image ID %d\n",
                          captureSource, lastImageId);
        }
        return true;
    }

    if (DEBUG_AUTO_CAPTURE) {
        Serial.printf("AutoCapture: Capture FAILED (source %u)\n", captureSource);
    }
    return false;
}

// ===========================
// Image ID Sequence
// ===========================

uint16_t AutoCapture::allocateImageId() {
    // Single sequence shared by manual (CAPTURE_NOW) and automatic captures;
    // wraps at 65535 (Phase 2 image sequencing owns durable IDs)
    return ++lastImageId;
}

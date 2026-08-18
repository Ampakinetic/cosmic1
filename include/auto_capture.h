#ifndef AUTO_CAPTURE_H
#define AUTO_CAPTURE_H

#include <Arduino.h>
#include "camera_manager.h"

// ===========================
// Auto Capture
// Balloon Unit - Interval-based automatic capture timer
// Phase 1: Command Protocol & Control (CTRL-03/CTRL-04)
// ===========================

// Interval bounds (mirrored by CommandHandler validation and re-validated
// here so a bogus interval cannot drive the timer even if handler
// validation is bypassed)
static constexpr uint32_t AUTO_CAPTURE_MIN_INTERVAL_MS = 1000;    // 1 second
static constexpr uint32_t AUTO_CAPTURE_MAX_INTERVAL_MS = 3600000; // 1 hour

// ===========================
// Auto Capture Class
// ===========================

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

    // Main processing - call from main loop
    void process();

    // Shared image ID sequence (manual and automatic captures)
    uint16_t allocateImageId();        // Pre-increments and returns the counter
    uint16_t getLastImageId() const { return lastImageId; }

private:
    CameraManager* camera;
    bool enabled;
    uint32_t intervalMs;
    uint32_t lastCaptureTime;
    uint16_t lastImageId;
};

// ===========================
// Global Instance Access
// ===========================

extern AutoCapture& AutoCap();

#endif // AUTO_CAPTURE_H

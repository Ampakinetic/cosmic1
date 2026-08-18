#include "auto_capture.h"

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
    enabled = false;

    if (DEBUG_AUTO_CAPTURE) {
        Serial.println("AutoCapture: Disabled");
    }
}

// ===========================
// Main Processing
// ===========================

void AutoCapture::process() {
    if (!enabled || camera == nullptr) {
        return;
    }

    // Wraparound-safe interval idiom (millis() difference, never absolute
    // time comparison) - same as CameraManager::isTimeToCapture
    if (millis() - lastCaptureTime >= intervalMs) {
        // Baseline updates BEFORE the attempt (T-01-09): a failed capture
        // does not reset the baseline early, preventing a tight failure loop
        lastCaptureTime = millis();

        if (camera->captureImage()) {
            lastImageId = allocateImageId();

            if (DEBUG_AUTO_CAPTURE) {
                Serial.printf("AutoCapture: Interval capture, image ID %d\n", lastImageId);
            }
        }
    }
}

// ===========================
// Image ID Sequence
// ===========================

uint16_t AutoCapture::allocateImageId() {
    // Single sequence shared by manual (CAPTURE_NOW) and automatic captures;
    // wraps at 65535 (Phase 2 image sequencing owns durable IDs)
    return ++lastImageId;
}

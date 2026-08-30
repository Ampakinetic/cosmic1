#include "auto_capture.h"
#include "sensor_manager.h"   // Sensors() — GPS feed for the delta triggers
#include "system_state.h"     // SysState() — flight-phase machine
#include "image_protocol.h"   // CaptureSource values for source stamping
#include "sd_store_balloon.h" // BalloonSdStoreTx() — the STORE-04 card-full capture gate (plan 02.5-03)
#include <TinyGPSPlus.h>      // distanceBetween static helper (research A2)
#include <Preferences.h>      // NVS — durable image-ID sequence (01-11, G-01-6)

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
// Image-ID Persistence (01-11 branch e / G-01-6)
// ===========================
//
// The image-ID counter was RAM-only: every reboot re-allocated ID 1, so a
// fresh-boot balloon's IMG_00001_* filenames OVERWROTE the card's existing
// image-1 files on the base ('CommandHandler: Captured image ID 1' on every
// fresh-boot session — balloon3.log:127; proven twice at the 01-10 bench
// with 4/4 COMPLETE transfers while /gallery stayed frozen at 6). NVS keeps
// the counter past reboot so captures append (IMG_00002_*) instead.
// File-scope on purpose: no header surface, callers unchanged.

static Preferences imageIdPrefs;          // NVS handle (namespace below)
static bool imageIdPrefsOpen = false;     // fail-open guard (NVS hiccup)
static const char kImageIdNamespace[] = "imgid";
static const char kImageIdKey[] = "last";
// G-01-10 (session-27): the deferred commit waits this long after the
// allocation that dirtied it — comfortably past the capture window (the
// WHEN_EMPTY refill completes ~100-200 ms after fb_return at XCLK 10 MHz),
// so the flash write never overlaps a live/just-rearmed LCD_CAM capture.
static const uint32_t kImageIdPersistDelayMs = 1500;

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
    , idPersistPending(false)
    , idPersistReqMs(0)
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

    // 01-11 (G-01-6): restore the durable image-ID sequence so a fresh-boot
    // balloon APPENDS instead of re-issuing ID 1. Fail-open: first boot (no
    // stored value) or an NVS failure degrades to the old RAM-only behavior
    // — a capture is never blocked by the persistence layer.
    imageIdPrefsOpen = imageIdPrefs.begin(kImageIdNamespace, false);

    if (imageIdPrefsOpen) {
        lastImageId = imageIdPrefs.getUShort(kImageIdKey, 0);
        if (DEBUG_AUTO_CAPTURE && lastImageId > 0) {
            Serial.printf("AutoCapture: image ID sequence restored from NVS - next capture is ID %u\n",
                          static_cast<unsigned>(lastImageId) + 1);
        }
    } else if (DEBUG_AUTO_CAPTURE) {
        Serial.println("AutoCapture: NVS unavailable - image IDs will NOT survive reboot (fail-open)");
    }

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

void AutoCapture::markCaptureBaseline() {
    // WR-03 (01-19): manual captures advance the SAME baseline every
    // automatic capture advances in fire() (same wraparound-safe millis
    // field, so the elapsed arithmetic in process() stays correct across
    // rollover). Called only from a successful manual capture — a failed
    // attempt must not defer the schedule.
    lastCaptureTime = millis();
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
    // G-01-10 (session-27): the deferred image-ID commit runs BEFORE the
    // master gate — manual CAPTURE_NOW allocations must persist even when
    // auto-capture is disabled (the balloon's bench and flight default).
    persistImageIdIfDue();

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

    // Baseline updates BEFORE the attempt (T-01-09): a failed capture does
    // not reset the baseline early, preventing a tight failure loop. The
    // advance sits ahead of the card-full gate below so a full card inherits
    // the SAME pacing — the next interval tick is the retry; no retry/backoff
    // machinery exists or is needed.
    lastCaptureTime = millis();

    // STORE-04 card-full gate (plan 02.5-03): skip BEFORE any camera work
    // when the flight archive lacks headroom. Worst-case reservation — a
    // refusal on estimate is honest (persistCapture's identical gate remains
    // the binding one). No image id is allocated, no source is stamped, no
    // camera/air resources are spent on a capture that cannot persist.
    if (!BalloonSdStoreTx().hasHeadroomFor(SD_STORE_CAPTURE_ESTIMATE_FULL,
                                           SD_STORE_CAPTURE_ESTIMATE_THUMB)) {
        Serial.printf("AutoCapture: capture skipped - card full (image id %u not taken)\n",
                      static_cast<unsigned>(static_cast<uint16_t>(lastImageId + 1)));
        return false;
    }

    // D-30: stamp the true trigger BEFORE capturing so the manifest and
    // sidecar carry it (ImageTx reads getLastCaptureSource at enqueue)
    camera->setLastCaptureSource(captureSource);

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
    ++lastImageId;

    // [CAPWIN] bisector (D1 round #16, debug doc section 29): every TG1WDT-era
    // death since balloon18 lands AFTER the "Image captured" print and BEFORE
    // the caller's image-ID print. These latch-free flushed lines now bracket
    // a µs-scale RAM-only interval — their value is the async-death
    // discriminator (a death BETWEEN them with nothing heavy in between is
    // corruption, not code). Removal: with the [MEM]/B1/B2/[STAMP]/[TICKSTAMP]
    // family after G-01-10 closes on bench evidence.
    Serial.printf("[CAPWIN] pre-id id=%u (G-01-10)\n", static_cast<unsigned>(lastImageId));
    Serial.flush();

    // G-01-10 (session-27, balloon24 A/B verdict): the id-commit is DEFERRED
    // out of the capture window. The A/B (flash write removed, everything
    // else identical) took the death rate from ~every capture (41 deaths /
    // 42 captures, balloon18-23) to zero, so the NVS flash write — a
    // cache-suspending SPI1 operation landing on a JUST-REARMED LCD_CAM
    // capture — is the named death interaction. The RAM ID stays the
    // authority handed out to callers (the 01-11 overwrite bug stays fixed:
    // every capture advances the counter instantly); the flash write follows
    // kImageIdPersistDelayMs later from process(). A crash in between costs
    // at most the last ID's persistence — the fail-open trade this NVS layer
    // has always accepted. A failed write still degrades to RAM-only for the
    // boot (logged at the deferred write, 01-11 contract preserved).
    if (imageIdPrefsOpen) {
        idPersistPending = true;
        idPersistReqMs = millis();
    }

    Serial.println("[CAPWIN] post-id (G-01-10)");
    Serial.flush();

    return lastImageId;
}

// The deferred half of the 01-11 image-ID persistence (G-01-10 session-27):
// called every loop pass from the TOP of process() — deliberately BEFORE the
// enabled/camera master gate, because manual CAPTURE_NOW allocations must
// persist too and the balloon runs auto-capture disabled.
void AutoCapture::persistImageIdIfDue() {
    if (!idPersistPending) {
        return;
    }
    if (!imageIdPrefsOpen) {
        idPersistPending = false;  // fail-open: RAM-only for the boot
        return;
    }
    // Wraparound-safe millis idiom; a NEWER allocation refreshes
    // idPersistReqMs while pending stays set, so one write always persists
    // the latest counter (NVS stores one value — committing lastImageId
    // covers every earlier allocation).
    if (millis() - idPersistReqMs < kImageIdPersistDelayMs) {
        return;  // not due yet
    }
    idPersistPending = false;  // one-shot: a failed write degrades to
                               // RAM-only for the boot (the 01-11 contract)

    // [CAPWIN] START/done pair (session-28, flushed): balloon25 died INSIDE
    // this write with only the done line in the binary — an unprinted done
    // line was ambiguous between "write died" and "write never ran". The
    // START line closes that hole: START present + done absent = the death
    // is inside the flash write itself. Removal: with the G-01-10
    // instrument family.
    Serial.println("[CAPWIN] deferred id-commit START (G-01-10)");
    Serial.flush();

    size_t written = imageIdPrefs.putUShort(kImageIdKey, lastImageId);

    Serial.printf("[CAPWIN] deferred id-commit id=%u written=%u (G-01-10)\n",
                  static_cast<unsigned>(lastImageId), static_cast<unsigned>(written));
    Serial.flush();
}

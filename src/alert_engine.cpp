#include "alert_engine.h"

#include <Preferences.h>

#include "image_protocol.h"   // TELEMETRY_BEACON_INTERVAL_MS (locked 5 s cadence)

// ===========================
// Static Instance
// ===========================

static AlertEngine alertEngineInstance;
AlertEngine& Alerts() {
    return alertEngineInstance;
}

// ===========================
// Constructor
// ===========================

AlertEngine::AlertEngine()
    : initialized(false)
    , hasLastSeq(false)
    , lastSeq(0)
    , lastValidFixMs(0)
    , prevAltCm(0)
    , prevBeaconMs(0)
    , hasPrevBeacon(false)
    , lastRateMps(0.0f)
    , hasRate(false)
    , baselineAltCm(0)
    , hasBaseline(false)
    , maxAltCm(0)
    , hasMaxAlt(false)
    , landingFlatRunning(false)
    , landingFlatSinceMs(0)
    , hasFirstBeacon(false)
    , firstBeaconMs(0)
    , signalCount(0)
{
    for (uint8_t i = 0; i < ALERT_TYPE_COUNT; i++) {
        condPrev[i] = false;
        warningActive[i] = false;
        latched[i] = false;
        clearUntilMs[i] = 0;
        fireMs[i] = 0;
        rowV1[i] = 0.0f;
        rowV2[i] = 0.0f;
    }
}

// ===========================
// NVS Persistence (D-43)
// ===========================

bool AlertEngine::begin() {
    // Defaults first — every persisted key loads OVER them and revalidates
    // against the WR-07 ranges in-module (T-03-08: a stale or corrupt NVS
    // value can never reach evaluation).
    thresholds = AlertThresholds{};

    Preferences prefs;
    prefs.begin("alerts", true);
    if (prefs.isKey("altWarnM")) {
        int32_t v = prefs.getInt("altWarnM", -1);
        if (v >= ALERT_ALT_WARN_MIN_M && v <= ALERT_ALT_WARN_MAX_M) {
            thresholds.altWarnM = v;
        }
    }
    if (prefs.isKey("battLowV")) {
        float v = prefs.getFloat("battLowV", -1.0f);
        if (v >= ALERT_BATT_LOW_MIN_V && v <= ALERT_BATT_LOW_MAX_V) {
            thresholds.battLowV = v;
        }
    }
    if (prefs.isKey("gpsLostS")) {
        int32_t v = prefs.getInt("gpsLostS", -1);
        if (v >= ALERT_GPS_LOST_MIN_S && v <= ALERT_GPS_LOST_MAX_S) {
            thresholds.gpsLostS = static_cast<uint16_t>(v);
        }
    }
    if (prefs.isKey("rateLimit")) {
        int32_t v = prefs.getInt("rateLimit", -1);
        if (v >= ALERT_RATE_MIN_MPS && v <= ALERT_RATE_MAX_MPS) {
            thresholds.rateLimitMps = static_cast<uint8_t>(v);
        }
    }
    if (prefs.isKey("beaconLossPct")) {
        int32_t v = prefs.getInt("beaconLossPct", -1);
        if (v >= ALERT_LOSS_MIN_PCT && v <= ALERT_LOSS_MAX_PCT) {
            thresholds.beaconLossPct = static_cast<uint8_t>(v);
        }
    }
    if (prefs.isKey("landRate")) {
        float v = prefs.getFloat("landRate", -1.0f);
        if (v >= ALERT_LAND_RATE_MIN_MPS && v <= ALERT_LAND_RATE_MAX_MPS) {
            thresholds.landingRateMps = v;
        }
    }
    if (prefs.isKey("landStable")) {
        int32_t v = prefs.getInt("landStable", -1);
        if (v >= ALERT_LAND_STABLE_MIN_S && v <= ALERT_LAND_STABLE_MAX_S) {
            thresholds.landingStableS = static_cast<uint16_t>(v);
        }
    }
    prefs.end();

    initialized = true;
    Serial.printf("AlertEngine: ready — evaluation ON (defaults %ld m / %.1f V / %u s / %u m/s / %u%% / %.1f m/s / %u s)\n",
                  static_cast<long>(thresholds.altWarnM), thresholds.battLowV,
                  thresholds.gpsLostS, thresholds.rateLimitMps,
                  thresholds.beaconLossPct, thresholds.landingRateMps,
                  thresholds.landingStableS);
    return true;
}

bool alertThresholdsValid(const AlertThresholds& t) {
    return t.altWarnM >= ALERT_ALT_WARN_MIN_M && t.altWarnM <= ALERT_ALT_WARN_MAX_M
        && t.battLowV >= ALERT_BATT_LOW_MIN_V && t.battLowV <= ALERT_BATT_LOW_MAX_V
        && t.gpsLostS >= ALERT_GPS_LOST_MIN_S && t.gpsLostS <= ALERT_GPS_LOST_MAX_S
        && t.rateLimitMps >= ALERT_RATE_MIN_MPS && t.rateLimitMps <= ALERT_RATE_MAX_MPS
        && t.beaconLossPct >= ALERT_LOSS_MIN_PCT && t.beaconLossPct <= ALERT_LOSS_MAX_PCT
        && t.landingRateMps >= ALERT_LAND_RATE_MIN_MPS && t.landingRateMps <= ALERT_LAND_RATE_MAX_MPS
        && t.landingStableS >= ALERT_LAND_STABLE_MIN_S && t.landingStableS <= ALERT_LAND_STABLE_MAX_S;
}

bool AlertEngine::setThresholds(const AlertThresholds& t) {
    // In-module WR-07 revalidation (T-03-08) — a route bug or bypassed
    // writer can never persist an unsafe value
    if (!alertThresholdsValid(t)) {
        return false;
    }
    thresholds = t;
    Preferences prefs;
    prefs.begin("alerts", false);
    bool ok = prefs.putInt("altWarnM", t.altWarnM) != 0;
    ok = prefs.putFloat("battLowV", t.battLowV) != 0 && ok;
    ok = prefs.putInt("gpsLostS", static_cast<int32_t>(t.gpsLostS)) != 0 && ok;
    ok = prefs.putInt("rateLimit", static_cast<int32_t>(t.rateLimitMps)) != 0 && ok;
    ok = prefs.putInt("beaconLossPct", static_cast<int32_t>(t.beaconLossPct)) != 0 && ok;
    ok = prefs.putFloat("landRate", t.landingRateMps) != 0 && ok;
    ok = prefs.putInt("landStable", static_cast<int32_t>(t.landingStableS)) != 0 && ok;
    prefs.end();
    return ok;
}

// ===========================
// Acknowledge (D-44)
// ===========================

bool AlertEngine::ack(AlertType type) {
    uint8_t i = static_cast<uint8_t>(type);
    if (i >= ALERT_TYPE_COUNT) {
        return false;
    }
    // Clear the latch; re-latch requires a fresh rising edge of the
    // condition (ack while the condition persists does NOT re-latch)
    latched[i] = false;
    return true;
}

// ===========================
// Evaluation
// ===========================

void AlertEngine::process() {
    if (!initialized) {
        return;
    }

    const TelemetrySnapshot& s = ImageRx().getTelemetrySnapshot();
    uint32_t now = millis();

    // Data-age term (Pitfall 9): altitude/rate/landing never evaluate a
    // stale snapshot as fresh — 3x the locked cadence is the freshness
    // bound. The GPS_LOST age logic intentionally owns the opposite
    // semantics (age is its signal).
    bool fresh = s.valid
        && (now - s.receivedMs) <= 3 * TELEMETRY_BEACON_INTERVAL_MS;

    // ---- advance beacon-derived state on each NEW beacon (seq-gated;
    // a re-polled duplicate snapshot is a no-op) ----
    if (s.valid && (!hasLastSeq || s.seq != lastSeq)) {
        int32_t altCm = lroundf(s.altitudeM * 100.0f);

        // ALRT-05: rate from raw cm deltas and elapsed millis — the
        // unrounded value is the only one compared (display rounds later)
        if (hasPrevBeacon && s.receivedMs > prevBeaconMs) {
            lastRateMps = (altCm - prevAltCm) * 10.0f
                        / static_cast<float>(s.receivedMs - prevBeaconMs);
            hasRate = true;
        }
        prevAltCm = altCm;
        prevBeaconMs = s.receivedMs;
        hasPrevBeacon = true;

        // ALRT-03: age is measured from the last beacon that CARRIED a
        // valid fix — a transfer-delayed beacon with a valid fix resets
        // it, an on-time beacon without one does not
        if (s.gpsValid) {
            lastValidFixMs = s.receivedMs;
        }

        // ALRT-04 bookkeeping: launch baseline = first valid fix;
        // flight maximum from every beacon
        if (s.gpsValid && !hasBaseline) {
            baselineAltCm = altCm;
            hasBaseline = true;
        }
        if (!hasMaxAlt || altCm > maxAltCm) {
            maxAltCm = altCm;
            hasMaxAlt = true;
        }

        // ALRT-06 window: one arrival record per seq advance
        appendSignalSample(s.receivedMs, s.seq);

        lastSeq = s.seq;
        hasLastSeq = true;
    }

    // ALRT-04: sustained-|rate|-below-bound timer — breaks on stale data
    // (stability is a claim about live beacons, not remembered ones)
    bool landingFlat = hasRate && fresh
        && fabsf(lastRateMps) < thresholds.landingRateMps;
    if (landingFlat) {
        if (!landingFlatRunning) {
            landingFlatRunning = true;
            landingFlatSinceMs = now;
        }
    } else {
        landingFlatRunning = false;
    }

    // ---- the six conditions, all base-side (D-41) ----

    // ALRT-01 (warning): strictly above, compared in the beacon's raw cm
    // units — the 1-decimal meters display never decides the alert
    bool condAltitude = false;
    if (fresh) {
        int32_t altCm = lroundf(s.altitudeM * 100.0f);
        condAltitude = altCm > thresholds.altWarnM * 100;
    }
    stepWarning(AlertType::ALTITUDE, condAltitude, now,
                fresh ? s.altitudeM : 0.0f,
                static_cast<float>(thresholds.altWarnM));

    // ALRT-02 (critical): integer millivolts, validity-gated — a beacon
    // without valid battery data never fires it. v1 = -1 sentinel on a
    // latched row while the balloon reports no valid voltage (the
    // browser renders the not-reported copy).
    bool condBattery = s.batteryValid
        && static_cast<int32_t>(s.batteryMv)
               < lroundf(thresholds.battLowV * 1000.0f);
    stepCritical(AlertType::BATTERY, condBattery, now,
                 s.batteryValid
                     ? static_cast<float>(s.batteryMv) / 1000.0f
                     : -1.0f,
                 thresholds.battLowV);

    // ALRT-03 (critical): sustained no-valid-fix age, measured from the
    // last valid fix — a single transfer-delayed beacon cannot false-fire
    // and never having had a fix never fires either
    bool condGpsLost = (lastValidFixMs != 0)
        && (now - lastValidFixMs)
               > static_cast<uint32_t>(thresholds.gpsLostS) * 1000UL;
    stepCritical(AlertType::GPS_LOST, condGpsLost, now,
                 (lastValidFixMs != 0)
                     ? static_cast<float>((now - lastValidFixMs) / 1000UL)
                     : 0.0f,
                 static_cast<float>(thresholds.gpsLostS));

    // ALRT-04 (critical): > 50 m gain over the launch baseline AND
    // |rate| below the bound continuously for landingStableS
    bool condLanding = hasBaseline && hasMaxAlt
        && (maxAltCm - baselineAltCm) > ALERT_LANDING_MIN_GAIN_M * 100
        && landingFlatRunning
        && (now - landingFlatSinceMs)
               >= static_cast<uint32_t>(thresholds.landingStableS) * 1000UL;
    float landAboveM = (hasBaseline && fresh)
        ? static_cast<float>(lroundf(s.altitudeM * 100.0f) - baselineAltCm) / 100.0f
        : 0.0f;
    float landStableS = landingFlatRunning
        ? static_cast<float>((now - landingFlatSinceMs) / 1000UL)
        : 0.0f;
    stepCritical(AlertType::LANDING, condLanding, now, landAboveM, landStableS);

    // ALRT-05 (warning): |rate| strictly above the limit, compared
    // unrounded from raw deltas
    bool condRate = fresh && hasRate
        && fabsf(lastRateMps) > static_cast<float>(thresholds.rateLimitMps);
    stepWarning(AlertType::RATE, condRate, now,
                hasRate ? lastRateMps : 0.0f,
                static_cast<float>(thresholds.rateLimitMps));

    // ALRT-06 (warning): derived link quality — percent of beacons missed
    // over the 60 s window (seq-gap accounting); strictly above the level
    float lossPct = signalLossPct(now);
    bool condSignal = (lossPct >= 0.0f)
        && lossPct > static_cast<float>(thresholds.beaconLossPct);
    stepWarning(AlertType::SIGNAL, condSignal, now,
                (lossPct >= 0.0f) ? lossPct : 0.0f,
                static_cast<float>(thresholds.beaconLossPct));
}

// ===========================
// D-44 Lifecycles
// ===========================

void AlertEngine::stepWarning(AlertType type, bool cond, uint32_t now,
                              float v1, float v2) {
    uint8_t i = static_cast<uint8_t>(type);
    if (cond) {
        if (!warningActive[i] && now >= clearUntilMs[i]) {
            warningActive[i] = true;    // fire (or re-fire after cooldown)
            fireMs[i] = now;
        }
    } else if (warningActive[i]) {
        warningActive[i] = false;       // auto-clear + cooldown
        clearUntilMs[i] = now + ALERT_REARM_COOLDOWN_MS;
    }
    if (warningActive[i]) {
        rowV1[i] = v1;                  // live values while the row shows
        rowV2[i] = v2;
    }
}

void AlertEngine::stepCritical(AlertType type, bool cond, uint32_t now,
                               float v1, float v2) {
    uint8_t i = static_cast<uint8_t>(type);
    // Rising-edge latch (D-44): acknowledging while the condition
    // persists keeps the row hidden until the condition clears AND
    // re-fires
    if (cond && !condPrev[i] && !latched[i]) {
        latched[i] = true;
        fireMs[i] = now;
    }
    condPrev[i] = cond;
    if (latched[i]) {
        rowV1[i] = v1;                  // live values until acknowledged
        rowV2[i] = v2;
    }
}

// ===========================
// ALRT-06 Signal Window
// ===========================

void AlertEngine::appendSignalSample(uint32_t tMs, uint16_t seq) {
    // First-ever beacon anchors the window-establishment check
    if (!hasFirstBeacon) {
        hasFirstBeacon = true;
        firstBeaconMs = tMs;
    }
    // Drop samples outside the window and reserve one slot for the new
    // arrival (oldest-first shifting buffer; <= 13 samples live in a 60 s
    // window at the locked 5 s cadence, so 16 never wraps mid-window)
    uint32_t cutoff = (tMs > ALERT_SIGNAL_WINDOW_MS)
                    ? tMs - ALERT_SIGNAL_WINDOW_MS : 0;
    uint8_t keep = ALERT_SIGNAL_SAMPLES - 1;
    uint8_t w = 0;
    for (uint8_t r = 0; r < signalCount; r++) {
        if (signalSamples[r].tMs >= cutoff && w < keep) {
            signalSamples[w++] = signalSamples[r];
        }
    }
    signalCount = w;
    signalSamples[signalCount].tMs = tMs;
    signalSamples[signalCount].seq = seq;
    signalCount++;
}

float AlertEngine::signalLossPct(uint32_t now) const {
    // < 0 = not enough data for a statement yet
    if (!hasFirstBeacon) {
        return -1.0f;
    }
    uint32_t cutoff = (now > ALERT_SIGNAL_WINDOW_MS)
                    ? now - ALERT_SIGNAL_WINDOW_MS : 0;
    uint8_t received = 0;
    uint32_t firstInWin = 0;
    bool haveFirstInWin = false;
    for (uint8_t i = 0; i < signalCount; i++) {
        if (signalSamples[i].tMs >= cutoff) {
            if (!haveFirstInWin) {
                firstInWin = signalSamples[i].tMs;
                haveFirstInWin = true;
            }
            received++;
        }
    }
    float expected;
    if (now - firstBeaconMs >= ALERT_SIGNAL_WINDOW_MS) {
        // Established window: with the balloon's cadence locked at
        // TELEMETRY_BEACON_INTERVAL_MS, expected slots per 60 s window
        // are exact — and missed = expected - received covers interior
        // seq gaps AND the window edges (a pure interior-gap scan is
        // blind to loss at both edges, including a total outage)
        expected = static_cast<float>(ALERT_SIGNAL_WINDOW_MS)
                 / static_cast<float>(TELEMETRY_BEACON_INTERVAL_MS);
    } else {
        // Young window (boot < 60 s): evaluate only once at least two
        // arrivals span two completed intervals — earlier than that any
        // percentage is noise
        if (received < 2 || !haveFirstInWin) {
            return -1.0f;
        }
        uint32_t elapsed = now - firstInWin;
        if (elapsed < 2 * TELEMETRY_BEACON_INTERVAL_MS) {
            return -1.0f;
        }
        expected = static_cast<float>(elapsed / TELEMETRY_BEACON_INTERVAL_MS);
        if (expected < 2.0f) {
            return -1.0f;
        }
    }
    float missed = expected - static_cast<float>(received);
    if (missed < 0.0f) {
        missed = 0.0f;   // boundary jitter can momentarily over-deliver
    }
    return missed * 100.0f / expected;
}

// ===========================
// Snapshot
// ===========================

uint8_t AlertEngine::getAlertSnapshot(AlertRow* rows, uint8_t maxRows) const {
    AlertRow present[ALERT_TYPE_COUNT];
    uint32_t keys[ALERT_TYPE_COUNT];
    uint8_t n = 0;
    for (uint8_t i = 0; i < ALERT_TYPE_COUNT; i++) {
        bool isCrit = alertTypeIsCritical(static_cast<AlertType>(i));
        if (isCrit ? latched[i] : warningActive[i]) {
            present[n].type = static_cast<AlertType>(i);
            present[n].latched = latched[i];   // warnings report false
            present[n].v1 = rowV1[i];
            present[n].v2 = rowV2[i];
            keys[n] = fireMs[i];
            n++;
        }
    }
    // Newest first (fire time descending) — the render order of the bar
    for (uint8_t i = 1; i < n; i++) {          // insertion sort, n <= 6
        AlertRow r = present[i];
        uint32_t k = keys[i];
        uint8_t j = i;
        while (j > 0 && keys[j - 1] < k) {
            present[j] = present[j - 1];
            keys[j] = keys[j - 1];
            j--;
        }
        present[j] = r;
        keys[j] = k;
    }
    uint8_t out = (n < maxRows) ? n : maxRows;
    for (uint8_t i = 0; i < out; i++) {
        rows[i] = present[i];
    }
    return out;
}

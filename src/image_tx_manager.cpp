#include "image_tx_manager.h"
#include "sd_store_balloon.h"
#include "auto_capture.h"
#include "sensor_manager.h"
#include "power_manager.h"
#include "sensor_pins.h"
#include <esp_rom_crc.h>
#include <esp_heap_caps.h>
// G-01-10 round #12 discriminator B1 (01-28): esp_reset_reason() for the
// boot reset-cause line — .planning/debug/d1-crash-regression-push-start.md §7.6
#include <esp_system.h>
// G-01-10 round #13 instrument [TWDT] (01-30): esp_task_wdt_status() for the
// boot-time IDLE0-subscription line — same doc §9.6
#include <esp_task_wdt.h>

// Debug configuration
#ifndef DEBUG_IMAGE_TX
#define DEBUG_IMAGE_TX true
#endif

// ===========================
// Static Instance
// ===========================

static ImageTxManager imageTxInstance;
ImageTxManager& ImageTx() {
    return imageTxInstance;
}

// ===========================
// Constructor/Destructor
// ===========================

ImageTxManager::ImageTxManager()
    : lora(nullptr)
    , initialized(false)
    , bootResetCause(ESP_RST_UNKNOWN)
    , nextEnqueueSeq(0)
    , lastEnqueuedImageId(0)
    , lastBeaconMs(0)
    , beaconSeq(0)
    , firstBeaconLogged(false)
    , beaconsSent(0)
    , lastBeaconOk(false)
    , memDiagLastMs(0)
    , memDiagMinHeap(0)
    , memDiagMinStackHw(0)
    , lastProcessPassMs(0)
    , loopSlowPassLogged(false)
{
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        entries[i] = ImageTxEntry{};
        entries[i].state = ImageTxEntryState::IDLE;
    }
}

ImageTxManager::~ImageTxManager() {
    end();
}

// ===========================
// Initialization
// ===========================

// G-01-10 round #12 discriminator B1 (01-28): local reset-reason name map.
// IDF 5.5.4's prebuilt headers expose NO esp_reset_reason_to_name() —
// esp_system.h declares only esp_reset_reason() — so the mapping is local.
// Full 16-value enum per esp_system.h (verified against the deployed
// framework-arduinoespressif32-libs/esp32s3 headers).
static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:     return "UNKNOWN";
        case ESP_RST_POWERON:     return "POWERON";
        case ESP_RST_EXT:         return "EXT";
        case ESP_RST_SW:          return "SW";
        case ESP_RST_PANIC:       return "PANIC";
        case ESP_RST_INT_WDT:     return "INT_WDT";
        case ESP_RST_TASK_WDT:    return "TASK_WDT";
        case ESP_RST_WDT:         return "WDT";
        case ESP_RST_DEEPSLEEP:   return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:    return "BROWNOUT";
        case ESP_RST_SDIO:        return "SDIO";
        case ESP_RST_USB:         return "USB";
        case ESP_RST_JTAG:        return "JTAG";
        case ESP_RST_EFUSE:       return "EFUSE";
        case ESP_RST_PWR_GLITCH:  return "PWR_GLITCH";
        case ESP_RST_CPU_LOCKUP:  return "CPU_LOCKUP";
        default:                  return "UNMAPPED";
    }
}

// Crash-class reset predicate — the balloon9 crash-loop gate (2026-08-30).
// The D1 silent-reset family (TG0WDT stage-1 hardware reset, sessions 8/9 and
// balloon8/9) surfaces as esp_reset_reason() TASK_WDT; the other cases are
// the same firmware-died-unexpectedly class. On such a boot the 02.5-02
// boot-rescan resume is SKIPPED (the gate lives in main_balloon.cpp's
// wiring): every balloon9 silent reset died mid-push, so the unconditional
// resume re-entered the crash-correlated card/push path ~2.5 s into every
// boot (Uptime 2537 ms, six consecutive resets) and died again before the
// base's COMPLETE reply could be processed — the delivery livelock. A
// POWERON/EXT/SW/brownout-class boot resumes normally, and no data is
// stranded either way: handleFullRequest re-admits any archived record on
// the base's request. MITIGATION, not the D1 root-cause fix — its fate rides
// the G-01-10 outcome, not this gate.
static bool resetCauseIsCrashClass(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_TASK_WDT:    // the D1 silent TG0WDT family
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
        case ESP_RST_PANIC:
        case ESP_RST_CPU_LOCKUP:
            return true;
        default:
            return false;
    }
}

bool ImageTxManager::begin(E32LoRa* lora) {
    if (!lora) {
        return false;
    }

    this->lora = lora;
    initialized = true;

    // G-01-10 D1 fix, lever 1 (01-25) per
    // .planning/debug/d1-crash-regression-push-start.md §3: boot-time
    // first-use PSRAM heap warm-up. Both session-7 crashes hit during the
    // FIRST post-boot capture push — the session's first run-time ps_malloc,
    // PSRAM memcpy and CRC. Allocating, touching (cache-line-filling) and
    // freeing one IMG_MAX_IMAGE_SIZE-class block here, BEFORE the loop
    // starts, moves that first-use frontier to boot where a fault is visible
    // and harmless. Inert if first-use is not the trigger.
    if (psramFound()) {
        uint8_t* warm = (uint8_t*)ps_malloc(IMG_MAX_IMAGE_SIZE);
        if (warm != nullptr) {
            memset(warm, 0xA5, IMG_MAX_IMAGE_SIZE);
            free(warm);
            Serial.println("ImageTx: PSRAM first-use warm-up done (G-01-10)");
        } else {
            Serial.println("ImageTx: PSRAM warm-up alloc failed (G-01-10)");
        }
    }

    // G-01-10 D1 instrumentation baselines (01-25): latch post-setup min-heap
    // and loopTask stack high-water so the process() watch prints only NEW
    // lows from this point on.
    memDiagLastMs = millis();
    memDiagMinHeap = esp_get_minimum_free_heap_size();
    memDiagMinStackHw = uxTaskGetStackHighWaterMark(nullptr);

    // G-01-10 round #12 discriminator B2 state (01-28): re-baseline the
    // process()-pass stamp at begin() so a re-begin cannot carry a stale
    // epoch into the slow-pass watch (a first pass after begin() records its
    // stamp without a gap check).
    lastProcessPassMs = 0;
    loopSlowPassLogged = false;

    // G-01-10 D1 round #12 discriminator B1 (01-28) per
    // .planning/debug/d1-crash-regression-push-start.md §7.6: boot reset-cause
    // line. Gives the 01-29 bench the reset class on every boot even when the
    // ROM banner (rst:0x7 TG0WDT_SYS_RST …) scrolls out of console capture —
    // session 8's class decodes here as TASK_WDT/INT_WDT-class names instead
    // of a raw ROM code. REMOVAL CONDITION: strips WITH the [MEM]
    // instrumentation after G-01-10 closes on bench evidence.
    // Latch once; the B1 line and the crash-loop gate (see
    // resetCauseIsCrashClass above) both read this member, so the wiring's
    // skip decision and the console always quote the SAME reset class.
    bootResetCause = esp_reset_reason();
    Serial.printf("ImageTx: [BOOT] reset-cause: %s (G-01-10)\n",
                  resetReasonName(bootResetCause));

    // G-01-10 round #13 instrument [TWDT] (01-30) per
    // .planning/debug/d1-crash-regression-push-start.md §9.6: boot-time
    // one-shot confirmation that IDLE0 is subscribed to the task WDT.
    // sdkconfig has CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y and
    // INCLUDE_xTaskGetIdleTaskHandle=1, so status(idle0) is expected
    // ESP_OK — turning §7.1's stage-0-inferred-from-silence into a positive
    // boot fact: once stage-0 is proven armed, a future SILENT rst:0x7 with
    // no "Tasks currently running" print is affirmative evidence the
    // stage-0 INT path died, not a config assumption. REMOVAL CONDITION:
    // strips WITH the [MEM] instrumentation after G-01-10 closes on bench
    // evidence.
    Serial.printf("ImageTx: [TWDT] idle0 wdt status=%s (G-01-10)\n",
                  esp_err_to_name(
                      esp_task_wdt_status(xTaskGetIdleTaskHandleForCPU(0))));
    // ROUND-#14 HANDLE FIX (01-32) per .planning/debug/
    // d1-crash-regression-push-start.md §10.5 item 2: the round-#13 call
    // used the core-ambiguous xTaskGetIdleTaskHandle() from CPU1 setup()
    // context — it returned IDLE1's handle at the session-10 boots, whose
    // ESP_ERR_NOT_FOUND is exactly what config predicts for the UNsubscribed
    // CPU1 idle task (sdkconfig CHECK_IDLE_TASK_CPU1 not set) — the
    // wrong-handle instrument bug reading (a). The explicit ForCPU(0) call
    // samples IDLE0, whose configured subscription makes ESP_OK the
    // expected 01-34 reading. Declaration site on the pinned framework
    // (pioarduino arduino-esp32 3.3.9): freertos/idf_additions.h:645 — a
    // deprecated inline forwarding to xTaskGetIdleTaskHandleForCore(:144);
    // not in task.h. Log format, esp_err_to_name rendering, the comment
    // above, and the instrument's removal condition above all unchanged.

    if (DEBUG_IMAGE_TX) {
        Serial.println("ImageTx: Initialized");
    }

    return true;
}

void ImageTxManager::end() {
    initialized = false;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        freeEntry(entries[i]);
    }
}

// Boot reset-cause facts — see the header block by the declarations. The
// accessors expose the begin()-latched reason to the resume wiring so the
// gate and the B1 console line cannot disagree.
const char* ImageTxManager::bootResetCauseName() const {
    return resetReasonName(bootResetCause);
}

bool ImageTxManager::bootResetWasCrashClass() const {
    return resetCauseIsCrashClass(bootResetCause);
}

// ===========================
// Main Processing
// ===========================

void ImageTxManager::process() {
    if (!initialized || lora == nullptr) {
        return;
    }

    uint32_t now = millis();

    // G-01-10 D1 round #12 discriminator B2 (01-28) per
    // .planning/debug/d1-crash-regression-push-start.md §7.6: latch-guarded
    // slow-pass watch. A gap between consecutive process() calls beyond
    // IMG_LOOP_SLOW_PASS_MS (2500 ms — ≈2.3× the ~1102 ms sustained
    // FULL-window service cadence, half the TWDT stage-0 period) means the
    // main loop itself stalled. 01-29's discriminating read: a TG0WDT reset
    // with NO preceding [LOOP] line = loopTask never stalled (the session-8
    // CPU0-side signature); [LOOP] lines before a reset = the stall caught
    // the loop too (a different, loop-visible class). One line per episode
    // (log-once latch), re-armed on a normal pass.
    // REMOVAL CONDITION: strips WITH the [MEM] instrumentation after G-01-10
    // closes on bench evidence.
    if (lastProcessPassMs != 0) {
        uint32_t passGap = now - lastProcessPassMs;
        if (passGap > IMG_LOOP_SLOW_PASS_MS) {
            if (!loopSlowPassLogged) {
                loopSlowPassLogged = true;
                Serial.printf("ImageTx: [LOOP] slow pass gap %lu ms (G-01-10)\n",
                              (unsigned long)passGap);
            }
        } else {
            loopSlowPassLogged = false;
        }
    }
    lastProcessPassMs = now;

    // G-01-10 D1 instrumentation watch (01-25): 1 s-throttled check that
    // prints one [MEM] line ONLY when min-ever internal heap or the loopTask
    // stack high-water hits a new low (monotone quantities — naturally
    // bounded, a handful of lines per session at most). The enqueue-time
    // [MEM] line is the primary bench discriminator; this watch catches
    // degradation between captures. REMOVAL: with the enqueue site, after
    // 01-27 closes G-01-10.
    if ((uint32_t)(now - memDiagLastMs) >= 1000) {
        memDiagLastMs = now;
        uint32_t minHeap = esp_get_minimum_free_heap_size();
        UBaseType_t stackHw = uxTaskGetStackHighWaterMark(nullptr);
        if (minHeap < memDiagMinHeap || stackHw < memDiagMinStackHw) {
            memDiagMinHeap = minHeap;
            memDiagMinStackHw = stackHw;
            logMemDiagnostic("new-low");
        }
    }

    // Eviction policy (c): entries idle longer than IMG_ENTRY_TTL_MS are
    // evicted with a log — bounded memory regardless of capture rate
    // (bookkeeping only; costs no transmit).
    sweepExpiredEntries();

    // Poll the single image-ID authority every pass (CR-05 invariant):
    // AutoCap().getLastImageId() is the ONLY image-ID source. A changed ID
    // means a capture completed and its buffers must be taken over BEFORE
    // the next capture frees them.
    uint16_t currentId = AutoCap().getLastImageId();
    if (currentId != lastEnqueuedImageId) {
        uint16_t enqueueId = currentId;
        lastEnqueuedImageId = currentId;
        enqueueCapture(enqueueId);
    }

    // TX arbitration (Pattern 6 / PRI-01): when the telemetry beacon is due
    // it consumes this pass's single transmit and RETURNS before any chunk
    // work — flight telemetry always wins the radio over image traffic.
    // Command responses already outrank the beacon by LOOP ORDER
    // (CmdHandler().process() runs before ImageTx().process()). Wraparound-
    // safe millis subtraction; a beacon (or response) is never delayed by
    // more than one chunk transmit (~250-400 ms).
    if ((millis() - lastBeaconMs) >= TELEMETRY_BEACON_INTERVAL_MS) {
        sendTelemetryBeacon();
        return; // one transmit per pass — beacon XOR chunk, never both
    }

    // Push discipline (Pattern 4): exactly ONE transmit per process() pass —
    // the E32 transmit is synchronous and costs ~250-400 ms of blocked loop
    pushPending();
}

// ===========================
// Telemetry Beacon (PRI-01 / SC-5, 0x14 transmit side)
// ===========================

bool ImageTxManager::sendTelemetryBeacon() {
    // Baseline BEFORE the attempt (AutoCapture millis idiom): a failed
    // transmit cannot drive a tight retry loop — the next due cycle retries,
    // and the failure is logged below rather than silently skipped.
    lastBeaconMs = millis();

    // Live sensor state — the same sources main_balloon feeds SysState
    GPSData gps = Sensors().getGPSData();
    BMP280Data bmp = Sensors().getBMP280Data();

    // GPS validity: the same signal main_balloon uses for appState.gpsActive
    bool gpsValid = (gps.satellites > 0);

    TelemetryBeaconBody body{};
    body.seq = beaconSeq++;
    if (gpsValid) {
        body.altitudeCm = static_cast<int32_t>(lroundf(gps.altitude * 100.0f));
        body.latE6 = static_cast<int32_t>(lroundf(gps.latitude * 1000000.0f));
        body.lonE6 = static_cast<int32_t>(lroundf(gps.longitude * 1000000.0f));
    } else {
        // No fix: still beacon — altitude/lat/lon zero, gpsValid clear. A
        // stale gap in telemetry is worse than an invalid-flagged sample, and
        // the base reports honestly from the flag.
        body.altitudeCm = 0;
        body.latE6 = 0;
        body.lonE6 = 0;
    }
    body.tempCentiC = static_cast<int16_t>(lroundf(bmp.temperature * 100.0f));

    // Battery rides the beacon (D-41 / ALRT-02): mV is truth only when the
    // sense line actually sees a pack — voltage within physical range AND a
    // nonzero raw ADC reading. Otherwise batteryMilliV stays 0 (from the {}
    // init) with bit1 clear, and the base renders the honest absent state.
    bool batteryValid = false;
    #ifdef BATTERY_SENSE_PIN
        float batteryV = PowerMgr().getBatteryVoltage();
        if (batteryV >= 1.8f && batteryV <= 8.0f && analogRead(BATTERY_SENSE_PIN) != 0) {
            body.batteryMilliV = static_cast<uint16_t>(lroundf(batteryV * 1000.0f));
            batteryValid = true;
        }
    #else
        batteryValid = true;
    #endif
    
    body.flags = static_cast<uint8_t>((gpsValid ? 0x01 : 0x00) | (batteryValid ? 0x02 : 0x00));

    TelemetryBeaconPacket pkt = createTelemetryBeaconPacket(body); // factory owns the 0x14 type byte

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeTelemetryBeacon(pkt, buffer, length) &&
              lora->transmit(buffer, length);

    // OLED diagnostics (status_display): latch the attempt result so the
    // balloon screen can distinguish a dead radio (TX FAIL) from a link the
    // base is not hearing (seq advancing, OK)
    lastBeaconOk = ok;
    if (ok) {
        beaconsSent++;
    }

    // Transition-only logging (not per beacon): the first beacon after boot
    // and every transmit failure
    if (!firstBeaconLogged) {
        firstBeaconLogged = true;
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: telemetry beacon stream started (seq %u, %s)%s\n",
                         static_cast<unsigned>(body.seq),
                         gpsValid ? "gps valid" : "no gps fix",
                         ok ? "" : " — transmit FAILED");
        }
    } else if (!ok) {
        Serial.printf("ImageTx: telemetry beacon transmit FAILED (seq %u)\n",
                     static_cast<unsigned>(body.seq));
    }
    return ok;
}

// ===========================
// Enqueue (ownership transfer)
// ===========================

// G-01-7 round #15 (plan 01-35): the safe-reclaim predicate — an entry whose
// FULL transfer is fully receipt-confirmed. Every WINDOW_COMPLETE receipt the
// base sends is merged into fullAcked by handleImageAck and the whole map is
// cleared by KIND_FAILED_CRC, so all-bits-set means the base provably holds
// every byte of the full and the entry's buffers are reclaimable. Reads only
// state that already exists on the entry (no new fields, no header change);
// the 16-word bitmap covers the 919-chunk wire cap (IMG_MAX_IMAGE_SIZE
// 204800 / IMG_CHUNK_PAYLOAD_SIZE). Guarded on fullTotalChunks > 0 so a
// thumb-only entry (the 01-26 full-unavailable degradation path) never ranks
// class 0 vacuously.
static bool fullReceiptComplete(const ImageTxEntry& entry) {
    if (entry.fullTotalChunks == 0) {
        return false;
    }
    for (uint16_t chunk = 0; chunk < entry.fullTotalChunks; chunk++) {
        if ((entry.fullAcked[chunk >> 6] & (1ULL << (chunk & 63))) == 0) {
            return false;
        }
    }
    return true;
}

// Overflow-eviction class (02-05 / CR-03 fix c) — LOWER class evicted sooner:
//   0 FULL fully receipt-confirmed (every WINDOW_COMPLETE in)
//                            safe reclaim: the base provably holds every
//                            byte; buffers reclaimable (G-01-7 round #15,
//                            01-35 — the session-11 over-rejection routing)
//   1 THUMB_PUSHED            parked, nothing left to deliver
//   2 SERVED                  full offered through its tail; heal-only
//   3 ANNOUNCED (!everArmed && never requested)
//                            queued, no receipt evidence — never armed, never
//                            asked for (01-21: an inbound window request IS
//                            receipt evidence — the base demonstrably holds a
//                            manifest and is asking, per session-6 image 24,
//                            evicted at balloon11.log:406 while the base held
//                            its manifest, base6.log:3947)
//   4 PUSH_THUMB_* / ANNOUNCE_FULL   push still in flight
//   5 ANNOUNCED (everArmed or receipt-evidenced)
//                            the ACTIVE-PULL context, armed or between
//                            windows, or evidenced by an inbound request —
//                            LAST resort (D-19)
// ONE shared ranking: both the enqueue-overflow scan and the supersede scan
// call this function — no forked or supersede-private ranking (T-01-33-02).
static uint8_t evictionClassOf(const ImageTxEntry& entry) {
    // Round #15: the fully-receipted safe reclaim outranks everything — a
    // fully-delivered entry is exactly the waste the session-11 over-rejection
    // mode named (an honest reject that sacrificed admission to a completed
    // transfer's TTL residue).
    if (fullReceiptComplete(entry)) {
        return 0;
    }
    switch (entry.state) {
        case ImageTxEntryState::THUMB_PUSHED:
            return 1;
        case ImageTxEntryState::SERVED:
            return 2;
        case ImageTxEntryState::ANNOUNCED:
            return (entry.windowEverArmed || entry.lastWindowRequestMs != 0) ? 5 : 3;
        case ImageTxEntryState::PUSH_THUMB_MANIFEST:
        case ImageTxEntryState::PUSH_THUMB_CHUNKS:
        case ImageTxEntryState::ANNOUNCE_FULL:
        default:
            return 4;
    }
}

// G-01-10 D1 instrumentation (01-25): one bounded [MEM] diagnostic line.
// Prints internal-heap free, lifetime-min internal heap, PSRAM free, and the
// loopTask stack high-water mark (words; NULL = calling task = loopTask for
// every call site in this file). Bounded: per-capture at enqueue plus
// new-monotone-low events only. REMOVAL CONDITION: 01-27 bench closes
// G-01-10 (.planning/debug/d1-crash-regression-push-start.md §3, lever 2).
void ImageTxManager::logMemDiagnostic(const char* phase) {
    Serial.printf("ImageTx: [MEM] %s heap=%u minHeap=%u psram=%u stackHW=%u\n",
                  phase,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void ImageTxManager::enqueueCapture(uint16_t imageId) {
    ImageData img = Camera().getCurrentImage();
    if (!img.valid || img.buffer == nullptr) {
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: image %u has no valid full buffer; nothing to enqueue\n", imageId);
        }
        return;
    }

    // G-01-10 D1 instrumentation (01-25): one [MEM] line per capture at the
    // exact session-7 crash phase (enqueue → thumbnail → PSRAM copy →
    // manifest). Bounded by capture count. REMOVAL: strip after the 01-27
    // bench closes G-01-10 (debug doc §3, lever 2).
    logMemDiagnostic("enqueue");

    // FIRST caller of the CR-04-fixed createThumbnail path — Task 1 made
    // this safe (no dangling thumbnail member on failure)
    bool haveThumb = Camera().captureThumbnail();
    ThumbnailData thumb = Camera().getCurrentThumbnail();

    ImageTxEntry entry{};
    entry.used = true;
    entry.enqueueSeq = nextEnqueueSeq++;
    entry.imageId = imageId;
    entry.captureSource = Camera().getLastCaptureSource();
    entry.captureTimeMs = img.timestamp;
    entry.settings = snapshotSettings();

    // Enqueue gate (research Q4 resolution / PRI-03): a full image larger
    // than IMG_MAX_IMAGE_SIZE never ARMS a full transfer — the wire is
    // frozen and the base's startTransfer validates totalSize against its
    // own MAX_IMAGE_SIZE, so a larger manifest would be rejected base-side
    // (the cap is a wire-contract bound, NOT a balloon RAM limit anymore —
    // the Phase 2.5 file-backed path holds no PSRAM image copy). KEEP-
    // EVERYTHING note: the capture is still persisted to the card archive
    // below; the cap governs wire armability only, never what the archive
    // keeps.
    bool fullArmable = (img.length <= IMG_MAX_IMAGE_SIZE);
    if (!fullArmable) {
        Serial.printf("ImageTx: image %u full size %u B exceeds cap %u B; skipping full transfer (thumbnail still pushes)\n",
                     imageId,
                     static_cast<unsigned>(img.length),
                     static_cast<unsigned>(IMG_MAX_IMAGE_SIZE));
    }

    // Thumbnail-bytes truth, shared by both admission branches below.
    const bool haveThumbBytes =
        (haveThumb && thumb.valid && thumb.buffer != nullptr && thumb.length > 0);

    // STORE-01 / D-02: persist BEFORE any manifest state exists — the card
    // write is the admission gate, so the 0x12 manifest can never precede
    // the file. CRCs are computed HERE over the Camera() buffers (the
    // caller owns them; the module only stores what it is given).
    const uint32_t fullCrc = esp_rom_crc32_le(0, img.buffer, img.length);
    const uint32_t thumbCrc = haveThumbBytes
        ? esp_rom_crc32_le(0, thumb.buffer, thumb.length)
        : 0;
    const PersistOutcome outcome = BalloonSdStoreTx().persistCapture(
        imageId,
        entry.captureSource,
        entry.captureTimeMs,
        entry.settings,
        img.buffer, img.length, fullCrc,
        haveThumbBytes ? thumb.buffer : nullptr,
        haveThumbBytes ? thumb.length : 0,
        thumbCrc);

    if (outcome == PersistOutcome::CARD_FULL) {
        // Locked keep-everything decision: a card-full verdict at persist
        // time refuses the capture honestly and NEVER routes to the volatile
        // fallback (the fallback must not become the normal path for a full
        // card). This is the race backstop for plan 02.5-03's pre-capture
        // gate — the card filled between that gate and this persist. No
        // queue entry, no manifest; the Camera() buffers are freed by the
        // existing capture path (the next captureImage()).
        Serial.printf("ImageTx: card full - image %u capture refused\n", imageId);
        return;
    }

    if (outcome == PersistOutcome::IO_ERROR) {
        // D-03: the volatile PSRAM queue survives ONLY as this honestly-
        // labeled SD-write-failure fallback — the admission names the
        // degradation and its loss-on-reboot consequence (prohibition 3:
        // never entered, logged, or labeled as the normal path).
        entry.volatileFallback = true;
        Serial.printf("ImageTx: SD write failed for image %u - VOLATILE fallback engaged (image lost on reboot)\n",
                     imageId);
        logMemDiagnostic("volatile-fallback");

        if (fullArmable) {
            // Take PSRAM ownership of the full image BEFORE returning from
            // this branch (Pitfall 7): the next capture's freeCurrentImage()
            // must not pull bytes out from under a transfer
            entry.fullBuffer = (uint8_t*)ps_malloc(img.length);
            if (!entry.fullBuffer) {
                // WR-01 (review 7d96a98): PSRAM exhaustion is exactly when the
                // cheapest payload must survive — mark the full unavailable and
                // fall through to the thumbnail branch (mirrors the oversize
                // skip above and the thumb-alloc-failure path below); the
                // both-empty guard after the thumbnail branch keeps a
                // nothing-transferable capture from occupying a queue slot.
                Serial.printf("ImageTx: PSRAM allocation failed for image %u full buffer (%u bytes); full dropped, thumbnail still pushes\n",
                             imageId, static_cast<unsigned>(img.length));
                entry.fullBuffer = nullptr;
                entry.fullLength = 0;
                entry.fullTotalChunks = 0;
            } else {
                memcpy(entry.fullBuffer, img.buffer, img.length);
                entry.fullLength = img.length;
                entry.fullCrc32 = esp_rom_crc32_le(0, entry.fullBuffer, entry.fullLength);
                entry.fullTotalChunks = chunksForSize(entry.fullLength);
            }
        } else {
            entry.fullBuffer = nullptr;
            entry.fullLength = 0;
            entry.fullTotalChunks = 0;
        }

        if (haveThumb && thumb.valid && thumb.buffer != nullptr) {
            entry.thumbBuffer = (uint8_t*)ps_malloc(thumb.length);
            if (entry.thumbBuffer != nullptr) {
                memcpy(entry.thumbBuffer, thumb.buffer, thumb.length);
                entry.thumbLength = thumb.length;
                entry.thumbCrc32 = esp_rom_crc32_le(0, entry.thumbBuffer, entry.thumbLength);
                entry.thumbTotalChunks = chunksForSize(thumb.length);
                entry.state = ImageTxEntryState::PUSH_THUMB_MANIFEST;
            } else {
                // Thumbnail copy failed — still enqueue the full image; skip the
                // thumbnail pushes (logged, never silent)
                if (DEBUG_IMAGE_TX) {
                    Serial.printf("ImageTx: PSRAM allocation failed for image %u thumbnail (%u bytes); pushing full image only\n",
                                 imageId, static_cast<unsigned>(thumb.length));
                }
                entry.thumbBuffer = nullptr;
                entry.thumbLength = 0;
                entry.thumbTotalChunks = 0;
                entry.state = fullTransferArmable(entry) ? ImageTxEntryState::ANNOUNCE_FULL
                                                         : ImageTxEntryState::THUMB_PUSHED;
            }
        } else {
            // captureThumbnail failed — still enqueue the full image and skip
            // the thumbnail pushes (logged, never silent)
            if (DEBUG_IMAGE_TX) {
                Serial.printf("ImageTx: thumbnail capture failed for image %u; pushing full image only\n", imageId);
            }
            entry.thumbBuffer = nullptr;
            entry.thumbLength = 0;
            entry.thumbTotalChunks = 0;
            entry.state = fullTransferArmable(entry) ? ImageTxEntryState::ANNOUNCE_FULL
                                                     : ImageTxEntryState::THUMB_PUSHED;
        }
    } else {
        // PERSISTED (STORE-01/STORE-03 normal path): the card now holds
        // IMG_{id}.JPG + IMG_{id}_T.JPG + the META commit record, written
        // BEFORE this entry exists. The entry is a small RAM index over
        // those card bytes — fullBuffer/thumbBuffer stay null ({}-init) and
        // chunk bytes are read from the card at transmit time. NO ps_malloc
        // copy happens on this branch.
        entry.fullLength = img.length;
        entry.fullCrc32 = fullCrc;
        entry.fullTotalChunks = chunksForSize(img.length);
        if (haveThumbBytes) {
            entry.thumbLength = thumb.length;
            entry.thumbCrc32 = thumbCrc;
            entry.thumbTotalChunks = chunksForSize(thumb.length);
        }
        Serial.printf("ImageTx: image %u persisted to SD (full %u B / %u chunks, thumb %u B / %u chunks)\n",
                     imageId,
                     static_cast<unsigned>(entry.fullLength),
                     static_cast<unsigned>(entry.fullTotalChunks),
                     static_cast<unsigned>(entry.thumbLength),
                     static_cast<unsigned>(entry.thumbTotalChunks));
        if (!haveThumbBytes) {
            // captureThumbnail failed — the persist carried the full only;
            // skip the thumbnail pushes exactly as the volatile branch does
            // (logged, never silent)
            if (DEBUG_IMAGE_TX) {
                Serial.printf("ImageTx: thumbnail capture failed for image %u; pushing full image only\n", imageId);
            }
            entry.thumbLength = 0;
            entry.thumbTotalChunks = 0;
            entry.state = fullTransferArmable(entry) ? ImageTxEntryState::ANNOUNCE_FULL
                                                     : ImageTxEntryState::THUMB_PUSHED;
            // 02.5-02 Task 1: the record carries NO thumbnail (thumbLength 0),
            // so the thumbnail delivery obligation is vacuous — retire it now,
            // at persist time with the flags otherwise zero. The boot rescan
            // then judges such a record purely on the full's bit, and a fully
            // delivered thumbless capture is never re-announced. A thumbnail
            // DROPPED at the manifest bound keeps its bit clear: its record
            // carries a real thumbLength, and a later boot must re-announce it
            // (the crash-resume value).
            if (!BalloonSdStoreTx().markDelivered(imageId, ImageKind::THUMBNAIL)) {
                Serial.printf("SdStore: delivery flag update failed for image %u kind %u\n",
                              static_cast<unsigned>(imageId),
                              static_cast<unsigned>(ImageKind::THUMBNAIL));
            }
        } else {
            entry.state = ImageTxEntryState::PUSH_THUMB_MANIFEST;
        }
    }
    entry.lastActivityMs = millis();

    // Nothing transferable (no thumbnail AND no armable full) — do not
    // occupy a queue slot. Byte truth is path-dependent: a volatile entry
    // owns PSRAM buffers; a file-backed entry's buffers are null by
    // construction and its truth is the persist-derived lengths.
    const bool noBytes = entry.volatileFallback
        ? (entry.thumbBuffer == nullptr && entry.fullBuffer == nullptr)
        : (entry.thumbLength == 0 && entry.fullLength == 0);
    if (entry.state == ImageTxEntryState::THUMB_PUSHED && noBytes) {
        return;
    }

    // Place the entry — bounded queue with drop-oldest on overflow
    ImageTxEntry* slot = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            slot = &entries[i];
            break;
        }
    }
    if (slot == nullptr) {
        // Overflow: class-ranked eviction (02-05 / CR-03 fix c) — never free
        // the active pull's target while any finished/parked/queued entry
        // exists. Classes in eviction-PREFERENCE order; within a class the
        // oldest (lowest enqueueSeq) entry goes. Every eviction is logged
        // with its class — never silently. (ANNOUNCE_FULL rides class 4:
        // like PUSH_THUMB_* it still has push work in flight — the one-time
        // full manifest — and no pull can reference it yet.)
        //
        // WR-02 (01-14): the victim scan is TWO-PASS. Pass 1 adds the same
        // mid-service skip the supersede path got in 01-12 — an entry whose
        // window is armed and incomplete (the BUSY predicate, copied
        // verbatim — an in-flight heal/pull's chunks are on the link) is not
        // an eligible victim while any alternative exists. Pass 2 runs only
        // when pass 1 found nothing (every entry mid-service) and drops the
        // skip: the queue evicts the class-ranked victim and admits the new
        // capture rather than wedging at depth 3. The skip lives ONLY in
        // this scan — evictionClassOf's ranking and sweepExpiredEntries' TTL
        // path keep their full eviction rights (carried 01-12 prohibition).
        static constexpr uint8_t EVICT_CLASS_COUNT = 5;
        ImageTxEntry* victim = nullptr;
        uint8_t victimClass = 0;
        bool midServiceFallback = false;
        for (uint8_t scanPass = 0; scanPass < 2 && victim == nullptr; scanPass++) {
            const bool skipMidService = (scanPass == 0);
            midServiceFallback = (scanPass == 1);
            for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
                if (!entries[i].used) {
                    continue;
                }
                if (skipMidService && entries[i].windowArmed &&
                    entries[i].windowNextIndex < entries[i].windowStart + entries[i].windowCount) {
                    continue;   // pass 1: an in-flight heal/pull is never the preferred victim
                }
                uint8_t cls = evictionClassOf(entries[i]);
                if (victim == nullptr || cls < victimClass ||
                    (cls == victimClass && entries[i].enqueueSeq < victim->enqueueSeq)) {
                    victim = &entries[i];
                    victimClass = cls;
                }
            }
        }
        // slot == nullptr implies every entry is used, so victim is guaranteed
        const char* className =
            victimClass == 0 ? "fully receipt-confirmed (safe reclaim)" :
            victimClass == 1 ? "parked THUMB_PUSHED" :
            victimClass == 2 ? "SERVED (heal-only)" :
            victimClass == 3 ? "queued, no airtime invested" :
            victimClass == 4 ? "push in flight" :
                               "ACTIVE-PULL context or receipt-evidenced (last resort)";
        if (midServiceFallback) {
            Serial.printf("ImageTx: queue overflow - all entries mid-service; evicting class %u (%s) entry image %u for image %u\n",
                         victimClass, className,
                         victim->imageId, imageId);
        } else {
            Serial.printf("ImageTx: queue overflow (depth %u); evicting class %u (%s) entry image %u for image %u\n",
                         static_cast<unsigned>(IMG_TX_QUEUE_DEPTH), victimClass, className,
                         victim->imageId, imageId);
        }
        freeEntry(*victim);
        slot = victim;
    }
    *slot = entry;

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: enqueued image %u (full %u B, thumb %u B / %u chunks, source %u)\n",
                     imageId,
                     static_cast<unsigned>(entry.fullLength),
                     static_cast<unsigned>(entry.thumbLength),
                     static_cast<unsigned>(entry.thumbTotalChunks),
                     entry.captureSource);
    }
}

// ===========================
// Crash-Resume Admission (02.5-02, STORE-02)
// ===========================

// Entry construction for boot-rescanned records — the PERSISTED branch of
// enqueueCapture replayed from a validated META record instead of live
// Camera() buffers: null buffers (the card is the byte source), META-derived
// lengths/CRCs/chunk counts.
// Image-transfer rework (the balloon6.log crash-loop fix): the resume honors
// the persisted receipt state —
//   - thumbDelivered (SD_ST_DELIV_THUMB, set by handleImageAck's
//     KIND_COMPLETE verdict): the record enters at THUMB_PUSHED and
//     announces NOTHING — the base provably holds the verified thumbnail,
//     so re-announcing would only restart its (kept) row for nothing;
//   - thumb undelivered: PUSH_THUMB_MANIFEST as before, but the record's
//     thumbAcked bitmap (base-confirmed chunks, persisted per receipt)
//     makes the resumed push skip past everything the base already holds;
//   - no thumbnail: ANNOUNCE_FULL/THUMB_PUSHED unchanged (its only
//     discovery path).
void ImageTxManager::admitRescannedFillEntry(ImageTxEntry& entry, const BalloonResumedRecord& rec) {
    entry = ImageTxEntry{};   // zero-init: null buffers, clean streaks/budgets
    entry.used = true;
    entry.enqueueSeq = nextEnqueueSeq++;   // admission order == FIFO resume order
    entry.imageId = rec.imageId;
    entry.captureSource = rec.captureSource;
    entry.captureTimeMs = rec.captureTimeMs;
    static_assert(sizeof(ImageTxSettings) == 7,
                  "BalloonResumedRecord's 7 settings bytes assume the ImageTxSettings layout");
    memcpy(&entry.settings, rec.settings, sizeof(entry.settings));
    entry.volatileFallback = false;   // file-backed by construction ({}-init; explicit)
    entry.fullLength = rec.fullLength;
    entry.fullCrc32 = rec.fullCrc32;
    entry.fullTotalChunks = chunksForSize(rec.fullLength);
    entry.thumbAcked = rec.thumbAcked;   // base-confirmed bits survive the reboot
    if (rec.thumbLength > 0) {
        entry.thumbLength = rec.thumbLength;
        entry.thumbCrc32 = rec.thumbCrc32;
        entry.thumbTotalChunks = chunksForSize(rec.thumbLength);
        entry.state = rec.thumbDelivered ? ImageTxEntryState::THUMB_PUSHED
                                         : ImageTxEntryState::PUSH_THUMB_MANIFEST;
    } else {
        // No thumbnail in the record (persist-time thumbnail failure, whose
        // vacuous thumb obligation was already retired in the META) — mirror
        // the PERSISTED no-thumb path: skip the thumbnail pushes, announce
        // the armable full or park.
        entry.thumbLength = 0;
        entry.thumbCrc32 = 0;
        entry.thumbTotalChunks = 0;
        entry.state = fullTransferArmable(entry) ? ImageTxEntryState::ANNOUNCE_FULL
                                                 : ImageTxEntryState::THUMB_PUSHED;
    }
    entry.lastActivityMs = millis();
}

uint8_t ImageTxManager::admitRescanned(const BalloonResumedRecord* records, uint8_t count) {
    if (records == nullptr || count == 0) {
        return 0;
    }
    uint8_t admitted = 0;
    for (uint8_t r = 0; r < count; r++) {
        const BalloonResumedRecord& rec = records[r];

        // Nothing-transferable guard (mirrors enqueueCapture): a validated
        // record carrying zero bytes on both kinds must not occupy a slot.
        if (rec.fullLength == 0 && rec.thumbLength == 0) {
            Serial.printf("ImageTx: rescanned image %u carries no transferable bytes; skipped\n",
                          static_cast<unsigned>(rec.imageId));
            continue;
        }

        // Id-collision guard — one comparison scan. Impossible in practice
        // (ids are monotonic via NVS), but a duplicate admission would
        // corrupt window addressing.
        bool collides = false;
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            if (entries[i].used && entries[i].imageId == rec.imageId) {
                collides = true;
                break;
            }
        }
        if (collides) {
            Serial.printf("ImageTx: rescanned image %u already queued; skipped\n",
                          static_cast<unsigned>(rec.imageId));
            continue;
        }

        ImageTxEntry* slot = nullptr;
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            if (!entries[i].used) {
                slot = &entries[i];
                break;
            }
        }
        if (slot == nullptr) {
            // Honest named stop (the rescan cap arithmetic makes this rare):
            // more undelivered work surfaced than the depth-5 queue holds —
            // the leftovers wait for a future boot's rescan.
            Serial.printf("ImageTx: admitRescanned - queue full (depth %u) at image %u; %u record(s) remain for a future rescan\n",
                          static_cast<unsigned>(QUEUE_DEPTH),
                          static_cast<unsigned>(rec.imageId),
                          static_cast<unsigned>(count - r));
            break;
        }
        admitRescannedFillEntry(*slot, rec);
        // Session-16 52-row livelock latch (§14.4 candidate, card-resident —
        // see SD_ST_RESUME_LATCH's block comment): this admission spends the
        // row's auto-resume attempt. One boot-time bookkeeping write here,
        // OFF the push hot path; a later delivery (by auto-resume, heal, or
        // explicit pull) clears the bit through markDelivered. Latched rows
        // are withheld by bootRescan from FUTURE resume sets — explicit base
        // asks bypass. bootRescan already withheld rows the bit found SET at
        // this boot (rec.resumeLatched true never reaches this loop).
        BalloonSdStoreTx().markResumeLatched(rec.imageId);
        admitted++;
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: admitted rescanned image %u (full %u B / %u chunks, thumb %u B / %u chunks) - resume\n",
                          static_cast<unsigned>(rec.imageId),
                          static_cast<unsigned>(rec.fullLength),
                          static_cast<unsigned>(slot->fullTotalChunks),
                          static_cast<unsigned>(rec.thumbLength),
                          static_cast<unsigned>(slot->thumbTotalChunks));
        }
    }
    return admitted;
}

// ===========================
// Push Side
// ===========================

ImageTxEntry* ImageTxManager::findActiveEntry() {
    // FIFO by enqueue order: the oldest entry with unfinished PUSH work
    // (thumbnail manifest/chunks, or the pending full announcement)
    ImageTxEntry* best = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            continue;
        }
        switch (entries[i].state) {
            case ImageTxEntryState::PUSH_THUMB_MANIFEST:
            case ImageTxEntryState::PUSH_THUMB_CHUNKS:
            case ImageTxEntryState::ANNOUNCE_FULL:
                if (best == nullptr || entries[i].enqueueSeq < best->enqueueSeq) {
                    best = &entries[i];
                }
                break;
            default:
                break;
        }
    }
    return best;
}

ImageTxEntry* ImageTxManager::findWindowServiceEntry() {
    // FIFO by enqueue order (D-19): the earliest entry with an armed,
    // not-yet-complete window. ANNOUNCED entries serve FULL windows; a
    // THUMB_PUSHED entry is admitted ONLY while a (thumbnail heal) window is
    // armed on it (02-05 / CR-01 — otherwise it is a parked oversize entry
    // with no service work)
    ImageTxEntry* best = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            continue;
        }
        const bool stateServiceable =
            entries[i].state == ImageTxEntryState::ANNOUNCED ||
            (entries[i].state == ImageTxEntryState::THUMB_PUSHED && entries[i].windowArmed);
        if (!stateServiceable) {
            continue;
        }
        if (!entries[i].windowArmed ||
            entries[i].windowNextIndex >= entries[i].windowStart + entries[i].windowCount) {
            continue;
        }
        if (best == nullptr || entries[i].enqueueSeq < best->enqueueSeq) {
            best = &entries[i];
        }
    }
    return best;
}

void ImageTxManager::pushPending() {
    // Bounded interleaving (02-05 / CR-03 fix a): an armed window whose entry
    // has waited longer than IMG_WINDOW_SERVICE_PREEMPT_MS PREEMPTS push work
    // for this pass's single transmit. The armed entry's lastActivityMs
    // advances only via its own transmits and re-arms, so a neighbor
    // capture's push naturally ages it; preempting at 5000 ms — strictly
    // below the base's 15000 ms IMG_WINDOW_STALL_MS, whose clock resets on
    // every accepted chunk — means the base's stall can never trip while the
    // balloon holds an armed window. PRI-01 non-regression: this preemption
    // lives INSIDE the chunk branch — the beacon early-return in process()
    // still runs first, and command responses still outrank both by loop order.
    ImageTxEntry* starved = findWindowServiceEntry();
    if (starved != nullptr &&
        (millis() - starved->lastActivityMs) > IMG_WINDOW_SERVICE_PREEMPT_MS) {
        serviceWindowChunk(*starved);
        return; // one transmit per pass
    }

    // D-19 transmit ordering within the chunk branch: push work (thumbnail
    // manifests/chunks and full announcements) outranks window service, so a
    // new capture's thumbnail pushes immediately even while an older entry's
    // window is mid-service — and the in-progress pull is never abandoned:
    // its window context simply waits for the push work to drain (bounded by
    // the preemption above, never unbounded).
    ImageTxEntry* entry = findActiveEntry();
    if (entry != nullptr) {
        if (entry->state == ImageTxEntryState::PUSH_THUMB_MANIFEST) {
            pushThumbManifest(*entry);
            return; // one transmit per pass
        }

        if (entry->state == ImageTxEntryState::PUSH_THUMB_CHUNKS) {
            pushThumbChunk(*entry);
            return; // one transmit per pass
        }

        if (entry->state == ImageTxEntryState::ANNOUNCE_FULL) {
            announceFullManifest(*entry);
            return; // one transmit per pass
        }
    }

    // No push work pending: service the earliest armed window, one chunk
    // this pass (the balloon never volunteers chunks outside a requested
    // window — Pitfall 5)
    ImageTxEntry* serving = findWindowServiceEntry();
    if (serving != nullptr) {
        serviceWindowChunk(*serving);
        return; // one transmit per pass
    }

    // Image-transfer rework: the idle-slot FULL re-announce (G-01-9 defect C)
    // is REMOVED — FULL is request-driven (IMAGE_FULL_REQUEST) and the base
    // re-requests what it wants; the balloon never volunteers manifests in
    // idle slots anymore.
}

bool ImageTxManager::pushThumbManifest(ImageTxEntry& entry) {
    ImageManifestBody body{};
    body.imageId = entry.imageId;
    body.imageKind = static_cast<uint8_t>(ImageKind::THUMBNAIL);
    body.captureSource = entry.captureSource;
    body.totalSize = entry.thumbLength;
    body.chunkSize = IMG_CHUNK_PAYLOAD_SIZE;
    body.totalChunks = entry.thumbTotalChunks;
    body.crc32 = entry.thumbCrc32;   // D-23: CRC over THIS kind's payload (thumbnail bytes)
    body.captureTimeMs = entry.captureTimeMs;
    body.resolution = entry.settings.resolution;
    body.quality = entry.settings.quality;
    body.brightness = entry.settings.brightness;
    body.contrast = entry.settings.contrast;
    body.saturation = entry.settings.saturation;
    body.exposure = entry.settings.exposure;
    body.wbMode = entry.settings.wbMode;

    ImageManifestPacket pkt = createManifestPacket(body); // factory owns the 0x12 type byte

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeManifest(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: manifest(image %u, thumb %u B, %u chunks) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.thumbLength),
                     static_cast<unsigned>(entry.thumbTotalChunks),
                     ok ? "sent" : "FAILED");
    }

    // WR-01 (01-14): the one-shot push state advances ONLY on a successful
    // transmit — a failed thumbnail manifest retries on the next process()
    // pass (one transmit per pass is the existing pacing), bounded by
    // IMG_MANIFEST_MAX_ATTEMPTS. At the bound the thumbnail is dropped
    // honestly (buffer freed, named log) and the entry proceeds exactly as a
    // completed push would, minus thumbnail bytes (full announce or
    // THUMB_PUSHED park via completedThumbState).
    if (ok) {
        entry.manifestAttempts = 0;
        entry.nextThumbChunk = 0;
        entry.state = ImageTxEntryState::PUSH_THUMB_CHUNKS;
    } else {
        entry.manifestAttempts++;
        if (entry.manifestAttempts < IMG_MANIFEST_MAX_ATTEMPTS) {
            // stay in PUSH_THUMB_MANIFEST — the next pass retries the manifest
        } else {
            Serial.printf("ImageTx: thumbnail manifest for image %u failed after %u attempts; thumbnail dropped\n",
                         entry.imageId,
                         static_cast<unsigned>(IMG_MANIFEST_MAX_ATTEMPTS));
            free(entry.thumbBuffer);
            entry.thumbBuffer = nullptr;
            entry.thumbLength = 0;
            entry.thumbCrc32 = 0;
            entry.thumbTotalChunks = 0;
            entry.state = completedThumbState(entry);
        }
    }
    entry.lastActivityMs = millis();
    return ok;
}

bool ImageTxManager::pushThumbChunk(ImageTxEntry& entry) {
    // Base-confirmed skip (image-transfer rework): leading chunks the base's
    // IMAGE_ACK receipts already confirmed never re-transmit. This is what
    // makes a reboot-resumed push converge in one pass: the base's resume
    // prefix receipts fill thumbAcked, and this loop jumps the cursor past
    // them (a full-kind COMPLETE verdict sets every bit, so the very next
    // pass lands on the done-check below). u64 covers the thumbnail cap;
    // bounds hold by construction (thumbTotalChunks <= 37 < 64).
    {
        uint8_t skipped = 0;
        while (entry.nextThumbChunk < entry.thumbTotalChunks &&
               entry.nextThumbChunk < 64 &&
               ((entry.thumbAcked >> entry.nextThumbChunk) & 1ULL)) {
            entry.nextThumbChunk++;
            skipped++;
        }
        if (skipped > 0) {
            Serial.printf("ImageTx: image %u resumed push - skipping %u base-confirmed chunk(s)\n",
                          static_cast<unsigned>(entry.imageId), skipped);
        }
    }
    if (entry.nextThumbChunk >= entry.thumbTotalChunks) {
        entry.state = completedThumbState(entry);
        return true;
    }

    // Byte source (STORE-03 / D-03): a file-backed entry (the default, no
    // PSRAM image copy exists) reads its thumbnail chunk from the card file
    // at chunkIndex * IMG_CHUNK_PAYLOAD_SIZE into a stack buffer; a
    // volatile-fallback entry slices its PSRAM thumbBuffer exactly as before.
    // Only the byte SOURCE changes — framing, pacing, and WR-08 semantics
    // below are untouched.
    uint8_t chunkBuf[IMG_CHUNK_PAYLOAD_SIZE];
    const uint8_t* source = nullptr;
    uint8_t chunkLen = 0;
    bool sdReadFailed = false;
    if (!entry.volatileFallback) {
        size_t got = 0;
        if (BalloonSdStoreTx().readChunk(entry.imageId, ImageKind::THUMBNAIL,
                                         entry.nextThumbChunk,
                                         chunkBuf, sizeof(chunkBuf), &got)) {
            chunkLen = static_cast<uint8_t>(got);
            source = chunkBuf;
        } else {
            // A readChunk false return takes the EXACT failure semantics of
            // a transmit failure (same-index fail streak, IMG_CHUNK_TX_RETRY_MAX
            // bound, the named skip log below extended with "SD read failed") —
            // never a fabricated chunk, never cursor advance on unread bytes.
            sdReadFailed = true;
        }
    } else {
        size_t offset = static_cast<size_t>(entry.nextThumbChunk) * IMG_CHUNK_PAYLOAD_SIZE;
        size_t remaining = entry.thumbLength - offset;
        chunkLen = static_cast<uint8_t>(
            (remaining > IMG_CHUNK_PAYLOAD_SIZE) ? IMG_CHUNK_PAYLOAD_SIZE : remaining);
        source = entry.thumbBuffer + offset;
    }

    bool ok = false;
    if (!sdReadFailed) {
        ImageChunkPacket pkt = createChunkPacket(entry.imageId, static_cast<uint8_t>(ImageKind::THUMBNAIL),
                                                 entry.nextThumbChunk,
                                                 source, chunkLen);

        uint8_t buffer[CMD_MAX_PACKET_SIZE];
        size_t length = 0;
        ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);
    }

    if (DEBUG_IMAGE_TX) {
        // Kind discriminator (CR-01, 01-13): the push is always THUMBNAIL —
        // printing it keeps push and window lines symmetric so bench logs can
        // tell which kind's bytes left the balloon at every index
        if (sdReadFailed) {
            Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u) SD read failed\n",
                         entry.imageId,
                         static_cast<unsigned>(ImageKind::THUMBNAIL),
                         static_cast<unsigned>(entry.nextThumbChunk + 1),
                         static_cast<unsigned>(entry.thumbTotalChunks));
        } else {
            Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u, %u B) %s\n",
                         entry.imageId,
                         static_cast<unsigned>(ImageKind::THUMBNAIL),
                         static_cast<unsigned>(entry.nextThumbChunk + 1),
                         static_cast<unsigned>(entry.thumbTotalChunks),
                         chunkLen,
                         ok ? "sent" : "FAILED");
        }
    }

    // WR-08 (01-17): the cursor advances ONLY on a successful transmit — a
    // failed transmit (or SD read) retries the SAME index on subsequent
    // process() passes (one transmit per pass is the existing pacing),
    // bounded by IMG_CHUNK_TX_RETRY_MAX. At the bound the chunk is skipped
    // for its pass with a named log; before the bound the failed index
    // simply retries.
    if (ok) {
        entry.thumbChunkFailStreak = 0;
        entry.nextThumbChunk++;
        // Image-transfer rework: the DELIVERY moment is no longer "the balloon
        // transmitted it" — it is the base's ACK-confirmed verdict
        // (handleImageAck KIND_COMPLETE / WINDOW_COMPLETE receipts, persisted
        // via META). A TX-success mark here would un-anchor the receipt state:
        // TX success says nothing about what the base actually holds.
    } else {
        entry.thumbChunkFailStreak++;
        if (entry.thumbChunkFailStreak >= IMG_CHUNK_TX_RETRY_MAX) {
            if (sdReadFailed) {
                Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u) skipped after %u failed attempts - SD read failed\n",
                             entry.imageId,
                             static_cast<unsigned>(ImageKind::THUMBNAIL),
                             static_cast<unsigned>(entry.nextThumbChunk + 1),
                             static_cast<unsigned>(entry.thumbTotalChunks),
                             static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
            } else {
                Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u) skipped after %u failed transmit attempts\n",
                             entry.imageId,
                             static_cast<unsigned>(ImageKind::THUMBNAIL),
                             static_cast<unsigned>(entry.nextThumbChunk + 1),
                             static_cast<unsigned>(entry.thumbTotalChunks),
                             static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
            }
            entry.thumbChunkFailStreak = 0;
            entry.nextThumbChunk++;
        }
        // else: stay on the same index — the next pass retries it
    }
    // Every transmit attempt is activity — keeps the preempt clock honest
    // against pushPending (a retrying entry is not an idle entry)
    entry.lastActivityMs = millis();
    if (entry.nextThumbChunk >= entry.thumbTotalChunks) {
        entry.state = completedThumbState(entry);
        // Thumbnail holes from skipped-after-bound chunks are healed by the
        // SAME windowed-pull re-request path (D-22), wired in 02-02/02-03
    }
    return ok;
}

// Where an entry lands once its thumbnail push completes (image-transfer
// rework): ALWAYS THUMB_PUSHED — the thumbnail is the delivery, not the
// prelude. FULL never announces automatically; it announces only when the
// base explicitly asks via IMAGE_FULL_REQUEST (handleFullRequest sets
// ANNOUNCE_FULL on the parked entry). fullTransferArmable is therefore no
// longer consulted here.
ImageTxEntryState ImageTxManager::completedThumbState(const ImageTxEntry& entry) const {
    (void)entry;
    return ImageTxEntryState::THUMB_PUSHED;
}

// ===========================
// Full-Image Announcement (D-17 pull half)
// ===========================

// Shared 0x12 FULL_IMAGE manifest body construction (G-01-9 defect C, 01-17):
// the one-shot announce and the idle re-announce transmit the IDENTICAL body —
// one construction site, no duplication, no wire change (body stays 27 B).
ImageManifestBody ImageTxManager::fillFullManifestBody(const ImageTxEntry& entry) {
    ImageManifestBody body{};
    body.imageId = entry.imageId;
    body.imageKind = static_cast<uint8_t>(ImageKind::FULL_IMAGE);
    body.captureSource = entry.captureSource;
    body.totalSize = entry.fullLength;
    body.chunkSize = IMG_CHUNK_PAYLOAD_SIZE;
    body.totalChunks = entry.fullTotalChunks;
    body.crc32 = entry.fullCrc32;   // D-23: end-to-end CRC over the full image bytes
    body.captureTimeMs = entry.captureTimeMs;
    body.resolution = entry.settings.resolution;
    body.quality = entry.settings.quality;
    body.brightness = entry.settings.brightness;
    body.contrast = entry.settings.contrast;
    body.saturation = entry.settings.saturation;
    body.exposure = entry.settings.exposure;
    body.wbMode = entry.settings.wbMode;
    return body;
}

bool ImageTxManager::announceFullManifest(ImageTxEntry& entry) {
    ImageManifestBody body = fillFullManifestBody(entry);

    ImageManifestPacket pkt = createManifestPacket(body); // factory owns the 0x12 type byte

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeManifest(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: FULL manifest(image %u, %u B, %u chunks) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.fullLength),
                     static_cast<unsigned>(entry.fullTotalChunks),
                     ok ? "sent" : "FAILED");
    }

    // CR-02 (01-14, G-01-9 defect C): the announcement is consumed ONLY on a
    // successful transmit (comment updated from "emitted exactly ONCE" —
    // failures retry bounded by IMG_MANIFEST_MAX_ATTEMPTS). From ANNOUNCED on,
    // chunks flow only through a window context armed by handleWindowRequest.
    if (ok) {
        entry.manifestAttempts = 0;
        entry.state = ImageTxEntryState::ANNOUNCED;
    } else {
        entry.manifestAttempts++;
        if (entry.manifestAttempts < IMG_MANIFEST_MAX_ATTEMPTS) {
            // stay in ANNOUNCE_FULL — the next pass retries the announcement
        } else {
            Serial.printf("ImageTx: FULL manifest for image %u failed after %u attempts; full dropped\n",
                         entry.imageId,
                         static_cast<unsigned>(IMG_MANIFEST_MAX_ATTEMPTS));
            // Park-and-free at the bound (PRI-03 honest degradation): the
            // thumbnail has already pushed (push precedes announce by
            // construction), so the base degrades to thumbnail-only for this
            // capture — bounded, logged, never silent. thumbBuffer stays
            // owned so THUMBNAIL window heals keep working on the parked
            // entry.
            free(entry.fullBuffer);
            entry.fullBuffer = nullptr;
            entry.fullLength = 0;
            entry.fullCrc32 = 0;
            entry.fullTotalChunks = 0;
            entry.state = ImageTxEntryState::THUMB_PUSHED;
        }
    }
    entry.lastActivityMs = millis();
    return ok;
}

// ===========================
// Window Servicing (D-21 pull half, Pitfall 5)
// ===========================

WindowRequestResult ImageTxManager::handleWindowRequest(const uint8_t* payload, size_t len) {
    if (!initialized || payload == nullptr || len < 6) {
        return WindowRequestResult::INVALID_RANGE;
    }

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): channel-liveness
    // stamp — EVERY inbound window request counts, any kind, any verdict,
    // including frames later rejected as unknown/evicted. Session-6's
    // post-drop rejects (balloon11.log:680/:702/:729/:751 — the base still
    // asking for images 25/26 after the bound expired) were exactly the
    // liveness evidence the 01-17 drop-clock ignored. Write-only timing data,
    // never used for content decisions (T-01-21-01); the per-entry receipt
    // stamp further below is reachable only after full untrusted-input
    // validation (kind enum + entry match + per-kind range bounds — WR-02).
    // Image-transfer rework: the inbound-window-stamp/busy-hold pair fed the
    // (now removed) idle re-announce; lastWindowRequestMs below keeps its
    // eviction-evidence duty (G-01-7 lever 1), so only that stamp remains.

    // PayloadImageWindowRequest: imageId BE16, imageKind u8, startChunk BE16,
    // count u8 — 6 bytes (Pitfall 9: decode via the big-endian helpers, never
    // a struct memcpy)
    uint16_t imageId = CommandProtocol::readUint16(payload);
    uint8_t imageKind = payload[2];
    uint16_t startChunk = CommandProtocol::readUint16(payload + 3);
    uint16_t count = payload[5];

    // Untrusted RF input (T-02-11 extends T-02-04): validate the kind byte
    // against the ImageKind enum BEFORE any entry matching or buffer access —
    // a crafted request cannot address an unowned buffer
    if (imageKind != static_cast<uint8_t>(ImageKind::THUMBNAIL) &&
        imageKind != static_cast<uint8_t>(ImageKind::FULL_IMAGE)) {
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: window request for image %u carries unknown kind %u; rejected\n",
                         imageId, imageKind);
        }
        return WindowRequestResult::INVALID_RANGE;
    }
    const bool thumbWindow = (imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL));

    // Untrusted RF input (T-02-04): validate EVERYTHING before arming. Target
    // matching is KIND-SPLIT (D-22 / CR-01): FULL_IMAGE requests address
    // ANNOUNCED entries (post-full-manifest, exactly as before) and SERVED
    // entries (tail re-requests re-open service — 02-05); THUMBNAIL requests
    // address any same-id entry whose thumbnail push provably finished (state
    // past PUSH_THUMB_*) and that still owns thumbnail bytes — this admits
    // THUMB_PUSHED oversize parks (whose full never announces) alongside
    // ANNOUNCED/SERVED entries, so thumbnail heals are servable end-to-end
    ImageTxEntry* target = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used || entries[i].imageId != imageId) {
            continue;
        }
        if (thumbWindow) {
            // Thumbnail-bytes truth (STORE-03): a volatile-fallback entry owns
            // a PSRAM buffer; a file-backed entry's bytes live on the card and
            // its truth is the persist-derived length (buffers null).
            const bool haveThumbBytes = entries[i].volatileFallback
                ? (entries[i].thumbBuffer != nullptr)
                : (entries[i].thumbLength > 0);
            if (!haveThumbBytes ||
                entries[i].state == ImageTxEntryState::PUSH_THUMB_MANIFEST ||
                entries[i].state == ImageTxEntryState::PUSH_THUMB_CHUNKS) {
                continue;
            }
        } else if (entries[i].state != ImageTxEntryState::ANNOUNCED &&
                   entries[i].state != ImageTxEntryState::SERVED) {
            continue;
        }
        if (target == nullptr || entries[i].enqueueSeq < target->enqueueSeq) {
            target = &entries[i];
        }
    }
    if (target == nullptr) {
        // Thumbnail-heal rescue (session-15, balloon12.log:689/:753/:821/:977):
        // a THUMBNAIL window request with no RAM entry means the base holds
        // this image's manifest and is missing chunks while the balloon's
        // entry is gone — crash-class-gated boots skip the boot rescan
        // (e9400b9); TTL evictions and fresh boots lose it too. The FULL
        // pull has a rescue end-to-end (handleFullRequest's card re-admit +
        // the base's window-NACK re-arm); the thumbnail push — balloon-
        // driven, base-healed — had NONE, and there is no wire command by
        // which the base can request a thumb re-announce (D-17: the thumb is
        // balloon-pushed). The base's stall detector then finalizes the row
        // INCOMPLETE (base12.log:238, image 56 at 2/7). So the balloon
        // re-admits HERE, through the SAME validated fill the boot rescan
        // uses — the entry lands at PUSH_THUMB_MANIFEST (persisted receipt
        // state honored), the next process() pass re-announces the manifest,
        // and the base's restart-on-new-manifest behavior re-drives the row;
        // the UNKNOWN_IMAGE NACK that still returns this pass is moot. This
        // re-admit deliberately BYPASSES SD_ST_RESUME_LATCH (explicit ask
        // re-arms a latched row).
        // Guards: only for thumb-kind asks (the FULL class is already
        // rescued — and a re-admitted thumbDelivered record parks at
        // THUMB_PUSHED where no window can arm, so answering it would only
        // obscure the base's working rescue); only while the record itself
        // still owes a thumbnail; never while ANY entry already carries the
        // id (a push may be mid-flight in a state the kind-split matching
        // above deliberately skips). Exposure matches handleFullRequest's
        // shipped card re-admit: untrusted RF can admit only records that
        // validate against the card archive, bounded by the queue depth,
        // id-idempotent.
        if (thumbWindow) {
            bool idAlreadyQueued = false;
            ImageTxEntry* slot = nullptr;
            for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
                if (entries[i].used && entries[i].imageId == imageId) {
                    idAlreadyQueued = true;
                    break;
                }
                if (slot == nullptr && !entries[i].used) {
                    slot = &entries[i];
                }
            }
            BalloonResumedRecord rec{};
            if (!idAlreadyQueued && slot != nullptr &&
                BalloonSdStoreTx().loadResumedRecord(imageId, rec) &&
                rec.thumbLength > 0 && !rec.thumbDelivered) {
                admitRescannedFillEntry(*slot, rec);
                Serial.printf("ImageTx: thumbnail window request for image %u - card re-admitted; push re-announces on next pass\n",
                              static_cast<unsigned>(imageId));
                return WindowRequestResult::UNKNOWN_IMAGE;   // not armable this pass; the re-announce answers the base
            }
        }
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: window request for unknown/evicted image %u rejected\n", imageId);
        }
        return WindowRequestResult::UNKNOWN_IMAGE;
    }

    // Per-kind range validation (T-02-11): startChunk/count are bounded by
    // the totalChunks of the kind the request actually addresses
    const uint16_t totalChunks = thumbWindow ? target->thumbTotalChunks
                                             : target->fullTotalChunks;
    if (count < 1 || count > IMG_WINDOW_MAX_CHUNKS || startChunk >= totalChunks) {
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: window request for image %u out of bounds (start %u, count %u, total %u)\n",
                         imageId, startChunk, count, totalChunks);
        }
        return WindowRequestResult::INVALID_RANGE;
    }
    // Clamp the tail: a request that overruns the last chunk is trimmed to
    // what actually exists (a request fully past the end is rejected above)
    if (static_cast<uint32_t>(startChunk) + count > totalChunks) {
        count = static_cast<uint16_t>(totalChunks - startChunk);
    }

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): receipt evidence —
    // this request MATCHED a queued entry AND passed FULL untrusted-input
    // validation (kind enum check, entry match, per-kind range bounds and
    // tail clamp above — T-01-21-01's full-validation clause), so the base
    // demonstrably holds at least one of its manifests and is asking. WR-02
    // (01-REVIEW): the stamp sits AFTER full validation — in its original
    // pre-validation position a CRC-valid but malformed request (count 0 /
    // out-of-bounds startChunk) stamped receipt evidence onto an entry,
    // pinning it in the protected eviction class indefinitely. The stamp's
    // standing duty is eviction evidence only (G-01-7 class ranking).
    target->lastWindowRequestMs = millis();

    if (!thumbWindow) {
        // FIFO pull order (D-19): the base asking for this ID means it has
        // moved past older entries — implicitly complete them and free their
        // buffers. FULL requests ONLY (02-05 Gap 2 eviction safety): a
        // THUMBNAIL heal request must NEVER evict — any parked or queued
        // entry may still be the active pull's target mid-stream.
        // G-01-7 lever 2 EXCEPTION (01-12): a supersede DEFERS (never evicts)
        // an entry whose window is mid-service — an in-flight heal/pull is
        // not "moved past" until its armed window completes (the deferred
        // entry then hits the BUSY check below and the base retries).
        // G-01-7 round #14 (01-33, 01-G01-7-LEVER.md section 4 option a):
        // the supersede victim selection is ranked through evictionClassOf.
        // A false return means every eligible older candidate is receipt-
        // evidenced (class 5) — admission would evict an entry the base
        // demonstrably holds a manifest for and is actively pulling. Reject
        // honestly through the EXISTING unknown/evicted NACK_INVALID class
        // (no new protocol surface): the base's D-24 pass machinery retries
        // within its existing bound. The receipt stamp + budget re-arm above
        // stay — the request genuinely matched this entry.
        // G-01-7 round #15 (01-35) CAPACITY-NEED GATE: the supersede scan runs
        // ONLY when the queue holds no free slot. Session 11 proved the
        // round-#14 lever over-rejects — image 45's SVGA FULL starved 0/165
        // at 2/5 occupancy behind image 44's fully-SERVED COMPLETE entry
        // (balloon5.log:883-:1056) because lowest-class-match forced eviction
        // regardless of capacity need. With a free slot the request is
        // ADMITTED: no older entry is touched, no line printed, fall through
        // to the BUSY check and arm below. When the queue IS full, the ranked
        // scan, its false-return honest-reject, and the named line run exactly
        // as round #14 left them — and a fully-receipted older entry (class 0,
        // the shared ranking above) is now the safe reclaim victim the
        // session-11 routing named.
        bool queueFull = true;
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            if (!entries[i].used) {
                queueFull = false;
                break;
            }
        }
        if (queueFull && !evictEntriesOlderThan(*target)) {
            Serial.printf("ImageTx: window request for image %u rejected - queue holds only receipt-evidenced entries (G-01-7)\n", imageId);
            return WindowRequestResult::UNKNOWN_IMAGE;
        }
    }

    // One entry actively services at a time: any OTHER entry with an armed,
    // incomplete window means the base must retry later (its existing
    // timeout/retry machinery handles the wait)
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (&entries[i] != target && entries[i].used && entries[i].windowArmed &&
            entries[i].windowNextIndex < entries[i].windowStart + entries[i].windowCount) {
            if (DEBUG_IMAGE_TX) {
                Serial.printf("ImageTx: window request for image %u deferred — image %u window mid-service\n",
                             imageId, entries[i].imageId);
            }
            return WindowRequestResult::BUSY;
        }
    }

    // Arm — idempotent (Pitfall 10): arming (re-)resets the cursor to
    // startChunk, so a duplicate or re-requested window simply re-sends the
    // same indices; no ID allocation, no queue mutation, no state corruption
    if (!thumbWindow && target->state == ImageTxEntryState::SERVED) {
        // A tail re-request re-opens service: SERVED -> ANNOUNCED (the entry
        // keeps its buffers the whole time). THUMBNAIL windows never change
        // entry state.
        target->state = ImageTxEntryState::ANNOUNCED;
    }
    target->windowArmed = true;
    target->windowKind = imageKind;
    target->windowEverArmed = true;
    // Image-transfer rework: the G-01-9 fullWindowEverArmed receipt latch is
    // removed with the idle re-announce it stopped. windowEverArmed above
    // keeps its eviction-evidence duty.
    target->windowStart = startChunk;
    target->windowCount = count;
    target->windowNextIndex = startChunk;
    target->windowArmedAtMs = millis();   // G-01-7 lever 3: settle clock starts at arming
    target->lastActivityMs = millis();

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: %s window armed for image %u (chunks %u..%u)\n",
                     thumbWindow ? "THUMBNAIL" : "FULL",
                     imageId, startChunk, static_cast<unsigned>(startChunk) + count - 1);
    }
    return WindowRequestResult::ARMED;
}

bool ImageTxManager::handleImageAck(const ImageAckBody& ack) {
    // Receipt dispatch (image-transfer rework): find the entry by id — ids
    // are unique per entry (AutoCap monotonic sequence), so the kind byte
    // only names which of the entry's two kinds the receipt covers.
    ImageTxEntry* entry = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used && entries[i].imageId == ack.imageId) {
            entry = &entries[i];
            break;
        }
    }
    if (entry == nullptr) {
        Serial.printf("ImageTx: image ACK for unknown image %u kind %u ignored\n",
                      static_cast<unsigned>(ack.imageId), ack.imageKind);
        return false;   // idempotent: nothing to merge into
    }

    // RX events never consume the TX budget: lastActivityMs is untouched —
    // the preempt/stall clocks measure the balloon's own transmit cadence.
    const bool thumbReceipt =
        (ack.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL));
    const bool kindScoped =
        (ack.status >= static_cast<uint8_t>(ImageAckStatus::KIND_COMPLETE_CRC_OK));

    if (!kindScoped) {
        // Window-scoped receipt: merge the bitmap into the entry's
        // thumbAcked. FULL receipts are log-only — FULL is base-pulled (the
        // base re-requests exactly what it wants), so the balloon keeps no
        // full bitmap; the thumbnail is balloon-PUSHED, so this bitmap is
        // the push-skip + boot-resume truth.
        if (thumbReceipt) {
            uint8_t newlySet = 0;
            for (uint8_t b = 0; b < 32; b++) {
                if ((ack.bitmap & (1UL << b)) == 0) {
                    continue;
                }
                const uint16_t chunk = ack.windowBase + b;
                if (chunk >= entry->thumbTotalChunks || chunk >= 64) {
                    continue;   // bounds: thumbnail cap and bitmap width
                }
                const uint64_t bit = (1ULL << chunk);
                if ((entry->thumbAcked & bit) == 0) {
                    entry->thumbAcked |= bit;
                    newlySet++;
                }
            }
            if (newlySet > 0) {
                // Persist at most once per receipt (≤3 windows per thumb) —
                // never inside the per-chunk loop. Volatile-fallback entries
                // have no record; their merge stays RAM-only.
                if (!entry->volatileFallback &&
                    !BalloonSdStoreTx().persistThumbAcked(entry->imageId, entry->thumbAcked)) {
                    Serial.printf("SdStore: thumbAcked persist failed for image %u (receipt kept in RAM)\n",
                                  static_cast<unsigned>(entry->imageId));
                }
                Serial.printf("ImageTx: image %u thumbnail ACK(id=%u win=%u bm=%08X) - %u chunk(s) base-confirmed\n",
                              static_cast<unsigned>(entry->imageId),
                              static_cast<unsigned>(ack.imageId),
                              static_cast<unsigned>(ack.windowBase),
                              (unsigned)ack.bitmap, newlySet);
            }
        } else {
            // 09-01 bench fix (base40.log): the receipt is no longer log-only.
            // Merge the window bitmap into fullAcked so serviceWindowChunk's
            // skip can jump confirmed chunks on a re-armed span — a 1-2 chunk
            // tail heal instead of a 16-chunk re-blast. The log line stays
            // byte-identical for bench-log tooling.
            for (uint8_t b = 0; b < 32; b++) {
                if ((ack.bitmap & (1UL << b)) == 0) {
                    continue;
                }
                const uint16_t chunk = ack.windowBase + b;
                if (chunk >= entry->fullTotalChunks || chunk >= 1024) {
                    continue;   // bounds: wire cap and bitmap width
                }
                entry->fullAcked[chunk >> 6] |= (1ULL << (chunk & 63));
            }
            Serial.printf("ImageTx: image %u FULL window receipt (win=%u bm=%08X st=%u)\n",
                          static_cast<unsigned>(entry->imageId),
                          static_cast<unsigned>(ack.windowBase),
                          (unsigned)ack.bitmap, ack.status);
        }
        return true;
    }

    // Kind-scoped verdicts
    if (ack.status == static_cast<uint8_t>(ImageAckStatus::KIND_COMPLETE_CRC_OK)) {
        if (thumbReceipt) {
            // The verdict covers every chunk: fold a full mask into the
            // bitmap so an in-flight push terminates through the skip loop
            // on its next pass, and persist the delivery bit.
            for (uint16_t c = 0; c < entry->thumbTotalChunks && c < 64; c++) {
                entry->thumbAcked |= (1ULL << c);
            }
            if (!entry->volatileFallback) {
                BalloonSdStoreTx().persistThumbAcked(entry->imageId, entry->thumbAcked);
                BalloonSdStoreTx().markDelivered(entry->imageId, ImageKind::THUMBNAIL);
            }
            Serial.printf("ImageTx: image %u thumbnail base-confirmed COMPLETE - delivery persisted\n",
                          static_cast<unsigned>(entry->imageId));
        } else {
            if (!entry->volatileFallback) {
                BalloonSdStoreTx().markDelivered(entry->imageId, ImageKind::FULL_IMAGE);
            }
            Serial.printf("SdStore: image %u fully delivered (base-confirmed) - RAM index slot released (files kept on card)\n",
                          static_cast<unsigned>(entry->imageId));
            freeEntry(*entry);
        }
        return true;
    }

    // KIND_FAILED_CRC: the base held the bytes up to its verify and rejected
    // them — the receipt state for this kind is invalid.
    if (thumbReceipt) {
        entry->thumbAcked = 0;
        if (!entry->volatileFallback) {
            BalloonSdStoreTx().persistThumbAcked(entry->imageId, 0);
        }
        Serial.printf("ImageTx: image %u thumbnail CRC rejected by base - receipt state cleared (heal re-pushes)\n",
                      static_cast<unsigned>(entry->imageId));
    } else {
        // 09-01: the verdict invalidates every receipt bit too — fullAcked
        // says "the base holds these bytes", and the base just swore it does
        // NOT. Leaving the map set would make the re-pull's skip loop jump
        // exactly the chunks the base rejected, and the pull could never
        // heal. Same discipline as the thumbnail branch above.
        memset(entry->fullAcked, 0, sizeof(entry->fullAcked));
        Serial.printf("ImageTx: image %u FULL CRC rejected by base - entry kept, receipt map cleared (base re-pulls)\n",
                      static_cast<unsigned>(entry->imageId));
    }
    return true;
}

FullRequestResult ImageTxManager::handleFullRequest(const uint8_t* payload, size_t len) {
    if (!initialized || payload == nullptr || len < 2) {
        return FullRequestResult::UNKNOWN;
    }
    // PayloadImageFullRequest shape: imageId BE16 + 1 reserved byte (send 0,
    // ignored on receipt — decode via the big-endian helper, Pitfall 9)
    const uint16_t imageId = CommandProtocol::readUint16(payload);

    // RAM entry first
    ImageTxEntry* entry = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used && entries[i].imageId == imageId) {
            entry = &entries[i];
            break;
        }
    }

    if (entry != nullptr) {
        // A mid-push entry answers BUSY: the thumbnail obligation runs first
        // (thumb-first is the delivery contract); the base's request retry
        // re-arms once the push drains.
        if (entry->state == ImageTxEntryState::PUSH_THUMB_MANIFEST ||
            entry->state == ImageTxEntryState::PUSH_THUMB_CHUNKS) {
            Serial.printf("ImageTx: FULL request for image %u deferred - thumbnail push in flight\n",
                          static_cast<unsigned>(imageId));
            return FullRequestResult::BUSY;
        }
        // THUMB_PUSHED (the normal park), ANNOUNCE_FULL (a request already
        // armed — idempotent re-arm), ANNOUNCED/SERVED (base lost its row —
        // re-announce; mirrors the tail-heal re-open semantics): arm the
        // announce with a fresh manifest budget. Armability is checked for
        // every state — a parked oversize entry refuses honestly.
        if (!fullTransferArmable(*entry)) {
            Serial.printf("ImageTx: FULL request for image %u refused - full not armable (no bytes or above cap)\n",
                          static_cast<unsigned>(imageId));
            return FullRequestResult::NOT_ARMABLE;
        }
        entry->manifestAttempts = 0;   // CR-02/WR-01: a fresh request starts a fresh announce budget
        entry->state = ImageTxEntryState::ANNOUNCE_FULL;
        entry->lastActivityMs = millis();
        Serial.printf("ImageTx: FULL request for image %u accepted - announcing on next pass\n",
                      static_cast<unsigned>(imageId));
        return FullRequestResult::ACCEPTED;
    }

    // No RAM entry (TTL-evicted / fresh boot): card re-admit through the
    // SAME validated fill the boot rescan uses — the card archive is the
    // truth, so a request for any archived image works far beyond the
    // depth-5 RAM queue's horizon. This path deliberately BYPASSES
    // SD_ST_RESUME_LATCH: an explicit base ask re-arms a latched row (the
    // latch suppresses only the automatic boot-rescan resume), and the
    // pull's delivery clears the latch via markDelivered.
    BalloonResumedRecord rec{};
    if (!BalloonSdStoreTx().loadResumedRecord(imageId, rec)) {
        Serial.printf("ImageTx: FULL request for image %u refused - no record on card\n",
                      static_cast<unsigned>(imageId));
        return FullRequestResult::UNKNOWN;
    }

    // An outstanding thumbnail obligation answers BUSY with the record
    // admitted: the push starts now, and the base's retry lands after it.
    const bool thumbOwed = (rec.thumbLength > 0) && !rec.thumbDelivered;
    if (rec.fullLength == 0 || rec.fullLength > IMG_MAX_IMAGE_SIZE) {
        Serial.printf("ImageTx: FULL request for image %u refused - card full bytes %u B not armable\n",
                      static_cast<unsigned>(imageId),
                      static_cast<unsigned>(rec.fullLength));
        return FullRequestResult::NOT_ARMABLE;
    }

    ImageTxEntry* slot = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            slot = &entries[i];
            break;
        }
    }
    if (slot == nullptr) {
        Serial.printf("ImageTx: FULL request for image %u deferred - queue full (depth %u)\n",
                      static_cast<unsigned>(imageId),
                      static_cast<unsigned>(QUEUE_DEPTH));
        return FullRequestResult::BUSY;   // TTL drains; the base retries
    }
    admitRescannedFillEntry(*slot, rec);
    slot->manifestAttempts = 0;
    slot->state = ImageTxEntryState::ANNOUNCE_FULL;   // the request IS the announce trigger
    if (thumbOwed) {
        // The record's thumb bit was cleared (or never set): admitRescannedFillEntry
        // put a thumb obligation in the META-derived state — restore it so the
        // push runs BEFORE the requested announce (thumb-first contract).
        slot->state = ImageTxEntryState::PUSH_THUMB_MANIFEST;
        Serial.printf("ImageTx: FULL request for image %u deferred - card thumb undelivered; push admitted first, retry after\n",
                      static_cast<unsigned>(imageId));
        return FullRequestResult::BUSY;
    }
    Serial.printf("ImageTx: FULL request for image %u accepted from card - announcing on next pass\n",
                  static_cast<unsigned>(imageId));
    return FullRequestResult::ACCEPTED;
}

bool ImageTxManager::serviceWindowChunk(ImageTxEntry& entry) {
    if (!entry.windowArmed ||
        entry.windowNextIndex >= entry.windowStart + entry.windowCount) {
        entry.windowArmed = false;
        return true;
    }

    // G-01-7 lever 3 (01-12): RX-settle gate — only the FIRST chunk of a
    // freshly (re-)armed window waits out IMG_WINDOW_RX_SETTLE_MS after its
    // arming. The 01-11 session-2 discriminator (five immediate tail
    // re-requests, seq 53/55-59, base2.log:415-498, 6 END MARKER MISS) named
    // the half-duplex immediate-retransmit turnaround-collision class:
    // answering a re-request instantly collides with the link still turning
    // around. A re-armed span re-settles (windowArmedAtMs refreshes at every
    // arming); mid-window continuation is unaffected, and the settle (500 ms)
    // sits far below both the preempt (5000 ms) and stall (15000 ms) clocks.
    if (entry.windowNextIndex == entry.windowStart &&
        (millis() - entry.windowArmedAtMs) < IMG_WINDOW_RX_SETTLE_MS) {
        return true;   // no transmit this pass — the settle window holds
    }

    // Base-confirmed skip (09-01 bench fix, base40.log) — the FULL-kind
    // mirror of pushThumbChunk's thumbAcked skip: a stall/tail re-request
    // re-arms the span and (re-)resets the cursor to windowStart, so without
    // this loop every heal re-blasted the whole window (~10 s of airtime and
    // fresh half-duplex collision windows) to recover the 1-2 chunks the
    // base was actually missing. Chunks a WINDOW_COMPLETE receipt confirmed
    // are jumped in one pass; if that drains the span, the window completes
    // without spending a single packet — the SERVED marker is honest (the
    // base's own receipt, not a TX success, is the evidence). THUMBNAIL
    // windows consult thumbAcked through the push path only — never this map.
    if (entry.windowKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE)) {
        uint8_t skipped = 0;
        while (entry.windowNextIndex < entry.windowStart + entry.windowCount &&
               entry.windowNextIndex < entry.fullTotalChunks &&
               entry.windowNextIndex < 1024 &&
               ((entry.fullAcked[entry.windowNextIndex >> 6] >>
                 (entry.windowNextIndex & 63)) & 1ULL)) {
            entry.windowNextIndex++;
            skipped++;
        }
        if (skipped > 0) {
            Serial.printf("ImageTx: window re-request for image %u - skipping %u base-confirmed chunk(s)\n",
                          entry.imageId, skipped);
        }
        if (entry.windowNextIndex >= entry.windowStart + entry.windowCount) {
            entry.windowArmed = false;
            if (static_cast<uint32_t>(entry.windowStart) + entry.windowCount >=
                entry.fullTotalChunks) {
                entry.state = ImageTxEntryState::SERVED;
            }
            return true;   // span already held base-side — no transmit this pass
        }
    }

    // KIND-SELECTED source (D-22 / CR-01): the armed window names which bytes
    // it serves — THUMBNAIL windows serve thumbnail bytes, FULL windows serve
    // full-image bytes. STORE-03 (Phase 2.5): a file-backed entry (the
    // default, no PSRAM image copy exists) reads the armed kind's card file
    // at chunkIndex * IMG_CHUNK_PAYLOAD_SIZE into a stack buffer; a volatile-
    // fallback entry slices the selected PSRAM buffer as before (offset math
    // identical for both kinds: chunk index * IMG_CHUNK_PAYLOAD_SIZE (223),
    // tail-clamped by the
    // selected byte length; bounds hold by construction: windowNextIndex <
    // windowStart + windowCount <= the armed kind's totalChunks). Only the
    // byte SOURCE changes — the settle gate above, WR-08 retry/skip, and the
    // SERVED transition below are untouched.
    const bool thumbWindow = (entry.windowKind == static_cast<uint8_t>(ImageKind::THUMBNAIL));
    uint16_t idx = entry.windowNextIndex;

    uint8_t chunkBuf[IMG_CHUNK_PAYLOAD_SIZE];
    const uint8_t* source = nullptr;
    uint8_t chunkLen = 0;
    bool sdReadFailed = false;
    if (!entry.volatileFallback) {
        size_t got = 0;
        if (BalloonSdStoreTx().readChunk(entry.imageId,
                                         thumbWindow ? ImageKind::THUMBNAIL : ImageKind::FULL_IMAGE,
                                         idx,
                                         chunkBuf, sizeof(chunkBuf), &got)) {
            chunkLen = static_cast<uint8_t>(got);
            source = chunkBuf;
        } else {
            // A readChunk false return takes the EXACT failure semantics of
            // a transmit failure (same-index fail streak, IMG_CHUNK_TX_RETRY_MAX
            // bound, the named skip log below extended with "SD read failed") —
            // never a fabricated chunk, never cursor advance on unread bytes.
            sdReadFailed = true;
        }
    } else {
        const uint8_t* buf = thumbWindow ? entry.thumbBuffer : entry.fullBuffer;
        const size_t sourceLength = thumbWindow ? entry.thumbLength : entry.fullLength;
        size_t offset = static_cast<size_t>(idx) * IMG_CHUNK_PAYLOAD_SIZE;
        size_t remaining = sourceLength - offset;
        chunkLen = static_cast<uint8_t>(
            (remaining > IMG_CHUNK_PAYLOAD_SIZE) ? IMG_CHUNK_PAYLOAD_SIZE : remaining);
        source = buf + offset;
    }

    bool ok = false;
    if (!sdReadFailed) {
        ImageChunkPacket pkt = createChunkPacket(entry.imageId, entry.windowKind,
                                                 idx,
                                                 source, chunkLen);

        uint8_t buffer[CMD_MAX_PACKET_SIZE];
        size_t length = 0;
        ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);
    }

    if (DEBUG_IMAGE_TX) {
        // Kind discriminator (CR-01 / G-01-9 defect B, 01-13): the log names
        // the kind the window serves — bench logs can discriminate which
        // kind's bytes left the balloon at every index
        if (sdReadFailed) {
            Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u) SD read failed\n",
                         entry.imageId,
                         static_cast<unsigned>(entry.windowKind),
                         static_cast<unsigned>(idx - entry.windowStart + 1),
                         static_cast<unsigned>(entry.windowCount));
        } else {
            Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u, %u B) %s\n",
                         entry.imageId,
                         static_cast<unsigned>(entry.windowKind),
                         static_cast<unsigned>(idx - entry.windowStart + 1),
                         static_cast<unsigned>(entry.windowCount),
                         chunkLen,
                         ok ? "sent" : "FAILED");
        }
    }

    // WR-08 (01-17): the cursor advances ONLY on a successful transmit — the
    // same same-index bound as the push path (an SD read failure counts
    // identically), with the skip log naming the window kind. The SERVED
    // transition is SUCCESS-GATED on the final chunk: a final chunk skipped
    // after the bound still completes the window (cursor advanced,
    // windowArmed cleared — the base's tail re-request re-opens service) but
    // leaves the entry at ANNOUNCED, never SERVED for bytes that never left
    // the balloon.
    bool finalChunkSkipped = false;
    if (ok) {
        entry.windowChunkFailStreak = 0;
        entry.windowNextIndex++;
    } else {
        entry.windowChunkFailStreak++;
        if (entry.windowChunkFailStreak >= IMG_CHUNK_TX_RETRY_MAX) {
            if (sdReadFailed) {
                Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u) skipped after %u failed attempts - SD read failed\n",
                             entry.imageId,
                             static_cast<unsigned>(entry.windowKind),
                             static_cast<unsigned>(idx - entry.windowStart + 1),
                             static_cast<unsigned>(entry.windowCount),
                             static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
            } else {
                Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u) skipped after %u failed transmit attempts\n",
                             entry.imageId,
                             static_cast<unsigned>(entry.windowKind),
                             static_cast<unsigned>(idx - entry.windowStart + 1),
                             static_cast<unsigned>(entry.windowCount),
                             static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
            }
            entry.windowChunkFailStreak = 0;
            entry.windowNextIndex++;
            finalChunkSkipped =
                (entry.windowNextIndex >= entry.windowStart + entry.windowCount);
        }
        // else: stay on the same index — the next pass retries it
    }
    // Every transmit attempt is activity — keeps the preempt clock honest
    // (a retrying window never looks idle to pushPending)
    entry.lastActivityMs = millis();
    if (entry.windowNextIndex >= entry.windowStart + entry.windowCount) {
        // Window complete: clear the armed context and await the next request
        entry.windowArmed = false;
        // Completion marker (02-05 / CR-03 fix c): a FULL window whose clamped
        // span reached the image tail (windowStart + windowCount ==
        // fullTotalChunks) has offered the base every full chunk at least
        // once — mark SERVED (preferred eviction candidate; buffers KEPT so
        // a tail re-request can still heal). THUMBNAIL windows never change
        // entry state. WR-08 (01-17): SERVED is reachable ONLY from the
        // transmit-success path of the final chunk — finalChunkSkipped keeps
        // the skip-after-bound completion at ANNOUNCED so a never-offered
        // tail can still pull (and the base's tail re-request re-opens
        // service exactly as it does for any ANNOUNCED entry).
        if (!finalChunkSkipped &&
            entry.windowKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE) &&
            static_cast<uint32_t>(entry.windowStart) + entry.windowCount >= entry.fullTotalChunks) {
            // Image-transfer rework: the SERVED transition itself is kept
            // (tail-heal semantics unchanged — a tail re-request re-opens
            // service), but the delivery mark + RAM-slot release that used to
            // ride TX success are GONE. Delivery is now the base's
            // ACK-confirmed verdict (handleImageAck KIND_COMPLETE_CRC_OK),
            // which persists the META bit and frees the slot only when the
            // base proved it holds verified bytes. The entry survives SERVED
            // so a later FULL request can still find it.
            entry.state = ImageTxEntryState::SERVED;
        }
    }
    return ok;
}

// ===========================
// Eviction Policy (bounded memory, T-02-05)
// ===========================

// G-01-7 round #14 (plan 01-33, 01-G01-7-LEVER.md section 4 option a): the
// supersede victim selection is ranked through evictionClassOf — the SAME
// ranking the enqueue-overflow scan uses, closing the two-standards gap the
// session-10 census proved structural: the supersede path consulted no
// class-5 protection at all and flushed every older non-mid-service entry in
// slot order, so image 42's window request evicted the base-activated,
// receipt-evidenced image 41 entry (balloon4.log:816-817) -> the seven
// honest rejections (:1003-:1162) burned the base's D-24 budget to 0/31.
// Mechanics:
//   - candidates stay "used AND strictly older" (enqueueSeq < reference),
//     exactly as before;
//   - the 01-12 mid-service deferral keeps its guard verbatim (predicate and
//     line) — an in-flight heal/pull is never a victim, and the guard stays
//     unconditional here (the overflow scan's pass-2 drop-the-skip does not
//     apply to supersede);
//   - lower-class candidates are evicted in ascending evictionClassOf order
//     (ties: oldest enqueueSeq — the overflow scan's tie-break), and the
//     existing "supersedes older entry ... evicted" line is preserved;
//   - a receipt-evidenced entry (class 5: windowEverArmed ||
//     lastWindowRequestMs != 0) is NEVER the victim while any lower-class
//     candidate exists — each spared entry prints the engagement
//     discriminator line (latch-free by design: an event line, bounded by
//     queue events);
//   - when ONLY class-5 candidates exist, nothing is evicted and this call
//     returns false: the caller rejects the incoming request honestly
//     through the EXISTING unknown/evicted NACK class extended with its own
//     named line — no new protocol surface — and the base's D-24 pass
//     machinery retries within its existing bound (01-G01-7-LEVER.md
//     section 2: prevent the eviction, not soften the rejection).
// evictionClassOf is reused VERBATIM — no forked ranking (T-01-33-02);
// sweepExpiredEntries' TTL path keeps its full eviction rights.
bool ImageTxManager::evictEntriesOlderThan(const ImageTxEntry& reference) {
    // Pass 1 — the 01-12 mid-service deferral (verbatim): an entry whose
    // window is armed and incomplete is not a candidate this call (its
    // heal/pull chunks are in flight on the link — evicting it mid-stream is
    // exactly the 01-11 session-2 failure, balloon2.log:656). The BUSY check
    // after the supersede still gates the request on these entries.
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used && entries[i].enqueueSeq < reference.enqueueSeq &&
            entries[i].windowArmed &&
            entries[i].windowNextIndex < entries[i].windowStart + entries[i].windowCount) {
            Serial.printf("ImageTx: supersede of image %u deferred - window mid-service\n",
                         entries[i].imageId);
        }
    }
    // Pass 2 — ranked eviction: evict the lowest-class (oldest-first within
    // a class) non-mid-service candidate until only receipt-evidenced
    // candidates remain.
    bool evictedLower = false;
    uint8_t lastEvictedClass = 0;
    uint16_t lastEvictedImageId = 0;
    for (;;) {
        ImageTxEntry* victim = nullptr;
        uint8_t victimClass = 0;
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            ImageTxEntry& e = entries[i];
            if (!e.used || e.enqueueSeq >= reference.enqueueSeq) {
                continue;
            }
            if (e.windowArmed &&
                e.windowNextIndex < e.windowStart + e.windowCount) {
                continue;   // deferred in pass 1 — never a supersede victim
            }
            uint8_t cls = evictionClassOf(e);
            if (victim == nullptr || cls < victimClass ||
                (cls == victimClass && e.enqueueSeq < victim->enqueueSeq)) {
                victim = &e;
                victimClass = cls;
            }
        }
        if (victim == nullptr) {
            return true;   // nothing older remains — request may proceed
        }
        if (victimClass >= 5) {
            // The scan picks the LOWEST class, so class 5 here means every
            // remaining candidate is receipt-evidenced.
            if (!evictedLower) {
                // Admission would sacrifice an actively-pulled entry for the
                // incoming request — refuse: the caller rejects honestly and
                // the base's bounded budget retries.
                return false;
            }
            // Lower-class entries were evicted instead — spare each
            // receipt-evidenced remainder with the engagement line.
            for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
                ImageTxEntry& e = entries[i];
                if (!e.used || e.enqueueSeq >= reference.enqueueSeq) {
                    continue;
                }
                if (e.windowArmed &&
                    e.windowNextIndex < e.windowStart + e.windowCount) {
                    continue;
                }
                if (evictionClassOf(e) >= 5) {
                    Serial.printf("ImageTx: supersede of receipt-evidenced image %u avoided - evicting class %u entry image %u instead (G-01-7)\n",
                                 e.imageId, lastEvictedClass, lastEvictedImageId);
                }
            }
            return true;
        }
        if (victimClass == 0) {
            // G-01-7 round #15 (01-35): the safe-reclaim discriminator — the
            // selected victim is fully receipt-confirmed (class 0, the shared
            // ranking), so the base provably holds every byte and the buffers
            // are reclaimable: the session-11 over-rejection geometry's safe
            // victim. Kept distinct from the generic line below, which still
            // prints for EVERY eviction (bench-log tooling greps both).
            Serial.printf("ImageTx: supersede victim image %u fully receipt-confirmed - safe reclaim class 0, every WINDOW_COMPLETE receipt in (G-01-7)\n",
                         victim->imageId);
        }
        Serial.printf("ImageTx: window request for image %u supersedes older entry image %u; evicted\n",
                     reference.imageId, victim->imageId);
        lastEvictedClass = victimClass;
        lastEvictedImageId = victim->imageId;
        freeEntry(*victim);
        evictedLower = true;
    }
}

void ImageTxManager::sweepExpiredEntries() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        // Wraparound-safe comparison; an armed mid-service window refreshes
        // lastActivityMs per chunk, so an active pull can never expire
        if (entries[i].used && (now - entries[i].lastActivityMs) > IMG_ENTRY_TTL_MS) {
            Serial.printf("ImageTx: entry image %u idle > %u ms (TTL); evicted\n",
                         entries[i].imageId, static_cast<unsigned>(IMG_ENTRY_TTL_MS));
            freeEntry(entries[i]);
        }
    }
}

// ===========================
// Helpers
// ===========================

// Real esp32-camera framesize_t -> FrameSize wire code, matched BY NAME.
// Mirrors CommandHandler::frameSizeFromEsp (the two enums number identical
// names differently — never reinterpret one as the other numerically).
static uint8_t frameSizeWireCode(framesize_t espFrameSize) {
    switch (espFrameSize) {
        case FRAMESIZE_QQVGA: return static_cast<uint8_t>(FrameSize::FRAMESIZE_QQVGA);
        case FRAMESIZE_QVGA:  return static_cast<uint8_t>(FrameSize::FRAMESIZE_QVGA);
        case FRAMESIZE_HQVGA: return static_cast<uint8_t>(FrameSize::FRAMESIZE_HQVGA);
        case FRAMESIZE_CIF:   return static_cast<uint8_t>(FrameSize::FRAMESIZE_CIF);
        case FRAMESIZE_VGA:   return static_cast<uint8_t>(FrameSize::FRAMESIZE_VGA);
        case FRAMESIZE_SVGA:  return static_cast<uint8_t>(FrameSize::FRAMESIZE_SVGA);
        case FRAMESIZE_XGA:   return static_cast<uint8_t>(FrameSize::FRAMESIZE_XGA);
        case FRAMESIZE_SXGA:  return static_cast<uint8_t>(FrameSize::FRAMESIZE_SXGA);
        case FRAMESIZE_UXGA:  return static_cast<uint8_t>(FrameSize::FRAMESIZE_UXGA);
        default:
            // Real sizes with no protocol code report the boot-default QVGA
            return static_cast<uint8_t>(FrameSize::FRAMESIZE_QVGA);
    }
}

ImageTxSettings ImageTxManager::snapshotSettings() const {
    ImageTxSettings s{};
    s.resolution = frameSizeWireCode(Camera().getFrameSize());
    s.quality = static_cast<uint8_t>(Camera().getQuality());
    s.brightness = static_cast<int8_t>(Camera().getBrightness());
    s.contrast = static_cast<int8_t>(Camera().getContrast());
    s.saturation = static_cast<int8_t>(Camera().getSaturation());
    s.exposure = static_cast<int8_t>(Camera().getExposure());
    s.wbMode = static_cast<uint8_t>(Camera().getWBMode());
    return s;
}

// A full transfer can only be armed when the entry's full bytes exist — a
// volatile-fallback entry's PSRAM buffer, or (STORE-03, the default) a
// file-backed entry's persist-derived length with the bytes on the card —
// and the length is inside the IMG_MAX_IMAGE_SIZE cap (oversize entries park
// after their thumbnail push — the Q4 wire-contract gate, not a RAM limit)
bool ImageTxManager::fullTransferArmable(const ImageTxEntry& entry) const {
    const bool haveFullBytes = entry.volatileFallback
        ? (entry.fullBuffer != nullptr)
        : (entry.fullLength > 0);
    return haveFullBytes && entry.fullLength > 0 &&
           entry.fullLength <= IMG_MAX_IMAGE_SIZE;
}

uint16_t ImageTxManager::chunksForSize(size_t lengthBytes) {
    return static_cast<uint16_t>((lengthBytes + IMG_CHUNK_PAYLOAD_SIZE - 1) / IMG_CHUNK_PAYLOAD_SIZE);
}

void ImageTxManager::freeEntry(ImageTxEntry& entry) {
    if (entry.fullBuffer != nullptr) {
        free(entry.fullBuffer);
        entry.fullBuffer = nullptr;
    }
    if (entry.thumbBuffer != nullptr) {
        free(entry.thumbBuffer);
        entry.thumbBuffer = nullptr;
    }
    entry.fullLength = 0;
    entry.fullCrc32 = 0;
    entry.fullTotalChunks = 0;
    entry.thumbLength = 0;
    entry.thumbCrc32 = 0;
    entry.thumbTotalChunks = 0;
    entry.nextThumbChunk = 0;
    entry.thumbAcked = 0;             // image-transfer rework: a recycled slot carries no base-confirmed bits
    memset(entry.fullAcked, 0, sizeof(entry.fullAcked));   // 09-01: same discipline for the FULL receipt map
    entry.manifestAttempts = 0;   // CR-02/WR-01: a recycled slot starts with a clean manifest budget
    entry.thumbChunkFailStreak = 0;   // WR-08: a recycled slot starts with clean fail streaks
    entry.windowChunkFailStreak = 0;
    entry.lastWindowRequestMs = 0;   // G-01-7 lever 1 (01-21): a recycled slot starts with no receipt evidence
    entry.windowArmed = false;
    entry.windowKind = 0;
    entry.windowEverArmed = false;
    entry.windowStart = 0;
    entry.windowCount = 0;
    entry.windowNextIndex = 0;
    entry.windowArmedAtMs = 0;
    entry.volatileFallback = false;   // D-03: a recycled slot starts file-backed (the default)
    entry.used = false;
    entry.state = ImageTxEntryState::IDLE;
}

// ===========================
// Queue Snapshot
// ===========================

uint8_t ImageTxManager::getQueueCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used) {
            count++;
        }
    }
    return count;
}

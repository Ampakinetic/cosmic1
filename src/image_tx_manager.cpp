#include "image_tx_manager.h"
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
    , nextEnqueueSeq(0)
    , lastEnqueuedImageId(0)
    , lastBeaconMs(0)
    , beaconSeq(0)
    , firstBeaconLogged(false)
    , beaconsSent(0)
    , lastBeaconOk(false)
    , lastInboundWindowRequestMs(0)
    , reannounceHoldLogged(false)
    , inboundWindowRequestSeen(false)
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

bool ImageTxManager::begin(E32LoRa* lora) {
    if (!lora) {
        return false;
    }

    this->lora = lora;
    initialized = true;

    // G-01-7 lever 1 (01-21): clean receipt-evidence state at boot — no
    // inbound request has ever been seen and no hold episode is open. Since
    // the 01-25 receipt-ever flag the zero stamp is inert (the hold gate
    // requires inboundWindowRequestSeen), so boot windows carry no phantom
    // quiet floor from this gate; re-announce spacing at boot comes from the
    // IMG_FULL_REANNOUNCE_IDLE_MS cadence alone.
    lastInboundWindowRequestMs = 0;
    reannounceHoldLogged = false;

    // G-01-11 / WINDOWS 16 fix (01-25): the receipt-ever flag resets at
    // begin() too, so the busy-hold gate cannot fire on the boot-epoch zero
    // stamp after a re-begin. A hold episode requires genuine inbound window
    // traffic first.
    inboundWindowRequestSeen = false;

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
    Serial.printf("ImageTx: [BOOT] reset-cause: %s (G-01-10)\n",
                  resetReasonName(esp_reset_reason()));

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
    // (the reannounceHoldLogged convention), re-armed on a normal pass.
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
    float batteryV = PowerMgr().getBatteryVoltage();
    if (batteryV >= 1.8f && batteryV <= 8.0f && analogRead(BATTERY_SENSE_PIN) != 0) {
        body.batteryMilliV = static_cast<uint16_t>(lroundf(batteryV * 1000.0f));
        batteryValid = true;
    }
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

// Overflow-eviction class (02-05 / CR-03 fix c) — LOWER class evicted sooner:
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
static uint8_t evictionClassOf(const ImageTxEntry& entry) {
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
    // than IMG_MAX_IMAGE_SIZE never arms a full transfer — log a warning
    // naming the image ID and size, skip the doomed copy entirely, and let
    // the thumbnail still push. The base never sees a FULL_IMAGE manifest
    // for it, so no pull is attempted (no airtime wasted on a transfer the
    // base would reject at its own MAX_IMAGE_SIZE validation).
    bool fullArmable = (img.length <= IMG_MAX_IMAGE_SIZE);
    if (!fullArmable) {
        Serial.printf("ImageTx: image %u full size %u B exceeds cap %u B; skipping full transfer (thumbnail still pushes)\n",
                     imageId,
                     static_cast<unsigned>(img.length),
                     static_cast<unsigned>(IMG_MAX_IMAGE_SIZE));
    }

    if (fullArmable) {
        // Take PSRAM ownership of the full image BEFORE returning from this
        // branch (Pitfall 7): the next capture's freeCurrentImage() must not
        // pull bytes out from under a transfer
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
    entry.lastActivityMs = millis();

    // Nothing transferable (no thumbnail AND no armable full) — do not
    // occupy a queue slot
    if (entry.state == ImageTxEntryState::THUMB_PUSHED &&
        entry.thumbBuffer == nullptr && entry.fullBuffer == nullptr) {
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
    // below the base's 8000 ms IMG_WINDOW_STALL_MS, whose clock resets on
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

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): known-busy hold on
    // the re-announce drop-clock. An inbound window request younger than
    // IMG_FULL_REANNOUNCE_BUSY_MS — any kind, any verdict, including the
    // unknown/evicted rejects — means the base is still working this burst;
    // the re-announce does not even look for a candidate this pass. The hold
    // consumes NOTHING: no attempt, no idle-period advance, no drop (the
    // 01-17 IMG_FULL_REANNOUNCE_IDLE_MS cadence and MAX bound resume
    // unchanged 15 s after the last inbound request). One named log line per
    // hold episode (reannounceHoldLogged latch, cleared whenever a re-announce
    // actually transmits).
    // G-01-11 / WINDOWS 16 fix (01-25): inboundWindowRequestSeen guards the
    // boot-epoch zero stamp — the hold can engage only after a genuine
    // inbound window request, so the :504-style phantom episode is impossible
    // and the hold line below stays a truthful G-01-7 discriminator.
    if (inboundWindowRequestSeen &&
        (millis() - lastInboundWindowRequestMs) < IMG_FULL_REANNOUNCE_BUSY_MS) {
        if (!reannounceHoldLogged) {
            reannounceHoldLogged = true;
            Serial.println("ImageTx: re-announce held - inbound window traffic active");
        }
        return;
    }

    // G-01-9 defect C (01-17): the channel's natural idle slot — no push
    // work, no armed window. Re-announce an ANNOUNCED full whose FULL window
    // never armed (receipt never observed) and which has been idle past
    // IMG_FULL_REANNOUNCE_IDLE_MS, as this pass's single transmit. The
    // re-announce thus NEVER competes with push work or window service, and
    // the beacon early-return in process() still outranks it (PRI-01
    // untouched); the first FULL window arm permanently stops the mechanism
    // (fullWindowEverArmed), confining any duplicate manifests to the
    // pre-first-arm phase where the base-side restart is benign (IN-08).
    ImageTxEntry* lost = findReannounceCandidate();
    if (lost != nullptr) {
        reannounceFullManifest(*lost);
    }
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
    if (entry.nextThumbChunk >= entry.thumbTotalChunks) {
        entry.state = completedThumbState(entry);
        return true;
    }

    size_t offset = static_cast<size_t>(entry.nextThumbChunk) * IMG_CHUNK_PAYLOAD_SIZE;
    size_t remaining = entry.thumbLength - offset;
    uint8_t chunkLen = static_cast<uint8_t>(
        (remaining > IMG_CHUNK_PAYLOAD_SIZE) ? IMG_CHUNK_PAYLOAD_SIZE : remaining);

    ImageChunkPacket pkt = createChunkPacket(entry.imageId, static_cast<uint8_t>(ImageKind::THUMBNAIL),
                                             entry.nextThumbChunk,
                                             entry.thumbBuffer + offset, chunkLen);

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        // Kind discriminator (CR-01, 01-13): the push is always THUMBNAIL —
        // printing it keeps push and window lines symmetric so bench logs can
        // tell which kind's bytes left the balloon at every index
        Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u, %u B) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(ImageKind::THUMBNAIL),
                     static_cast<unsigned>(entry.nextThumbChunk + 1),
                     static_cast<unsigned>(entry.thumbTotalChunks),
                     chunkLen,
                     ok ? "sent" : "FAILED");
    }

    // WR-08 (01-17): the cursor advances ONLY on a successful transmit — a
    // failed transmit retries the SAME index on subsequent process() passes
    // (one transmit per pass is the existing pacing), bounded by
    // IMG_CHUNK_TX_RETRY_MAX. At the bound the chunk is skipped for its pass
    // with a named log; before the bound the failed index simply retries.
    if (ok) {
        entry.thumbChunkFailStreak = 0;
        entry.nextThumbChunk++;
    } else {
        entry.thumbChunkFailStreak++;
        if (entry.thumbChunkFailStreak >= IMG_CHUNK_TX_RETRY_MAX) {
            Serial.printf("ImageTx: chunk(image %u kind %u, %u/%u) skipped after %u failed transmit attempts\n",
                         entry.imageId,
                         static_cast<unsigned>(ImageKind::THUMBNAIL),
                         static_cast<unsigned>(entry.nextThumbChunk + 1),
                         static_cast<unsigned>(entry.thumbTotalChunks),
                         static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
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

// Where an entry lands once its thumbnail push completes: armable fulls
// proceed to the one-time FULL_IMAGE announcement (D-17 ordering), everything
// else parks until eviction.
ImageTxEntryState ImageTxManager::completedThumbState(const ImageTxEntry& entry) const {
    return fullTransferArmable(entry) ? ImageTxEntryState::ANNOUNCE_FULL
                                      : ImageTxEntryState::THUMB_PUSHED;
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

// G-01-9 defect C (01-17): the earliest (lowest enqueueSeq) ANNOUNCED full
// that has NEVER had a FULL window armed (the receipt signal) and has been
// idle past IMG_FULL_REANNOUNCE_IDLE_MS — the air-lost-manifest class the
// TX-verdict-gated 01-14 bound structurally cannot see (balloon5.log:274:
// TX-success accounting, zero base receipts, silent full loss). A thumb
// heal alone never disqualifies the entry (image-15 class).
ImageTxEntry* ImageTxManager::findReannounceCandidate() {
    ImageTxEntry* best = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used ||
            entries[i].state != ImageTxEntryState::ANNOUNCED ||
            entries[i].fullBuffer == nullptr ||
            entries[i].fullWindowEverArmed) {
            continue;
        }
        if ((millis() - entries[i].lastActivityMs) < IMG_FULL_REANNOUNCE_IDLE_MS) {
            continue;
        }
        if (best == nullptr || entries[i].enqueueSeq < best->enqueueSeq) {
            best = &entries[i];
        }
    }
    return best;
}

// Bounded idle re-announce (G-01-9 defect C / PRI-03): retransmits the FULL
// manifest for an announced-but-never-pulled full, via the SAME body
// construction as the one-shot announce. Each transmit attempt is one
// bounded attempt AND one idle period (reannounceAttempts and
// lastActivityMs advance on every verdict — air loss is the class being
// treated, so a TX-successful re-announce that still draws no window must
// consume budget). At IMG_FULL_REANNOUNCE_MAX the full is dropped with a
// named log — park-and-free exactly like the announce-bound branch in
// announceFullManifest (thumbBuffer kept so THUMBNAIL window heals keep
// working on the parked entry).
void ImageTxManager::reannounceFullManifest(ImageTxEntry& entry) {
    // G-01-7 lever 1 (01-21): a re-announce actually running closes any open
    // hold episode — the next busy-hold (if inbound window traffic resumes)
    // prints its own named line.
    reannounceHoldLogged = false;

    ImageManifestBody body = fillFullManifestBody(entry);
    ImageManifestPacket pkt = createManifestPacket(body); // factory owns the 0x12 type byte

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeManifest(pkt, buffer, length) && lora->transmit(buffer, length);

    entry.reannounceAttempts++;
    if (ok) {
        Serial.printf("ImageTx: FULL manifest(image %u, %u B, %u chunks) re-announced (%u/%u) - no window armed since announce\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.fullLength),
                     static_cast<unsigned>(entry.fullTotalChunks),
                     static_cast<unsigned>(entry.reannounceAttempts),
                     static_cast<unsigned>(IMG_FULL_REANNOUNCE_MAX));
    } else {
        Serial.printf("ImageTx: FULL manifest(image %u, %u B, %u chunks) re-announce FAILED (%u/%u) - no window armed since announce\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.fullLength),
                     static_cast<unsigned>(entry.fullTotalChunks),
                     static_cast<unsigned>(entry.reannounceAttempts),
                     static_cast<unsigned>(IMG_FULL_REANNOUNCE_MAX));
    }
    if (entry.reannounceAttempts >= IMG_FULL_REANNOUNCE_MAX) {
        Serial.printf("ImageTx: image %u full dropped - no FULL window armed after %u re-announces\n",
                     entry.imageId,
                     static_cast<unsigned>(IMG_FULL_REANNOUNCE_MAX));
        // Park-and-free at the bound (PRI-03 honest degradation, mirroring
        // the announce-bound branch): the base degrades to thumbnail-only
        // for this capture — bounded, logged, never silent. thumbBuffer
        // stays owned so THUMBNAIL window heals keep working.
        free(entry.fullBuffer);
        entry.fullBuffer = nullptr;
        entry.fullLength = 0;
        entry.fullCrc32 = 0;
        entry.fullTotalChunks = 0;
        entry.state = ImageTxEntryState::THUMB_PUSHED;
    }
    entry.lastActivityMs = millis();
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
    // validation (kind enum + entry match).
    lastInboundWindowRequestMs = millis();
    // G-01-11 / WINDOWS 16 (01-25): first genuine receipt arms the busy-hold
    // gate — same trust-boundary position as the stamp (before kind
    // validation, write-only liveness data, never a content decision).
    inboundWindowRequestSeen = true;

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
            if (entries[i].thumbBuffer == nullptr ||
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
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: window request for unknown/evicted image %u rejected\n", imageId);
        }
        return WindowRequestResult::UNKNOWN_IMAGE;
    }

    // G-01-7 burst full-delivery, balloon lever 1 (01-21): receipt evidence —
    // this request MATCHED a queued entry, so the base demonstrably holds at
    // least one of its manifests and is asking (reached only after the kind
    // enum check and entry match above — a crafted frame cannot stamp an
    // entry it did not fully address, T-01-21-01). Stamp it, and re-arm the
    // bounded re-announce budget: a manifest the base is demonstrably still
    // working must not age out at the IMG_FULL_REANNOUNCE_MAX bound while the
    // base keeps asking for the entry (session-6 images 25/26 — base held
    // their manifests 3x/4x while the bound expired, balloon11.log:553/:575).
    target->lastWindowRequestMs = millis();
    if (target->state == ImageTxEntryState::ANNOUNCED &&
        !target->fullWindowEverArmed && target->reannounceAttempts > 0) {
        target->reannounceAttempts = 0;
        Serial.printf("ImageTx: re-announce budget re-armed - window request received for image %u\n",
                     imageId);
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
        evictEntriesOlderThan(*target);
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
    // G-01-9 defect C (01-17): a FULL window arm is the receipt signal that
    // permanently stops the FULL-manifest re-announce — set ONLY for
    // non-thumb windows. A THUMBNAIL heal arm must NOT stop it: image 15's
    // exact shape is thumb-healed-COMPLETE with the FULL manifest air-lost,
    // and that entry still needs its full re-announced.
    if (!thumbWindow) {
        target->fullWindowEverArmed = true;
    }
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
    // sits far below both the preempt (5000 ms) and stall (8000 ms) clocks.
    if (entry.windowNextIndex == entry.windowStart &&
        (millis() - entry.windowArmedAtMs) < IMG_WINDOW_RX_SETTLE_MS) {
        return true;   // no transmit this pass — the settle window holds
    }

    // KIND-SELECTED source (D-22 / CR-01): the armed window names which owned
    // buffer it serves — THUMBNAIL windows slice thumbnail bytes, FULL windows
    // slice full-image bytes. The offset math is identical for both (chunk
    // index * 200, tail-clamped by the selected buffer's remaining bytes;
    // bounds hold by construction: windowNextIndex < windowStart +
    // windowCount <= the armed kind's totalChunks, and the arming matcher
    // guaranteed that kind's buffer is non-null)
    const bool thumbWindow = (entry.windowKind == static_cast<uint8_t>(ImageKind::THUMBNAIL));
    const uint8_t* source = thumbWindow ? entry.thumbBuffer : entry.fullBuffer;
    const size_t sourceLength = thumbWindow ? entry.thumbLength : entry.fullLength;

    uint16_t idx = entry.windowNextIndex;
    size_t offset = static_cast<size_t>(idx) * IMG_CHUNK_PAYLOAD_SIZE;
    size_t remaining = sourceLength - offset;
    uint8_t chunkLen = static_cast<uint8_t>(
        (remaining > IMG_CHUNK_PAYLOAD_SIZE) ? IMG_CHUNK_PAYLOAD_SIZE : remaining);

    ImageChunkPacket pkt = createChunkPacket(entry.imageId, entry.windowKind,
                                             idx,
                                             source + offset, chunkLen);

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        // Kind discriminator (CR-01 / G-01-9 defect B, 01-13): the log names
        // the kind the window serves — bench logs can discriminate which
        // kind's bytes left the balloon at every index
        Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u, %u B) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.windowKind),
                     static_cast<unsigned>(idx - entry.windowStart + 1),
                     static_cast<unsigned>(entry.windowCount),
                     chunkLen,
                     ok ? "sent" : "FAILED");
    }

    // WR-08 (01-17): the cursor advances ONLY on a successful transmit — the
    // same same-index bound as the push path, with the skip log naming the
    // window kind. The SERVED transition is SUCCESS-GATED on the final
    // chunk: a final chunk skipped after the bound still completes the window
    // (cursor advanced, windowArmed cleared — the base's tail re-request
    // re-opens service) but leaves the entry at ANNOUNCED, never SERVED for
    // bytes that never left the balloon.
    bool finalChunkSkipped = false;
    if (ok) {
        entry.windowChunkFailStreak = 0;
        entry.windowNextIndex++;
    } else {
        entry.windowChunkFailStreak++;
        if (entry.windowChunkFailStreak >= IMG_CHUNK_TX_RETRY_MAX) {
            Serial.printf("ImageTx: window chunk(image %u kind %u, %u/%u) skipped after %u failed transmit attempts\n",
                         entry.imageId,
                         static_cast<unsigned>(entry.windowKind),
                         static_cast<unsigned>(idx - entry.windowStart + 1),
                         static_cast<unsigned>(entry.windowCount),
                         static_cast<unsigned>(IMG_CHUNK_TX_RETRY_MAX));
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
            entry.state = ImageTxEntryState::SERVED;
        }
    }
    return ok;
}

// ===========================
// Eviction Policy (bounded memory, T-02-05)
// ===========================

void ImageTxManager::evictEntriesOlderThan(const ImageTxEntry& reference) {
    // G-01-7 lever 2 (01-12): the supersede path never evicts an entry whose
    // window is armed and mid-service — that entry's heal/pull chunks are in
    // flight on the link, and evicting it mid-stream is exactly the 01-11
    // session-2 failure (balloon2.log:656: image 2's pending thumbnail heal
    // evicted when image 3's window request arrived). Deferred entries age
    // into normal eviction once their window completes; the guard lives ONLY
    // here — evictionClassOf's overflow ranking and sweepExpiredEntries' TTL
    // path keep their full eviction rights (three pinned slots must never
    // wedge the IMG_TX_QUEUE_DEPTH 3 queue).
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used && entries[i].enqueueSeq < reference.enqueueSeq) {
            if (entries[i].windowArmed &&
                entries[i].windowNextIndex < entries[i].windowStart + entries[i].windowCount) {
                Serial.printf("ImageTx: supersede of image %u deferred - window mid-service\n",
                             entries[i].imageId);
                continue;   // never free an in-flight heal/pull window
            }
            Serial.printf("ImageTx: window request for image %u supersedes older entry image %u; evicted\n",
                         reference.imageId, entries[i].imageId);
            freeEntry(entries[i]);
        }
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

// A full transfer can only be armed when the owned buffer exists and its
// length is inside the IMG_MAX_IMAGE_SIZE cap (oversize entries park after
// their thumbnail push — the Q4 gate)
bool ImageTxManager::fullTransferArmable(const ImageTxEntry& entry) const {
    return entry.fullBuffer != nullptr && entry.fullLength > 0 &&
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
    entry.manifestAttempts = 0;   // CR-02/WR-01: a recycled slot starts with a clean manifest budget
    entry.thumbChunkFailStreak = 0;   // WR-08: a recycled slot starts with clean fail streaks
    entry.windowChunkFailStreak = 0;
    entry.fullWindowEverArmed = false;   // G-01-9 defect C: a recycled slot re-opens the re-announce gate
    entry.reannounceAttempts = 0;
    entry.lastWindowRequestMs = 0;   // G-01-7 lever 1 (01-21): a recycled slot starts with no receipt evidence
    entry.windowArmed = false;
    entry.windowKind = 0;
    entry.windowEverArmed = false;
    entry.windowStart = 0;
    entry.windowCount = 0;
    entry.windowNextIndex = 0;
    entry.windowArmedAtMs = 0;
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

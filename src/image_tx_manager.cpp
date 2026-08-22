#include "image_tx_manager.h"
#include "auto_capture.h"
#include "sensor_manager.h"
#include "power_manager.h"
#include "sensor_pins.h"
#include <esp_rom_crc.h>

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

bool ImageTxManager::begin(E32LoRa* lora) {
    if (!lora) {
        return false;
    }

    this->lora = lora;
    initialized = true;

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
//   3 ANNOUNCED (!everArmed)  queued, no window airtime invested
//   4 PUSH_THUMB_* / ANNOUNCE_FULL   push still in flight
//   5 ANNOUNCED (everArmed)   the ACTIVE-PULL context, armed or between
//                             windows — LAST resort (D-19)
static uint8_t evictionClassOf(const ImageTxEntry& entry) {
    switch (entry.state) {
        case ImageTxEntryState::THUMB_PUSHED:
            return 1;
        case ImageTxEntryState::SERVED:
            return 2;
        case ImageTxEntryState::ANNOUNCED:
            return entry.windowEverArmed ? 5 : 3;
        case ImageTxEntryState::PUSH_THUMB_MANIFEST:
        case ImageTxEntryState::PUSH_THUMB_CHUNKS:
        case ImageTxEntryState::ANNOUNCE_FULL:
        default:
            return 4;
    }
}

void ImageTxManager::enqueueCapture(uint16_t imageId) {
    ImageData img = Camera().getCurrentImage();
    if (!img.valid || img.buffer == nullptr) {
        if (DEBUG_IMAGE_TX) {
            Serial.printf("ImageTx: image %u has no valid full buffer; nothing to enqueue\n", imageId);
        }
        return;
    }

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
            if (DEBUG_IMAGE_TX) {
                Serial.printf("ImageTx: PSRAM allocation failed for image %u full buffer (%u bytes); dropped\n",
                             imageId, static_cast<unsigned>(img.length));
            }
            return;
        }
        memcpy(entry.fullBuffer, img.buffer, img.length);
        entry.fullLength = img.length;
        entry.fullCrc32 = esp_rom_crc32_le(0, entry.fullBuffer, entry.fullLength);
        entry.fullTotalChunks = chunksForSize(entry.fullLength);
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
        static constexpr uint8_t EVICT_CLASS_COUNT = 5;
        ImageTxEntry* victim = nullptr;
        uint8_t victimClass = 0;
        for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
            if (!entries[i].used) {
                continue;
            }
            uint8_t cls = evictionClassOf(entries[i]);
            if (victim == nullptr || cls < victimClass ||
                (cls == victimClass && entries[i].enqueueSeq < victim->enqueueSeq)) {
                victim = &entries[i];
                victimClass = cls;
            }
        }
        // slot == nullptr implies every entry is used, so victim is guaranteed
        const char* className =
            victimClass == 1 ? "parked THUMB_PUSHED" :
            victimClass == 2 ? "SERVED (heal-only)" :
            victimClass == 3 ? "queued, no airtime invested" :
            victimClass == 4 ? "push in flight" :
                               "ACTIVE-PULL context (last resort)";
        Serial.printf("ImageTx: queue overflow (depth %u); evicting class %u (%s) entry image %u for image %u\n",
                     static_cast<unsigned>(IMG_TX_QUEUE_DEPTH), victimClass, className,
                     victim->imageId, imageId);
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

    entry.nextThumbChunk = 0;
    entry.state = ImageTxEntryState::PUSH_THUMB_CHUNKS;
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

    ImageChunkPacket pkt = createChunkPacket(entry.imageId, entry.nextThumbChunk,
                                             entry.thumbBuffer + offset, chunkLen);

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: chunk(image %u, %u/%u, %u B) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(entry.nextThumbChunk + 1),
                     static_cast<unsigned>(entry.thumbTotalChunks),
                     chunkLen,
                     ok ? "sent" : "FAILED");
    }

    entry.nextThumbChunk++;
    entry.lastActivityMs = millis();
    if (entry.nextThumbChunk >= entry.thumbTotalChunks) {
        entry.state = completedThumbState(entry);
        // Thumbnail holes from failed transmits are healed by the SAME
        // windowed-pull re-request path (D-22), wired in 02-02/02-03
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

bool ImageTxManager::announceFullManifest(ImageTxEntry& entry) {
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

    // Emitted exactly ONCE per entry: from ANNOUNCED on, chunks flow only
    // through a window context armed by handleWindowRequest
    entry.state = ImageTxEntryState::ANNOUNCED;
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
        // entry may still be the active pull's target mid-stream
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
    target->windowStart = startChunk;
    target->windowCount = count;
    target->windowNextIndex = startChunk;
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

    ImageChunkPacket pkt = createChunkPacket(entry.imageId, idx,
                                             source + offset, chunkLen);

    uint8_t buffer[CMD_MAX_PACKET_SIZE];
    size_t length = 0;
    bool ok = CommandProtocol::serializeChunk(pkt, buffer, length) && lora->transmit(buffer, length);

    if (DEBUG_IMAGE_TX) {
        Serial.printf("ImageTx: window chunk(image %u, %u/%u, %u B) %s\n",
                     entry.imageId,
                     static_cast<unsigned>(idx - entry.windowStart + 1),
                     static_cast<unsigned>(entry.windowCount),
                     chunkLen,
                     ok ? "sent" : "FAILED");
    }

    entry.windowNextIndex++;
    entry.lastActivityMs = millis();
    if (entry.windowNextIndex >= entry.windowStart + entry.windowCount) {
        // Window complete: clear the armed context and await the next request
        entry.windowArmed = false;
        // Completion marker (02-05 / CR-03 fix c): a FULL window whose clamped
        // span reached the image tail (windowStart + windowCount ==
        // fullTotalChunks) has offered the base every full chunk at least
        // once — mark SERVED (preferred eviction candidate; buffers KEPT so
        // a tail re-request can still heal). THUMBNAIL windows never change
        // entry state.
        if (entry.windowKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE) &&
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
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (entries[i].used && entries[i].enqueueSeq < reference.enqueueSeq) {
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
    entry.windowArmed = false;
    entry.windowKind = 0;
    entry.windowEverArmed = false;
    entry.windowStart = 0;
    entry.windowCount = 0;
    entry.windowNextIndex = 0;
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

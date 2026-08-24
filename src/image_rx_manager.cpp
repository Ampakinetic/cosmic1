#include "image_rx_manager.h"
#include <esp_rom_crc.h>
#include "base_station_config.h"   // MAX_IMAGE_SIZE — allocation bound (Pitfall 11)
#include "command_sender.h"        // window requests ride the Phase 1 machinery
#include "sd_storage.h"            // stream-to-SD persistence (Pattern 5)

// WR-06: the balloon's arm gate (IMG_MAX_IMAGE_SIZE, image_protocol.h) and
// the base's manifest-validation bound (MAX_IMAGE_SIZE,
// base_station_config.h) are a PAIRED constant — the two must stay equal or
// the balloon would announce manifests the base rejects. Both definitions
// are visible here via the includes above; tie them at compile time so a
// one-sided edit fails the build instead of silently reintroducing the
// rejected-manifest failure mode.
static_assert(MAX_IMAGE_SIZE == static_cast<int32_t>(IMG_MAX_IMAGE_SIZE),
              "base manifest bound (MAX_IMAGE_SIZE) must match balloon arm cap (IMG_MAX_IMAGE_SIZE)");

// Debug configuration
#ifndef DEBUG_IMAGE_RX
#define DEBUG_IMAGE_RX true
#endif

// ===========================
// Static Instance
// ===========================

static ImageRxManager imageRxInstance;
ImageRxManager& ImageRx() {
    return imageRxInstance;
}

// ===========================
// Locked State Vocabulary (D-20)
// ===========================

// The SINGLE state-to-string mapping every consumer shares (/status JSON,
// the transfer panel). The UI never invents or defaults a state — it only
// maps these strings to presentation (chip label/color).
const char* transferStateToString(TransferDisplayState state) {
    switch (state) {
        case TransferDisplayState::QUEUED:     return "QUEUED";
        case TransferDisplayState::RECEIVING:  return "RECEIVING";
        case TransferDisplayState::RETRYING:   return "RETRYING";
        case TransferDisplayState::COMPLETE:   return "COMPLETE";
        case TransferDisplayState::INCOMPLETE: return "INCOMPLETE";
        default:                               return "INCOMPLETE";
    }
}

// ===========================
// Constructor/Destructor
// ===========================

ImageRxManager::ImageRxManager()
    : initialized(false)
    , nextArrivalSeq(0)
    , healHoldLoggedId(0)
    , latestThumbId(0)
    , latestThumbBuffer(nullptr)
    , latestThumbLength(0)
{
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        transfers[i] = ImageRxTransfer{};
    }
    memset(&telemetry, 0, sizeof(telemetry));
}

ImageRxManager::~ImageRxManager() {
    end();
}

// ===========================
// Initialization
// ===========================

bool ImageRxManager::begin() {
    initialized = true;

    if (DEBUG_IMAGE_RX) {
        Serial.println("ImageRx: Initialized");
    }

    return true;
}

void ImageRxManager::end() {
    initialized = false;
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        releaseSlotWork(transfers[i]);
        transfers[i] = ImageRxTransfer{};
    }
    freeLatest();
}

// ===========================
// Main Processing (window driver + stall fallback)
// ===========================

void ImageRxManager::process() {
    if (!initialized) {
        return;
    }

    // D-21: drive the ONE active full-image pull. The base speaks only on
    // window completion, stall, or manifest-while-idle (Pitfall 5) — every
    // issueWindowRequest call below sits on one of those three triggers.
    ImageRxTransfer* pull = findActivePull();
    if (pull != nullptr) {
        // Safety net: completion normally finalizes inline in acceptChunk
        if (pull->receivedCount == pull->totalChunks) {
            finalizeTransfer(*pull);
        } else if (pull->windowActive && windowSliceComplete(*pull)) {
            // Window completed. G-01-7 lever 3 (01-12): RX-settle gate — the
            // next window request waits out IMG_WINDOW_RX_SETTLE_MS after the
            // last accepted chunk. The 01-11 session-2 discriminator (five
            // immediate tail re-requests seq 53/55-59 for the 13..14/14..14
            // windows, 6 END MARKER MISS, base2.log:415-498) named the
            // half-duplex immediate-retransmit turnaround-collision class;
            // the settle gap breaks the collision. The check is NESTED in
            // this branch, so during the settle window no pass is charged and
            // the stall branch below cannot fire (settle 500 << stall 8000).
            if ((millis() - pull->lastProgressMs) < IMG_WINDOW_RX_SETTLE_MS) {
                return;   // settle holds this pass — no request, no stall path
            }
            // Advance to the next missing span. All chunks before the span
            // are present by construction (windows complete before
            // advancing), so the span starts at the global first miss.
            uint16_t fm = firstMissingChunk(*pull, 0);
            if (fm >= pull->totalChunks) {
                finalizeTransfer(*pull);
            } else {
                uint16_t count = pull->totalChunks - fm;
                if (count > IMG_WINDOW_MAX_CHUNKS) {
                    count = IMG_WINDOW_MAX_CHUNKS;
                }
                issueWindowRequest(*pull, fm, count);
            }
        } else if (millis() - pull->lastProgressMs > IMG_WINDOW_STALL_MS) {
            // Stalled stream. windowActive == false means the previous issue
            // never queued (command table full) — this is the FIRST real
            // attempt for the span, so no pass is charged; otherwise the
            // balloon failed to deliver and a retransmit pass begins.
            if (pull->windowActive && pull->passCount >= IMG_RETRANSMIT_MAX_PASSES) {
                // D-24: bounded passes exhausted — finalize incomplete, keep
                // what is on SD, free the slot for the next queued pull
                finalizeIncomplete(*pull, "retransmit passes exhausted");
            } else {
                if (pull->windowActive) {
                    pull->passCount++;
                }
                // Re-request ONLY the missing region of the current scope.
                // The wire format names ONE contiguous span, so the span runs
                // first-missing..last-missing inside the scope; already-
                // present indices inside the span are re-sent by the balloon
                // and dropped idempotently by the bitmap.
                uint16_t scopeFrom = pull->windowActive ? pull->windowBase : 0;
                uint16_t scopeTo = pull->totalChunks;
                if (pull->windowActive &&
                    static_cast<uint32_t>(pull->windowBase) + pull->windowCount < scopeTo) {
                    scopeTo = pull->windowBase + pull->windowCount;
                }
                uint16_t fm = firstMissingChunk(*pull, scopeFrom);
                uint16_t lm = lastMissingChunk(*pull, fm, scopeTo);
                uint16_t count = lm - fm + 1;
                if (count > IMG_WINDOW_MAX_CHUNKS) {
                    count = IMG_WINDOW_MAX_CHUNKS;
                }
                issueWindowRequest(*pull, fm, count);
            }
        }
    } else {
        // FIFO advance (D-19): no active pull — activate the earliest
        // manifest-arrival queued full (issues its first window request =
        // the manifest-while-idle trigger). G-01-7 lever 1 EXCEPTION
        // (01-12): activation is HELD while a non-terminal holed thumbnail
        // with a same-id FULL slot exists (pendingHealThumbnail) — the
        // thumbnail's heal completes first; the hold is priority ordering
        // between two existing D-21 triggers, not a new one, and it is
        // bounded by the D-24 3-pass finalization.
        activateNextPull();
    }

    // D-22: thumbnail hole fallback — a pushed thumbnail with holes runs the
    // SAME stall/re-request path (a thumbnail is just another pullable
    // image; no parallel best-effort mechanism exists)
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (!t.used || t.terminal ||
            t.imageKind != static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
            continue;
        }
        if (t.receivedCount == t.totalChunks) {
            finalizeTransfer(t);   // safety net
            continue;
        }
        if (millis() - t.lastProgressMs <= IMG_WINDOW_STALL_MS) {
            continue;
        }

        // Gated heal (02-05 / CR-01 base half): the base speaks only on the
        // D-21 triggers, so a thumbnail heal NEVER fires while a full pull
        // owns the link, and only once the balloon's thumbnail push for this
        // id provably finished — a same-id FULL manifest's arrival is that
        // proof (the push always precedes the announcement). Oversize
        // fallback: an oversize image never announces a full, so after
        // IMG_THUMB_HEAL_IDLE_MS with no progress the push is taken as
        // drained and the heal may fire anyway; the D-24 3-pass bound then
        // resolves the row honestly instead of an infinite RECEIVING stall.
        if (findActivePull() != nullptr) {
            continue;   // gate 1: a full pull owns the link
        }
        if (findTransfer(t.imageId, static_cast<uint8_t>(ImageKind::FULL_IMAGE)) == nullptr &&
            (millis() - t.lastProgressMs) <= IMG_THUMB_HEAL_IDLE_MS) {
            continue;   // gate 2: push not provably finished; oversize idle fallback pending
        }

        if (t.windowActive && t.passCount >= IMG_RETRANSMIT_MAX_PASSES) {
            finalizeIncomplete(t, "thumbnail push stalled; passes exhausted");
            continue;
        }
        if (t.windowActive) {
            t.passCount++;
        }
        // Scope: the whole thumbnail from its first hole (capped at one
        // window per request — the balloon arms a single span)
        uint16_t fm = firstMissingChunk(t, 0);
        if (fm >= t.totalChunks) {
            finalizeTransfer(t);
            continue;
        }
        uint16_t count = t.totalChunks - fm;
        if (count > IMG_WINDOW_MAX_CHUNKS) {
            count = IMG_WINDOW_MAX_CHUNKS;
        }
        issueWindowRequest(t, fm, count);
    }
}

// ===========================
// Frame Entry Points
// ===========================

void ImageRxManager::onManifestFrame(const uint8_t* frame, size_t length) {
    ImageManifestPacket pkt;
    if (!CommandProtocol::deserializeManifest(frame, length, pkt)) {
        if (DEBUG_IMAGE_RX) {
            Serial.println("ImageRx: malformed manifest frame ignored");
        }
        return;
    }
    startTransfer(pkt.body);
}

void ImageRxManager::onChunkFrame(const uint8_t* frame, size_t length) {
    ImageChunkPacket pkt;
    if (!CommandProtocol::deserializeChunk(frame, length, pkt)) {
        if (DEBUG_IMAGE_RX) {
            Serial.println("ImageRx: malformed chunk frame ignored");
        }
        return;
    }

    const ImageChunkBody& c = pkt.body;

    // Routing by (imageId, kind). Heal-window precedence (02-05 / Gap 2):
    // while a THUMBNAIL slot for this id is non-terminal with windowActive
    // set (a heal window is in flight), its chunks route to the THUMBNAIL
    // slot FIRST — the heal is never issued while a full pull is active
    // (gate 1 above) and routing is per-image-id, so the armed heal window
    // unambiguously owns this id's incoming chunks. The chunk body carries
    // no kind byte — the requester's own window state is the discriminator.
    // Without this, heal bytes would land in a QUEUED full's SD file and
    // corrupt it.
    ImageRxTransfer* t = findTransfer(c.imageId, static_cast<uint8_t>(ImageKind::THUMBNAIL));
    if (!(t != nullptr && !t->terminal && t->windowActive)) {
        // Wire-order precedence (unchanged): thumbnail chunks are only ever
        // sent BEFORE the full manifest of the same id (the push completes
        // before the announcement), and after it every chunk on the wire for
        // that id is a window-pull answer. So a non-terminal FULL slot takes
        // precedence; otherwise the chunk belongs to the thumbnail push.
        t = findTransfer(c.imageId, static_cast<uint8_t>(ImageKind::FULL_IMAGE));
        if (t == nullptr || t->terminal) {
            t = findTransfer(c.imageId, static_cast<uint8_t>(ImageKind::THUMBNAIL));
        }
    }
    if (t == nullptr) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk for image %u with no matching manifest ignored\n",
                          c.imageId);
        }
        return;
    }
    if (t->terminal) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk for finalized image %u kind %u ignored\n",
                          c.imageId, t->imageKind);
        }
        return;
    }

    acceptChunk(*t, c);
}

void ImageRxManager::onTelemetryBeaconFrame(const uint8_t* frame, size_t length) {
    TelemetryBeaconPacket pkt;
    if (!CommandProtocol::deserializeTelemetryBeacon(frame, length, pkt)) {
        if (DEBUG_IMAGE_RX) {
            Serial.println("ImageRx: malformed telemetry beacon ignored");
        }
        return;
    }

    const TelemetryBeaconBody& b = pkt.body;
    telemetry.valid = true;
    telemetry.receivedMs = millis();
    telemetry.seq = b.seq;
    // Debug session balloon-no-data-oled-blank (link half): proof the
    // balloon->base beacon path accepted a CRC-verified frame end-to-end
    Serial0.printf("[BCNRX] seq=%u accepted\n", (unsigned)b.seq);
    telemetry.altitudeM = static_cast<float>(b.altitudeCm) / 100.0f;
    telemetry.tempC = static_cast<float>(b.tempCentiC) / 100.0f;
    telemetry.lat = static_cast<float>(b.latE6) / 1000000.0f;
    telemetry.lon = static_cast<float>(b.lonE6) / 1000000.0f;
    telemetry.gpsValid = (b.flags & 0x01) != 0;
    telemetry.batteryMv = b.batteryMilliV;
    telemetry.batteryValid = (b.flags & 0x02) != 0;

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: beacon seq=%u alt=%.1fm temp=%.1fC gps=%s batt=%s\n",
                      telemetry.seq,
                      telemetry.altitudeM,
                      telemetry.tempC,
                      telemetry.gpsValid ? "valid" : "no-fix",
                      telemetry.batteryValid ? "valid" : "n/a");
    }
}

// ===========================
// Slot Handling
// ===========================

ImageRxTransfer* ImageRxManager::findTransfer(uint16_t imageId, uint8_t kind) {
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (t.used && t.imageId == imageId && t.imageKind == kind) {
            return &t;
        }
    }
    return nullptr;
}

ImageRxTransfer* ImageRxManager::findActivePull() {
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (t.used && !t.terminal && t.pullActive) {
            return &t;
        }
    }
    return nullptr;
}

ImageRxTransfer* ImageRxManager::pendingHealThumbnail() {
    // G-01-7 lever 1 (01-12): the earliest-by-arrival non-terminal THUMBNAIL
    // slot with holes AND a same-id FULL slot. The FULL slot's existence is
    // the heal loop's gate-2 proof (manifests arrive post-push), so this
    // condition can never hold while a push is still streaming — no separate
    // push-state tracking is needed. While a slot is returned, full-pull
    // activation is HELD: the thumbnail's heal (the existing stall-driven
    // D-22 path) completes first, then the FIFO advances.
    ImageRxTransfer* best = nullptr;
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (!t.used || t.terminal ||
            t.imageKind != static_cast<uint8_t>(ImageKind::THUMBNAIL) ||
            t.receivedCount >= t.totalChunks) {
            continue;
        }
        if (findTransfer(t.imageId, static_cast<uint8_t>(ImageKind::FULL_IMAGE)) == nullptr) {
            continue;   // push not provably finished — no FULL manifest yet
        }
        if (best == nullptr || t.arrivalSeq < best->arrivalSeq) {
            best = &t;
        }
    }
    return best;
}

ImageRxTransfer* ImageRxManager::allocateSlot(uint8_t kind) {
    // 1. A free slot
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        if (!transfers[i].used) {
            return &transfers[i];
        }
    }

    // 2. Recycle the OLDEST terminal slot — finished history yields to live
    //    work (the D-20 rows it fed are gone; its files stay on SD)
    ImageRxTransfer* oldestTerminal = nullptr;
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (t.used && t.terminal &&
            (oldestTerminal == nullptr || t.arrivalSeq < oldestTerminal->arrivalSeq)) {
            oldestTerminal = &t;
        }
    }
    if (oldestTerminal != nullptr) {
        Serial.printf("ImageRx: slot pressure — recycling terminal row image %u kind %u\n",
                      oldestTerminal->imageId, oldestTerminal->imageKind);
        releaseSlotWork(*oldestTerminal);
        *oldestTerminal = ImageRxTransfer{};
        return oldestTerminal;
    }

    // 3. Evict the oldest non-terminal slot that is NOT the active pull —
    //    finalize it incomplete (D-24-style bound under slot pressure,
    //    logged, never silent). Evicting the in-flight pull mid-stream is
    //    never an option.
    ImageRxTransfer* oldest = nullptr;
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (t.used && !t.terminal && !t.pullActive &&
            (oldest == nullptr || t.arrivalSeq < oldest->arrivalSeq)) {
            oldest = &t;
        }
    }
    if (oldest != nullptr) {
        Serial.printf("ImageRx: slot pressure — evicting pending transfer image %u kind %u "
                      "(finalized incomplete)\n",
                      oldest->imageId, oldest->imageKind);
        finalizeIncomplete(*oldest, "slot pressure");
        // Full slot re-init (02-05 / CR-02, Gap 3): clears the stale terminal flag and
        // counters exactly as step 2 does — next occupant starts at 0/N, never zombie.
        *oldest = ImageRxTransfer{};
        return oldest;
    }

    // 4. Every slot is the active pull or otherwise un-evictable — reject
    Serial.println("ImageRx: no transfer slot available; manifest rejected");
    return nullptr;
}

void ImageRxManager::releaseSlotWork(ImageRxTransfer& t) {
    if (t.buffer != nullptr) {
        // The retained latest thumbnail owns its buffer (ownership moved at
        // finalize) — terminal slots carry buffer == nullptr, so a non-null
        // buffer here is always working memory safe to free
        free(t.buffer);
        t.buffer = nullptr;
    }
    if (t.chunkPresent != nullptr) {
        free(t.chunkPresent);
        t.chunkPresent = nullptr;
    }
}

// ===========================
// Transfer Handling
// ===========================

void ImageRxManager::startTransfer(const ImageManifestBody& m) {
    // Pitfall 11: validate EVERYTHING before allocating a single byte
    // (T-02-07: a pathological manifest cannot pre-allocate or seek past
    // bounds — sizes, counts, and indices are all bounded here and in
    // acceptChunk, and the D-24 pass bound keeps a loss-flooding sender from
    // holding a slot forever)
    if (m.totalSize == 0 || m.totalSize > MAX_IMAGE_SIZE) {
        Serial.printf("ImageRx: manifest image %u totalSize %u out of bounds (1..%d); rejected\n",
                      m.imageId, static_cast<unsigned>(m.totalSize), MAX_IMAGE_SIZE);
        return;
    }
    if (m.chunkSize == 0 || m.chunkSize > IMG_CHUNK_PAYLOAD_SIZE) {
        Serial.printf("ImageRx: manifest image %u chunkSize %u out of bounds (1..%d); rejected\n",
                      m.imageId, m.chunkSize, static_cast<int>(IMG_CHUNK_PAYLOAD_SIZE));
        return;
    }
    uint32_t expectedChunks = (m.totalSize + m.chunkSize - 1) / m.chunkSize;
    if (m.totalChunks < 1 || m.totalChunks != expectedChunks) {
        Serial.printf("ImageRx: manifest image %u totalChunks %u != ceil(%u/%u)=%u; rejected\n",
                      m.imageId, m.totalChunks,
                      static_cast<unsigned>(m.totalSize),
                      m.chunkSize,
                      static_cast<unsigned>(expectedChunks));
        return;
    }
    if (m.imageKind != static_cast<uint8_t>(ImageKind::THUMBNAIL)
            && m.imageKind != static_cast<uint8_t>(ImageKind::FULL_IMAGE)) {
        Serial.printf("ImageRx: manifest image %u unknown kind %u; rejected\n",
                      m.imageId, m.imageKind);
        return;
    }

    // Same (id, kind) manifest again: restart that slot's reassembly (02-01
    // semantics — a same-id re-push restarts cleanly; D-19 keeps OTHER
    // transfers running: a new capture never abandons an in-progress pull)
    ImageRxTransfer* t = findTransfer(m.imageId, m.imageKind);
    if (t == nullptr) {
        t = allocateSlot(m.imageKind);
        if (t == nullptr) {
            return; // logged inside allocateSlot
        }
    } else {
        Serial.printf("ImageRx: re-manifest for image %u kind %u — restarting reassembly "
                      "(had %u/%u chunks)\n",
                      m.imageId, m.imageKind,
                      static_cast<unsigned>(t->receivedCount),
                      static_cast<unsigned>(t->totalChunks));
        releaseSlotWork(*t);
        bool wasPull = t->pullActive;
        *t = ImageRxTransfer{};
        t->pullActive = wasPull;   // an active pull re-arms on the same slot
    }

    t->used = true;
    t->arrivalSeq = nextArrivalSeq++;
    t->imageId = m.imageId;
    t->imageKind = m.imageKind;
    t->captureSource = m.captureSource;
    t->totalSize = m.totalSize;
    t->chunkSize = m.chunkSize;
    t->totalChunks = m.totalChunks;
    t->crc32 = m.crc32;   // D-23: CRC over THIS kind's payload bytes
    t->captureTimeMs = m.captureTimeMs;
    t->resolution = m.resolution;
    t->quality = m.quality;
    t->brightness = m.brightness;
    t->contrast = m.contrast;
    t->saturation = m.saturation;
    t->exposure = m.exposure;
    t->wbMode = m.wbMode;
    t->lastProgressMs = millis();

    t->chunkPresent = (uint8_t*)calloc(m.totalChunks, 1);
    if (t->chunkPresent == nullptr) {
        Serial.printf("ImageRx: bitmap allocation failed for image %u (%u flags); rejected\n",
                      m.imageId, m.totalChunks);
        *t = ImageRxTransfer{};
        return;
    }

    // Fulls stream straight to SD (Pattern 5 — no whole-image RAM buffer on
    // this PSRAM-less build; the untrusted totalSize is never allocated).
    // Thumbnails additionally reassemble in RAM for the 02-01 retention
    // path (serving robustness when SD is degraded).
    if (m.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
        t->buffer = (uint8_t*)malloc(m.totalSize);
        if (t->buffer == nullptr) {
            Serial.printf("ImageRx: buffer allocation failed for image %u (%u B); rejected\n",
                          m.imageId, static_cast<unsigned>(m.totalSize));
            releaseSlotWork(*t);
            *t = ImageRxTransfer{};
            return;
        }
    }

    // SD transfer file (degrades internally when storage is unavailable —
    // accounting continues, nothing persisted). CR-01: THUMBNAILs open here
    // (their push chunks follow the manifest on the wire immediately), but a
    // FULL opens LAZILY — only when it actually becomes the active pull
    // (activateNextPull / the manifest-while-idle branch below). Opening a
    // QUEUED full here would steal the active pull's single kind handle:
    // openTransfer closes the previous partial and writeChunk would then
    // drop every subsequent chunk of the active pull — 100% reception
    // finalizing INCOMPLETE. A re-manifested ACTIVE pull still reopens
    // (truncate) here so the restarted reassembly starts from a file
    // consistent with its cleared bitmap.
    if (m.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL)
            || t->pullActive) {
        SDStorage().openTransfer(m.imageId, m.imageKind, m.totalSize);
    }

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: manifest image %u kind %u (%u B, %u chunks of %u, CRC %08X)\n",
                      m.imageId, m.imageKind,
                      static_cast<unsigned>(m.totalSize),
                      m.totalChunks, m.chunkSize,
                      static_cast<unsigned int>(m.crc32));
    }

    if (m.imageKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE) && !t->pullActive) {
        // QUEUED (D-19 FIFO). If no pull is active this manifest arrived
        // while idle — the one trigger that starts a pull immediately.
        // G-01-7 lever 1 (01-12): the trigger ALSO requires no pending
        // thumbnail heal (serialization hold) — a same-id or earlier holed
        // thumbnail completes before any full pull activates.
        if (findActivePull() == nullptr && pendingHealThumbnail() == nullptr) {
            t->pullActive = true;
            // CR-01: the SD file opens HERE, at activation — a QUEUED
            // manifest must never touch the kind handle an active pull owns
            // (see the openTransfer note in the manifest handling above)
            SDStorage().openTransfer(m.imageId, m.imageKind, m.totalSize);
            uint16_t fm = firstMissingChunk(*t, 0);
            uint16_t count = t->totalChunks - fm;
            if (count > IMG_WINDOW_MAX_CHUNKS) {
                count = IMG_WINDOW_MAX_CHUNKS;
            }
            issueWindowRequest(*t, fm, count);
        }
    }
    // THUMBNAIL: the push stream follows the manifest on the wire; holes
    // heal through the stall path above (D-22 — same machinery)
}

bool ImageRxManager::acceptChunk(ImageRxTransfer& t, const ImageChunkBody& c) {
    if (c.chunkIndex >= t.totalChunks) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk index %u out of range (%u total); ignored\n",
                          c.chunkIndex,
                          static_cast<unsigned>(t.totalChunks));
        }
        return false;
    }

    // The chunk must carry exactly the bytes its slot spans — full
    // chunkSize, or the remainder for the final partial chunk
    size_t offset = static_cast<size_t>(c.chunkIndex) * t.chunkSize;
    size_t expectedLen = t.totalSize - offset;
    if (expectedLen > t.chunkSize) {
        expectedLen = t.chunkSize;
    }
    if (c.dataLen != expectedLen) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk %u of image %u carries %u B, expected %u; ignored\n",
                          c.chunkIndex, t.imageId, c.dataLen,
                          static_cast<unsigned>(expectedLen));
        }
        return false;
    }

    if (t.chunkPresent[c.chunkIndex]) {
        // Duplicate — retransmissions can double-deliver; drop idempotently
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: duplicate chunk %u of image %u dropped\n",
                          c.chunkIndex, t.imageId);
        }
        return false;
    }

    // Commit: RAM (thumbnails only) + SD stream (Pattern 5: seek to the
    // chunk's fixed offset — out-of-order arrival lands correctly)
    if (t.buffer != nullptr) {
        memcpy(t.buffer + offset, c.data, c.dataLen);
    }
    SDStorage().writeChunk(t.imageId, t.imageKind, c.chunkIndex, t.chunkSize,
                           c.data, c.dataLen);
    t.chunkPresent[c.chunkIndex] = 1;
    t.receivedCount++;
    t.bytesReceived += c.dataLen;
    t.lastProgressMs = millis();
    // D-24 bounds only CONSECUTIVE unhealed stalls (02-05 / CR-03 fix b):
    // accepted-chunk progress retires charged passes — a healed stall is not
    // a failed pass, so cumulative healed stalls over a pull's lifetime can
    // no longer finalize it INCOMPLETE. This is the ONLY passCount zero-writer.
    t.passCount = 0;

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: image %u kind %u chunk %u/%u (%u B)\n",
                      t.imageId, t.imageKind,
                      static_cast<unsigned>(t.receivedCount),
                      static_cast<unsigned>(t.totalChunks),
                      c.dataLen);
    }

    if (t.receivedCount == t.totalChunks) {
        finalizeTransfer(t);
    }
    return true;
}

// ===========================
// Finalization
// ===========================

void ImageRxManager::finalizeTransfer(ImageRxTransfer& t) {
    // CR-02: flush this id's buffered writes BEFORE the stored-bytes
    // read-back below — verifyStoredCrc32 reads through a SECOND file
    // handle, and the write handle's stdio buffer may still hold the final
    // chunk's bytes (an unflushed short read would fail verification and
    // finalize a fully-received image INCOMPLETE).
    SDStorage().flushTransfer(t.imageId, t.imageKind);

    // D-23: an image is COMPLETE only after the end-to-end CRC32 over its
    // bytes matches the manifest. Thumbnail bytes live in RAM (the 02-01
    // reassembly); full-image bytes live only on SD, so their CRC is
    // computed by reading the stored file back (chunks arrived out of
    // order, so the CRC cannot stream; the read-back is bounded by
    // MAX_IMAGE_SIZE).
    bool complete = false;
    bool crcMismatch = false;
    bool notStored = false;

    if (t.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL) && t.buffer != nullptr) {
        uint32_t crc = esp_rom_crc32_le(0, t.buffer, t.totalSize);
        complete = (crc == t.crc32);
        crcMismatch = !complete;
        if (!complete) {
            Serial.printf("ImageRx: image %u CRC mismatch (got %08X, manifest says %08X)\n",
                          t.imageId,
                          static_cast<unsigned int>(crc),
                          static_cast<unsigned int>(t.crc32));
        }
    } else {
        uint32_t storedCrc = 0;
        if (verifyStoredCrc32(t, storedCrc)) {
            complete = (storedCrc == t.crc32);
            crcMismatch = !complete;
            if (!complete) {
                Serial.printf("ImageRx: image %u stored-bytes CRC mismatch (got %08X, manifest %08X)\n",
                              t.imageId,
                              static_cast<unsigned int>(storedCrc),
                              static_cast<unsigned int>(t.crc32));
            }
        } else {
            // SD degraded/absent or the stored file is short: the bytes
            // cannot be verified, so the image is NOT complete — never a
            // fabricated success (a fully-received full with no storage
            // still reports INCOMPLETE)
            notStored = true;
        }
    }

    t.terminal = true;
    t.complete = complete;
    t.crcMismatch = crcMismatch;
    t.notStored = notStored;

    Serial.printf("ImageRx: image %u kind %u finalized %s (%u/%u chunks, %u B)%s\n",
                  t.imageId, t.imageKind,
                  complete ? "COMPLETE" : "INCOMPLETE",
                  static_cast<unsigned>(t.receivedCount),
                  static_cast<unsigned>(t.totalChunks),
                  static_cast<unsigned>(t.bytesReceived),
                  notStored ? " — not stored (SD degraded)" : "");

    // Sidecar ONCE at finalization with honest flags (Pitfall 8)
    writeSidecarFor(t, complete, crcMismatch, notStored);

    // Thumbnail retention: only a CRC-VERIFIED thumbnail replaces the
    // latest — ownership moves into the latest* slot (02-01 discipline)
    if (complete && t.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL)
            && t.buffer != nullptr) {
        freeLatest();
        latestThumbId = t.imageId;
        latestThumbBuffer = t.buffer;
        latestThumbLength = t.totalSize;
        t.buffer = nullptr;
    }

    releaseSlotWork(t);

    if (t.pullActive) {
        t.pullActive = false;
        activateNextPull();   // FIFO: the next queued pull starts now
    }
}

void ImageRxManager::finalizeIncomplete(ImageRxTransfer& t, const char* reason) {
    // D-24: bounded degradation — keep what is on SD, flag it in the
    // sidecar, free the transfer slot for the next queued pull. Never
    // loops forever (PRI-03).
    t.terminal = true;
    t.complete = false;
    t.crcMismatch = false;
    t.notStored = false;

    Serial.printf("ImageRx: image %u kind %u finalized INCOMPLETE (%s): %u/%u chunks after %u passes\n",
                  t.imageId, t.imageKind, reason,
                  static_cast<unsigned>(t.receivedCount),
                  static_cast<unsigned>(t.totalChunks),
                  t.passCount);

    writeSidecarFor(t, false, false, false);
    releaseSlotWork(t);

    if (t.pullActive) {
        t.pullActive = false;
        activateNextPull();
    }
}

void ImageRxManager::writeSidecarFor(const ImageRxTransfer& t, bool complete,
                                     bool crcMismatch, bool notStored) {
    SdImageMetadata meta{};
    meta.imageId = t.imageId;
    meta.kind = t.imageKind;
    meta.captureSource = t.captureSource;
    meta.captureTimeMs = t.captureTimeMs;
    meta.receiptTimeMs = millis();

    // Latest beacon at finalize (D-30); invalid-flagged, never fabricated
    meta.telemetryValid = telemetry.valid;
    meta.altitudeM = telemetry.altitudeM;
    meta.lat = telemetry.lat;
    meta.lon = telemetry.lon;

    meta.resolution = t.resolution;
    meta.quality = t.quality;
    meta.brightness = t.brightness;
    meta.contrast = t.contrast;
    meta.saturation = t.saturation;
    meta.exposure = t.exposure;
    meta.wbMode = t.wbMode;

    meta.chunksReceived = t.receivedCount;
    meta.chunksTotal = t.totalChunks;
    meta.bytesReceived = t.bytesReceived;
    meta.complete = complete;
    meta.crcMismatch = crcMismatch || notStored;   // sidecar note for either cause
    (void)notStored;

    SDStorage().finalizeImage(meta);   // storedToSd computed inside (truth)
}

// ===========================
// Window Driver (D-21)
// ===========================

void ImageRxManager::activateNextPull() {
    // G-01-7 lever 1 (01-12) — serialization hold: while a non-terminal holed
    // thumbnail whose same-id FULL manifest already arrived exists, full-pull
    // activation is HELD so the thumbnail completes (push + heal, D-22 single
    // path) before its full pull starts. This covers the FIFO branch and both
    // post-finalize advances. The hold reorders priority between two existing
    // D-21 triggers — the heal loop below needs no change: with activation
    // held there is no active pull, its gate 1 passes, and the existing
    // stall-driven heal fires on its own clock. Bounded: the D-24 3-pass
    // finalization makes a stuck thumbnail terminal, releasing the hold.
    ImageRxTransfer* held = pendingHealThumbnail();
    if (held != nullptr) {
        if (held->imageId != healHoldLoggedId) {
            Serial.printf("ImageRx: full-pull activation held - thumbnail heal pending for image %u\n",
                          held->imageId);
            healHoldLoggedId = held->imageId;
        }
        return;   // hold — no full pull activates while the heal is pending
    }

    // FIFO by manifest arrival order (D-19 base side): the earliest queued
    // full becomes the active pull and issues its first window request
    ImageRxTransfer* best = nullptr;
    for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
        ImageRxTransfer& t = transfers[i];
        if (t.used && !t.terminal && !t.pullActive &&
            t.imageKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE) &&
            (best == nullptr || t.arrivalSeq < best->arrivalSeq)) {
            best = &t;
        }
    }
    if (best == nullptr) {
        return;
    }

    best->pullActive = true;
    uint16_t fm = firstMissingChunk(*best, 0);
    if (fm >= best->totalChunks) {
        finalizeTransfer(*best);   // already complete (e.g. re-push)
        return;
    }
    // CR-01: the SD file opens HERE, at activation (FIFO advance) — never at
    // manifest time. A QUEUED manifest opening its file stole the active
    // pull's single kind handle (openTransfer closes the previous partial),
    // making the active pull silently stop persisting and finalize
    // INCOMPLETE despite 100% chunk reception.
    SDStorage().openTransfer(best->imageId, best->imageKind, best->totalSize);
    uint16_t count = best->totalChunks - fm;
    if (count > IMG_WINDOW_MAX_CHUNKS) {
        count = IMG_WINDOW_MAX_CHUNKS;
    }
    issueWindowRequest(*best, fm, count);
}

void ImageRxManager::issueWindowRequest(ImageRxTransfer& t, uint16_t startChunk,
                                        uint16_t count) {
    // PayloadImageWindowRequest on the Phase 1 tracked-command machinery:
    // the ACK resolves the slot, duplicate/terminal guards apply unchanged,
    // and the CMD_ACK_TIMEOUT_WINDOW_MS class bounds the whole 16-chunk
    // exchange (Pitfall 4). sendCommand only QUEUES — the transmit happens
    // in CmdSender().process(), after the RX buffer is fully drained.
    // 6-byte layout (02-05 / CR-01): imageId BE16, imageKind u8, startChunk
    // BE16, count u8 — the kind byte makes thumbnail heals addressable (D-22).
    uint8_t payload[6];
    CommandProtocol::writeUint16(payload, t.imageId);
    payload[2] = t.imageKind;
    CommandProtocol::writeUint16(payload + 3, startChunk);
    payload[5] = static_cast<uint8_t>(count);

    uint16_t seq = CmdSender().sendCommand(CameraCommand::IMAGE_WINDOW_REQUEST, payload, 6);
    if (seq == 0) {
        // Command table full — nothing queued, so no window is in flight.
        // NOT retried on a timer (Pitfall 5): the stall path re-issues this
        // span after IMG_WINDOW_STALL_MS without charging a pass.
        Serial.printf("ImageRx: window request for image %u (%u..%u) could not queue; "
                      "will retry on stall\n",
                      t.imageId, startChunk,
                      static_cast<unsigned>(startChunk) + count - 1);
        t.windowActive = false;
        t.lastProgressMs = millis();
        return;
    }

    t.windowActive = true;
    t.windowBase = startChunk;
    t.windowCount = count;
    t.lastProgressMs = millis();   // the stall clock starts at the request

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: window request queued for image %u kind %u "
                      "(chunks %u..%u, seq %u, pass %u)\n",
                      t.imageId, t.imageKind, startChunk,
                      static_cast<unsigned>(startChunk) + count - 1,
                      seq, t.passCount);
    }
}

bool ImageRxManager::windowSliceComplete(const ImageRxTransfer& t) const {
    uint16_t end = t.windowBase + t.windowCount;
    if (end > t.totalChunks) {
        end = t.totalChunks;
    }
    for (uint16_t i = t.windowBase; i < end; i++) {
        if (!t.chunkPresent[i]) {
            return false;
        }
    }
    return true;
}

uint16_t ImageRxManager::firstMissingChunk(const ImageRxTransfer& t, uint16_t from) const {
    for (uint16_t i = from; i < t.totalChunks; i++) {
        if (!t.chunkPresent[i]) {
            return i;
        }
    }
    return t.totalChunks;   // == totalChunks means "nothing missing from here"
}

uint16_t ImageRxManager::lastMissingChunk(const ImageRxTransfer& t, uint16_t from,
                                          uint16_t to) const {
    uint16_t last = from;
    for (uint16_t i = from; i < to && i < t.totalChunks; i++) {
        if (!t.chunkPresent[i]) {
            last = i;
        }
    }
    return last;
}

bool ImageRxManager::verifyStoredCrc32(const ImageRxTransfer& t, uint32_t& crcOut) {
    // D-23 for fulls: CRC over the STORED bytes, read back in bounded
    // pieces (esp_rom_crc32_le chains: pass the previous result, 0 first —
    // documented in the ROM header). Returns false when the bytes cannot be
    // read (SD degraded/absent) or the file length disagrees with the
    // manifest — never fabricates a verdict.
    File f = SDStorage().serveFile(t.imageId, t.imageKind);
    if (!f) {
        return false;
    }

    uint32_t crc = 0;
    uint32_t total = 0;
    uint8_t piece[256];
    while (f.available() > 0) {
        size_t n = f.read(piece, sizeof(piece));
        if (n == 0) {
            f.close();
            return false;
        }
        crc = esp_rom_crc32_le(crc, piece, n);
        total += n;
    }
    f.close();

    if (total != t.totalSize) {
        Serial.printf("ImageRx: stored file for image %u is %u B, manifest says %u; cannot verify\n",
                      t.imageId, static_cast<unsigned>(total),
                      static_cast<unsigned>(t.totalSize));
        return false;
    }
    crcOut = crc;
    return true;
}

// ===========================
// Snapshot (D-20)
// ===========================

uint8_t ImageRxManager::getTransferSnapshot(TransferRow* rows, uint8_t maxRows) const {
    if (rows == nullptr || maxRows == 0) {
        return 0;
    }

    // Fill in manifest-arrival order (stable FIFO view) — 8 slots, so a
    // small selection loop over arrivalSeq keeps it sorted without state
    uint8_t count = 0;
    uint32_t lastSeq = 0;
    for (uint8_t n = 0; n < RX_TRANSFER_SLOTS; n++) {
        const ImageRxTransfer* best = nullptr;
        for (uint8_t i = 0; i < RX_TRANSFER_SLOTS; i++) {
            const ImageRxTransfer& t = transfers[i];
            if (!t.used || (n > 0 && t.arrivalSeq <= lastSeq)) {
                continue;
            }
            if (best == nullptr || t.arrivalSeq < best->arrivalSeq) {
                best = &t;
            }
        }
        if (best == nullptr || count >= maxRows) {
            break;
        }
        lastSeq = best->arrivalSeq;

        TransferRow& row = rows[count++];
        row.imageId = best->imageId;
        row.kind = best->imageKind;
        row.receivedChunks = best->receivedCount;
        row.totalChunks = best->totalChunks;
        row.percent = (best->totalChunks > 0)
                          ? static_cast<uint8_t>(
                                (static_cast<uint32_t>(best->receivedCount) * 100)
                                / best->totalChunks)
                          : 0;

        // Locked vocabulary, DERIVED from bitmap/pass/terminal truth only
        if (best->terminal) {
            row.state = best->complete ? TransferDisplayState::COMPLETE
                                       : TransferDisplayState::INCOMPLETE;
        } else if (best->imageKind == static_cast<uint8_t>(ImageKind::FULL_IMAGE)
                   && !best->pullActive) {
            row.state = TransferDisplayState::QUEUED;
        } else if (best->passCount > 0) {
            row.state = TransferDisplayState::RETRYING;
        } else {
            row.state = TransferDisplayState::RECEIVING;
        }
    }
    return count;
}

// ===========================
// Retained Thumbnail
// ===========================

void ImageRxManager::freeLatest() {
    if (latestThumbBuffer != nullptr) {
        free(latestThumbBuffer);
        latestThumbBuffer = nullptr;
    }
    latestThumbLength = 0;
    latestThumbId = 0;
}

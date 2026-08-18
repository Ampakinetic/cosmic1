#include "image_tx_manager.h"
#include "auto_capture.h"
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

    // Push discipline (Pattern 4): exactly ONE transmit per process() pass —
    // the E32 transmit is synchronous and costs ~250-400 ms of blocked loop
    pushPending();
}

// ===========================
// Enqueue (ownership transfer)
// ===========================

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

    if (haveThumb && thumb.valid && thumb.buffer != nullptr) {
        entry.thumbBuffer = (uint8_t*)ps_malloc(thumb.length);
        if (entry.thumbBuffer != nullptr) {
            memcpy(entry.thumbBuffer, thumb.buffer, thumb.length);
            entry.thumbLength = thumb.length;
            entry.thumbCrc32 = esp_rom_crc32_le(0, entry.thumbBuffer, entry.thumbLength);
            entry.thumbTotalChunks = static_cast<uint16_t>(
                (thumb.length + IMG_CHUNK_PAYLOAD_SIZE - 1) / IMG_CHUNK_PAYLOAD_SIZE);
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
            entry.state = ImageTxEntryState::THUMB_PUSHED;
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
        entry.state = ImageTxEntryState::THUMB_PUSHED;
    }
    entry.lastActivityMs = millis();

    // Place the entry — bounded queue with drop-oldest on overflow
    ImageTxEntry* slot = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            slot = &entries[i];
            break;
        }
    }
    if (slot == nullptr) {
        // Overflow: drop the OLDEST entry (lowest enqueue sequence) with a
        // Serial warning — never silently
        uint32_t oldestSeq = entries[0].enqueueSeq;
        slot = &entries[0];
        for (uint8_t i = 1; i < QUEUE_DEPTH; i++) {
            if (entries[i].enqueueSeq < oldestSeq) {
                oldestSeq = entries[i].enqueueSeq;
                slot = &entries[i];
            }
        }
        Serial.printf("ImageTx: queue overflow (depth %u); dropping OLDEST entry image %u for image %u\n",
                     static_cast<unsigned>(QUEUE_DEPTH), slot->imageId, imageId);
        freeEntry(*slot);
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
    // FIFO by enqueue order: the oldest entry with an unfinished push
    ImageTxEntry* best = nullptr;
    for (uint8_t i = 0; i < QUEUE_DEPTH; i++) {
        if (!entries[i].used) {
            continue;
        }
        if (entries[i].state != ImageTxEntryState::THUMB_PUSHED) {
            if (best == nullptr || entries[i].enqueueSeq < best->enqueueSeq) {
                best = &entries[i];
            }
        }
    }
    return best;
}

void ImageTxManager::pushPending() {
    ImageTxEntry* entry = findActiveEntry();
    if (entry == nullptr) {
        return;
    }

    if (entry->state == ImageTxEntryState::PUSH_THUMB_MANIFEST) {
        pushThumbManifest(*entry);
        return; // one transmit per pass
    }

    if (entry->state == ImageTxEntryState::PUSH_THUMB_CHUNKS) {
        pushThumbChunk(*entry);
        return; // one transmit per pass
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
        entry.state = ImageTxEntryState::THUMB_PUSHED;
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
        entry.state = ImageTxEntryState::THUMB_PUSHED;
        // Thumbnail holes from failed transmits are healed by the SAME
        // windowed-pull re-request path (D-22), wired in 02-02/02-03
    }
    return ok;
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
    entry.thumbLength = 0;
    entry.thumbTotalChunks = 0;
    entry.nextThumbChunk = 0;
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

#include "image_rx_manager.h"
#include <esp_rom_crc.h>
#include "base_station_config.h"   // MAX_IMAGE_SIZE — allocation bound (Pitfall 11)

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
// Constructor/Destructor
// ===========================

ImageRxManager::ImageRxManager()
    : initialized(false)
    , latestThumbId(0)
    , latestThumbBuffer(nullptr)
    , latestThumbLength(0)
{
    memset(&transfer, 0, sizeof(transfer));
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
    clearTransfer();
    freeLatest();
}

// ===========================
// Main Processing
// ===========================

void ImageRxManager::process() {
    if (!initialized) {
        return;
    }

    // Stall watchdog: an in-flight transfer with no chunk progress within
    // IMG_WINDOW_STALL_MS is dropped. Holes left by lost chunks are healed by
    // the SAME windowed-pull re-request path (D-22) wired in 02-03 — this
    // plan only prevents a dead manifest from squatting on memory forever.
    if (transfer.active && (millis() - transfer.lastActivityMs > IMG_WINDOW_STALL_MS)) {
        Serial.printf("ImageRx: image %u transfer stalled (no chunk for %u ms); dropped\n",
                      transfer.imageId, static_cast<unsigned>(IMG_WINDOW_STALL_MS));
        clearTransfer();
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

    if (!transfer.active) {
        if (DEBUG_IMAGE_RX) {
            Serial.println("ImageRx: chunk with no active manifest ignored");
        }
        return;
    }

    const ImageChunkBody& c = pkt.body;

    if (c.imageId != transfer.imageId) {
        // Chunk for an older/other image than the one being reassembled
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk for image %u while reassembling %u; ignored\n",
                          c.imageId, transfer.imageId);
        }
        return;
    }

    if (c.chunkIndex >= transfer.totalChunks) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk index %u out of range (%u total); ignored\n",
                          c.chunkIndex, transfer.totalChunks);
        }
        return;
    }

    // The chunk must carry exactly the bytes its slot spans — full
    // chunkSize, or the remainder for the final partial chunk
    size_t offset = static_cast<size_t>(c.chunkIndex) * transfer.chunkSize;
    size_t expectedLen = transfer.totalSize - offset;
    if (expectedLen > transfer.chunkSize) {
        expectedLen = transfer.chunkSize;
    }
    if (c.dataLen != expectedLen) {
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: chunk %u of image %u carries %u B, expected %u; ignored\n",
                          c.chunkIndex, transfer.imageId, c.dataLen,
                          static_cast<unsigned>(expectedLen));
        }
        return;
    }

    if (transfer.chunkPresent[c.chunkIndex]) {
        // Duplicate — retransmissions can double-deliver; drop idempotently
        if (DEBUG_IMAGE_RX) {
            Serial.printf("ImageRx: duplicate chunk %u of image %u dropped\n",
                          c.chunkIndex, transfer.imageId);
        }
        return;
    }

    memcpy(transfer.buffer + offset, c.data, c.dataLen);
    transfer.chunkPresent[c.chunkIndex] = 1;
    transfer.receivedCount++;
    transfer.lastActivityMs = millis();

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: image %u chunk %u/%u (%u B)\n",
                      transfer.imageId,
                      static_cast<unsigned>(transfer.receivedCount),
                      static_cast<unsigned>(transfer.totalChunks),
                      c.dataLen);
    }

    if (transfer.receivedCount == transfer.totalChunks) {
        finalizeTransfer();
    }
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
    telemetry.altitudeM = static_cast<float>(b.altitudeCm) / 100.0f;
    telemetry.tempC = static_cast<float>(b.tempCentiC) / 100.0f;
    telemetry.lat = static_cast<float>(b.latE6) / 1000000.0f;
    telemetry.lon = static_cast<float>(b.lonE6) / 1000000.0f;
    telemetry.gpsValid = (b.flags & 0x01) != 0;

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: beacon seq=%u alt=%.1fm temp=%.1fC gps=%s\n",
                      telemetry.seq,
                      telemetry.altitudeM,
                      telemetry.tempC,
                      telemetry.gpsValid ? "valid" : "no-fix");
    }
}

// ===========================
// Transfer Handling
// ===========================

void ImageRxManager::startTransfer(const ImageManifestBody& m) {
    // Pitfall 11: validate EVERYTHING before allocating a single byte.
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

    // A new manifest supersedes any in-flight transfer — the base follows the
    // balloon's push stream, so the newest image wins the reassembly slot
    if (transfer.active) {
        Serial.printf("ImageRx: manifest image %u supersedes in-flight image %u (%u/%u chunks)\n",
                      m.imageId, transfer.imageId,
                      static_cast<unsigned>(transfer.receivedCount),
                      static_cast<unsigned>(transfer.totalChunks));
        clearTransfer();
    }

    transfer.active = true;
    transfer.imageId = m.imageId;
    transfer.imageKind = m.imageKind;
    transfer.captureSource = m.captureSource;
    transfer.totalSize = m.totalSize;
    transfer.chunkSize = m.chunkSize;
    transfer.totalChunks = m.totalChunks;
    transfer.crc32 = m.crc32;   // D-23: CRC over THIS kind's payload bytes
    transfer.captureTimeMs = m.captureTimeMs;
    transfer.resolution = m.resolution;
    transfer.quality = m.quality;
    transfer.brightness = m.brightness;
    transfer.contrast = m.contrast;
    transfer.saturation = m.saturation;
    transfer.exposure = m.exposure;
    transfer.wbMode = m.wbMode;

    transfer.buffer = (uint8_t*)malloc(m.totalSize);
    transfer.chunkPresent = (uint8_t*)calloc(m.totalChunks, 1);
    transfer.receivedCount = 0;
    transfer.lastActivityMs = millis();

    if (transfer.buffer == nullptr || transfer.chunkPresent == nullptr) {
        Serial.printf("ImageRx: allocation failed for image %u (%u B + %u flags); rejected\n",
                      m.imageId,
                      static_cast<unsigned>(m.totalSize),
                      m.totalChunks);
        clearTransfer();
        return;
    }

    if (DEBUG_IMAGE_RX) {
        Serial.printf("ImageRx: manifest image %u kind %u (%u B, %u chunks of %u, CRC %08X)\n",
                      m.imageId, m.imageKind,
                      static_cast<unsigned>(m.totalSize),
                      m.totalChunks, m.chunkSize,
                      static_cast<unsigned int>(m.crc32));
    }
}

void ImageRxManager::finalizeTransfer() {
    // D-23: end-to-end CRC32 over the reassembled bytes must match the
    // manifest's CRC for this kind before anything is exposed
    uint32_t crc = esp_rom_crc32_le(0, transfer.buffer, transfer.totalSize);
    if (crc != transfer.crc32) {
        // Keep the PREVIOUS verified thumbnail; never fabricate success
        Serial.printf("ImageRx: image %u CRC mismatch (got %08X, manifest says %08X); dropped\n",
                      transfer.imageId,
                      static_cast<unsigned int>(crc),
                      static_cast<unsigned int>(transfer.crc32));
        clearTransfer();
        return;
    }

    Serial.printf("ImageRx: image %u complete and CRC-verified (%u bytes)\n",
                  transfer.imageId,
                  static_cast<unsigned>(transfer.totalSize));

    if (transfer.imageKind == static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
        // Retain as the newest verified thumbnail — buffer ownership moves
        // into the latest* slot; clearTransfer frees only the rest
        freeLatest();
        latestThumbId = transfer.imageId;
        latestThumbBuffer = transfer.buffer;
        latestThumbLength = transfer.totalSize;
        transfer.buffer = nullptr;
    }
    // FULL_IMAGE completions are verified + logged here; retention and
    // serving of full images arrive in 02-02's windowed-pull extension

    clearTransfer();
}

void ImageRxManager::clearTransfer() {
    if (transfer.buffer != nullptr) {
        free(transfer.buffer);
        transfer.buffer = nullptr;
    }
    if (transfer.chunkPresent != nullptr) {
        free(transfer.chunkPresent);
        transfer.chunkPresent = nullptr;
    }
    transfer = ImageRxTransfer{};
}

void ImageRxManager::freeLatest() {
    if (latestThumbBuffer != nullptr) {
        free(latestThumbBuffer);
        latestThumbBuffer = nullptr;
    }
    latestThumbLength = 0;
    latestThumbId = 0;
}

#include "sd_storage.h"
#include "base_station_config.h"   // SD_SCK_PIN / SD_MISO_PIN / SD_MOSI_PIN / SD_CS_PIN

// Debug configuration
#ifndef DEBUG_SD_STORAGE
#define DEBUG_SD_STORAGE true
#endif

// ===========================
// Static Instance
// ===========================

static SdStorage sdStorageInstance;
SdStorage& SDStorage() {
    return sdStorageInstance;
}

// Dedicated SPI bus for the card — a SEPARATE SPIClass(HSPI) instance, NOT
// the default bus. The instance-overload SD.begin(CS, sdSPI) below is
// mandatory for custom pins on arduino-esp32: the pin-less SD.begin(CS)
// overload ignores them entirely (research Code Example / issue #8457).
static SPIClass sdSPI(HSPI);

// ===========================
// Constructor
// ===========================

SdStorage::SdStorage()
    : available(false)
    , fullFileId(0)
    , fullPersistedBytes(0)
    , thumbFileId(0)
    , thumbPersistedBytes(0)
{
    status.available = false;
    status.initFailed = false;
    status.writeFailed = false;
}

// ===========================
// Initialization
// ===========================

bool SdStorage::begin() {
    // Custom pins via the dedicated instance — all four pins are passed to
    // sdSPI.begin, and the instance is handed to SD.begin below
    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, sdSPI)) {
        // Degrade, never halt: the station runs without storage, serving the
        // RAM-retained thumbnail; the UI/storage chip reports UNAVAILABLE
        Serial.printf("SdStorage: SD.begin failed (pins SCK=%d MISO=%d MOSI=%d CS=%d); "
                      "running WITHOUT storage (images will not persist)\n",
                      SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
        available = false;
        status.available = false;
        status.initFailed = true;
        status.writeFailed = false;
        return true;
    }

    // Flat image directory (D-32 — single /images dir, no clock-dependent
    // folder logic). mkdir is idempotent on FAT.
    if (!SD.mkdir("/images")) {
        Serial.println("SdStorage: /images mkdir failed (existing dir?); continuing");
    }

    available = true;
    status.available = true;
    status.initFailed = false;
    status.writeFailed = false;

    Serial.println("SdStorage: card mounted, /images ready");
    return true;
}

// ===========================
// Transfer Files
// ===========================

void SdStorage::imagePath(char* out, size_t cap, uint16_t imageId, uint8_t kind) {
    // D-29/D-31: ids zero-padded to 5 (Pitfall 10 — wrap-safe ordering on the
    // FAT directory); thumbnails carry the _T.JPG suffix. Names are built
    // ONLY from the %05u-formatted numeric id — no network-supplied string
    // ever enters a file path (T-02-08).
    if (kind == static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
        snprintf(out, cap, "/images/IMG_%05u_T.JPG", static_cast<unsigned>(imageId));
    } else {
        snprintf(out, cap, "/images/IMG_%05u.JPG", static_cast<unsigned>(imageId));
    }
}

void SdStorage::sidecarPath(char* out, size_t cap, uint16_t imageId, uint8_t kind) {
    // Kind-suffixed (02-05 / WR-05, Gap 4): thumbnail and full D-30 records
    // persist as SEPARATE files — the full's finalization must never
    // truncate the thumbnail's record. The _T sidecar convention mirrors
    // D-31's IMG_{id}_T.JPG and is the Phase 3 gallery contract. As with
    // imagePath, names are built ONLY from the %05u-formatted numeric id
    // (T-02-08).
    if (kind == static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
        snprintf(out, cap, "/images/IMG_%05u_T.JSON", static_cast<unsigned>(imageId));
    } else {
        snprintf(out, cap, "/images/IMG_%05u.JSON", static_cast<unsigned>(imageId));
    }
}

const char* SdStorage::triggerSourceName(uint8_t captureSource) {
    switch (static_cast<CaptureSource>(captureSource)) {
        case CaptureSource::MANUAL:          return "manual";
        case CaptureSource::INTERVAL:        return "interval";
        case CaptureSource::EVENT_ALTITUDE:  return "event-altitude";
        case CaptureSource::EVENT_DISTANCE:  return "event-distance";
        case CaptureSource::EVENT_PHASE:     return "event-phase";
        default:                             return "unknown";
    }
}

void SdStorage::fileFor(uint8_t kind, File** out, uint16_t** outId, uint32_t** outPersisted) {
    if (kind == static_cast<uint8_t>(ImageKind::THUMBNAIL)) {
        *out = &thumbFile;
        *outId = &thumbFileId;
        *outPersisted = &thumbPersistedBytes;
    } else {
        *out = &fullFile;
        *outId = &fullFileId;
        *outPersisted = &fullPersistedBytes;
    }
}

bool SdStorage::openTransfer(uint16_t imageId, uint8_t kind, uint32_t totalSize) {
    if (!available) {
        return false; // degraded — accounting continues, nothing persisted
    }

    char path[32];
    imagePath(path, sizeof(path), imageId, kind);

    File* handle = nullptr;
    uint16_t* handleId = nullptr;
    uint32_t* persistedBytes = nullptr;
    fileFor(kind, &handle, &handleId, &persistedBytes);

    // A re-open for the same (id, kind) restarts the file; a different id
    // closes the previous PARTIAL file first — it stays on disk (kept,
    // flagged in its sidecar at its own finalization; never deleted)
    if (*handle && *handleId != imageId) {
        Serial.printf("SdStorage: closing partial %s before opening image %u\n",
                      handle->name(), imageId);
        handle->close();
        *handle = File();
    }

    // FILE_WRITE on arduino-esp32 is "w" — truncate-create. The untrusted
    // totalSize is NOT used to pre-allocate anything (Pitfall 11): the file
    // grows chunk-by-chunk as bytes actually land.
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        // Q5 stop-storing-and-warn: an open failure is the disk-full/IO
        // signal on this FS API (no free-space query exists) — degrade, warn,
        // never delete anything to make room
        degrade("open failed (disk full?)");
        return false;
    }

    *handle = f;
    *handleId = imageId;
    *persistedBytes = 0;

    if (DEBUG_SD_STORAGE) {
        Serial.printf("SdStorage: opened %s for image %u (%u B expected)\n",
                      path, imageId, static_cast<unsigned>(totalSize));
    }
    return true;
}

bool SdStorage::writeChunk(uint16_t imageId, uint8_t kind, uint16_t chunkIndex,
                           uint16_t chunkSize, const uint8_t* data, size_t len) {
    if (!available) {
        return false;
    }

    File* handle = nullptr;
    uint16_t* handleId = nullptr;
    uint32_t* persistedBytes = nullptr;
    fileFor(kind, &handle, &handleId, &persistedBytes);
    if (!*handle || *handleId != imageId) {
        // CR-01 safety net: a chunk for a file this kind's single handle is
        // not currently holding (e.g. a thumbnail heal window competing with
        // a newer push — one handle per kind) is REOPENED, never silently
        // dropped: dropping made the transfer complete its bitmap in RAM
        // while SD lagged behind, finalizing INCOMPLETE despite 100% chunk
        // reception. The reopen is deliberately NON-truncating ("r+" when
        // the file exists): openTransfer's FILE_WRITE ("w") truncates, so
        // two interleaved ids flipping the handle would destroy each
        // other's persisted bytes on every flip. ImageRxManager opens fulls
        // lazily at pull activation, so in practice this is the rare
        // heal-vs-push path — but a drop here can never again be silent.
        if (DEBUG_SD_STORAGE) {
            Serial.printf("SdStorage: reopening file for image %u kind %u for chunk %u "
                          "(handle held another transfer)\n",
                          imageId, kind, chunkIndex);
        }
        if (*handle) {
            handle->close();   // flush the other id's buffered bytes to disk
            *handle = File();
        }
        char path[32];
        imagePath(path, sizeof(path), imageId, kind);
        File f = SD.exists(path) ? SD.open(path, "r+") : SD.open(path, FILE_WRITE);
        if (!f) {
            degrade("reopen failed (disk full?)");
            return false;
        }
        *handle = f;
        *handleId = imageId;
        *persistedBytes = 0;   // accounting restarts at each flip (conservative)
    }

    // Pattern 5: seek to the chunk's fixed offset — out-of-order arrival
    // lands at the right position; no whole-image RAM buffer exists
    if (!handle->seek(static_cast<size_t>(chunkIndex) * chunkSize)) {
        degrade("seek failed");
        return false;
    }

    size_t written = handle->write(data, len);
    if (written != len) {
        // Q5: mid-flight write failure — stop storing, surface the warning;
        // existing files are never touched again, let alone deleted
        degrade("write failed (disk full?)");
        return false;
    }

    *persistedBytes += len;
    return true;
}

// ===========================
// Finalization (sidecar written ONCE — Pitfall 8)
// ===========================

bool SdStorage::finalizeImage(const SdImageMetadata& meta) {
    char path[32];
    imagePath(path, sizeof(path), meta.imageId, meta.kind);

    File* handle = nullptr;
    uint16_t* handleId = nullptr;
    uint32_t* persistedBytes = nullptr;
    fileFor(meta.kind, &handle, &handleId, &persistedBytes);

    if (*handle && *handleId == meta.imageId) {
        handle->close();
        *handle = File();
    }

    if (!available) {
        Serial.printf("SdStorage: image %u finalized while degraded — NOT persisted "
                      "(%u/%u chunks, complete=%s)\n",
                      meta.imageId, meta.chunksReceived, meta.chunksTotal,
                      meta.complete ? "true" : "false");
        return false;
    }

    // storedToSd is computed truth: whether bytes actually landed on the
    // card for this file — set here from SdStorage's own tracking, never
    // fabricated by the caller
    SdImageMetadata m = meta;
    m.storedToSd = *persistedBytes > 0;

    bool ok = writeSidecar(m);
    Serial.printf("SdStorage: finalized %s (%u/%u chunks, %u B persisted, complete=%s)%s\n",
                  path, meta.chunksReceived, meta.chunksTotal,
                  static_cast<unsigned>(*persistedBytes),
                  meta.complete ? "true" : "false",
                  ok ? "" : " — SIDECAR WRITE FAILED");
    return ok;
}

bool SdStorage::writeSidecar(const SdImageMetadata& meta) {
    // Hand-built String JSON, matching the main_basestation style — the
    // base env deliberately has no ArduinoJson dependency (research
    // Supporting table). Written exactly once per image at finalization.
    char imgPath[32];
    imagePath(imgPath, sizeof(imgPath), meta.imageId, meta.kind);
    char sidePath[32];
    sidecarPath(sidePath, sizeof(sidePath), meta.imageId, meta.kind);

    File f = SD.open(sidePath, FILE_WRITE);
    if (!f) {
        Serial.printf("SdStorage: sidecar open failed for %s\n", sidePath);
        return false;
    }

    String json = "{";
    json += "\"id\":" + String(meta.imageId) + ",";
    json += "\"kind\":\"" + String(meta.kind == static_cast<uint8_t>(ImageKind::THUMBNAIL)
                                       ? "thumbnail" : "full") + "\",";
    json += "\"file\":\"" + String(imgPath) + "\",";
    json += "\"captureTimeMs\":" + String(meta.captureTimeMs) + ",";
    json += "\"receiptTimeMs\":" + String(meta.receiptTimeMs) + ",";
    json += "\"triggerSource\":\"" + String(triggerSourceName(meta.captureSource)) + "\",";

    // Latest telemetry beacon at receipt (D-30); invalid-flagged rather than
    // fabricated when no beacon was ever received
    if (meta.telemetryValid) {
        json += "\"altitudeM\":" + String(meta.altitudeM, 1) + ",";
        json += "\"lat\":" + String(meta.lat, 6) + ",";
        json += "\"lon\":" + String(meta.lon, 6) + ",";
    } else {
        json += "\"altitudeM\":null,\"lat\":null,\"lon\":null,";
    }

    json += "\"cameraSettings\":{";
    json += "\"resolution\":" + String(meta.resolution) + ",";
    json += "\"quality\":" + String(meta.quality) + ",";
    json += "\"brightness\":" + String(meta.brightness) + ",";
    json += "\"contrast\":" + String(meta.contrast) + ",";
    json += "\"saturation\":" + String(meta.saturation) + ",";
    json += "\"exposure\":" + String(meta.exposure) + ",";
    json += "\"wbMode\":" + String(meta.wbMode) + "},";

    json += "\"chunksReceived\":" + String(meta.chunksReceived) + ",";
    json += "\"chunksTotal\":" + String(meta.chunksTotal) + ",";
    json += "\"bytesReceived\":" + String(meta.bytesReceived) + ",";
    json += "\"complete\":" + String(meta.complete ? "true" : "false") + ",";
    if (meta.crcMismatch) {
        json += "\"crcMismatch\":true,";
    }

    // "rssi": null — the E32 in transparent mode exposes no per-packet RSSI
    // (verified: no RSSI API anywhere in the radio driver), so the value is
    // recorded as null with this documented reason instead of being
    // fabricated (D-30 deviation, surfaced in 02-03's plan frontmatter).
    json += "\"rssi\":null,";

    json += "\"storedToSd\":" + String(meta.storedToSd ? "true" : "false");
    json += "}";

    size_t written = f.print(json);
    f.close();
    if (written != json.length()) {
        Serial.printf("SdStorage: sidecar write short (%u of %u) for %s\n",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(json.length()), sidePath);
        return false;
    }
    return true;
}

// ===========================
// Serving
// ===========================

File SdStorage::serveFile(uint16_t imageId, uint8_t kind) {
    if (!available) {
        return File(); // degraded — absence reported honestly
    }
    char path[32];
    imagePath(path, sizeof(path), imageId, kind);
    if (!SD.exists(path)) {
        return File();
    }
    return SD.open(path, FILE_READ);
}

// ===========================
// Degradation
// ===========================

void SdStorage::degrade(const char* reason) {
    // Stop-storing-and-warn (Q5): stop persisting, surface the reason via
    // getStatus() — the UI/storage chip and /status consume it. NO deletion
    // or overwrite of any existing file, ever.
    Serial.printf("SdStorage: DEGRADED — %s; storing stopped (existing files kept)\n", reason);
    if (fullFile) {
        fullFile.close();
        fullFile = File();
    }
    if (thumbFile) {
        thumbFile.close();
        thumbFile = File();
    }
    available = false;
    status.available = false;
    status.writeFailed = true;
}

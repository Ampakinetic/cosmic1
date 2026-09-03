#include "sd_storage.h"
#include "base_station_config.h"   // SD_CLK_PIN / SD_CMD_PIN / SD_DATA_PIN

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

// SD transport: the board's BUILT-IN slot is SDMMC-wired (operator-verified
// against the module datasheet at the 01-09 Task 1 bench checkpoint —
// CLK/CMD/DATA, no CS line), so the card rides the native SDMMC host in
// 1-bit mode via the SD_MMC global. Custom pins REQUIRE setPins() before
// begin() on GPIO-matrix targets (arduino-esp32 3.x): the SDMMCFS
// constructor only loads pin defaults when the variant defines
// BOARD_HAS_SDMMC, which esp32-s3-devkitc-1 does not — without setPins,
// begin() fails with "some SD pins are not set".

// ===========================
// Constructor
// ===========================

SdStorage::SdStorage()
    : available(false)
    , clearing(false)
    , fullFileId(0)
    , fullPersistedBytes(0)
    , thumbFileId(0)
    , thumbPersistedBytes(0)
    , galleryCount(0)
    , indexVersion(0)
    , indexedVersion(0)
{
    status.available = false;
    status.initFailed = false;
    status.writeFailed = false;
}

// ===========================
// Initialization
// ===========================

bool SdStorage::begin() {
    // Built-in SDMMC slot: pins come from the datasheet-verified constants.
    // setPins must precede begin() (see the transport note above); it fails
    // only on an invalid pin or a post-begin call — both are config errors
    // that begin() would surface anyway, so they share the failure verdict.
    bool pinsOk = SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_DATA_PIN);

    // 1-bit mode (the slot wires only D0). Frequency deliberately
    // SDMMC_FREQ_DEFAULT (20 MHz), not the 40 MHz HIGHSPEED default:
    // transfers arrive at a 9.6 kbps air rate, so throughput is irrelevant
    // and the lower clock is the SD_MMC.h-documented stability lever for
    // marginal slots/traces.
    if (!pinsOk || !SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        // Degrade, never halt: the station runs without storage, serving the
        // RAM-retained thumbnail; the UI/storage chip reports UNAVAILABLE
        Serial.printf("SdStorage: SD_MMC.begin failed (1-bit SDMMC pins CLK=%d CMD=%d D0=%d); "
                      "running WITHOUT storage (images will not persist)\n",
                      SD_CLK_PIN, SD_CMD_PIN, SD_DATA_PIN);
        available = false;
        status.available = false;
        status.initFailed = true;
        status.writeFailed = false;
        return true;
    }

    // Flat image directory (D-32 — single /images dir, no clock-dependent
    // folder logic). mkdir is idempotent on FAT.
    if (!SD_MMC.mkdir("/images")) {
        Serial.println("SdStorage: /images mkdir failed (existing dir?); continuing");
    }

    available = true;
    status.available = true;
    status.initFailed = false;
    status.writeFailed = false;

    // Boot enumeration (03-04): one openNextFile walk builds the RAM gallery
    // index BEFORE any web client can ask for /gallery — pagination later
    // slices this index, never the directory (Pitfall 5).
    buildIndex();

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
    // Wipe hold (quick-260831): a transfer opening a file while the explicit
    // /sd-clear walk runs must fail fast WITHOUT degrade() — a deliberate
    // wipe must never latch the boot-permanent writeFailed state.
    if (clearing) {
        return false;
    }
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
    File f = SD_MMC.open(path, FILE_WRITE);
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
    // Wipe hold (quick-260831): a chunk arriving during the /sd-clear walk
    // must fail fast WITHOUT degrade() — the wipe must not latch the
    // boot-permanent writeFailed state (nor flood the console per chunk).
    if (clearing) {
        return false;
    }
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
        File f = SD_MMC.exists(path) ? SD_MMC.open(path, "r+") : SD_MMC.open(path, FILE_WRITE);
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

void SdStorage::flushTransfer(uint16_t imageId, uint8_t kind) {
    // CR-02: stdio-buffered writes are invisible to a second FILE* until
    // flushed — verifyStoredCrc32 reads through serveFile() while this kind's
    // write handle may still hold the final chunk's bytes in its stdio
    // buffer (each writeChunk's seek flushes only the PREVIOUS write). Flush
    // rather than close so the transfer could keep writing afterwards.
    if (!available) {
        return;
    }
    File* handle = nullptr;
    uint16_t* handleId = nullptr;
    uint32_t* persistedBytes = nullptr;
    fileFor(kind, &handle, &handleId, &persistedBytes);
    if (*handle && *handleId == imageId) {
        handle->flush();
    }
}

// ===========================
// Finalization (sidecar written ONCE — Pitfall 8)
// ===========================

bool SdStorage::finalizeImage(const SdImageMetadata& meta) {
    // Gallery index invalidation (03-04, key_link indexVersion): a
    // finalization changed /images — at minimum the transfer's file has
    // existed since openTransfer, and on success a sidecar was just added.
    // Bumped on EVERY finalize path (both kinds, the degraded early-return
    // included: a degraded finalize can still leave a partial file behind,
    // and the directory is the truth). The next gallery request lazily
    // rebuilds via ensureIndexCurrent() — never a per-request rescan.
    indexVersion++;

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
    // fabricated by the caller. WR-01 ownership guard: the per-kind byte
    // counter belongs to whichever image the kind handle last served
    // (*handleId) — a finalize for a slot-pressure-evicted transfer must
    // never claim a newer image's persisted bytes.
    SdImageMetadata m = meta;
    m.storedToSd = (*handleId == meta.imageId) && (*persistedBytes > 0);

    bool ok = writeSidecar(m);
    Serial.printf("SdStorage: finalized %s (%u/%u chunks, %u B persisted, complete=%s)%s\n",
                  path, meta.chunksReceived, meta.chunksTotal,
                  static_cast<unsigned>(*persistedBytes),
                  meta.complete ? "true" : "false",
                  ok ? "" : " — SIDECAR WRITE FAILED");
    return ok;
}

bool SdStorage::writeSidecar(const SdImageMetadata& meta) {
    // Wipe hold (quick-260831): a sidecar landing during the /sd-clear walk
    // must fail fast WITHOUT degrade() — the wipe must not latch the
    // boot-permanent writeFailed state.
    if (clearing) {
        return false;
    }
    // Hand-built String JSON, matching the main_basestation style — the
    // base env deliberately has no ArduinoJson dependency (research
    // Supporting table). Written exactly once per image at finalization.
    char imgPath[32];
    imagePath(imgPath, sizeof(imgPath), meta.imageId, meta.kind);
    char sidePath[32];
    sidecarPath(sidePath, sizeof(sidePath), meta.imageId, meta.kind);

    File f = SD_MMC.open(sidePath, FILE_WRITE);
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
    // WR-05: READ availability is independent of WRITE availability. After a
    // mid-flight degrade() the card is still mounted and every file already
    // stored stays servable (the Q5 disk-full policy KEEPS existing files —
    // serving them must keep working too, or the UI's COMPLETE rows would
    // 404 and stored-CRC read-backs would fail for healthy files). Only a
    // failed begin() (no card mounted) has nothing to serve.
    if (status.initFailed) {
        return File(); // no card mounted — absence reported honestly
    }
    char path[32];
    imagePath(path, sizeof(path), imageId, kind);
    if (!SD_MMC.exists(path)) {
        return File();
    }
    return SD_MMC.open(path, FILE_READ);
}

// ===========================
// Gallery Index (IMG-06, 03-04)
// ===========================

// File-class bits for the name parser below (module-internal)
enum : uint8_t {
    GF_FULL         = 0x01,   // IMG_{id}.JPG
    GF_THUMB        = 0x02,   // IMG_{id}_T.JPG
    GF_FULL_SIDECAR = 0x04,   // IMG_{id}.JSON
    GF_THUMB_SIDECAR = 0x08   // IMG_{id}_T.JSON
};

// Match one /images basename against the D-29/D-31/D-32 convention:
// IMG_{exactly 5 digits} + one of ".JPG" / "_T.JPG" / ".JSON" /
// "_T.JSON" (ids are %05u zero-padded uint16). Every character is validated
// BEFORE any number is parsed — a hostile or foreign name can never smuggle
// a path fragment or an out-of-range id into the index (T-03-10: ids are
// strictly numeric, 1..0xFFFF, and file names elsewhere are built only
// from the %05u-formatted id).
static bool parseGalleryName(const char* base, uint16_t* outId, uint8_t* outFlags) {
    static const char IMG_PREFIX[] = "IMG_";
    const size_t plen = strlen(IMG_PREFIX);
    if (strlen(base) < plen + 5 + 4) return false;   // shortest: IMG_00000.JPG
    if (strncmp(base, IMG_PREFIX, plen) != 0) return false;

    // Exactly 5 digits, per-character (strtol alone would accept signs,
    // leading spaces, and hex prefixes)
    for (size_t i = 0; i < 5; i++) {
        if (!isdigit((unsigned char)base[plen + i])) return false;
    }

    const char* suffix = base + plen + 5;
    uint8_t flags;
    if      (strcmp(suffix, ".JPG") == 0)   flags = GF_FULL;
    else if (strcmp(suffix, "_T.JPG") == 0) flags = GF_THUMB;
    else if (strcmp(suffix, ".JSON") == 0)  flags = GF_FULL_SIDECAR;
    else if (strcmp(suffix, "_T.JSON") == 0) flags = GF_THUMB_SIDECAR;
    else return false;

    // 5 validated decimal digits: 1..99999 — the range check rejects the
    // ids beyond the uint16 image-ID space (id 00000 never exists;
    // allocateImageId pre-increments from 0)
    long id = strtol(base + plen, nullptr, 10);
    if (id < 1 || id > 0xFFFF) return false;

    *outId = static_cast<uint16_t>(id);
    *outFlags = flags;
    return true;
}

// qsort comparator: DESCENDING by id (newest first)
static int galleryEntryCompare(const void* a, const void* b) {
    uint16_t ia = static_cast<const SdGalleryEntry*>(a)->id;
    uint16_t ib = static_cast<const SdGalleryEntry*>(b)->id;
    return (ia > ib) ? -1 : (ia < ib) ? 1 : 0;
}

// Post-sort index pass (feature: capture markers): one sidecar read per
// indexed image fills the capture position the base stamped at finalize
// (lat/lon/alt from the then-latest beacon). Honest rules: absent sidecar,
// absent telemetry block, or an invalid flag all leave hasGps false —
// nothing is fabricated. Cost: +1 small file read per image at boot
// (~2-4 s extra at the 1000-image cap on the 1-bit SDMMC bus), paid once
// in exchange for sidecar-free marker serving at request time.
void SdStorage::fillIndexGps() {
    SdImageMetadata meta;
    uint16_t gpsCount = 0;
    for (uint16_t i = 0; i < galleryCount; i++) {
        SdGalleryEntry* e = &galleryIndex[i];
        e->hasGps = false;
        if (!e->hasFullSidecar && !e->hasThumbSidecar) {
            continue;
        }
        // Full sidecar preferred (both carry the telemetry triple); thumb
        // sidecar covers thumbnails whose full transfer never completed
        const bool thumb = !e->hasFullSidecar;
        if (!readSidecarMeta(e->id, thumb, &meta)) {
            continue;
        }
        if ((meta.present & SD_SC_PRESENT_TELEMETRY) == 0 || !meta.telemetryValid) {
            continue;
        }
        e->latE6 = lroundf(meta.lat * 1000000.0f);
        e->lonE6 = lroundf(meta.lon * 1000000.0f);
        e->altM  = static_cast<int16_t>(constrain(meta.altitudeM, -1000.0f, 32000.0f));
        e->hasGps = true;
        gpsCount++;
    }
    if (DEBUG_SD_STORAGE) {
        Serial.printf("SdStorage: index GPS pass — %u capture position(s)\n",
                      static_cast<unsigned>(gpsCount));
    }
}

void SdStorage::buildIndex() {
    galleryCount = 0;
    indexedVersion = indexVersion;   // single-threaded loop: no interleaving

    // No card mounted: the empty index IS the honest state (/gallery serves
    // the empty listing; the storage chip reports UNAVAILABLE)
    if (status.initFailed) {
        return;
    }

    File dir = SD_MMC.open("/images");
    if (!dir || !dir.isDirectory()) {
        Serial.println("SdStorage: /images open failed during index build — index left empty");
        return;
    }

    File f;
    while (f = dir.openNextFile()) {
        if (!f.isDirectory()) {
            // f.name() may carry a leading path depending on the core
            // version — parse the LAST path component only
            const char* base = f.name();
            const char* slash = strrchr(base, '/');
            if (slash) {
                base = slash + 1;
            }
            uint16_t id = 0;
            uint8_t flags = 0;
            if (parseGalleryName(base, &id, &flags)) {
                mergeIntoIndex(id, flags, static_cast<uint32_t>(f.size()));
            }
        }
        f.close();
    }
    dir.close();

    sortIndexDescending();
    fillIndexGps();

    if (DEBUG_SD_STORAGE) {
        Serial.printf("SdStorage: gallery index built — %u image(s)%s\n",
                      static_cast<unsigned>(galleryCount),
                      galleryCount == SD_GALLERY_MAX_ENTRIES
                          ? " (cap reached — newest kept)" : "");
    }
}

void SdStorage::mergeIntoIndex(uint16_t id, uint8_t galleryFlags, uint32_t fullSize) {
    SdGalleryEntry* e = nullptr;
    for (uint16_t i = 0; i < galleryCount; i++) {
        if (galleryIndex[i].id == id) {
            e = &galleryIndex[i];
            break;
        }
    }
    if (e == nullptr) {
        if (galleryCount < SD_GALLERY_MAX_ENTRIES) {
            e = &galleryIndex[galleryCount++];
        } else {
            // Cap reached: keep the NEWEST 1000 (documented honest
            // degradation) — evict the oldest indexed id only when the new
            // one is newer; an older id is honestly beyond the window
            uint16_t oldest = 0;
            for (uint16_t i = 1; i < galleryCount; i++) {
                if (galleryIndex[i].id < galleryIndex[oldest].id) {
                    oldest = i;
                }
            }
            if (id <= galleryIndex[oldest].id) {
                return;
            }
            e = &galleryIndex[oldest];
            galleryCount = SD_GALLERY_MAX_ENTRIES;   // (already there; intent)
        }
        e->id = id;
        e->hasThumb = false;
        e->hasFull = false;
        e->hasThumbSidecar = false;
        e->hasFullSidecar = false;
        e->fullSize = 0;
        e->hasGps = false;
        e->latE6 = 0;
        e->lonE6 = 0;
        e->altM = 0;
    }
    if (galleryFlags & GF_FULL) {
        e->hasFull = true;
        e->fullSize = fullSize;
    }
    if (galleryFlags & GF_THUMB)         e->hasThumb = true;
    if (galleryFlags & GF_FULL_SIDECAR)  e->hasFullSidecar = true;
    if (galleryFlags & GF_THUMB_SIDECAR) e->hasThumbSidecar = true;
}

void SdStorage::sortIndexDescending() {
    if (galleryCount > 1) {
        qsort(galleryIndex, galleryCount, sizeof(SdGalleryEntry), galleryEntryCompare);
    }
}

void SdStorage::ensureIndexCurrent() {
    if (indexedVersion != indexVersion) {
        buildIndex();
    }
}

uint16_t SdStorage::getTotalCount() const {
    return galleryCount;
}

uint16_t SdStorage::getPageCount() const {
    if (galleryCount == 0) {
        return 1;   // the honest empty listing is still "page 1 of 1"
    }
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(galleryCount) + SD_GALLERY_PAGE_SIZE - 1)
        / SD_GALLERY_PAGE_SIZE);
}

uint8_t SdStorage::getIndexPage(uint16_t pageIdx, SdGalleryEntry* dst, uint8_t max) {
    if (dst == nullptr || max == 0 || pageIdx < 1) {
        return 0;
    }
    uint32_t start = (static_cast<uint32_t>(pageIdx) - 1) * SD_GALLERY_PAGE_SIZE;
    if (start >= galleryCount) {
        return 0;
    }
    uint32_t avail = galleryCount - start;
    uint32_t n = (avail < SD_GALLERY_PAGE_SIZE) ? avail : SD_GALLERY_PAGE_SIZE;
    if (n > max) {
        n = max;
    }
    memcpy(dst, &galleryIndex[start], static_cast<size_t>(n) * sizeof(SdGalleryEntry));
    return static_cast<uint8_t>(n);
}

bool SdStorage::findIndexEntry(uint16_t imageId, SdGalleryEntry* out) const {
    for (uint16_t i = 0; i < galleryCount; i++) {
        if (galleryIndex[i].id == imageId) {
            if (out) {
                *out = galleryIndex[i];
            }
            return true;
        }
    }
    return false;
}

// ===========================
// Sidecar Parsing (gallery detail, D-47 / T-03-09)
// ===========================

// Bounded read cap: the hand-built writer emits ~600-700 chars. A hostile
// card may hold anything — only the first SD_SIDECAR_MAX_BYTES are read;
// fields beyond the cap parse as absent (honest omission, no overflow).
static constexpr size_t SD_SIDECAR_MAX_BYTES = 1024;

// Locate "\"key\": in buf — returns the index of the first value character
// after the colon (spaces skipped), or -1 when absent. The closing quote of
// the pattern makes key-prefix collisions impossible.
static int32_t jsonValuePos(const char* buf, const char* key) {
    char pattern[28];
    if (snprintf(pattern, sizeof(pattern), "\"%s\":", key) >= static_cast<int>(sizeof(pattern))) {
        return -1;   // key longer than the scratch pattern — impossible here
    }
    const char* hit = strstr(buf, pattern);
    if (hit == nullptr) {
        return -1;
    }
    int32_t pos = static_cast<int32_t>(hit - buf + strlen(pattern));
    while (buf[pos] == ' ') {
        pos++;
    }
    return pos;
}

static bool jsonIsNullAt(const char* buf, int32_t pos) {
    return pos >= 0 && strncmp(buf + pos, "null", 4) == 0;
}

// Bounded quoted-string copy into dst (at most dstCap-1 chars + NUL).
// Overlong values are REJECTED, never truncated mid-token.
static bool jsonStringAt(const char* buf, int32_t pos, char* dst, size_t dstCap) {
    if (pos < 0 || buf[pos] != '"') {
        return false;
    }
    const char* start = buf + pos + 1;
    const char* end = strchr(start, '"');
    if (end == nullptr) {
        return false;
    }
    size_t len = static_cast<size_t>(end - start);
    if (len >= dstCap) {
        return false;
    }
    memcpy(dst, start, len);
    dst[len] = '\0';
    return true;
}

// Unsigned integer with a clamp to [0, hi] — a hostile overlong digit run
// saturates into the clamp, never wraps
static bool jsonUintAt(const char* buf, int32_t pos, uint32_t hi, uint32_t* out) {
    if (pos < 0 || !isdigit((unsigned char)buf[pos])) {
        return false;
    }
    char* endp = nullptr;
    unsigned long v = strtoul(buf + pos, &endp, 10);
    if (endp == buf + pos) {
        return false;
    }
    if (v > hi) {
        v = hi;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

// Signed integer with a clamp to [lo, hi]
static bool jsonIntAt(const char* buf, int32_t pos, long lo, long hi, long* out) {
    if (pos < 0) {
        return false;
    }
    const char* s = buf + pos;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (!isdigit((unsigned char)s[i])) {
        return false;
    }
    char* endp = nullptr;
    long v = strtol(s, &endp, 10);
    if (endp == s) {
        return false;
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *out = v;
    return true;
}

// Float with a clamp to [lo, hi] — lat/lon/altitude ranges make fabricated
// coordinates impossible to smuggle through
static bool jsonFloatAt(const char* buf, int32_t pos, float lo, float hi, float* out) {
    if (pos < 0) {
        return false;
    }
    const char* s = buf + pos;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (!isdigit((unsigned char)s[i]) && s[i] != '.') {
        return false;
    }
    char* endp = nullptr;
    float v = strtof(s, &endp);
    if (endp == s) {
        return false;
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *out = v;
    return true;
}

static bool jsonBoolAt(const char* buf, int32_t pos, bool* out) {
    if (pos < 0) {
        return false;
    }
    if (strncmp(buf + pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(buf + pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

// Reverse of triggerSourceName: bounded compare against the KNOWN names
// only — an unknown or hostile string maps to 0xFF, which serializes back
// through triggerSourceName as "unknown" (no sidecar string ever reaches
// the browser verbatim)
static uint8_t captureSourceFromName(const char* name) {
    if      (strcmp(name, "manual") == 0)         return static_cast<uint8_t>(CaptureSource::MANUAL);
    else if (strcmp(name, "interval") == 0)       return static_cast<uint8_t>(CaptureSource::INTERVAL);
    else if (strcmp(name, "event-altitude") == 0) return static_cast<uint8_t>(CaptureSource::EVENT_ALTITUDE);
    else if (strcmp(name, "event-distance") == 0) return static_cast<uint8_t>(CaptureSource::EVENT_DISTANCE);
    else if (strcmp(name, "event-phase") == 0)    return static_cast<uint8_t>(CaptureSource::EVENT_PHASE);
    else return 0xFF;
}

bool SdStorage::readSidecarMeta(uint16_t imageId, bool thumb, SdImageMetadata* out) {
    if (out == nullptr || status.initFailed) {
        return false;
    }

    char path[32];
    sidecarPath(path, sizeof(path), imageId,
                thumb ? static_cast<uint8_t>(ImageKind::THUMBNAIL)
                      : static_cast<uint8_t>(ImageKind::FULL_IMAGE));
    if (!SD_MMC.exists(path)) {
        return false;
    }

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
        return false;
    }
    char buf[SD_SIDECAR_MAX_BYTES + 1];
    size_t got = f.readBytes(buf, SD_SIDECAR_MAX_BYTES);
    f.close();
    buf[got] = '\0';

    // Zero the output first — the present bits alone decide what the caller
    // may render (absent fields omitted, never zero-filled)
    memset(out, 0, sizeof(*out));
    out->imageId = imageId;
    out->kind = thumb ? static_cast<uint8_t>(ImageKind::THUMBNAIL)
                      : static_cast<uint8_t>(ImageKind::FULL_IMAGE);

    char str[24];
    uint32_t u = 0;
    bool b = false;

    int32_t pos = jsonValuePos(buf, "captureTimeMs");
    if (jsonUintAt(buf, pos, 0xFFFFFFFFUL, &u)) {
        out->captureTimeMs = u;
        out->present |= SD_SC_PRESENT_CAPTURETIME;
    }

    pos = jsonValuePos(buf, "triggerSource");
    if (jsonStringAt(buf, pos, str, sizeof(str))) {
        out->captureSource = captureSourceFromName(str);
        out->present |= SD_SC_PRESENT_TRIGGER;
    }

    // Telemetry triple: the writer emits altitude/lat/lon all-numeric (a
    // beacon was received) or all-null. All three must be present, non-null,
    // and in-range — otherwise the capture had no valid fix and the detail
    // view renders "GPS no fix" (D-47).
    int32_t posLat = jsonValuePos(buf, "lat");
    int32_t posLon = jsonValuePos(buf, "lon");
    int32_t posAlt = jsonValuePos(buf, "altitudeM");
    float lat = 0.0f, lon = 0.0f, alt = 0.0f;
    if (posLat >= 0 && posLon >= 0 && posAlt >= 0
            && !jsonIsNullAt(buf, posLat) && !jsonIsNullAt(buf, posLon)
            && !jsonIsNullAt(buf, posAlt)
            && jsonFloatAt(buf, posLat, -90.0f, 90.0f, &lat)
            && jsonFloatAt(buf, posLon, -180.0f, 180.0f, &lon)
            && jsonFloatAt(buf, posAlt, -1000.0f, 100000.0f, &alt)) {
        out->lat = lat;
        out->lon = lon;
        out->altitudeM = alt;
        out->telemetryValid = true;   // doubles as the detail view's gpsValid
        out->present |= SD_SC_PRESENT_TELEMETRY;
    }

    // Camera settings: the writer always emits the 7-field block as a unit —
    // all seven must parse or the block reads as absent (a partially-parsed
    // block would zero-fill the missing members)
    if (jsonValuePos(buf, "cameraSettings") >= 0) {
        long v[7];
        if (jsonIntAt(buf, jsonValuePos(buf, "resolution"), 0, 255, &v[0])
                && jsonIntAt(buf, jsonValuePos(buf, "quality"), 0, 255, &v[1])
                && jsonIntAt(buf, jsonValuePos(buf, "brightness"), -128, 127, &v[2])
                && jsonIntAt(buf, jsonValuePos(buf, "contrast"), -128, 127, &v[3])
                && jsonIntAt(buf, jsonValuePos(buf, "saturation"), -128, 127, &v[4])
                && jsonIntAt(buf, jsonValuePos(buf, "exposure"), -128, 127, &v[5])
                && jsonIntAt(buf, jsonValuePos(buf, "wbMode"), 0, 255, &v[6])) {
            out->resolution = static_cast<uint8_t>(v[0]);
            out->quality = static_cast<uint8_t>(v[1]);
            out->brightness = static_cast<int8_t>(v[2]);
            out->contrast = static_cast<int8_t>(v[3]);
            out->saturation = static_cast<int8_t>(v[4]);
            out->exposure = static_cast<int8_t>(v[5]);
            out->wbMode = static_cast<uint8_t>(v[6]);
            out->present |= SD_SC_PRESENT_CAMERA;
        }
    }

    // Chunk accounting: the pair parses together or not at all
    int32_t posRecv = jsonValuePos(buf, "chunksReceived");
    int32_t posTot = jsonValuePos(buf, "chunksTotal");
    uint32_t recv = 0, tot = 0;
    if (jsonUintAt(buf, posRecv, 0xFFFF, &recv)
            && jsonUintAt(buf, posTot, 0xFFFF, &tot)) {
        out->chunksReceived = static_cast<uint16_t>(recv);
        out->chunksTotal = static_cast<uint16_t>(tot);
        out->present |= SD_SC_PRESENT_CHUNKS;
        if (jsonUintAt(buf, jsonValuePos(buf, "bytesReceived"), 0xFFFFFFFFUL, &u)) {
            out->bytesReceived = u;
        }
    }

    pos = jsonValuePos(buf, "complete");
    if (jsonBoolAt(buf, pos, &b)) {
        out->complete = b;
        out->present |= SD_SC_PRESENT_COMPLETE;
    }

    pos = jsonValuePos(buf, "storedToSd");
    if (jsonBoolAt(buf, pos, &b)) {
        out->storedToSd = b;
        out->present |= SD_SC_PRESENT_STORED;
    }

    return true;
}

// ===========================
// Explicit Operator Wipe (quick-260831 — the base card's ONLY deletion
// surface, invoked solely by the /sd-clear HTTP handler)
// ===========================

// Order matters: the not-mounted honesty check comes first; the open write
// handles are closed BEFORE any removal (deleting a file under an open FAT
// handle is the hazard step c removes); the walk mirrors buildIndex's
// openNextFile idiom and the balloon module's per-file + summary logging
// (02.5-03 SDCLEAR precedent); the RAM index is reset in place with a
// version bump + alignment so /gallery and the /api/state galleryCount
// refetch signal (D-36) reflect the empty card on the next poll WITHOUT a
// directory re-walk. Single-threaded loop: no transfer can truly interleave
// (the clearing guards are belt-and-braces), and a full card is a few
// hundred ms of deletes — synchronous is acceptable, mirroring the balloon.
uint16_t SdStorage::clearAllImages() {
    // Not-mounted honesty: nothing to walk, nothing fabricated
    if (status.initFailed) {
        Serial.println("SdStorage: clear skipped - card not mounted");
        return 0;
    }

    clearing = true;

    // Close both open write handles and forget their accounting — deleting
    // a file under an open FAT handle is the hazard this step removes
    if (fullFile) {
        fullFile.close();
        fullFile = File();
    }
    if (thumbFile) {
        thumbFile.close();
        thumbFile = File();
    }
    fullFileId = 0;
    thumbFileId = 0;
    fullPersistedBytes = 0;
    thumbPersistedBytes = 0;

    uint16_t removed = 0;

    File dir = SD_MMC.open("/images");
    if (!dir || !dir.isDirectory()) {
        Serial.println("SdStorage: clear - /images open failed; nothing removed");
    } else {
        // Flat layout (D-32): every file lives directly in /images — walk
        // WITHOUT recursing; a directory entry is skipped with a named log.
        // Names are copied out before close (f.name() is valid only while
        // open); a name that cannot fit the bounded path buffer is skipped
        // with a named line rather than silently truncated into a
        // wrong-path remove.
        File f;
        while (f = dir.openNextFile()) {
            if (f.isDirectory()) {
                Serial.printf("SdStorage: clear skipped directory entry %s "
                              "(flat layout - never recursed)\n", f.name());
                f.close();
                continue;
            }
            const char* base = f.name();
            const char* slash = strrchr(base, '/');
            if (slash) {
                base = slash + 1;   // f.name() may carry a leading path
            }
            char path[48];
            if (strlen(base) + strlen("/images") + 2 > sizeof(path)) {
                Serial.println("SdStorage: clear skipped for over-long name (kept on card)");
                f.close();
                continue;
            }
            snprintf(path, sizeof(path), "/images/%s", base);
            f.close();
            if (SD_MMC.remove(path)) {
                removed++;
                Serial.printf("SdStorage: cleared %s\n", path);
            } else {
                Serial.printf("SdStorage: clear FAILED for %s (kept on card)\n", path);
            }
        }
        dir.close();

        Serial.printf("SdStorage: clear removed %u file(s)\n",
                      static_cast<unsigned>(removed));
    }

    // Reset the RAM gallery index in place: no stale entries, and the
    // version bump + alignment makes ensureIndexCurrent() a no-op — /gallery
    // and the /api/state galleryCount refetch signal (D-36) reflect the
    // empty card on the next poll without a directory re-walk.
    galleryCount = 0;
    indexVersion++;
    indexedVersion = indexVersion;

    // Single exit after the latch was set — clearing = false runs on EVERY
    // path past the not-mounted early return.
    clearing = false;
    return removed;
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
